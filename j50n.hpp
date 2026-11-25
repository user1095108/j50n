#ifndef J50N_HPP
# define J50N_HPP
# pragma once

#include <algorithm> // std::min()
#include <array>
#include <bit>
#include <charconv> // std::from_chars()
#include <climits> // CHAR_BIT
#include <compare> // std::partial_ordering
#include <cstddef> // std::size_t
#include <cstdint> // std::uintmax_t
#include <cstring> // std::memcpy()
#include <ostream> // operator<<()
#include <string>
#include <string_view>
#include <tuple> // std::ignore
#include <type_traits>
#include <utility> // std::forward()

static_assert(std::endian::big == std::endian::native ||
  std::endian::little == std::endian::native,
  "j50n: mixed-endian targets are not supported");

namespace j50n_::detail
{
  constexpr unsigned char operator""_uc(
    unsigned long long const c) noexcept
  {
    return static_cast<unsigned char>(c);
  }
}

class j50n
{
  std::string_view s_;

  struct S
  {
    using size_type = std::string_view::size_type;
    using uchar = unsigned char;

    template <std::size_t N>
    struct fixed_string
    {
      struct { char const data[N > 1u ? N - 1 : 1]; } const value;

      consteval fixed_string(char const(&s)[N]) noexcept :
        value{[&]<auto ...I>(std::index_sequence<I...>) noexcept
          {
            return decltype(value){s[I]...};
          }(std::make_index_sequence<N - 1>())}
      {
      }

      [[nodiscard]] constexpr operator std::string_view() const noexcept
      {
        return {value.data, N - 1};
      }
    };

    template <fixed_string... Strings>
    [[nodiscard]] static constexpr bool any_of(
      std::string_view const sv) noexcept
    {
      return ((sv == Strings) || ...);
    }

    template <char... Chars>
    [[nodiscard]] static constexpr bool any_of(char const c) noexcept
    {
      return ((c == Chars) || ...);
    }

    template <fixed_string... Strings>
    [[nodiscard]] static constexpr std::size_t index_of(
      std::string_view const sv) noexcept
    {
      std::size_t ndx{};
      return std::ignore = ((sv == Strings || !++ndx) || ...), ndx;
    }

    template <auto lo, decltype(lo) hi>
    [[nodiscard]] static constexpr bool range(auto const c) noexcept
    {
      static_assert(lo <= hi, "range bounds must be ordered");
      return c >= decltype(c)(lo) && c <= decltype(c)(hi);
    }

    [[nodiscard]] static constexpr bool is_hex(char const c) noexcept
    {
      return range<'0', '9'>(c) || range<'a', 'f'>(c | 0x20);
    }

    template <typename T> requires(T(-1) > T{} && T(2) > T(1))
    [[nodiscard]] static constexpr T splat(uchar const b) noexcept
    { // 0xbbbb…bb for byte value b
      return T(~T()) / uchar(~uchar()) * T(b);
    }

    template <typename T> requires(T(-1) > T{} && T(2) > T(1))
    [[nodiscard]] static constexpr T has_zero(T const v) noexcept
    { // nonzero iff v contains a zero byte;
      return (v - splat<T>(1)) & T(~v) & T(~splat<T>(uchar(~uchar{}) >> 1));
    }

    template <typename T>
    [[nodiscard]] static constexpr T byteswap(T const value) noexcept
    {
#if defined(__cpp_lib_byteswap)   // C++23
      return std::byteswap(value);
#else                             // C++20
      return []<auto ...I>(auto&& a, std::index_sequence<I...>) noexcept
        {
          return std::bit_cast<T>(std::array<std::byte, sizeof(T)>{
            a[sizeof(T) - 1 - I]...});
        }(std::bit_cast<std::array<std::byte, sizeof(T)>>(value),
          std::make_index_sequence<sizeof(T)>{});
#endif // __cpp_lib_byteswap
    }

