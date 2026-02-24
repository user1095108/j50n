#ifndef J50N_HPP
# define J50N_HPP
# pragma once

#include <cassert>
#include <cstring> // std::strncmp()
#include <charconv> // std::from_chars()
#include <iterator> // std::next()
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

  static std::string_view find(std::string_view const& s,
    std::string_view const& k, std::size_t index = 1) noexcept
  {
    char const* val{};
    auto cur(s.begin()), start(cur);

    auto const cap([&]() noexcept
      {
        if (val && !index) //r = {val, cur + i + 1};
          return true;
        else if (auto const ks(k.size()); ks && start)
          index = (ks != std::string_view::size_type(cur - start)) ||
            std::strncmp(start, k.begin(), ks) ? 2 : 0,
          start = {};

        return false;
      }
    );

    auto const push([&](std::size_t const i) noexcept
      {
        if (k.size() && (1 == index))
          start = cur + i;
        else if (!index)
          val = cur + i; // + i
        else
          --index;
      }
    );

    int depth{};
    int utf8_remain{};

    enum
    {
      S_STRUCT,
      S_BARE,
      S_STRING,
      S_UTF8,
      S_ESC,
    } state{S_STRUCT};

    for (auto const end(s.end()); end != cur; ++cur)
    {
      again:
      switch (state)
      {
      case S_STRUCT:
        switch (*cur) {
        case '\t':
        case ' ':
        case '\r':
        case '\n':
        case ':':
        case ',':
          continue;

        case '"': goto l_qup;
        case '[': goto l_up;
        case ']': goto l_down;
        case '{': goto l_up;
        case '}': goto l_down;

        case '-': goto l_bare;
        default: {
          if (range(*cur, '0', '9') ||
              range(*cur, 'A', 'Z') ||
              range(*cur, 'a', 'z'))
            goto l_bare;
          else
            goto l_bad;
        }
        }
        assert(0);
      case S_BARE:
        switch (*cur) {
        case '\t':
        case ' ':
        case '\r':
        case '\n':
        case ',':
        case ']':
        case '}':
        case ':':
          goto l_unbare;
        default:  {
          // could be more pedantic/validation-checking
          if (range(*cur, 32, 126))
            continue;
          goto l_bad;
        }
        }
        assert(0);
      case S_STRING:
        if (*cur == '\\') {
          state = S_ESC;
          continue;
        }
        if (*cur == '"')
          goto l_qdown;
        if (range(*cur, 32, 126))
          continue;
        if ((*cur & 224) == 192) { // range(*cur, 192, 223))
          state = S_UTF8;
          utf8_remain = 1;
          continue;
        }
        if ((*cur & 240) == 224) { // range(*cur, 224, 239)
          state = S_UTF8;
          utf8_remain = 2;
          continue;
        }
        if ((*cur & 248) == 240) { // range(*cur, 240, 247)
          state = S_UTF8;
          utf8_remain = 3;
          continue;
        }
        goto l_bad;
      // XXX no utf8 outside strings?
      case S_UTF8:
        if ((*cur & 192) == 128) { // range(*cur, 128, 191)
          if (!--utf8_remain)
            state = S_STRING;
          continue;
        }
        goto l_bad;
      case S_ESC:
        switch (*cur) {
        case '"':
        case '\\':
        case '/':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
        case 'u':
          state = S_STRING;
          continue;
        default:
          goto l_bad;
        }
      }
      assert (0);
      l_bad:
      assert("bad");
      return {};

      l_up:
      if (1 == depth) push(0);
      ++depth;
      continue;

      l_down:
      --depth;
      if ((1 == depth) && cap()) return {val, cur + 1};
      continue;

      l_qup:
      if (1 == depth) push(1);
      state = S_STRING;
      continue;

      l_qdown:
      if ((1 == depth) && cap()) return {val, cur};
      state = S_STRUCT;
      continue;

      l_bare:
      if (1 == depth) push(0);
      state = S_BARE;
      continue;

      l_unbare:
      if ((1 == depth) && cap()) return {val, cur};
      state = S_STRUCT;
      goto again;
    }

    assert(!depth);
    return {};
  }

  };

public:
  j50n() = default;

  j50n(j50n const&) = default;
  j50n(j50n&&) = default;

  template <std::size_t N> j50n(char const(&a)[N]) noexcept: s_(a, N - 1) { }

  j50n(auto&& ...a)
    noexcept(noexcept(decltype(s_)(std::forward<decltype(a)>(a)...)))
    requires std::is_constructible_v<decltype(s_), decltype(a)&&...>:
    s_(std::forward<decltype(a)>(a)...)
  {
  }

  //
  j50n& operator=(j50n const&) = default;
  j50n& operator=(j50n&&) = default;

  template <std::size_t N>
  j50n& operator=(char const(&a)[N]) noexcept
  {
    s_ = {a, N - 1}; return *this;
  }

  j50n& operator=(auto&& a)
    noexcept(noexcept(s_ = std::forward<decltype(a)>(a)))
    requires std::is_assignable_v<decltype(s_), decltype(a)&&>
  {
    s_ = std::forward<decltype(a)>(a); return *this;
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
      return S::find(s_, {}, std::forward<decltype(a)>(a));
  }

  //
  bool is_empty() const noexcept { return s_.empty(); }
  auto is_array() const noexcept { return !is_empty() && ('[' == s_.front());}
  auto is_object() const noexcept {return !is_empty() && ('{' == s_.front());}

  //
  auto& get() const noexcept { return s_; }

  auto get(auto&& a, auto&& ...b) const noexcept
  {
    auto r((*this)[std::forward<decltype(a)>(a)]);

    return ((r = r[std::forward<decltype(b)>(b)]), ...), r.s_; // !!!
  }

  template <typename U>
  auto get(auto&& ...a) const noexcept
    requires(std::is_arithmetic_v<U> &&
      !std::is_same_v<bool, std::remove_cv_t<U>>)
  {
    auto const s(get(std::forward<decltype(a)>(a)...));

    U r;

    auto const q(std::from_chars(s.begin(), s.end(), r).ec != std::errc{});

    return std::pair(r, q);
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
    if (!is_array()) return;

    for (std::size_t i{};; ++i)
      if (auto const e((*this)[i]); e.is_empty())
        break;
      else if constexpr(std::is_same_v<bool,
        decltype(f(std::declval<j50n const&>()))>)
      {
        if (f(e)) break;
      }
      else
        f(e);
  }

  void feach(auto f) const
    noexcept(noexcept(f(std::declval<j50n const&>(),
      std::declval<std::size_t>())))
    requires(requires{f(std::declval<j50n const&>(),
      std::declval<std::size_t>());})
  {
    if (!is_array()) return;

    for (std::size_t i{};; ++i)
      if (auto const e((*this)[i]); e.is_empty())
        break;
      else if constexpr(std::is_same_v<bool,
        decltype(f(std::declval<j50n const&>(),
          std::declval<std::size_t>()))>)
      {
        if (f(e, i)) break;
      }
      else
        f(e, i);
  }
};

//////////////////////////////////////////////////////////////////////////////
auto& operator<<(std::ostream& os, j50n const& j)
{ // !!!
  return os << j.get();
}

#endif // J50N_HPP
