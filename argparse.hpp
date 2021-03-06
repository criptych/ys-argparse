///
/// Copyright (c) 2015 Yusuke Sasaki
///
/// This software is released under the MIT License.
/// See http://opensource.org/licenses/mit-license.php or <LICENSE>.
///

#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace argparse {

namespace detail {
   template <typename In, typename Out, bool pass = std::is_convertible<In, Out>::value>
   struct lexical_cast_t;

   template <typename In, typename Out>
   struct lexical_cast_t<In, Out, true> {
      static Out apply(In const& val) {
         return static_cast<Out>(val);
      }
   };

   template <typename In>
   struct lexical_cast_t<In, std::string, false> {
      static std::string apply(In const& val) {
         std::ostringstream osstr;
         osstr << val;
         return osstr.str();
      }
   };

   template <typename Out>
   struct lexical_cast_t<std::string, Out, false> {
      static Out apply(std::string const& val) {
         std::istringstream isstr(val);
         Out out;
         isstr >> out;
         return out;
      }
   };

   template <typename Out, std::size_t N>
   struct lexical_cast_t<char[N], Out, false> {
      static Out apply(char const (&val)[N]) {
         std::istringstream isstr(val);
         Out out;
         isstr >> out;
         return out;
      }
   };

   template <typename Out, std::size_t N>
   struct lexical_cast_t<char const[N], Out, false> {
      static Out apply(char const (&val)[N]) {
         std::istringstream isstr(val);
         Out out;
         isstr >> out;
         return out;
      }
   };

} // namespace detail;

template <typename Out, typename In>
inline Out lexical_cast(In const& val) {
    return detail::lexical_cast_t<In, Out>::apply(val);
}

template <std::size_t... Idx>
struct index_sequence {
   using next = index_sequence<Idx..., sizeof...(Idx)>;
};

namespace detail {
   template <std::size_t Nt, std::size_t N>
   struct iota { using type = typename iota<Nt-1, N-1>::type::next; };

   template <std::size_t Nt>
   struct iota<Nt, 0> { using type = index_sequence<>; };
} // namespace detail;

template <std::size_t N>
using make_index_sequence = typename detail::iota<N, N>::type;

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
   return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// ---------

class argparse_error : public std::runtime_error {
public:
   explicit argparse_error(std::string const& msg)
      : std::runtime_error{msg} {}
};

struct argument_base {
   virtual void assign(std::string const& src) = 0;
   virtual void store_true() = 0;
   virtual bool with_value() const = 0;

   argument_base(std::string const& name, char short_name, std::string const& help)
     : name_{name}, short_name_{short_name}, help_{help} {}

   std::string const& name() const { return name_; }
   char short_name() const { return short_name_; }

private:
   std::string name_;
   char short_name_;
   std::string help_;
};

template <typename T>
struct argument_with_value : public argument_base
{
   using arg_type = T;

   template <typename... Args>
   argument_with_value(Args&&... args)
      : argument_base{ std::forward<Args>(args) } {}

   void assign(std::string const& src) override {
      val_ = make_unique<T>(lexical_cast<T>(src));
   }

   void store_true() override { /* do nothing. */ }
   bool with_value() const override { return true; }

   T const& get() const {
      if (!val_)
         throw argparse_error{"argument is not initialized"};
      return *val_;
   }

private:
   std::unique_ptr<T> val_;
};

struct argument_flag : public argument_base
{
   using arg_type = bool;

   template <typename... Args>
   argument_flag(Args&&... args)
      : argument_base{ std::forward<Args>(args)... } {}

   void assign(std::string const&) override { /* do nothing. */ }
   void store_true() override { val_ = true; }
   bool with_value() const override { return false; }

   bool get() const { return val_; }

private:
   bool val_ = false;
};

template <typename... Args>
struct parser
{
   using args_type = std::tuple<typename Args::arg_type...>;

   parser(Args&&... args)
      : args_{ std::forward<Args>(args)... }
   {
      make_lookup_tables(make_index_sequence<sizeof...(Args)>());
   }

   void parse(int argc, char const* argv[]) {
      parse(std::vector<std::string>(argv, argv + argc));
   }

