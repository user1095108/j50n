#ifndef J50N_HPP
# define J50N_HPP
# pragma once

#include <cassert>
#include <cstring> // std::strncmp()
#include <charconv> // std::from_chars()
#include <ostream> // operator<<()
#include <string_view>
#include <type_traits>
#include <utility> // std::forward()

class j50n
{
  std::string_view s_;

  struct S
  {

  static auto range(auto const x, decltype(x) s, decltype(x) e) noexcept
  {
    return (x >= s) && (x <= e);
  }

  static char const* str_end(char const* p, char const* const end) noexcept
  {
    constexpr std::string_view esc("\"\\/bfnrtu"); // what may follow a '\'

    for (; end != p; ++p)
    {
      auto const c(static_cast<unsigned char>(*p));

      if ('"' == c)
        return p;
      else if ('\\' == c)
      {
        if (end == ++p) break;
        if (std::string_view::npos == esc.find(*p)) return {};
      }
      else if (!range(c, 32, 126)) // UTF-8: a lead byte, then continuations
      {
        int n((192 == (c & 224)) ? 1 : (224 == (c & 240)) ? 2 :
          (240 == (c & 248)) ? 3 : 0);

        if (!n) return {};

        for (; n; --n)
        {
          if (end == ++p) return end;
          if (128 != (static_cast<unsigned char>(*p) & 192)) return {};
        }
      }
    }

    return end;
  }

  static char const* bare_end(char const* p, char const* const end) noexcept
  {
    for (; end != p; ++p)
      switch (*p)
      {
      case '\t': case ' ': case '\r': case '\n':
      case ':': case ',': case ']': case '}':
        return p;

      default:
        // could be more pedantic/validation-checking
        if (!range(static_cast<unsigned char>(*p), 32, 126)) return {};
      }

    return end;
  }

  static std::string_view find(std::string_view const s,
    std::string_view const k, std::size_t index = 1,
    bool const keyed = true) noexcept
  {
    auto const end(s.data() + s.size());
    char const* val{}; // start of the wanted value
    char const* key{}; // start of the key being read
    int depth{};

    auto const open([&](char const* const p, bool const quoted) noexcept
      {
        if (keyed && (1 == index))
          key = p + (quoted ? 1 : 0);
        else if (!index)
          val = p;
        else
          --index;
      }
    );

    auto const close([&](char const* const p) noexcept
      {
        if (val)
          return true;

        if (keyed && key)
        {
          index = (k == std::string_view(key, std::size_t(p - key))) ? 0 : 2;
          key = {};
        }

        return false;
      }
    );

    auto const upto([&](char const* const e) noexcept
      { return std::string_view(val, std::size_t(e - val)); }
    );

    auto const bad([]() noexcept
      { assert(!"bad"); return std::string_view(); }
    );

    auto const eof([&]() noexcept
      { assert(!depth); return std::string_view(); }
    );

    for (auto p(s.data()); end != p; ++p)
      switch (*p)
      {
      case '\t': case ' ': case '\r': case '\n': case ':': case ',':
        break;

      case '[':
      case '{':
        if (!depth && keyed && ('{' != *p)) return {}; // arrays have no keys
        if (1 == depth) open(p, false);
        ++depth;
        break;

      case ']':
      case '}':
        if ((1 == --depth) && close(p)) return upto(p + 1);
        break;

      default: // a string or a bare word
        {
          auto const quoted('"' == *p);

          if (!quoted && ('-' != *p) && !range(*p, '0', '9') &&
            !range(*p, 'A', 'Z') && !range(*p, 'a', 'z'))
            return bad();

          auto const e(quoted ? str_end(p + 1, end) : bare_end(p, end));

          if (!e) return bad();
          if (end == e) return eof();

          if (1 == depth)
          {
            open(p, quoted);
            if (close(e)) return upto(e + (quoted ? 1 : 0));
          }

          // resume after a string, but at the terminator of a bare word
          p = quoted ? e : e - 1;
        }
      }

    return eof();
  }

  static void trim(std::string_view& s) noexcept
  {
    auto const p(s.find_first_not_of(" \t\n\r"));

    s = std::string_view::npos == p ? std::string_view() :
      std::string_view(s.begin() + p,
        s.begin() + s.find_last_not_of(" \t\n\r") + 1);
  }

  };

public:
  j50n() = default;

  j50n(j50n const&) = default;
  j50n(j50n&&) = default;

  template <std::size_t N>
  j50n(char const(&a)[N]) noexcept: j50n(a, N - 1) { }

  j50n(auto&& ...a)
    noexcept(noexcept(decltype(s_)(std::forward<decltype(a)>(a)...)))
    requires std::is_constructible_v<decltype(s_), decltype(a)&&...>:
    s_(std::forward<decltype(a)>(a)...)
  {
    S::trim(s_);
  }

  //
  j50n& operator=(j50n const&) = default;
  j50n& operator=(j50n&&) = default;

  template <std::size_t N>
  j50n& operator=(char const(&a)[N]) noexcept
  {
    return operator=(std::string_view(a, N - 1));
  }

