#ifndef J50N_HPP
# define J50N_HPP
# pragma once

#include <cstddef> // std::size_t
#include <charconv> // std::from_chars()
#include <compare> // std::partial_ordering
#include <ostream> // operator<<()
#include <string_view>
#include <type_traits>
#include <utility> // std::forward()

class j50n
{
  std::string_view s_;

  struct S
  {
    using size_type = std::string_view::size_type;

    template <std::size_t N>
    struct fixed_string
    {
      struct { char const data[N - 1]; } const value;

      consteval fixed_string(char const(&s)[N]) noexcept :
        value{[&]<auto ...Is>(std::index_sequence<Is...>) noexcept
          {
            return decltype(value){s[Is]...};
          }(std::make_index_sequence<N - 1>())}
      {
      }

      [[nodiscard]] constexpr operator std::string_view() const noexcept
      {
        return {value.data, N - 1};
      }
    };

    template <fixed_string... Strings>
    static constexpr bool any_of(std::string_view const sv) noexcept
    {
      return ((sv == Strings) || ...);
    }

    template <char... Chars>
    static constexpr bool any_of(char const c) noexcept
    {
      return ((c == Chars) || ...);
    }

    template <fixed_string... Strings>
    static constexpr std::size_t index_of(std::string_view const sv) noexcept
    {
      std::size_t ndx{};
      (void)((sv == Strings || !++ndx) || ...);
      return ndx;
    }

    template <auto lo, decltype(lo) hi>
    static constexpr bool range(auto const c) noexcept
    {
      static_assert(lo <= hi, "range bounds must be ordered");
      return c >= decltype(c)(lo) && c <= decltype(c)(hi);
    }

    static constexpr bool is_hex(char const c) noexcept
    {
      return range<'0', '9'>(c) || range<'A', 'F'>(c) || range<'a', 'f'>(c);
    }

    static char const* str_end(auto i, decltype(i) const end) noexcept
    {
      for (; end != i; ++i)
        if (auto const b((unsigned char)*i); '"' == b) [[unlikely]]
          return i;
        else if ('\\' == b) [[unlikely]]
        {
          if (end == ++i ||
            !any_of<'"', '\\', '/', 'b', 'f', 'n', 'r', 't', 'u'>(*i))
            break;

          if ('u' == *i && (end - i < 5 || !is_hex(*++i) || !is_hex(*++i) ||
            !is_hex(*++i) || !is_hex(*++i)))
            break;
        } // 0x7F (DEL) is allowed unescaped per RFC 8259
        else if (!range<32, 127>(b)) // not printable ASCII (incl. 0x7F)
        {
          if (!range<0xc2, 0xf4>(b))
            break; // not a UTF-8 lead: invalid

          if (end == ++i || (unsigned char)*i < (0xE0u == b ? 0xA0u :
            0xF0u == b ? 0x90u : 0x80u) || (unsigned char)*i >
            (0xEDu == b ? 0x9Fu : 0xF4u == b ? 0x8Fu : 0xBFu))
            break;

          if (((b >= 0xE0u) && (end == ++i || (*i & 0xC0) != 0x80)) ||
            ((b >= 0xF0u) && (end == ++i || (*i & 0xC0) != 0x80)))
            break;
        }

      return {};
    }

    static char const* bare_end(auto i, decltype(i) const end) noexcept
    {
      for (; end != i; ++i)
        if (any_of<' ', '\t', '\n', '\r', ':', ',', ']', '}'>(*i)) return i;
        else if (!range<32, 126>(*i)) return {};

      return end;
    }

    static void each_item(std::string_view const s, auto cb) noexcept
    {
      char const *start{}, *end(s.data() + s.size());
      unsigned depth{};

      for (auto i(s.data()); end != i; ++i)
        if (any_of<' ', '\t', '\r', '\n', ':', ','>(*i))
          continue;
        else if (any_of<'[', '{'>(*i))
        {
          if (1 == depth++) start = i;
        }
        else if (any_of<']', '}'>(*i))
        {
          if (!depth) return;
          else if (--depth == 1)
          {
            if (cb(start, i, true)) return;
            start = {};
          }
        }
        else
        { // delimit token
          auto const e('"' == *i ? str_end(i + 1, end) :
            ('-' == *i || range<'0', '9'>(*i) || range<'A', 'Z'>(*i) ||
            range<'a', 'z'>(*i) ? bare_end(i, end) : nullptr));

          if (!e || (1 == depth && cb(i, e, '"' == *i)) || end == e)
            return;

          i = e - ('"' != *i);
        }
    }

    [[nodiscard]] static std::string_view find(std::string_view const s,
      std::string_view const key) noexcept
    {
      if (s.empty() || '{' != s.front()) return {};

      std::string_view val;

      each_item(s,
        [&, expecting_key = false, key_found = false](char const* const p,
          char const* const e, bool const incl) mutable noexcept -> bool
        {
          if ((expecting_key = !expecting_key))
          {
            if (bool const quoted('"' == *p); expecting_key &&
              std::string_view(p + quoted, size_type(e - p) - quoted) == key)
              key_found = true;
          }
          else if (key_found)
          {
            val = {p, size_type(e - p) + incl};
            return true;
          }

          return false;
        }
      );

      return val;
    }

