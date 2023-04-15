#include <cassert>
#include <cstring>
#include <string_view>
#include <type_traits>

class j50n
{
  std::string_view s_;

  static std::string_view find(std::string_view const& s,
    std::string_view const& k, std::size_t index = 1) noexcept
  {
    std::string_view r;

    const char* val{};
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

    auto cur(s.begin()), start(cur);

    auto const cap([&](std::size_t const i) noexcept
      {
        if (val && !index)
        {
          r = {val, cur + i + 1};

          return true;
        }
        else if (k.size() && start)
        {
          index = std::strncmp(start, k.begin(), k.size()) ? 2 : 0;
          start = {};
        }

        return false;
      }
    );

    auto const push([&](std::size_t const i) noexcept
      {
        if (k.size() && (1 == index))
        {
          start = cur + i;
        }
        else if (!index)
        {
          val = cur;
        }
        else
        {
          --index;
        }
      }
    );

    auto const range([](auto const x, decltype(x) s, decltype(x) e) noexcept
      {
        return (x >= s) && (x <= e);
      }
    );

    for (auto const end(s.end()); cur != end; ++cur)
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
      assert(!"bad");
      return {};

      l_up:
      if (1 == depth) push(0);
      ++depth;
      continue;

      l_down:
      --depth;
      if ((1 == depth) && cap(0)) return r;
      continue;

      l_qup:
      if (1 == depth) push(1);
      state = S_STRING;
      continue;

      l_qdown:
      if ((1 == depth) && cap(-1)) return r;
      state = S_STRUCT;
      continue;

      l_bare:
      if (1 == depth) push(0);
      state = S_BARE;
      continue;

      l_unbare:
      if ((1 == depth) && cap(-1)) return r;
      state = S_STRUCT;
      goto again;
    }

    assert(!depth);
    return {};
  }

public:
  j50n() = default;

  j50n(j50n const&) = default;
  j50n(j50n&&) = default;

  j50n(auto&& ...s)
    noexcept(noexcept(decltype(s_)(std::forward<decltype(s)>(s)...)))
    requires(std::is_constructible_v<decltype(s_), decltype(s)&&...>):
    s_(std::forward<decltype(s)>(s)...)
  {
  }

  //
  j50n& operator=(j50n const&) = default;
  j50n& operator=(j50n&&) = default;

  j50n& operator=(auto&& u)
    noexcept(noexcept(s_ = std::forward<decltype(u)>(u)))
    requires(std::is_assignable_v<decltype(s_), decltype(u)&&>)
  {
    s_ = std::forward<decltype(u)>(u); return *this;
  }

  j50n operator[](auto&& k) const noexcept
  {
    using U = std::remove_cvref_t<decltype(k)>;

    if constexpr(std::is_convertible_v<U, std::string_view>)
    {
      return find(s_, std::forward<decltype(k)>(k));
    }
    else
    {
      return find(s_, {}, k);
    }
  }

  //
  bool is_valid() const noexcept { return s_.size(); }

  bool is_array() const noexcept { return is_valid() && ('[' == s_.front()); }
  bool is_object() const noexcept { return is_valid() && ('{' == s_.front()); }
  bool is_string() const noexcept { return is_valid() && ('"' == s_.front()); }

  //
  auto size() const noexcept
  {
    std::size_t i{};

    if (is_array()) for (; (*this)[i].is_valid(); ++i);

    return i;
  }

  //
  auto get() const noexcept
  {
    return is_string() ? decltype(s_)(std::next(s_.begin()), s_.end()) : s_;
  }

  auto& view() const noexcept { return s_; }
};