  j50n& operator=(auto&& a)
    noexcept(noexcept(
      S::trim(s_ = std::string_view(std::forward<decltype(a)>(a)))))
    requires std::is_constructible_v<decltype(s_), decltype(a)&&>
  {
    S::trim(s_ = std::string_view(std::forward<decltype(a)>(a)));

    return *this;
  }

  //
  template <std::size_t N>
  j50n operator[](char const(&a)[N]) const noexcept
  {
    return S::find(s_, std::string_view(a, N - 1));
  }

  j50n operator[](auto&& a) const noexcept
  {
    if constexpr(std::is_convertible_v<decltype(a), std::string_view>)
      return S::find(s_, std::forward<decltype(a)>(a));
    else
      return S::find(s_, {}, std::forward<decltype(a)>(a), false);
  }

  //
  bool is_empty() const noexcept { return s_.empty(); }
  bool is_array() const noexcept { return s_.size() && ('[' == s_.front()); }
  bool is_object() const noexcept { return s_.size() && ('{' == s_.front()); }
  bool is_string() const noexcept { return s_.size() && ('"' == s_.front()); }
  bool is_null() const noexcept { return std::string_view("null", 4) == s_; }
  bool is_number() const noexcept { return !get<double>().second; }

  bool is_bare() const noexcept
  {
    return s_.size() && ('[' != s_.front()) && ('{' != s_.front()) &&
      ('"' != s_.front());
  }

  bool is_bool() const noexcept
  {
    return (std::string_view("true", 4) == s_) ||
      (std::string_view("false", 5) == s_);
  }

  //
  j50n const& view() const noexcept { return *this; }

  j50n view(auto&& a, auto&& ...b) const noexcept
  {
    auto r((*this)[std::forward<decltype(a)>(a)]);

    return ((r = r[std::forward<decltype(b)>(b)]), ...), r; // !!!
  }

  std::string_view get() const noexcept
  {
    if (!is_string()) return s_;

    auto r(s_.substr(1));
    if (r.size() && ('"' == r.back()))
      r.remove_suffix(1); // drop quotes

    return r;
  }

  std::string_view get(auto&& ...a) const noexcept
  {
    return view(std::forward<decltype(a)>(a)...).get();
  }

  template <std::size_t N>
  std::string_view get_or(char const (&a)[N], auto&& ...b) const noexcept
  {
    auto const v(view(std::forward<decltype(b)>(b)...));

    return v.s_.empty() ? std::string_view(a, N - 1) : v.get();
  }

  std::string_view get_or(std::string_view const a,auto&& ...b) const noexcept
  {
    auto const v(view(std::forward<decltype(b)>(b)...));

    return v.s_.empty() ? a : v.get();
  }

  template <typename U>
  std::pair<U, bool> get(auto&& ...a) const noexcept
    requires(std::is_arithmetic_v<U> &&
      !std::is_same_v<bool, std::remove_cv_t<U>>)
  {
    auto const sv(view(std::forward<decltype(a)>(a)...).s_);

    U r{};
    auto const [ptr, ec](std::from_chars(sv.data(), sv.data() + sv.size(), r));
    return std::pair(r, std::errc{} != ec || (sv.data() + sv.size()) != ptr);
  }

  template <typename U>
  U get_or(U const a, auto&& ...b) const noexcept
  {
    auto const [v, err](get<U>(std::forward<decltype(b)>(b)...));

    return err ? a : v;
  }

  template <typename U>
  auto get(auto&& ...a) const noexcept
    requires(std::is_same_v<bool, std::remove_cv_t<U>>)
  {
    auto const sv(view(std::forward<decltype(a)>(a)...).s_);

    if (std::string_view("true", 4) == sv)
      return std::pair(true, false);
    else if (std::string_view("false", 5) == sv)
      return std::pair(false, false);
    else
      return std::pair(false, true);
  }

  //
  auto size() const noexcept
  {
    std::size_t i{};

    if (is_array()) while (!(*this)[i].is_empty()) ++i;

    return i;
  }

  //
  void feach(auto f) const
    noexcept(noexcept(f(std::declval<j50n const&>())))
    requires(requires{f(std::declval<j50n const&>());})
  {
    if (is_array()) // !!!
      for (std::size_t i{};; ++i)
      {
        if (auto const e((*this)[i]); e.is_empty())
          break;
        else if constexpr(requires{bool(f(e));})
        {
          if (f(e)) break;
        }
        else
          f(e);
      }
  }

  void feach(auto f) const
    noexcept(noexcept(f(std::declval<j50n const&>(),
      std::declval<std::size_t>())))
    requires(requires{f(std::declval<j50n const&>(),
      std::declval<std::size_t>());})
  {
    if (is_array()) // !!!
      for (std::size_t i{};; ++i)
      {
        if (auto const e((*this)[i]); e.is_empty())
          break;
        else if constexpr(requires{bool(f(e, i));})
        {
          if (f(e, i)) break;
        }
        else
          f(e, i);
      }
  }
};

//////////////////////////////////////////////////////////////////////////////
inline auto& operator<<(std::ostream& os, j50n const& j)
{ // !!!
  return os << j.get();
}

#endif // J50N_HPP