    [[nodiscard]] static auto str_end(auto i, decltype(i) const end) noexcept
    {
      using namespace j50n_::detail;

      for (;;)
      {
        std::size_t const rem(end - i);
        std::uintmax_t w;

        if (rem >= sizeof w)
          std::memcpy(&w, i, sizeof w);
        else
          w = splat<decltype(w)>('!'),
          [&rem, q = reinterpret_cast<char*>(&w), p = i]<auto... I>(
            std::index_sequence<I...>) mutable noexcept
          {
            (
              (rem & (std::size_t(1) << I) ?
                std::memcpy(q, p, std::size_t(1) << I),
                I != sizeof...(I) - 1 ?
                  q += std::size_t(1) << I, p += std::size_t(1) << I :
                  nullptr :
               nullptr), ...
            );
          }(std::make_index_sequence<std::bit_width(sizeof(w)) - 1>{});

        if constexpr(std::endian::big == std::endian::native)
          w = byteswap(w);

        if (auto const m(
          has_zero(w ^ splat<decltype(w)>(0x22)) |       // byte == '"'
          has_zero(w ^ splat<decltype(w)>(0x5C)) |       // byte == '\\'
          (((w - splat<decltype(w)>(0x20)) | w) & ~splat<decltype(w)>(0x7f)) // byte < 0x20 or >= 0x80
          ); m)
        {
          unsigned const lane(unsigned(std::countr_zero(m)) / CHAR_BIT);
          i += lane;

          switch (uchar const b(w >> (lane * CHAR_BIT)); b)
          {
            case '"':
              return i;

            case '\\':
              if (end == ++i) [[unlikely]]
                return decltype(i){};

              // !any_of<'"', '\\', '/', 'b', 'f', 'n', 'r', 't', 'u'>(*i))
              if (auto const u{uchar(*i)}; u != 34_uc && u != 47_uc && // '"'  '/'
                (u - 92_uc >= 26_uc || !((0x03440441u >> (u - 92_uc)) & 1u))) // '\\' b f n r t u
                return decltype(i){};

              if ('u' == *i && (end - i < 5 || !is_hex(*++i) || !is_hex(*++i) ||
                !is_hex(*++i) || !is_hex(*++i)))
                return decltype(i){};

              break;

            default: // byte < 0x20 or >= 0x80
              if (!range<0xc2, 0xf4>(b))
                return decltype(i){};

              if (end == ++i || uchar(*i) < (0xE0_uc == b ? 0xA0_uc :
                0xF0_uc == b ? 0x90_uc : 0x80_uc) || uchar(*i) >
                (0xED_uc == b ? 0x9F_uc : 0xF4_uc == b ? 0x8F_uc : 0xBF_uc))
                return decltype(i){};

              if (((b >= 0xE0_uc) &&
                (end == ++i || (*i & 0xC0_uc) != 0x80_uc)) ||
                ((b >= 0xF0_uc) && (end == ++i || (*i & 0xC0_uc) != 0x80_uc)))
                return decltype(i){};
          }

          ++i;
        }
        else if (rem >= sizeof w)
          i += sizeof w;
        else
          return decltype(i){};
      }
    }

    static auto bare_end(auto i, decltype(i) const end) noexcept
    {
      using namespace j50n_::detail;

      for (;;)
      {
        std::size_t const rem(end - i);
        std::uintmax_t w;

        if (rem >= sizeof w)
          std::memcpy(&w, i, sizeof w);
        else
          w = splat<decltype(w)>('!'),
          [&rem, q = reinterpret_cast<char*>(&w), p = i]<auto... I>(
            std::index_sequence<I...>) mutable noexcept
          {
            (
              (rem & (std::size_t(1) << I) ?
                std::memcpy(q, p, std::size_t(1) << I),
                I != sizeof...(I) - 1 ?
                  q += std::size_t(1) << I, p += std::size_t(1) << I :
                  nullptr :
               nullptr), ...
            );
          }(std::make_index_sequence<std::bit_width(sizeof(w)) - 1>{});

        if constexpr(std::endian::big == std::endian::native)
          w = byteswap(w);

        if (auto const m(
          has_zero(w ^ splat<decltype(w)>(':')) |
          has_zero(w ^ splat<decltype(w)>(',')) |
          has_zero((w ^ splat<decltype(w)>(0x5D)) & ~splat<decltype(w)>(0x20)) | // byte == ']' or '}'
          (((w - splat<decltype(w)>(33)) | (w + splat<decltype(w)>(1)) | w) & // byte < 33 || byte > 126
          ~splat<decltype(w)>(0x7F))); m)
        {
          unsigned const lane(unsigned(std::countr_zero(m)) / CHAR_BIT);
          uchar const b(w >> (lane * CHAR_BIT));

          return ((b - 0x20_uc <= 0x5E_uc) || // b ∈ [0x20, 0x7E]
            (b < 0x20_uc && (0x2600u >> b) & 1u)) ? // b < 0x20 and tab / LF / CR (0x09, 0x0A, 0x0D)
            i + lane : decltype(i){};
        }
        else if (rem >= sizeof w)
          i += sizeof w;
        else
          return end;
      }
    }

