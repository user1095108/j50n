#ifndef J50N_HPP
# define J50N_HPP
# pragma once

#include <cassert>
#include <cstddef> // std::size_t
#include <charconv> // std::from_chars()
#include <ostream> // operator<<()
#include <string_view>
#include <type_traits>
#include <utility> // std::forward()

using namespace std::literals::string_view_literals;

class j50n
{
  std::string_view s_;

  struct S
  {
    static auto range(auto const x, decltype(x) s, decltype(x) e) noexcept
    {
      return x >= s && x <= e;
    }

    template <char... chars>
    static bool one_of(char const c) noexcept
    {
      return ((c == chars) || ...);
    }

    static char const* str_end(char const* p, char const* const end) noexcept
    {
      for (; end != p; ++p)
      {
        auto const c(static_cast<unsigned char>(*p));

        if ('"' == c) return p;
        if ('\\' == c)
        {
          if (end == ++p) break;
          if (!one_of<'"', '\\', '/', 'b', 'f', 'n', 'r', 't', 'u'>(*p))
            return {};
        }
        else if (!range(c, 32, 126))
        {
          int n((c & 224) == 192 ? 1 :
            (c & 240) == 224 ? 2 : (c & 248) == 240 ? 3 : 0);
          if (!n) return {};

          for (; n; --n)
          {
            if (end == ++p) return end;
            if ((static_cast<unsigned char>(*p) & 192) != 128) return {};
          }
        }
      }

      return end;
    }

    static char const* bare_end(char const* p, char const* const end) noexcept
    {
      for (; end != p; ++p)
        if (one_of<' ', '\t', '\n', '\r', ':', ',', ']', '}'>(*p)) return p;
        else if (!range(static_cast<unsigned char>(*p), 32, 126)) return {};

      return end;
    }

    static char const* token_end(char const* const p,
      char const* const end) noexcept
    {
      if ('"' == *p) return str_end(p + 1, end);
      return ('-' == *p || range(*p, '0', '9') || range(*p, 'A', 'Z') ||
        range(*p, 'a', 'z')) ? bare_end(p, end) : nullptr;
    }

    static std::string_view find(std::string_view const s,
      std::string_view const k, std::size_t index = 1,
      bool const keyed = true) noexcept
    {
      enum state : std::size_t
      {
        search_key = 1,
        key_found  = 0,
        skip_value = 2
      };

      auto const end(s.data() + s.size());
      char const* val{}, *key{};
      int depth{};

      auto const fail = [](bool ok) noexcept
        { assert(ok); return std::string_view{}; };

      auto const open = [&](char const* p) noexcept
        {
          if (keyed && index == search_key)
            key = p + ('"' == *p);
          else if (index == key_found)
            val = p;
          else
            --index;
        };

      auto const seal = [&](char const* e, bool incl) noexcept -> std::string_view
        {
          if (val) return {val, static_cast<std::size_t>(e - val) + incl};

          if (key)
          {
            index = k == std::string_view(key, std::size_t(e - key)) ?
              key_found : skip_value;
            key = {};
          }

          return {};
        };

      for (auto p(s.data()); end != p; ++p)
      {
        if (one_of<' ', '\t', '\r', '\n', ':', ','>(*p)) continue;

        if (one_of<'[', '{'>(*p))
        {
          if (!depth && keyed && '{' != *p) return {};
          if (depth == 1) open(p);
          ++depth;
        }
        else if (one_of<']', '}'>(*p))
        {
          if (--depth == 1)
            if (auto r = seal(p, true); !r.empty()) return r;
        }
        else
        {
          auto const e(token_end(p, end));
          if (!e) return fail(false);
          if (end == e) return fail(!depth);

          if (1 == depth)
          {
            open(p);
            if (auto r = seal(e, '"' == *p); !r.empty()) return r;
          }

          p = e - ('"' != *p);
        }
      }

      return fail(!depth);
    }

    template <typename F>
    static consteval bool feach_noexcept() noexcept
    {
      if constexpr(requires(F f){f(std::declval<j50n>(),
        std::declval<std::size_t>());})
        return noexcept(std::declval<F&>()(std::declval<j50n>(),
          std::declval<std::size_t>()));
      else
        return noexcept(std::declval<F&>()(std::declval<j50n>()));
    }

