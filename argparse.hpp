#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <sstream>
#include <type_traits>
#include <functional>
#include <memory>
#include <unordered_map>

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

//

struct argument_base {
   virtual void assign(std::string const& src) = 0;
   virtual void store_true() = 0;
   virtual bool with_value() const = 0;

   argument_base(std::string const& name, char short_name,
                 std::string const& help)
     : name_{name}, short_name_{short_name}, help_{help}
  {
  }

   std::string const& name() const { return name_; }
   char short_name() const { return short_name_; }

private:
   std::string name_;
   char short_name_;
   std::string help_;
};

template <typename T>
struct argument : public argument_base {
   using arg_type = T;

   argument(std::string const& name, char short_name,
            std::string const& help)
      : argument_base{name, short_name, help}
   {
   }

   void assign(std::string const& src) override {
      val_.reset(new T(lexical_cast<T>(src)));
   }

   void store_true() override { /* do nothing. */ }
   bool with_value() const override { return true; }

private:
   std::unique_ptr<T> val_;
};

template <>
struct argument<void> : public argument_base {
   using arg_type = bool;

   argument(std::string const& name, char short_name,
            std::string const& help)
      : argument_base{ name, short_name, help }
   {
   }

   void assign(std::string const&) override { /* do nothing. */ }
   void store_true() override { val_ = true; }
   bool with_value() const override { return false; }

private:
   bool val_ = false;
};


template <typename T>
inline argument<T> arg(std::string const& name, char short_name = '\0',
                       std::string const& help = "")
{
   return argument<T>{ name, short_name, help };
}

template <typename T>
inline argument<T> arg(std::string const& name, std::string const& help)
{
   return argument<T>{ name, '\0', help };
}

inline argument<void> flag(std::string const& name, char short_name = '\0',
                           std::string const& help = "")
{
   return argument<void>{ name, short_name, help };
}

inline argument<void> flag(std::string const& name, std::string const& help)
{
   return argument<void>{ name, '\0', help };
}

template <typename... Args>
struct parser
{
   using args_type = std::tuple<typename Args::arg_type...>;

   parser(Args&&... args)
      : args_{ std::forward<Args>(args)... }
   {
      // make lookup tables.
   }

   void parse(std::vector<std::string> const& args) {
      if (args.empty())
         throw std::runtime_error("argument must be at least one item(s).");

      progname_ = args[0];

      for (auto it = args.begin(); it != args.end(); ++it)
      {
         auto& arg = *it;
         if (arg.substr(0,2)=="--") { // long option
            std::string::size_type pos = arg.find('=',2);
            if (pos==std::string::npos) {
               std::string key = arg.substr(2);
               if (lookup_.count(key) == 0)
                  throw std::runtime_error("unknown option: --" + key);
               argument_base& a = lookup_.at(key).get();
               if (a.with_value()) {
                  // read next argument
                  ++it;
                  std::string val = *it;
                  a.assign(val);
               }
               else {
                  a.store_true();
               }
            }
            else {
               std::string key = arg.substr(2, pos);
               std::string val = arg.substr(pos+1);
               argument_base& a = lookup_.at(key).get();
               if (a.with_value()) {
                  a.assign(val);
               }
               else {
                  a.store_true();
               }
            }
         }
         else if (arg[0] == '-') { // short option
            if (arg.length() < 2) continue;
            char s = arg[1];
            if (short_lookup_.count(s) == 0)
               throw std::runtime_error(std::string("unknown short option: -") + s);
            argument_base& a = short_lookup_.at(s).get();
            if (a.with_value()) {
               ++it;
               std::string val = *it;
               a.assign(val);
            } else {
               a.store_true();
            }
         }
         else { // normal argument
            remains_.push_back(arg);
         }
      }
   }

   std::string const& progname() const { return progname_; }
   args_type const& options() const { return options_; }
   std::vector<std::string> const& remains() const { return remains_; }

private:
   std::tuple<Args...> args_;
   std::unordered_map<std::string, std::reference_wrapper<argument_base>> lookup_;
   std::unordered_map<char,        std::reference_wrapper<argument_base>> short_lookup_;

   std::string progname_;
   args_type options_;
   std::vector<std::string> remains_;
};

template <typename... Args>
parser<Args...> make_parser(Args&&... args) {
   return parser<Args...>{ std::forward<Args>(args)... };
}

} // namespace argparse;