    [[nodiscard]] static std::string_view find(std::string_view const s,
      std::size_t index) noexcept
    {
      if (s.empty() || '[' != s.front()) return {};

      std::string_view val;

      each_item(s,
        [&](char const* const p, char const* const e,
          bool const incl) noexcept -> bool
        {
          return index ? --index, false :
            (val = {p, size_type(e - p) + incl}, true);
        }
      );

      return val;
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

      each_item(s,
        [&, i = std::size_t{}](char const* const p, char const* const e,
          bool const incl) mutable
          noexcept(feach_noexcept<decltype(f)>()) -> bool
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

      each_item(s,
        [&, expecting_key = false, key = std::string_view()](
          char const* const p, char const* const e, bool const incl) mutable
          noexcept(noexcept(f(std::declval<std::string_view>(),
            std::declval<j50n>()))) -> bool
          {
            if ((expecting_key = !expecting_key))
            {
              bool const quoted('"' == *p);
              key = {p + quoted, size_type(e - p) - quoted};

              return false;
            }

            j50n const val(p, size_type(e - p) + incl);

            if constexpr(requires{bool(f(key, val));}) return f(key, val);
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

  explicit j50n(int, auto&& ...a)
    noexcept(noexcept(decltype(s_)(std::forward<decltype(a)>(a)...)))
    requires std::is_constructible_v<decltype(s_), decltype(a)&&...>:
    s_(std::forward<decltype(a)>(a)...)
  {
  }

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

  bool operator==(auto const a) const noexcept
    requires std::is_arithmetic_v<decltype(a)>
  {
    return *this <=> a == 0;
  }

  std::partial_ordering operator<=>(auto const a) const noexcept
    requires(std::is_arithmetic_v<decltype(a)>)
  {
    if (auto const [x, err](get<decltype(a)>()); !err)
      return x <=> a;

    if constexpr(!std::is_same_v<bool, std::remove_cv_t<decltype(a)>>)
      if (auto const [x, err](get<double>()); !err)
        return x <=> double(a);

    return std::partial_ordering::unordered;
  }

  template <std::size_t N>
  bool operator==(char const(&a)[N]) const
    noexcept(noexcept(operator==(std::string_view(a, N - 1))))
  {
    return operator==(std::string_view(a, N - 1));
  }

  bool operator==(std::string_view const v) const
  {
    if (!is_string()) return false;

    auto const sv(get());
    return v == (std::string_view::npos == sv.find('\\') ? sv : unescape(sv));
  }

  [[nodiscard]] explicit operator bool() const noexcept
  {
    if (is_empty() || is_null() || S::any_of<"false">(s_))
      return false;
    else if ('"' == s_.front()) return !get().empty();
    else if (auto const [v, err](get<double>()); !err)
      return bool(v);
    else 
      return !S::any_of<'[', '{'>(s_.front()) || size();
  }

  //
  template <std::size_t N>
  [[nodiscard]] j50n operator[](char const(&a)[N]) const noexcept
  {
    return j50n(1, S::find(s_, std::string_view(a, N - 1)));
  }

  [[nodiscard]] j50n operator[](auto&& a) const noexcept
  {
    return j50n(1, S::find(s_, std::forward<decltype(a)>(a)));
  }

  //
  [[nodiscard]] bool is_array() const noexcept
  {
    return !s_.empty() && ('[' == s_.front());
  }

  [[nodiscard]] bool is_bare() const noexcept
  {
    return !s_.empty() && !S::any_of<'[', '{', '"'>(s_.front());
  }

  [[nodiscard]] bool is_bool() const noexcept
  {
    return S::any_of<"true", "false">(s_);
  }

  [[nodiscard]] bool is_empty() const noexcept
  {
    return s_.empty();
  }

  [[nodiscard]] bool is_empty_string() const noexcept
  {
    return S::any_of<"\"\"">(s_);
  }

  [[nodiscard]] bool is_object() const noexcept
  {
    return !s_.empty() && ('{' == s_.front());
  }

  [[nodiscard]] bool is_string() const noexcept
  {
    return !s_.empty() && ('"' == s_.front());
  }

  [[nodiscard]] bool is_null() const noexcept
  {
    return S::any_of<"null">(s_);
  }

  [[nodiscard]] bool is_number() const noexcept
  {
    return !get<double>().second;
  }

  //
  [[nodiscard]] j50n const& view() const noexcept { return *this; }

  [[nodiscard]] j50n view(auto&& a, auto&& ...b) const noexcept
  {
    auto r((*this)[std::forward<decltype(a)>(a)]);

    return ((r = r[std::forward<decltype(b)>(b)]), ...), r; // !!!
  }

  [[nodiscard]] bool contains(auto&& ...a) const noexcept
    requires(!!sizeof...(a))
  {
    return !view(std::forward<decltype(a)>(a)...).is_empty();
  }

  [[nodiscard]] std::string_view get() const noexcept
  {
    if (!is_string()) return s_;

    auto r(s_.substr(1));
    if (!r.empty() && ('"' == r.back()))
      r.remove_suffix(1); // drop quotes

    return r;
  }

  template <int = 0>
  [[nodiscard]] std::string_view get(auto&& ...a) const noexcept
  {
    return view(std::forward<decltype(a)>(a)...).get();
  }

  template <std::size_t N>
  [[nodiscard]] std::string_view get_or(char const (&a)[N],
    auto&& ...b) const noexcept
  {
    auto const v(view(std::forward<decltype(b)>(b)...));

    return v.is_empty() ? std::string_view(a, N - 1) : v.get();
  }

  [[nodiscard]] std::string_view get_or(std::string_view const a,
    auto&& ...b) const noexcept
  {
    auto const v(view(std::forward<decltype(b)>(b)...));

    return v.is_empty() ? a : v.get();
  }

  template <typename U>
  [[nodiscard]] std::pair<std::remove_cv_t<U>, bool> get(
    auto&& ...a) const noexcept
    requires(std::is_arithmetic_v<U> &&
      !std::is_same_v<bool, std::remove_cv_t<U>>)
  {
    auto const sv(view(std::forward<decltype(a)>(a)...).s_);

    std::remove_cv_t<U> r{}; auto const end(sv.data() + sv.size());
    auto const [ptr, ec](std::from_chars(sv.data(), end, r));
    return std::pair(r, std::errc{} != ec || end != ptr);
  }

  template <typename U>
  [[nodiscard]] std::pair<bool, bool> get(auto&& ...a) const noexcept
    requires std::is_same_v<bool, std::remove_cv_t<U>>
  {
    auto const ndx(S::index_of<"true", "false">(
      view(std::forward<decltype(a)>(a)...).s_));
    return {!ndx, 2u == ndx};
  }

  template <typename U>
  [[nodiscard]] std::remove_cv_t<U> get_or(U const a,
    auto&& ...b) const noexcept
    requires std::is_arithmetic_v<U>
  {
    auto const [v, err](get<U>(std::forward<decltype(b)>(b)...));

    return err ? a : v;
  }

  [[nodiscard]] static std::string unescape(std::string_view const sv)
  {
    std::string r;
    r.reserve(sv.size());

    auto const hex4 = [](auto&& p) noexcept
      {
        unsigned v{};

        for (auto const end(p + 4); end != p; ++p)
          if (v <<= 4; S::range<'0', '9'>(*p)) v |= unsigned(*p - '0');
          else if (S::range<'a', 'f'>(*p)) v |= unsigned(*p - 'a' + 10);
          else if (S::range<'A', 'F'>(*p)) v |= unsigned(*p - 'A' + 10);
          else return std::pair(v, false);

        return std::pair(v, true);
      };

    auto const end(sv.data() + sv.size());

    for (auto p(sv.data()); end != p;)
    {
      if (auto const c(*p++); '\\' != c || p == end) { r += c; continue; }

      switch (auto const e(*p++); e)
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
          if (end - p < 4) { r.append({'\\', 'u'}); break; }
          auto const p0 = p;
          auto const [cp, ok] = hex4(p);
          if (!ok) { p = p0; r.append({'\\', 'u'}); break; }

          unsigned u(unsigned(cp - 0xD800) < 0x800 ? 0xFFFD : cp);

          if (unsigned hi(cp - 0xD800); hi < 0x400 && end - p >= 6 &&
            p[0] == '\\' && p[1] == 'u')
            if (auto [lo, ok](hex4(p + 2)); ok &&
              unsigned(lo - 0xDC00) < 0x400)
              u = 0x10000 + (hi << 10) + (lo - 0xDC00), p += 6;

          if (u < 0x80u) r += char(u);
          else [[unlikely]]
            switch (unsigned char const n((u >= 0x800u) + (u >= 0x10000u)); n)
            {
              char b[4];
              case 2:  b[3] = char(0x80 | (u & 0x3F)); u >>= 6; [[fallthrough]];
              case 1:  b[2] = char(0x80 | (u & 0x3F)); u >>= 6; [[fallthrough]];
              default: b[1] = char(0x80 | (u & 0x3F)); u >>= 6;
                       *b   = char(u | (0xFFu << (6 - n)));
                       r.append(b, n + 2);
            }

          break;
        }

        default:
          r.append({'\\', e});
          break;
      }
    }

    return r;
  }

  //
  [[nodiscard]] std::size_t size() const noexcept
  {
    std::size_t sz{};

    S::feach(s_, [&](j50n) noexcept { ++sz; });
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
inline std::ostream& operator<<(std::ostream& os, j50n const j)
{ // !!!
  return os << j.get();
}

#endif // J50N_HPP