    static void feach(std::string_view const s, auto f)
      noexcept(feach_noexcept<decltype(f)>())
      requires(
        requires{f(std::declval<j50n>(), std::declval<std::size_t>());} ||
        requires{f(std::declval<j50n>());}
      )
    {
      if (s.empty() || '[' != s.front()) return; // arrays only

      auto const end(s.data() + s.size());
      char const* val{};
      int depth{};
      std::size_t i{};

      auto const emit = [&](char const* const item_end, bool const incl)
        noexcept(feach_noexcept<decltype(f)>())
        {
          j50n const item(val, std::size_t(item_end - val) + incl);
          val = {};
          if constexpr(requires{bool(f(item, i));}) return f(item, i++);
          else if constexpr(requires{bool(f(item));}){ ++i; return f(item); }
          else if constexpr(requires{f(item);}){ ++i; f(item); return false; }
          else { f(item, i++); return false; }
        };

      for (auto p(s.data()); end != p; ++p)
      {
        if (one_of<' ', '\t', '\r', '\n', ':', ','>(*p)) continue;

        if (one_of<'[', '{'>(*p))
        {
          if (depth == 1) val = p;
          ++depth;
        }
        else if (one_of<']', '}'>(*p))
        {
          if (--depth == 1 && val && emit(p, true)) break;
        }
        else
        {
          auto const e(token_end(p, end));
          if (!e || end == e) break;

          if (depth == 1)
          {
            val = p;
            if (emit(e, '"' == *p)) break;
          }

          p = e - ('"' != *p);
        }
      }
    }

    static void feach(std::string_view const s, auto f)
      noexcept(noexcept(f(std::declval<std::string_view>(),
        std::declval<j50n>())))
      requires(requires{f(std::declval<std::string_view>(),
        std::declval<j50n>());})
    {
      if (s.empty() || '{' != s.front()) return; // objects only

      auto const end(s.data() + s.size());
      char const* k_ptr{};
      char const* k_end{};
      char const* v_ptr{};
      int depth{};
      bool expecting_key{true};

      auto const emit = [&](char const* v_end, bool incl)
        noexcept(noexcept(f(std::declval<std::string_view>(),
          std::declval<j50n>())))
        {
          bool const quoted('"' == *k_ptr);
          std::string_view const key(k_ptr + quoted,
            std::size_t(k_end - k_ptr - quoted));
          j50n const val(v_ptr, std::size_t(v_end - v_ptr) + incl);
          expecting_key = true;
          v_ptr = {};
          if constexpr (requires { bool(f(key, val)); }) return f(key, val);
          else { f(key, val); return false; }
        };

      for (auto p(s.data()); end != p; ++p)
      {
        if (one_of<' ', '\t', '\r', '\n', ':', ','>(*p)) continue;

        if (one_of<'[', '{'>(*p))
        {
          if (depth == 1 && !expecting_key) v_ptr = p;
          ++depth;
        }
        else if (one_of<']', '}'>(*p))
        {
          if (--depth == 1 && !expecting_key && v_ptr)
          {
            if (emit(p, true)) break;
          }
        }
        else
        {
          auto const e(token_end(p, end));
          if (!e || end == e) break;

          if (depth == 1)
          {
            if (expecting_key)
            {
              k_ptr = p;
              k_end = e;
              expecting_key = false;
            }
            else
            {
              v_ptr = p;
              if (emit(e, '"' == *p)) break;
            }
          }
          p = e - ('"' != *p);
        }
      }
    }

    static void trim(std::string_view& s) noexcept
    {
      auto const p(s.find_first_not_of(" \t\n\r"));
      s = std::string_view::npos == p ? std::string_view{} :
        s.substr(p, s.find_last_not_of(" \t\n\r") - p + 1);
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

  template <int = 0>
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

    if (is_array())
      S::feach(s_, [&](j50n const&, std::size_t) noexcept
        {
          ++i;
        }
      );
    else
      S::feach(s_, [&](std::string_view const&, j50n const&) noexcept
        {
          ++i;
        }
      );

    return i;
  }

  //
  void feach(auto&& f) const
    noexcept(noexcept(f(std::declval<j50n>())))
    requires(requires{f(std::declval<j50n>());})
  {
    S::feach(s_, std::forward<decltype(f)>(f));
  }

  void feach(auto&& f) const
    noexcept(noexcept(f(std::declval<j50n>(),
      std::declval<std::size_t>())))
    requires(requires{f(std::declval<j50n>(),
      std::declval<std::size_t>());})
  {
    S::feach(s_, std::forward<decltype(f)>(f));
  }

  void feach(auto&& f) const
    noexcept(noexcept(f(std::declval<std::string_view>(),
      std::declval<j50n>())))
    requires(requires{f(std::declval<std::string_view>(),
      std::declval<j50n>());})
  {
    S::feach(s_, std::forward<decltype(f)>(f));
  }
};

//////////////////////////////////////////////////////////////////////////////
inline auto& operator<<(std::ostream& os, j50n const j)
{ // !!!
  return os << j.get();
}

#endif // J50N_HPP