    static void each_item(std::string_view const s, auto cb) noexcept
    {
      using namespace j50n_::detail;

      char const *start, *end(s.data() + s.size());
      unsigned depth{};

      for (auto i(s.data()); end != i; ++i)
        if (auto const c{uchar(*i)};
          any_of<' ', '\t', '\r', '\n', ':', ','>(c))
          continue;
        else if ('[' == (c & 0xDF_uc))         // '[' or '{'
        {
          if (2u == ++depth) start = i;
        }
        else if (']' == (c & 0xDF_uc))         // ']' or '}'
        {
          if (depth && (1u == --depth))
          {
            if ((']' == c ? '[': '{') != *start || cb(start, i, true)) break;
          }
          else if (!depth) break;
        }
        else
        { // delimit token
          auto const quot('"' == c);
          auto const e(quot ? str_end(i + 1, end) :
            ('-' == c || range<'0', '9'>(c) || range<'a', 'z'>(c | 0x20) ?
              bare_end(i, end) : nullptr));

          if (!e || (1u == depth && cb(i, e, quot)) || end == e)
            break;

          i = e - !quot;
        }
    }

    [[nodiscard]] static std::string_view find(std::string_view const s,
      std::string_view const key) noexcept
    {
      if (s.empty() || '{' != s.front()) return {};

      std::string_view val;

      each_item(s,
        [&, expecting_key = false, key_found = false](auto const p,
          auto const e, bool const incl) mutable noexcept -> bool
        {
          if ((expecting_key = !expecting_key))
          {
            if (bool const quoted('"' == *p); expecting_key &&
              std::string_view(p + quoted, size_type(e - p) - quoted) == key)
              key_found = true;
          }
          else if (key_found)
            return val = {p, size_type(e - p) + incl}, true;

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
        [&](auto const p, auto const e, bool const incl) noexcept -> bool
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
      else if constexpr(requires(F f){f(std::declval<j50n>());})
        return noexcept(std::declval<F&>()(std::declval<j50n>()));
      else
        return noexcept(std::declval<F&>()(std::declval<std::string_view>(),
          std::declval<j50n>()));
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
    requires(std::is_constructible_v<decltype(s_), decltype(a)&&...> &&
      ((!std::is_same_v<std::remove_cvref_t<decltype(a)>, std::nullptr_t>) && ...)):
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
    requires(std::is_constructible_v<decltype(s_), decltype(a)&&> &&
      !std::is_same_v<std::remove_cvref_t<decltype(a)>, std::nullptr_t>)
  {
    S::trim(s_ = std::string_view(std::forward<decltype(a)>(a)));

    return *this;
  }

  //
  [[nodiscard]] bool operator==(j50n const o) const noexcept
  {
    return s_ == o.s_;
  }

  [[nodiscard]] bool operator==(std::nullptr_t) const noexcept
  {
    return is_null();
  }

  [[nodiscard]] bool operator==(auto const a) const noexcept
    requires std::is_arithmetic_v<decltype(a)>
  {
    return *this <=> a == 0;
  }

  [[nodiscard]] std::partial_ordering operator<=>(auto const a) const noexcept
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
  [[nodiscard]] bool operator==(char const(&a)[N]) const
    noexcept(noexcept(operator==(std::string_view(a, N - 1))))
  {
    return operator==(std::string_view(a, N - 1));
  }

  [[nodiscard]] bool operator==(std::string_view const v) const
  {
    if (!is_string()) return false;

    auto const sv(get());
    return v == (std::string_view::npos == sv.find('\\') ? sv : unescape(sv));
  }

  [[nodiscard]] explicit operator bool() const noexcept
  {
    return !(is_empty() || is_null());
  }

  //
  template <std::size_t N>
  [[nodiscard]] j50n operator[](char const(&a)[N]) const noexcept
  {
    return j50n({}, S::find(s_, std::string_view(a, N - 1)));
  }

  [[nodiscard]] j50n operator[](auto&& a) const noexcept
  {
    return j50n({}, S::find(s_, std::forward<decltype(a)>(a)));
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

  [[nodiscard]] bool is_container() const noexcept
  {
    return is_array() || is_object();
  }

  [[nodiscard]] bool is_empty() const noexcept
  {
    return s_.empty();
  }

  [[nodiscard]] bool is_empty_array() const noexcept
  {
    return is_array() && !size();
  }

  [[nodiscard]] bool is_empty_object() const noexcept
  {
    return is_object() && !size();
  }

  [[nodiscard]] bool is_empty_string() const noexcept
  {
    return S::any_of<"\"\"">(s_);
  }

  [[nodiscard]] bool is_false() const noexcept
  {
    return S::any_of<"false">(s_);
  }

  [[nodiscard]] bool is_integer() const noexcept
  {
    return !get<std::intmax_t>().second;
  }

  [[nodiscard]] bool is_unsigned() const noexcept
  {
    return !get<std::uintmax_t>().second;
  }

  [[nodiscard]] bool is_null() const noexcept
  {
    return S::any_of<"null">(s_);
  }

  [[nodiscard]] bool is_number() const noexcept
  {
    return !get<double>().second;
  }

  [[nodiscard]] bool is_object() const noexcept
  {
    return !s_.empty() && ('{' == s_.front());
  }

  [[nodiscard]] bool is_string() const noexcept
  {
    return !s_.empty() && ('"' == s_.front());
  }

  [[nodiscard]] bool is_true() const noexcept
  {
    return S::any_of<"true">(s_);
  }

  //
  [[nodiscard]] j50n const& view() const noexcept { return *this; }

  [[nodiscard]] j50n view(auto&& a, auto&& ...b) const noexcept
  {
    auto r((*this)[std::forward<decltype(a)>(a)]);

    return ((r = r[std::forward<decltype(b)>(b)]), ...), r;
  }

  [[nodiscard]] bool contains(auto&& ...a) const noexcept
    requires(!!sizeof...(a))
  {
    return !view(std::forward<decltype(a)>(a)...).is_empty();
  }

  [[nodiscard]] std::string_view get() const noexcept
  {
    if (!is_string()) return s_;

    auto const sz(s_.size());
    return {s_.data() + 1, sz - 1 - (sz >= 2u && '"' == s_.back())};
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
    using namespace j50n_::detail;

    std::string r;
    r.reserve(sv.size());

    auto const hex4 = [](auto p) noexcept
      {
        unsigned v{};
        for (auto const end(p + 4); end != p; ++p)
          if (v <<= 4; S::range<'0', '9'>(*p))
            v |= unsigned(*p - '0');
          else if (S::uchar c(*p | 0x20_uc);
            S::range<'a', 'f'>(c)) v |= unsigned(c - 'a' + 10);
          else return std::pair(v, false);
        return std::pair(v, true);
      };

    auto const end(sv.data() + sv.size());

    for (auto p(sv.data()), p0(p);;)
    {
      std::size_t const rem(end - p);
      std::uintmax_t w;

      if (rem >= sizeof w)
        std::memcpy(&w, p, sizeof w);
      else
      {
        w = {};
        [&rem, q = reinterpret_cast<char*>(&w), p]<auto... I>(
          std::index_sequence<I...>) mutable noexcept
        {
          (
            (rem & (std::size_t(1) << I) ?
              std::memcpy(q, p, std::size_t(1) << I),
              I != sizeof...(I) - 1 ?
                q += std::size_t(1) << I, p += std::size_t(1) << I :
                nullptr :
             nullptr), ...
          );
        }(std::make_index_sequence<std::bit_width(sizeof(w)) - 1>{});
      }

      if constexpr(std::endian::big == std::endian::native)
        w = S::byteswap(w);

      if (auto const m(S::has_zero(w ^ S::splat<decltype(w)>('\\'))); m)
        p += unsigned(std::countr_zero(m)) / CHAR_BIT ;
      else if (rem >= sizeof w)
      {
        p += sizeof w;
        continue; // full word, no backslash — keep extending the run
      }
      else
      {
        r.append(p0, std::size_t(p - p0) + rem);
        break;
      }

      //assert('\\' == *p);
      r.append(p0, std::size_t(p - p0));

      if (end == ++p) [[unlikely]] // trailing lone backslash
      {
        r.append(p - 1, 1);
        break;
      }

      switch (auto const e(S::uchar(*p++)); e)
      {
        case '"': case '\\': case '/': case 'b':
        case 'f': case 'n': case 'r': case 't':
        {
          static constexpr std::array unescape_map{
            '\\', '\t', '\f', '\n', '/', '"', '\b', '\r'};
          r += unescape_map[(e + 341u) & (e + 341u) >> 6];
          break;
        }
        case 'u':
        {
          if (end - p < 4) goto default_;
          auto const [cp, ok] = hex4(p);
          if (!ok) goto default_;
          p += 4;

          auto u(cp - 0xD800u < 0x800u ? 0xFFFDu : cp);

          if (unsigned const hi(cp - 0xD800u); hi < 0x400u && end - p >= 6 &&
            p[0] == '\\' && p[1] == 'u')
            if (auto const [lo, ok](hex4(p + 2)); ok &&
              unsigned(lo - 0xDC00u) < 0x400u)
              u = 0x10000u + (hi << 10u) + (lo - 0xDC00u), p += 6;

          if (u < 0x80u) r += char(u);
          else
            switch (unsigned const b((u >= 0x800u) + (u >= 0x10000u)); b)
            {
              char buf[4];
              case 2: buf[3] = char(0x80 | (u & 0x3F)), u >>= 6;[[fallthrough]];
              case 1: buf[2] = char(0x80 | (u & 0x3F)), u >>= 6;[[fallthrough]];
              default: buf[1] = char(0x80 | (u & 0x3F));
                       *buf  = char(~0x3Fu >> b | u >> 6);
                       r.append(buf, b + 2u);
            }
          break;
        }
        default: default_:
          r.append(p - 2, 2);
      }

      p0 = p;                   // literal run restarts after the escape
    }

    return r;
  }

  //
  [[nodiscard]] std::size_t size() const noexcept
  {
    std::size_t sz{};

    feach([&](j50n) noexcept { ++sz; });
    feach([&](std::string_view, j50n) noexcept { ++sz; });

    return sz;
  }

  //
  void feach(auto f) const
    noexcept(S::feach_noexcept<decltype(f)>())
    requires(
      requires{f(std::declval<j50n>(), std::declval<std::size_t>());} ||
      requires{f(std::declval<j50n>());} ||
      requires{f(std::declval<std::string_view>(), std::declval<j50n>());}
    )
  {
    if (s_.empty()) return;

    if constexpr(requires{f(std::declval<j50n>(),
      std::declval<std::size_t>());} || requires{f(std::declval<j50n>());})
    { // element-style callable: arrays only
      if ('[' != s_.front()) return;

      S::each_item(s_,
        [&, i = std::size_t{}](auto const p, auto const e,
          bool const incl) mutable
          noexcept(S::feach_noexcept<decltype(f)>()) -> bool
          {
            j50n const item({}, p, S::size_type(e - p) + incl);
            if constexpr(requires{bool(f(item, i));}) return f(item, i++);
            else if constexpr(requires{bool(f(item));})
              { ++i; return f(item); }
            else if constexpr(requires{f(item);})
              { ++i; f(item); return false; }
            else { f(item, i++); return false; }
          }
      );
    }
    else if constexpr(requires{f(std::declval<std::string_view>(),
      std::declval<j50n>());})
    { // key/value callable: objects only
      if ('{' != s_.front()) return;

      S::each_item(s_,
        [&, expecting_key = false, key = std::string_view()](
          auto const p, auto const e, bool const incl) mutable
          noexcept(S::feach_noexcept<decltype(f)>()) -> bool
          {
            if ((expecting_key = !expecting_key))
            {
              bool const quoted('"' == *p);
              key = {p + quoted, S::size_type(e - p) - quoted};

              return false;
            }

            j50n const val({}, p, S::size_type(e - p) + incl);

            if constexpr(requires{bool(f(key, val));}) return f(key, val);
            else { f(key, val); return false; }
          }
      );
    }
  }
};

//////////////////////////////////////////////////////////////////////////////
inline std::ostream& operator<<(std::ostream& os, j50n const j)
{
  return os << j.get();
}

#endif // J50N_HPP
