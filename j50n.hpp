#ifndef J50N_HPP
# define J50N_HPP
# pragma once

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
    using size_type = std::string_view::size_type;

    template <auto lo, decltype(lo) hi>
    static constexpr bool range(auto const c) noexcept
    {
      static_assert(lo <= hi, "range bounds must be ordered");
      return c >= lo && c <= hi;
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
        if (auto const c(static_cast<unsigned char>(*p)); '"' == c)
          return p;
        else if ('\\' == c)
        {
          if ((end == ++p) ||
            !one_of<'"', '\\', '/', 'b', 'f', 'n', 'r', 't', 'u'>(*p))
            return {};
        }
        else if (!range<32u, 127u>(c)) // 0x7F (DEL) is allowed unescaped per RFC 8259
        {
          auto n((c & 224) == 192 ? 1u :
            (c & 240) == 224 ? 2u : (c & 248) == 240 ? 3u : 0u);
          if (!n) return {};

          for (; n; --n)
            if ((end == ++p) || ((static_cast<unsigned char>(*p) & 192) != 128))
              return {};
        }
      }

      return {};
    }

    static char const* bare_end(char const* p, char const* const end) noexcept
    {
      for (; end != p; ++p)
        if (one_of<' ', '\t', '\n', '\r', ':', ',', ']', '}'>(*p)) return p;
        else if (!range<32u, 126u>(static_cast<unsigned char>(*p))) return {};

      return end;
    }

    static void each_item(std::string_view const s, auto const cb) noexcept
    {
      auto const end(s.data() + s.size());
      char const* start{};
      int depth{};

      for (auto p(s.data()); end != p; ++p)
      {
        if (one_of<' ', '\t', '\r', '\n', ':', ','>(*p))
          continue;
        else if (one_of<'[', '{'>(*p))
        {
          if (depth == 1) start = p;
          ++depth;
        }
        else if (one_of<']', '}'>(*p))
        {
          if (--depth == 1 && start)
          {
            if (cb(start, p, true)) return;
            start = {};
          }
        }
        else
        { // delimit token
          auto const e(('"' == *p) ? str_end(p + 1, end) :
            (('-' == *p || range<'0', '9'>(*p) || range<'A', 'Z'>(*p) ||
            range<'a', 'z'>(*p)) ? bare_end(p, end) : nullptr));

          if (!e || (depth == 1 && cb(p, e, '"' == *p)) || (end == e))
            return;

          p = e - ('"' != *p);
        }
      }
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

      if (s.empty() || (keyed ? '{' != s.front() : '[' != s.front()))
        return {};

      char const *val{}, *val_end{};
      bool val_incl{};

      each_item(s,
        [&](char const* p, char const* e, bool incl) noexcept -> bool
        {
          if (keyed && search_key == index)
          {
            bool const quoted('"' == *p);
            index = k == std::string_view(p + quoted,
              size_type(e - p) - quoted) ? key_found : skip_value;
          }
          else if (key_found == index)
          {
            val = p; val_end = e; val_incl = incl;
            return true;
          }
          else
            --index;

          return false;
        });

      return val ?
        std::string_view(val, size_type(val_end - val) + val_incl) :
        std::string_view();
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

      std::size_t i{};

      each_item(s,
        [&](char const* const p, char const* const e,
          bool const incl) noexcept(feach_noexcept<decltype(f)>()) -> bool
          {
            j50n const item(p, size_type(e - p) + incl);
            if constexpr(requires{bool(f(item, i));}) return f(item, i++);
            else if constexpr(requires{bool(f(item));}){ ++i; return f(item); }
            else if constexpr(requires{f(item);}){ ++i; f(item); return false; }
            else { f(item, i++); return false; }
          }
      );
    }

    static void feach(std::string_view const s, auto f)
      noexcept(noexcept(f(std::declval<std::string_view>(),
        std::declval<j50n>())))
      requires(requires{f(std::declval<std::string_view>(),
        std::declval<j50n>());})
    {
      if (s.empty() || '{' != s.front()) return; // objects only

      char const *k_ptr{}, *k_end{};
      bool expecting_key{true};

      each_item(s,
        [&](char const* const p, char const* const e, bool const incl)
          noexcept(noexcept(f(std::declval<std::string_view>(),
            std::declval<j50n>()))) -> bool
          {
            if (expecting_key)
            {
              k_ptr = p; k_end = e;
              return expecting_key = false;
            }

            expecting_key = true;

            bool const quoted('"' == *k_ptr);
            std::string_view const key(k_ptr + quoted,
              size_type(k_end - k_ptr) - quoted);
            j50n const val(p, size_type(e - p) + incl);

            if constexpr (requires { bool(f(key, val)); }) return f(key, val);
            else { f(key, val); return false; }
          }
      );
    }

    static void trim(std::string_view& s) noexcept
    {
      auto const p(s.find_first_not_of(" \t\n\r"));
      s = std::string_view::npos == p ? std::string_view{} :
        s.substr(p, size_type(s.find_last_not_of(" \t\n\r") - p) + 1);
    }
  };