   void parse(std::vector<std::string> const& args) {
      try {
         if (args.empty())
            throw argparse_error{"argument must be at least one item(s)."};

         progname_ = args[0];

         for (auto it = args.begin() + 1; it != args.end(); ++it)
         {
            std::string const& arg = *it;
            if (arg.substr(0, 2) == "--") {
               // long option
               std::string::size_type pos = arg.find('=', 2);
               if (pos == std::string::npos) {
                  std::string key = arg.substr(2);
                  if (it + 1 == args.end())
                     throw argparse_error{"needs value: " + key};
                  assign(key, it);
               }
               else {
                  std::string key = arg.substr(2, pos - 2);
                  std::string val = arg.substr(pos + 1);
                  assign(key, val);
               }
            }
            else if (arg[0] == '-') {
               // short option
               char s = arg[1];
               if (it + 1 == args.end())
                  throw argparse_error{std::string("needs value: ") + s};
               assign(s, it);
            }
            else {
               // normal argument
               remains_.push_back(arg);
            }
         }

         get_values(make_index_sequence<std::tuple_size<args_type>::value>());
      }
      catch (argparse_error const& err) {
         std::cerr << "parse error: " << err.what() << std::endl;
         std::exit(1);
      }
   }

   std::string const& progname() const { return progname_; }
   args_type const& options() const { return options_; }
   std::vector<std::string> const& remains() const { return remains_; }

private:
   template <typename T>
   using lookup_table_t = std::unordered_map<T, std::reference_wrapper<argument_base>>;

   std::tuple<Args...> args_;
   lookup_table_t<std::string> lookup_;
   lookup_table_t<char> short_lookup_;

   std::string progname_;
   args_type options_;
   std::vector<std::string> remains_;

private:
   template <std::size_t I, std::size_t... Idx>
   void make_lookup_tables(index_sequence<I, Idx...>) {
      argument_base& arg = std::get<I>(args_);
      lookup_.insert({ arg.name(), arg });
      char s = arg.short_name();
      if (s != '\0')
         short_lookup_.insert({ s, arg });

      make_lookup_tables(index_sequence<Idx...>());
   }
   inline void make_lookup_tables(index_sequence<>) {}

   template <std::size_t I, std::size_t... Idx>
   void get_values(index_sequence<I, Idx...>) {
      std::get<I>(options_) = std::get<I>(args_).get();
      get_values(index_sequence<Idx...>());
   }
   inline void get_values(index_sequence<>) {}

   argument_base& get_ref(std::string const& key) {
      if (lookup_.count(key) == 0)
         throw argparse_error{"unknown option: --" + key};
      return lookup_.at(key).get();
   }

   argument_base& get_ref(char s) {
      if (short_lookup_.count(s) == 0)
         throw argparse_error{std::string("unknown option: -") + s};
      return short_lookup_.at(s).get();
   }

   void assign(std::string const& key, std::string const& val) {
      argument_base& a = get_ref(key);
      if (a.with_value()) {
         a.assign(val);
      }
      else {
         a.store_true();
      }
   }

   template <typename Key>
   void assign(Key&& key, std::vector<std::string>::const_iterator& it) {
      argument_base& a = get_ref(std::forward<Key>(key));
      if (a.with_value()) {
         ++it;
         a.assign(*it);
      }
      else {
         a.store_true();
      }
   }
};

// -----

template <typename... Args>
inline parser<Args...> make_parser(Args&&... args) {
   return { std::forward<Args>(args)... };
}

template <typename T>
inline argument_with_value<T> arg(std::string const& name, char short_name = '\0',
                                  std::string const& help = "") {
   return { name, short_name, help };
}

template <typename T>
inline argument_with_value<T> arg(std::string const& name, std::string const& help) {
   return { name, '\0', help };
}

inline argument_flag flag(std::string const& name, char short_name = '\0',
                          std::string const& help = "") {
   return { name, short_name, help };
}

inline argument_flag flag(std::string const& name, std::string const& help) {
   return { name, '\0', help };
}

} // namespace argparse;