public:
  j50n() = default;

  j50n(j50n const&) = default;
  j50n(j50n&&) = default;

  template <std::size_t N>
  explicit j50n(char const(&a)[N]) noexcept: j50n(a, N - 1) { }

  explicit j50n(auto&& ...a)
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
  bool operator==(j50n const o) const noexcept { return s_ == o.s_; }
  bool operator==(std::nullptr_t) const noexcept { return is_null(); }

  template <typename U>
  bool operator==(U const v) const noexcept
    requires std::is_arithmetic_v<U>
  {
    auto const [x, err] = get<U>();

    return !err && x == v;
  }

  template <std::size_t N>
  bool operator==(char const(&a)[N]) const noexcept
  {
    return operator==(std::string_view(a, N - 1));
  }

  bool operator==(std::string_view const v) const
  {
    if (!is_string()) return false;

    auto const sv(get());
    if (std::string_view::npos == sv.find('\\'))
      return sv == v; // fast path

    return v == unescape(sv);
  }

  explicit operator bool() const noexcept
  {
    if (is_empty() || is_null() || (std::string_view("false", 5) == s_))
      return {};
    else if ('"' == s_.front()) return !get().empty();
    else if (auto const [v, err](get<double>()); !err)
      return v;
    else 
      return ('[' != s_.front() && '{' != s_.front()) || size();
  }

  //
  template <std::size_t N>
  j50n operator[](char const(&a)[N]) const noexcept
  {
    return j50n(S::find(s_, std::string_view(a, N - 1)));
  }

  j50n operator[](auto&& a) const noexcept
  {
    if constexpr(std::is_convertible_v<decltype(a), std::string_view>)
      return j50n(S::find(s_, std::forward<decltype(a)>(a)));
    else
      return j50n(S::find(s_, {}, std::forward<decltype(a)>(a), false));
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

  bool contains(auto&& ...a) const noexcept requires(!!sizeof...(a))
  {
    return !view(std::forward<decltype(a)>(a)...).is_empty();
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
  auto get(auto&& ...a) const noexcept
    requires(std::is_same_v<bool, U>)
  {
    auto const sv(view(std::forward<decltype(a)>(a)...).s_);

    if (std::string_view("true", 4) == sv)
      return std::pair(true, false);
    else if (std::string_view("false", 5) == sv)
      return std::pair(false, false);
    else
      return std::pair(false, true);
  }

  template <typename U>
  U get_or(U const a, auto&& ...b) const noexcept
  {
    auto const [v, err](get<U>(std::forward<decltype(b)>(b)...));

    return err ? a : v;
  }

  static std::string unescape(std::string_view const sv)
  {
    std::string r;
    r.reserve(sv.size());

    auto const hex = [](char const c) noexcept -> unsigned
      {
        return c >= '0' && c <= '9' ? unsigned(c - '0') :
               c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10) :
               c >= 'A' && c <= 'F' ? unsigned(c - 'A' + 10) : 16u;
      };

    auto const utf8 = [&](unsigned cp) noexcept
      {
        int const n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        char b[4];
        for (int i = n - 1; i > 0; --i, cp >>= 6) b[i] = char(0x80 | (cp & 0x3F));
        b[0] = char(cp | (n > 1 ? (0xFF << (8 - n)) & 0xFF : 0));
        r.append(b, n);
      };

    auto const hex4 = [&](auto& p) noexcept
      {
        unsigned v{};

        for (auto const end(p + 4); end != p; ++p)
        {
          auto const h(hex(*p));

          if (h > 15) return std::pair(v, false);
          v = (v << 4) | h;
        }

        return std::pair(v, true);
      };

    auto const end(sv.data() + sv.size());

    for (auto p(sv.data()); end != p;)
    {
      char const c(*p++);
      if ('\\' != c || p == end) { r += c; continue; }

      switch (char const e(*p++); e)
      {
        case '"':  r += '"';  break;
        case '\\': r += '\\'; break;
        case '/':  r += '/';  break;
        case 'b':  r += '\b'; break;
        case 'f':  r += '\f'; break;
        case 'n':  r += '\n'; break;
        case 'r':  r += '\r'; break;
        case 't':  r += '\t'; break;
        case 'u':
        {
          if (end - p < 4) { r += 'u'; break; }
          auto const p0 = p;
          auto const [cp, ok] = hex4(p);
          if (!ok) { p = p0; r += 'u'; break; }
          unsigned u = cp;
          if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 &&
              *p == '\\' && *(p + 1) == 'u')
          {
            auto q = p + 2;
            auto const [lo, ok2] = hex4(q);
            if (ok2 && lo >= 0xDC00 && lo <= 0xDFFF)
            {
              u = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
              p += 6;
            }
          }
          utf8(u);
          break;
        }

        default:
          (r += '\\') += e;
          break;
      }
    }

    return r;
  }

  //
  std::size_t size() const noexcept
  {
    std::size_t sz{};

    if (is_array())
      S::feach(s_, [&](j50n, std::size_t) noexcept { ++sz; });
    else
      S::feach(s_, [&](std::string_view, j50n) noexcept { ++sz; });

    return sz;
  }

  //
  void feach(auto&& f) const
    noexcept(noexcept(S::feach(s_, std::forward<decltype(f)>(f))))
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
