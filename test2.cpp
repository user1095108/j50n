// test2.cpp - test suite for j50n.hpp
//
// Build (assertions ON: malformed-input tests are skipped, see test_malformed):
//   g++ -std=c++20 -Wall -Wextra -g -fsanitize=address,undefined -fno-sanitize-recover=all test2.cpp -o test2
// Build (assertions OFF: also runs the malformed-input tests; -Werror at -O2
// lets gcc's -Wmaybe-uninitialized guard the get<number>() result, which no
// runtime check can reliably do):
//   g++ -std=c++20 -Wall -Wextra -Werror -O2 -DNDEBUG test2.cpp -o test2_ndebug
// The scanner must not depend on the signedness of char, so also try:
//   g++ -std=c++20 -O1 -DNDEBUG -funsigned-char test2.cpp -o test2_uc   (and -fsigned-char)
// Build with a second translation unit (checks that j50n.hpp can be included
// from several .cpp files, i.e. that nothing in it violates the ODR):
//   g++ -std=c++20 -DJ50N_TEST_ODR test2.cpp test2_odr.cpp -o test2_odr
//
// Run:   ./test2 [-v|--verbose] [--strict]
//   -v        print every PASS line, not just failures and per-section totals
//   --strict  treat open KNOWN_BUG entries as failures (use this while fixing)
//
// Conventions
//   CHECK(cond)            plain boolean assertion
//   CHECK_EQ(a, b)         equality; j50n operands are compared via .get();
//                          prints actual/expected on failure
//   CHECK_GET(expr, v)     expr is a get<T>() result: must be {v, no error}
//   CHECK_GET_ERR(expr)    expr is a get<T>() result: must report an error
//   KNOWN_BUG(cond, why)   cond is the *correct* behaviour that the library
//                          currently gets wrong.  While it fails it is reported
//                          as [KNOWN]; the moment it passes it is reported as
//                          [XPASS] so it can be promoted to a normal CHECK.
//                          (No defect is open at the moment; the mechanism is
//                          kept for the next one.)
//
// Run it under the sanitizers with -fno-sanitize-recover=all: j50n is a
// pointer-arithmetic-heavy parser, and any new UB should fail the run.

#include "j50n.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// ── fixtures ────────────────────────────────────────────────────────────────

static constexpr std::string_view JSON(R"({
  "name": "j50n-test",
  "version": 1,
  "pi": 3.14159,
  "active": true,
  "ratio": null,
  "utf8": "héllø 🌍",
  "empty_arr": [],
  "empty_obj": {},
  "numbers": [1, 2, 3, 4, 5],
  "objects": [
    { "id": 1, "value": 10, "tag": "first" },
    { "id": 2, "value": 20, "tag": null },
    { "id": 3, "value": 30 }
  ],
  "nested": {
    "a": {
      "b": {
        "c": "deep"
      }
    }
  }
})");

static constexpr std::string_view JSON_ESCAPED(R"({
  "quote":    "say \"hello\"",
  "backslash":"one\\two",
  "newline":  "line1\nline2",
  "tab":      "col1\tcol2",
  "solidus":  "a\/b",
  "unicode":  "\u0041BC"
})");

static constexpr std::string_view JSON_NUMBERS(R"({
  "neg":      -42,
  "negfloat": -3.14,
  "zero":     0,
  "big":      9007199254740992,
  "sci":      1.5e2,
  "sci_neg":  2.5e-1,
  "uint_max": 4294967295
})");

static constexpr std::string_view JSON_BOOLS(R"({
  "yes": true,
  "no":  false,
  "num": 1
})");

// ── tiny test framework ─────────────────────────────────────────────────────

namespace t
{

struct State
{
  int checks{}, failures{}, known_open{}, known_fixed{};
  bool verbose{}, strict{};
};

inline State g;

inline std::string escape(std::string_view s)
{
  std::string r;
  for (char const c : s)
    switch (c)
    {
    case '\n': r += "\\n"; break;
    case '\r': r += "\\r"; break;
    case '\t': r += "\\t"; break;
    default:   r += c;
    }
  return r;
}

template <class T>
std::string show(T const& v)
{
  std::ostringstream os;
  os << std::boolalpha << std::setprecision(17);
  if constexpr (std::is_convertible_v<T const&, std::string_view>)
    os << '"' << escape(std::string_view(v)) << '"';
  else
    os << v;
  return os.str();
}

// j50n operands are compared through their textual value
template <class T>
decltype(auto) val(T const& x)
{
  if constexpr (std::is_same_v<T, j50n>)
    return x.get();
  else
    return (x);
}

inline void record(bool ok, std::string_view what, int line,
  std::string const& detail = {})
{
  ++g.checks;
  if (ok)
  {
    if (g.verbose) std::cout << "  [PASS] " << what << '\n';
    return;
  }
  ++g.failures;
  std::cout << "  [FAIL] line " << line << ": " << what << '\n' << detail;
}

template <class A, class B>
void check_eq(A const& a, B const& b, char const* expr, int line)
{
  auto&& x(val(a));
  auto&& y(val(b));
  bool const ok(x == y);

  record(ok, expr, line, ok ? std::string{} :
    "           actual:   " + show(x) + "\n           expected: " + show(y) +
    "\n");
}

template <class P, class E>
void check_get(P const& r, E const& expected, char const* expr, int line)
{
  bool const ok(!r.second && r.first == expected);

  record(ok, expr, line, ok ? std::string{} :
    "           actual:   " + (r.second ? std::string("<error>") :
      show(r.first)) + "\n           expected: " + show(expected) + "\n");
}

template <class P>
void check_get_err(P const& r, char const* expr, int line)
{
  record(r.second, expr, line,
    r.second ? std::string{} : "           expected an error, got value " +
      show(r.first) + "\n");
}

inline void known_bug(bool correct, char const* expr, char const* why,
  int line)
{
  if (correct)
  {
    ++g.known_fixed;
    std::cout << "  [XPASS] line " << line << ": " << expr
      << "\n          now behaves correctly -> promote to CHECK (" << why
      << ")\n";
  }
  else if (g.strict)
  {
    ++g.checks;
    ++g.failures;
    std::cout << "  [FAIL] line " << line << ": " << expr << "\n          "
      << why << '\n';
  }
  else
  {
    ++g.known_open;
    std::cout << "  [KNOWN] line " << line << ": " << expr << "\n          "
      << why << '\n';
  }
}

struct Section
{
  char const* name;
  int c0, f0;

  explicit Section(char const* n) : name(n), c0(g.checks), f0(g.failures)
  {
    if (g.verbose) std::cout << "== " << name << " ==\n";
  }

  ~Section()
  {
    int const c(g.checks - c0), f(g.failures - f0);
    std::cout << (f ? "[FAIL] " : "[ ok ] ") << name << ": " << c
      << " checks";
    if (f) std::cout << ", " << f << " failed";
    std::cout << '\n';
  }
};

} // namespace t

#define CHECK(cond)             ::t::record(bool(cond), #cond, __LINE__)
#define CHECK_EQ(a, b)          ::t::check_eq((a), (b), #a " == " #b, __LINE__)
#define CHECK_GET(expr, v)      ::t::check_get((expr), (v), #expr, __LINE__)
#define CHECK_GET_ERR(expr)     ::t::check_get_err((expr), #expr, __LINE__)
#define KNOWN_BUG(cond, why)    ::t::known_bug(bool(cond), #cond, (why), __LINE__)
#define SECTION(name)           ::t::Section section_guard_(name)

static std::string str(j50n const& j)
{
  std::ostringstream os;
  os << j;
  return os.str();
}

// ── compile-time properties ─────────────────────────────────────────────────

static_assert(sizeof(j50n) == sizeof(std::string_view),
  "j50n should be exactly a string_view");
static_assert(std::is_trivially_copyable_v<j50n>);
static_assert(std::is_nothrow_default_constructible_v<j50n>);
static_assert(std::is_nothrow_copy_constructible_v<j50n>);
static_assert(std::is_nothrow_move_constructible_v<j50n>);
static_assert(std::is_nothrow_copy_assignable_v<j50n>);
static_assert(std::is_nothrow_move_assignable_v<j50n>);
static_assert(std::is_nothrow_constructible_v<j50n, std::string_view>);
static_assert(noexcept(std::declval<j50n&>() = std::string_view()));
static_assert(noexcept(std::declval<j50n&>() = "x"));
static_assert(noexcept(std::declval<j50n const&>()["key"]));
static_assert(noexcept(std::declval<j50n const&>()[std::size_t{}]));
static_assert(noexcept(std::declval<j50n const&>().size()));
static_assert(noexcept(std::declval<j50n const&>().is_object()));
static_assert(noexcept(std::declval<j50n const&>().get()));
static_assert(std::is_same_v<decltype(std::declval<j50n const&>().get<int>("x")),
  std::pair<int, bool>>);
static_assert(std::is_same_v<decltype(std::declval<j50n const&>().get<bool>("x")),
  std::pair<bool, bool>>);

// ── tests ───────────────────────────────────────────────────────────────────

static void test_basic_access()
{
  SECTION("basic object access");
  j50n const root(JSON.data(), JSON.size());

  CHECK_EQ(root["name"],    "j50n-test");
  CHECK_EQ(root["version"], "1");
  CHECK_EQ(root["pi"],      "3.14159");
  CHECK_EQ(root["utf8"],    "héllø 🌍");
  CHECK_EQ(root["active"],  "true");
  CHECK_EQ(root["ratio"],   "null");

  CHECK_EQ(root["nested"]["a"]["b"]["c"],       "deep");
  CHECK_EQ(root.get("nested", "a", "b", "c"),   "deep");
  CHECK_EQ(root.view("nested", "a", "b")["c"],  "deep");

  // operator<< prints the same text as get()
  CHECK_EQ(str(root["name"]),    "j50n-test");
  CHECK_EQ(str(root["version"]), "1");
  CHECK_EQ(str(root["utf8"]),    "héllø 🌍");
  CHECK_EQ(str(root["numbers"]), "[1, 2, 3, 4, 5]");
  CHECK_EQ(str(root["missing"]), "");

  std::ostringstream os;
  os << root["name"] << '|' << root["version"] << '|' << root["pi"];
  CHECK_EQ(os.str(), "j50n-test|1|3.14159");
}

static void test_value_boundaries()
{
  // The sub-view returned for every kind of value must start and end exactly
  // at the value - not one character early or late.
  SECTION("exact value boundaries");
  j50n const root(JSON.data(), JSON.size());

  CHECK_EQ(root["numbers"].get(),   "[1, 2, 3, 4, 5]");
  CHECK_EQ(root["empty_arr"].get(), "[]");
  CHECK_EQ(root["empty_obj"].get(), "{}");
  CHECK_EQ(root["objects"][0].get(),
    R"({ "id": 1, "value": 10, "tag": "first" })");
  CHECK_EQ(root["objects"][2].get(), R"({ "id": 3, "value": 30 })");

  auto const b(root["nested"]["a"]["b"].get());
  CHECK(b.front() == '{' && b.back() == '}');
  CHECK(b.find("deep") != std::string_view::npos);

  j50n const m(R"j({"a":[1,2],"o":{"k":"v"},"s":"str","n":-1.5e3,"t":true,"z":null,"e":"","last":7})j");
  CHECK_EQ(m["a"].get(),    "[1,2]");
  CHECK_EQ(m["o"].get(),    R"j({"k":"v"})j");
  CHECK_EQ(m["s"].get(),    "str");
  CHECK_EQ(m["n"].get(),    "-1.5e3");
  CHECK_EQ(m["t"].get(),    "true");
  CHECK_EQ(m["z"].get(),    "null");
  CHECK_EQ(m["e"].get(),    "");
  CHECK_EQ(m["last"].get(), "7");   // bare value directly before the final '}'

  // j50n is non-owning: results point into the original buffer
  auto const sv(root["name"].get());
  CHECK(sv.data() == JSON.data() + JSON.find("j50n-test"));
  CHECK_EQ(sv.size(), 9u);
}

static void test_typed_getters()
{
  SECTION("typed getters");
  j50n const root(JSON.data(), JSON.size());

  CHECK_GET(root.get<int>("version"),    1);
  CHECK_GET(root.get<double>("pi"),      3.14159);
  CHECK_GET(root.get<bool>("active"),    true);
  CHECK_GET_ERR(root.get<int>("name"));
  CHECK_GET_ERR(root.get<double>("name"));
  CHECK_GET_ERR(root.get<int>("does_not_exist"));
  CHECK_GET_ERR(root.get<double>("does_not_exist"));
  CHECK_GET_ERR(root.get<bool>("does_not_exist"));
  CHECK_GET_ERR(root.get<int>("ratio"));          // null
  CHECK_GET_ERR(root.get<int>("numbers"));        // array
  CHECK_GET_ERR(root.get<int>("nested"));         // object

  j50n const nums(JSON_NUMBERS.data(), JSON_NUMBERS.size());
  CHECK_GET(nums.get<int>("neg"),                 -42);
  CHECK_GET(nums.get<double>("negfloat"),         -3.14);
  CHECK_GET(nums.get<int>("zero"),                0);
  CHECK_GET(nums.get<long long>("big"),           9007199254740992LL);
  CHECK_GET(nums.get<double>("big"),              9007199254740992.0);
  CHECK_GET(nums.get<double>("sci"),              150.0);
  CHECK_GET(nums.get<double>("sci_neg"),          0.25);
  CHECK_GET(nums.get<unsigned>("uint_max"),       4294967295u);
  CHECK_GET(nums.get<std::uint64_t>("uint_max"),  4294967295u);
  CHECK_GET(nums.get<float>("sci_neg"),           0.25f);

  // range / sign errors reported by std::from_chars
  CHECK_GET_ERR(nums.get<int>("uint_max"));       // 4294967295 > INT_MAX
  CHECK_GET_ERR(nums.get<unsigned>("neg"));       // negative -> unsigned
  CHECK_GET_ERR(nums.get<std::uint8_t>("uint_max"));

  j50n const edge(R"j({"i64":9007199254740993,"i64min":-9223372036854775808,"u64max":18446744073709551615,"over":18446744073709551616,"negz":-0.0,"E":1E5,"s":"123"})j");
  CHECK_GET(edge.get<std::int64_t>("i64"),    9007199254740993LL);   // > 2^53, exact
  CHECK_GET(edge.get<std::int64_t>("i64min"), INT64_MIN);
  CHECK_GET(edge.get<std::uint64_t>("u64max"), UINT64_MAX);
  CHECK_GET_ERR(edge.get<std::uint64_t>("over"));
  CHECK_GET(edge.get<double>("E"),            100000.0);
  {
    auto const [v, e](edge.get<double>("negz"));
    CHECK(!e && v == 0.0 && std::signbit(v));
  }
  // a number encoded as a JSON string is not coerced
  CHECK_GET_ERR(edge.get<int>("s"));
  CHECK_GET_ERR(edge.get<double>("s"));

  // bool
  j50n const bools(JSON_BOOLS.data(), JSON_BOOLS.size());
  CHECK_GET(bools.get<bool>("yes"), true);
  CHECK_GET(bools.get<bool>("no"),  false);
  CHECK_GET_ERR(bools.get<bool>("num"));
  CHECK_GET_ERR(root.get<bool>("name"));
  CHECK_GET_ERR(root.get<bool>("ratio"));
  j50n const strbool(R"j({"a":"true","b":True,"c":truex,"d":1})j");
  CHECK_GET_ERR(strbool.get<bool>("a"));          // string, not bool
  CHECK_GET_ERR(strbool.get<bool>("b"));          // case-sensitive
  CHECK_GET_ERR(strbool.get<bool>("c"));
  CHECK_GET_ERR(strbool.get<bool>("d"));

  // typed access directly on an element / sub-view
  CHECK_GET(root["numbers"][2].get<int>(), 3);
  CHECK_GET_ERR(root["nested"].get<int>("missing"));

  // trailing garbage and partial numbers are errors, never silently truncated
  j50n const junk(R"j({"a":12abc,"b":3.9,"c":0x1F,"d":1e,"e":1.5.3,"f":-,"g":1-2,"h":2.5})j");
  CHECK_GET_ERR(junk.get<int>("a"));
  CHECK_GET_ERR(junk.get<int>("b"));          // 3.9 is not an int
  CHECK_GET(junk.get<double>("b"), 3.9);
  CHECK_GET_ERR(junk.get<int>("c"));
  CHECK_GET_ERR(junk.get<double>("d"));
  CHECK_GET_ERR(junk.get<double>("e"));
  CHECK_GET_ERR(junk.get<double>("f"));
  CHECK_GET_ERR(junk.get<int>("g"));
  CHECK_GET_ERR(junk.get<int>("h"));
  CHECK(junk["b"].is_number() && junk["h"].is_number());
  for (char const* k : {"a", "c", "d", "e", "f", "g"})
    t::record(!junk[k].is_number(), std::string("!is_number(") + k + ")",
      __LINE__);
  CHECK_GET_ERR(root.get<int>("pi"));         // 3.14159 is not an int
  CHECK(root.get_or<int>(-1, "pi") == -1);

  // on failure the value is value-initialised, never indeterminate
  CHECK(root.get<int>("name").first == 0);
  CHECK(root.get<int>("does_not_exist").first == 0);
  CHECK(root.get<double>("name").first == 0.0);
  CHECK(nums.get<int>("uint_max").first == 0);       // out of range
}

static void test_get_or()
{
  SECTION("get_or");
  j50n const root(JSON.data(), JSON.size());

  // string fallback
  CHECK_EQ(root.get_or("DEFAULT", "name"),                  "j50n-test");
  CHECK_EQ(root.get_or("DEFAULT", "does_not_exist"),        "DEFAULT");
  CHECK_EQ(root.get_or("FB", "nested", "a", "b", "c"),      "deep");
  CHECK_EQ(root.get_or("FB", "nested", "a", "NOPE"),        "FB");
  CHECK_EQ(root.get_or("FB", "objects", 1, "tag"),          "null");
  CHECK_EQ(root.get_or("FB", "objects", 2, "tag"),          "FB");
  CHECK_EQ(root.get_or("FB", "objects", 9, "tag"),          "FB");
  CHECK_EQ(root.get_or(std::string_view("SV"), "nope"),     "SV");

  // an empty string is a value, not "missing"
  j50n const e(R"j({"e":"","n":null})j");
  CHECK_EQ(e.get_or("D", "e"), "");
  // current behaviour: JSON null is a present value and comes back as "null"
  CHECK_EQ(e.get_or("D", "n"), "null");

  // numeric fallback
  CHECK(root.get_or<int>(-1, "version")        == 1);
  CHECK(root.get_or<int>(-1, "does_not_exist") == -1);
  CHECK(root.get_or<int>(-1, "name")           == -1);   // not a number
  CHECK(root.get_or<int>(-1, "ratio")          == -1);   // null
  CHECK(root.get_or<int>(-1, "objects", 1, "value") == 20);
  CHECK(root.get_or<int>(-1, "objects", 2, "tag")   == -1);
  CHECK(root.get_or<double>(0.0, "pi")         == 3.14159);
  CHECK(root.get_or<double>(2.5, "nope")       == 2.5);

  // bool fallback
  CHECK(root.get_or<bool>(false, "active")      == true);
  CHECK(root.get_or<bool>(true,  "does_not_exist") == true);
  CHECK(root.get_or<bool>(false, "does_not_exist") == false);
  CHECK(root.get_or<bool>(true,  "name")        == true);   // wrong type -> default
}

static void test_type_predicates()
{
  SECTION("type predicates");
  j50n const j(R"j({"obj":{},"arr":[],"str":"s","num":-1,"flt":1.5,"t":true,"f":false,"nul":null,"emp":""})j");

  struct Expect
  {
    char const* key;
    bool object, array, string, bare, number, boolean, null, empty;
  };

  //                       obj    arr    str    bare   num    bool   null   empty
  Expect const table[] = {
    {"obj", true,  false, false, false, false, false, false, false},
    {"arr", false, true,  false, false, false, false, false, false},
    {"str", false, false, true,  false, false, false, false, false},
    {"emp", false, false, true,  false, false, false, false, false},
    {"num", false, false, false, true,  true,  false, false, false},
    {"flt", false, false, false, true,  true,  false, false, false},
    {"t",   false, false, false, true,  false, true,  false, false},
    {"f",   false, false, false, true,  false, true,  false, false},
    {"nul", false, false, false, true,  false, false, true,  false},
  };

  for (auto const& x : table)
  {
    auto const v(j[x.key]);
    std::string const k(x.key);
    t::record(v.is_object() == x.object,  k + ".is_object",  __LINE__);
    t::record(v.is_array()  == x.array,   k + ".is_array",   __LINE__);
    t::record(v.is_string() == x.string,  k + ".is_string",  __LINE__);
    t::record(v.is_bare()   == x.bare,    k + ".is_bare",    __LINE__);
    t::record(v.is_number() == x.number,  k + ".is_number",  __LINE__);
    t::record(v.is_bool()   == x.boolean, k + ".is_bool",    __LINE__);
    t::record(v.is_null()   == x.null,    k + ".is_null",    __LINE__);
    t::record(v.is_empty()  == x.empty,   k + ".is_empty",   __LINE__);
  }

  // the document root itself
  CHECK(j.is_object());
  CHECK(!j.is_array() && !j.is_string() && !j.is_bare() && !j.is_empty());

  // is_number must not accept non-numbers
  j50n const w(R"j({"a":abc,"b":-,"c":"1"})j");
  CHECK(!w["a"].is_number());
  CHECK(!w["b"].is_number());
  CHECK(!w["c"].is_number());
}

static void test_empty_and_missing()
{
  SECTION("empty / missing handling");
  j50n const root(JSON.data(), JSON.size());
  j50n const numbers(root["numbers"]);

  // missing keys and indices
  CHECK(root["does_not_exist"].is_empty());
  CHECK(root["does_not_exist"]["a"]["b"].is_empty());
  CHECK(root["nested"]["a"]["x"].is_empty());
  CHECK(root.view("nested", "a", "x").is_empty());
  CHECK(root.view("nope", "a", "b", "c").is_empty());
  CHECK(numbers[4].get() == "5");
  CHECK(numbers[5].is_empty());                  // one past the end
  CHECK(numbers[100].is_empty());
  CHECK(numbers[-1].is_empty());                 // wraps to SIZE_MAX
  CHECK(numbers[std::size_t(-1)].is_empty());
  CHECK(root["objects"][3].is_empty());
  CHECK(root["objects"][9]["id"].is_empty());
  CHECK(root["empty_arr"][0].is_empty());
  CHECK(root["name"]["x"].is_empty());           // key lookup in a string
  CHECK(root["version"]["x"].is_empty());        // ... and in a number
  CHECK(root["version"][0].is_empty());

  // empty containers
  CHECK_EQ(root["empty_arr"].size(), 0u);
  CHECK_EQ(root["empty_obj"].size(), 0u);
  CHECK(!root["empty_arr"].is_empty());          // "[]" is a value
  CHECK(!root["empty_obj"].is_empty());
  CHECK(root["empty_obj"]["x"].is_empty());

  // default constructed / blank documents
  j50n const def;
  CHECK(def.is_empty());
  CHECK(!def.is_array() && !def.is_object() && !def.is_string());
  CHECK(!def.is_bare() && !def.is_number());
  CHECK_EQ(def.size(), 0u);
  CHECK_EQ(def.get(), "");
  CHECK(def["a"].is_empty());
  CHECK(def[0].is_empty());
  CHECK_EQ(def.get_or("D", "a"), "D");
  CHECK_GET_ERR(def.get<int>("a"));
  CHECK_GET_ERR(def.get<bool>());
  CHECK(j50n("").is_empty());
  CHECK(j50n("")["a"].is_empty());
  CHECK(j50n("   \t\r\n").is_empty());
  CHECK(j50n("   \t\r\n")["a"].is_empty());
  CHECK(j50n("{}")["a"].is_empty());
  CHECK(j50n("[]")[0].is_empty());
  CHECK(j50n("{ }")["a"].is_empty());
  CHECK(!j50n("{}").is_empty());

  // top-level scalars have no members; unterminated ones are simply empty
  CHECK(j50n("\"abc\"")["x"].is_empty());
  CHECK(j50n("\"abc")["x"].is_empty());          // unterminated string
  CHECK(j50n("\"a\\")["x"].is_empty());          // ... ending in a lone '\'
  CHECK(j50n("\"\xC3")["x"].is_empty());         // ... or in half a UTF-8 char
  CHECK(j50n("abc")["x"].is_empty());
  CHECK(j50n("42")[0].is_empty());
  CHECK(j50n("true")["x"].is_empty());

  // is_null()/is_bool() on empty views (these used to be undefined behaviour:
  // `s_.begin() + npos`; run under UBSan to keep it that way)
  CHECK(!root["does_not_exist"].is_null());
  CHECK(!root["does_not_exist"].is_bool());
  CHECK(!def.is_null());
  CHECK(!j50n("  ").is_bool());
}

static void test_arrays()
{
  SECTION("arrays");
  j50n const root(JSON.data(), JSON.size());
  j50n const numbers(root["numbers"]);

  CHECK_EQ(numbers.size(), 5u);
  for (std::size_t i = 0; i < numbers.size(); ++i)
    t::record(numbers[i].get() == std::to_string(i + 1),
      "numbers[" + std::to_string(i) + "]", __LINE__);

  // heterogeneous array
  j50n const h(R"j([10,"two",true,null,{"x":1},[2,3],"",-4.5e1])j");
  CHECK_EQ(h.size(), 8u);
  CHECK_EQ(h[0], "10");
  CHECK(h[1].is_string() && h[1].get() == "two");
  CHECK(h[2].is_bool()   && h[2].get() == "true");
  CHECK(h[3].is_null());
  CHECK(h[4].is_object() && h[4]["x"].get() == "1");
  CHECK(h[5].is_array()  && h[5].size() == 2);
  CHECK_EQ(h[5][1], "3");
  CHECK(h[6].is_string() && !h[6].is_empty() && h[6].get().empty());
  CHECK_GET(h[7].get<double>(), -45.0);
  CHECK(h[8].is_empty());

  // nested arrays
  j50n const n(R"j([[1,2],[3],[],[[4]]])j");
  CHECK_EQ(n.size(), 4u);
  CHECK_EQ(n[0].size(), 2u);
  CHECK_EQ(n[1].size(), 1u);
  CHECK_EQ(n[2].size(), 0u);
  CHECK_EQ(n[3].size(), 1u);
  CHECK_EQ(n[0][1], "2");
  CHECK_EQ(n[2].get(), "[]");
  CHECK_EQ(n[3][0][0], "4");
  CHECK(n[3][0][1].is_empty());

  // array of strings; strings full of structural characters
  j50n const s(R"j(["a","b","c"])j");
  CHECK_EQ(s.size(), 3u);
  CHECK_EQ(s[2], "c");
  j50n const tricky(R"j(["a,b","]","[","}","{",":","\"]",""])j");
  CHECK_EQ(tricky.size(), 8u);
  CHECK_EQ(tricky[0], "a,b");
  CHECK_EQ(tricky[1], "]");
  CHECK_EQ(tricky[5], ":");
  CHECK_EQ(tricky[6], R"j(\"])j");
  CHECK_EQ(tricky[7], "");

  // whitespace-only and single-element arrays, top-level arrays
  CHECK_EQ(j50n("[ ]").size(), 0u);
  CHECK_EQ(j50n("[ 1 ]").size(), 1u);
  CHECK_EQ(j50n("[ 1 ]")[0], "1");
  CHECK_EQ(j50n("[7]")[0], "7");
  CHECK_EQ(j50n("[\"x\"]")[0], "x");
  CHECK_EQ(j50n("[[]]").size(), 1u);
  CHECK_EQ(j50n("[1,2,3]")[2], "3");
  CHECK(j50n("[1,2,3]")[3].is_empty());

  // size() of non-arrays is 0
  CHECK_EQ(root["name"].size(),      0u);
  CHECK_EQ(root["pi"].size(),        0u);
  CHECK_EQ(root["active"].size(),    0u);
  CHECK_EQ(root["ratio"].size(),     0u);
  CHECK_EQ(root["nested"].size(),    0u);
  CHECK_EQ(root["empty_obj"].size(), 0u);
  CHECK_EQ(root.size(),              0u);   // object root
  CHECK_EQ(root["objects"].size(),   3u);

  // arrays of objects
  CHECK_GET(root["objects"][0].get<int>("id"),    1);
  CHECK_GET(root["objects"][1].get<int>("value"), 20);
  CHECK_EQ(root["objects"][0]["tag"], "first");
  CHECK(root["objects"][1]["tag"].is_null());
  CHECK(root["objects"][2]["tag"].is_empty());     // key absent
  CHECK_GET(root["objects"][2].get<int>("value"), 30);
}

static void test_feach()
{
  SECTION("feach");
  j50n const root(JSON.data(), JSON.size());
  j50n const numbers(root["numbers"]);

  // (element) visitor
  {
    std::string seen;
    numbers.feach([&](j50n const& e) { seen += std::string(e.get()) + ","; });
    CHECK_EQ(seen, "1,2,3,4,5,");
  }
  // (element, index) visitor
  {
    std::string seen;
    numbers.feach([&](j50n const& e, std::size_t i)
      { seen += std::to_string(i) + "=" + std::string(e.get()) + ";"; });
    CHECK_EQ(seen, "0=1;1=2;2=3;3=4;4=5;");
  }
  // array of objects
  {
    int sum{};
    std::size_t last{};
    root["objects"].feach([&](j50n const& o, std::size_t i)
      { sum += o.get_or<int>(0, "value"); last = i; });
    CHECK_EQ(sum, 60);
    CHECK_EQ(last, 2u);
  }
  // nested arrays
  {
    std::size_t total{};
    j50n(R"j([[1,2],[3],[]])j").feach([&](j50n const& a) { total += a.size(); });
    CHECK_EQ(total, 3u);
  }
  // mutable visitor
  {
    std::string seen;
    numbers.feach([&seen, n = 0](j50n const& e) mutable
      { seen += std::to_string(n++) + ":" + std::string(e.get()) + ";"; });
    CHECK_EQ(seen, "0:1;1:2;2:3;3:4;4:5;");
  }

  // early exit: returning true stops iteration, no further calls are made
  {
    std::size_t count{};
    numbers.feach([&](j50n const& e) -> bool
      { ++count; return e.get<int>().first >= 3; });
    CHECK_EQ(count, 3u);
  }
  {
    std::size_t count{};
    numbers.feach([&](j50n const&, std::size_t) -> bool
      { ++count; return count == 2; });
    CHECK_EQ(count, 2u);
  }
  {
    std::size_t count{};                       // stop on first element
    numbers.feach([&](j50n const&) -> bool { ++count; return true; });
    CHECK_EQ(count, 1u);
  }
  {
    std::size_t count{};                       // stop on last element
    numbers.feach([&](j50n const&, std::size_t i) -> bool
      { ++count; return i == 4; });
    CHECK_EQ(count, 5u);
  }
  {
    std::size_t count{};                       // never stop
    numbers.feach([&](j50n const&) -> bool { ++count; return false; });
    CHECK_EQ(count, 5u);
  }

  // no-ops: empty array, non-arrays, missing, default constructed
  std::size_t calls{};
  auto const f([&](j50n const&) { ++calls; });
  auto const g([&](j50n const&, std::size_t) { ++calls; });
  root["empty_arr"].feach(f);
  root["empty_arr"].feach(g);
  root["name"].feach(f);                       // string
  root["pi"].feach(g);                         // number
  root["nested"].feach(f);                     // object
  root["empty_obj"].feach(g);
  root["does_not_exist"].feach(f);
  j50n().feach(g);
  j50n("[ ]").feach(f);
  CHECK_EQ(calls, 0u);
}

static void test_object_keys()
{
  SECTION("object key lookup");

  // keys sharing a prefix; searched key longer than any present key
  j50n const p(R"j({"ab":1,"a":2,"abc":3})j");
  CHECK_EQ(p["a"],   "2");
  CHECK_EQ(p["ab"],  "1");
  CHECK_EQ(p["abc"], "3");
  CHECK(p["abcd"].is_empty());
  CHECK(p["b"].is_empty());
  CHECK(p["A"].is_empty());                    // case-sensitive

  // a *value* equal to the searched key must not be taken for the key
  j50n const v(R"j({"x":"name","name":5,"o":{"name":9}})j");
  CHECK_EQ(v["name"], "5");
  CHECK_EQ(v["o"]["name"], "9");
  CHECK_EQ(v["x"], "name");

  // keys of nested containers are invisible from the parent
  j50n const w(R"j({"o":{"name":1},"arr":[{"name":2}]})j");
  CHECK(w["name"].is_empty());
  CHECK_EQ(w["o"]["name"], "1");
  CHECK_EQ(w["arr"][0]["name"], "2");
  j50n const w2(R"j({"o":{"name":1},"name":2})j");
  CHECK_EQ(w2["name"], "2");                   // depth-1 key wins

  // duplicate keys: first occurrence wins
  CHECK_EQ(j50n(R"j({"a":1,"a":2})j")["a"], "1");

  // first / middle / last position, and values of every type before the key
  j50n const pos(R"j({"first":1,"s":"x","o":{"a":[1,{"b":2}]},"arr":[[],{}],"z":null,"last":"L"})j");
  CHECK_EQ(pos["first"], "1");
  CHECK_EQ(pos["last"],  "L");
  CHECK(pos["z"].is_null());
  CHECK_EQ(pos["o"]["a"][1]["b"], "2");

  // keys are matched against their raw (still escaped) text
  j50n const esc(R"j({"a\"b":1,"c":2})j");
  CHECK_EQ(esc[R"j(a\"b)j"], "1");
  CHECK(esc["a\"b"].is_empty());
  CHECK_EQ(esc["c"], "2");

  // key/value lookups work with every key type
  j50n const x(R"j({"a":7})j");
  std::string const key("a");
  std::string_view const kv("a");
  char const* const kp("a");
  CHECK_EQ(x[key],               "7");
  CHECK_EQ(x[std::string("a")],  "7");
  CHECK_EQ(x[kv],                "7");
  CHECK_EQ(x[kp],                "7");
  CHECK_EQ(x["a"],               "7");
  CHECK_EQ(x.get(key),           "7");
  CHECK(x[std::string("zz")].is_empty());

  // keys with structural characters inside them
  j50n const s(R"j({"k,}":1,"[x]":2,"a:b":3,"after":4})j");
  CHECK_EQ(s["k,}"],  "1");
  CHECK_EQ(s["[x]"],  "2");
  CHECK_EQ(s["a:b"],  "3");
  CHECK_EQ(s["after"], "4");

  // whole-document positional access: top-level array
  j50n const top(R"j([{"id":1},{"id":2}])j");
  CHECK_EQ(top[1]["id"], "2");

  // the empty key "" is an ordinary key, not a positional lookup
  j50n const ek(R"j({"a":1,"":5,"b":2})j");
  CHECK_EQ(ek[""],  "5");
  CHECK_EQ(ek["a"], "1");
  CHECK_EQ(ek["b"], "2");
  CHECK_EQ(ek[std::string()],      "5");
  CHECK_EQ(ek[std::string_view()], "5");
  CHECK_EQ(j50n(R"j({"":7})j")[""], "7");
  CHECK_EQ(j50n(R"j({"":{"x":1}})j")[""]["x"], "1");
  CHECK(j50n(R"j({"":""})j")[""].is_string());
  CHECK(!j50n(R"j({"":""})j")[""].is_empty());
  CHECK(j50n(R"j({"a":1,"b":2})j")[""].is_empty());   // absent: not the 1st value
  CHECK(j50n(R"j({"a":1})j")[std::string_view()].is_empty());
  CHECK(j50n("{}")[""].is_empty());
  CHECK(j50n(R"j({"a":"","b":2})j")[""].is_empty());  // a value "" is no key

  // key lookup only applies to objects: array elements are not keys
  j50n const sarr(R"j(["a","b","c"])j");
  CHECK(sarr["a"].is_empty());
  CHECK(sarr["b"].is_empty());
  CHECK(sarr[""].is_empty());
  CHECK(j50n("[1,2,3]")["1"].is_empty());
  CHECK(j50n("[true,false]")["true"].is_empty());
  CHECK(j50n(R"j([{"a":1}])j")["a"].is_empty());
  CHECK(j50n(R"j({"arr":["a","b"],"a":9})j")["arr"]["a"].is_empty());
  CHECK_EQ(j50n(R"j({"arr":["a","b"],"a":9})j")["a"], "9");
  CHECK_EQ(sarr[1], "b");                            // positional is unaffected
}

static void test_path_access()
{
  SECTION("path access (view / get / get_or with keys and indices)");
  j50n const root(JSON.data(), JSON.size());

  CHECK_GET(root.get<int>("objects", 1, "value"), 20);
  CHECK_GET(root.get<int>("objects", std::size_t(2), "id"), 3);
  CHECK_GET(root.get<int>("numbers", 4), 5);
  CHECK_EQ(root.get("objects", 0, "tag"), "first");
  CHECK_EQ(root.view("objects", 2).get<int>("value").first, 30);
  CHECK(root.view("objects", 2, "tag").is_empty());
  CHECK(root.view("objects", 7, "tag").is_empty());
  CHECK(root.view("numbers", 1, "x").is_empty());

  std::size_t i = 1;
  std::string const k("value");
  CHECK_GET(root.get<int>("objects", i, k), 20);

  // view() with no arguments is the identity
  CHECK_EQ(root.view().get(), JSON);
  CHECK(root.view("nested").is_object());

  // view() result is itself navigable
  auto const n(root.view("nested", "a"));
  CHECK_EQ(n["b"]["c"], "deep");
  CHECK_EQ(n.get("b", "c"), "deep");
}

static void test_strings_and_escapes()
{
  // j50n returns the raw JSON text between the quotes; it does not unescape.
  SECTION("strings and escapes (raw content preserved)");
  j50n const esc(JSON_ESCAPED.data(), JSON_ESCAPED.size());

  CHECK(esc["quote"].is_string());
  CHECK_EQ(esc["quote"].get(),     R"j(say \"hello\")j");
  CHECK_EQ(esc["backslash"].get(), R"j(one\\two)j");
  CHECK_EQ(esc["newline"].get(),   R"j(line1\nline2)j");
  CHECK_EQ(esc["tab"].get(),       R"j(col1\tcol2)j");
  CHECK_EQ(esc["solidus"].get(),   R"j(a\/b)j");
  CHECK_EQ(esc["unicode"].get(),   R"j(\u0041BC)j");

  // structural characters and escapes must not derail the scanner
  j50n const tricky(R"j({"s":"x,}]{[:","b":1,"e":"q\"}","bs":"\\","after":3,"bs2":"a\\\\","z":4})j");
  CHECK_EQ(tricky["s"].get(),     "x,}]{[:");
  CHECK_EQ(tricky["b"],           "1");
  CHECK_EQ(tricky["e"].get(),     R"j(q\"})j");
  CHECK_EQ(tricky["bs"].get(),    R"j(\\)j");      // string ending in "\\"
  CHECK_EQ(tricky["after"],       "3");
  CHECK_EQ(tricky["bs2"].get(),   R"j(a\\\\)j");
  CHECK_EQ(tricky["z"],           "4");

  // empty string
  j50n const e(R"j({"e":"","n":"x"})j");
  CHECK(e["e"].is_string());
  CHECK(!e["e"].is_empty());
  CHECK_EQ(e["e"].get().size(), 0u);
  CHECK_EQ(e["n"], "x");

  // string vs. bare number
  j50n const q(R"j({"s":"123","n":123})j");
  CHECK(q["s"].is_string() && !q["s"].is_bare() && !q["s"].is_number());
  CHECK(!q["n"].is_string() && q["n"].is_bare() && q["n"].is_number());
}

static void test_string_blanks()
{
  // Blanks inside string values are data and must survive, even though the
  // library trims whitespace around documents and views.
  SECTION("blanks inside strings are preserved");
  j50n const s(R"j({"a":"x  ","b":"  ","c":"  lead","d":" mid ","e":["p  ","q "],"f":"tail ","key  ":"v","k":"z","g":"end "})j");

  CHECK_EQ(s["a"].get(), "x  ");
  CHECK_EQ(s["b"].get(), "  ");
  CHECK_EQ(s["c"].get(), "  lead");
  CHECK_EQ(s["d"].get(), " mid ");
  CHECK_EQ(s["e"][0].get(), "p  ");
  CHECK_EQ(s["e"][1].get(), "q ");
  CHECK_EQ(s["f"].get(), "tail ");
  CHECK_EQ(s["g"].get(), "end ");                    // last member
  CHECK_EQ(s["a"].get().size(), 3u);
  CHECK_EQ(s["b"].get().size(), 2u);
  CHECK(s["b"].is_string() && !s["b"].is_empty());
  CHECK_EQ(str(s["a"]), "x  ");
  CHECK_EQ(str(s["b"]), "  ");
  CHECK_EQ(s.get_or("D", "b"), "  ");
  CHECK_EQ(s.get("e", 0), "p  ");

  // keys with trailing blanks are matched exactly
  CHECK_EQ(s["key  "], "v");
  CHECK(s["key"].is_empty());
  CHECK(s["key "].is_empty());
  CHECK_EQ(s["k"], "z");

  // string documents / sub-documents
  CHECK_EQ(j50n(R"j("abc")j").get(),         "abc");
  CHECK_EQ(j50n("  \"abc\"  ").get(),        "abc");
  CHECK_EQ(j50n("\"abc  \"").get(),          "abc  ");
  CHECK_EQ(j50n("\"  \"").get(),             "  ");
  CHECK_EQ(j50n("\"\"").get(),               "");
  CHECK(j50n("\"\"").is_string());
  CHECK(!j50n("\"\"").is_empty());
  CHECK_EQ(j50n(R"j("a\\")j").get(),       R"j(a\\)j");    // ends in an escaped backslash
  CHECK_EQ(j50n(R"j("say \"hi\"")j").get(), R"j(say \"hi\")j");
  CHECK_EQ(str(j50n("\"abc\"")),             "abc");

  // the closing quote is dropped only if there is one
  CHECK_EQ(j50n("\"abc").get(),               "abc");
  CHECK_EQ(j50n("\"").get(),                  "");
}

static void test_utf8()
{
  SECTION("UTF-8");
  // multi-byte sequences in values and keys, as explicit bytes
  std::string const doc(
    "{\"h" "\xC3\xA9" "llo\":1,"          // 2-byte  e-acute
    "\"" "\xE2\x82\xAC" "\":2,"           // 3-byte  euro sign
    "\"" "\xF0\x9F\x8C\x8D" "\":3,"       // 4-byte  globe
    "\"v\":\"" "\xC3\xB8\xE2\x82\xAC\xF0\x9F\x8C\x8D" "\","
    "\"end\":4}");
  j50n const j(doc);

  CHECK_EQ(j["h\xC3\xA9llo"],          "1");
  CHECK_EQ(j["\xE2\x82\xAC"],          "2");
  CHECK_EQ(j["\xF0\x9F\x8C\x8D"],      "3");
  CHECK_EQ(j["v"].get(), "\xC3\xB8\xE2\x82\xAC\xF0\x9F\x8C\x8D");
  CHECK_EQ(j["end"],                   "4");
  CHECK(j["h"].is_empty());                    // partial UTF-8 key is no match

  // the fixture's literal text, byte-exact
  j50n const root(JSON.data(), JSON.size());
  CHECK_EQ(root["utf8"].get().size(), 12u);   // h é(2) l l ø(2) ' ' 🌍(4)
}

static void test_character_classes()
{
  // The scanner's rules, byte by byte.
  SECTION("character classes");

  // string content: any printable ASCII character except '"' and '\'
  {
    std::string all;
    for (int c = 32; c <= 126; ++c)
      if (c != '"' && c != '\\') all += char(c);

    std::string const doc("{\"" + all + "\":\"" + all + "\",\"after\":1}");
    j50n const j(doc);
    CHECK_EQ(j[all].get(), all);                // as key and as value
    CHECK_EQ(j["after"], "1");
    CHECK(j[all.substr(1)].is_empty());
    CHECK(j[all.substr(0, all.size() - 1)].is_empty());
  }

  // UTF-8: a lead byte followed by the right number of continuation bytes
  for (int b = 0xC2; b <= 0xF4; ++b)
  {
    int const need((b <= 0xDF) ? 1 : (b <= 0xEF) ? 2 : 3);
    std::string const body(std::string(1, char(b)) + std::string(need, '\x80'));
    std::string const doc("{\"" + body + "\":\"" + body + "\",\"c\":2}");
    j50n const j(doc);

    t::record(j[body].get() == body && j["c"].get() == "2",
      "UTF-8 sequence with lead byte " + std::to_string(b), __LINE__);
  }

  // bare words (numbers, true, ...): a bare word runs up to a blank or one of
  // , : ] }  and anything printable may appear inside it (current rule)
  for (int c = 33; c <= 126; ++c)
  {
    if (c == ',' || c == ':' || c == ']' || c == '}') continue;

    std::string const w(std::string("x") + char(c) + "y");
    std::string const doc("{\"a\":" + w + ",\"b\":2}");   // j50n does not own it
    j50n const j(doc);

    t::record(j["a"].get() == w && j["b"].get() == "2",
      "bare word containing '" + std::string(1, char(c)) + "'", __LINE__);
  }
  for (char const* sep : {" ", "\t", "\r", "\n", ",", ":"})
  {
    std::string const doc(std::string("{\"a\":12") + sep + "\"b\":3}");
    j50n const j(doc);

    t::record(j["a"].get() == "12" && j["b"].get() == "3",
      "bare word ended by " + t::show(std::string(sep)), __LINE__);
  }
  CHECK_EQ(j50n("[1]")[0], "1");                // ... and by ] and }
  CHECK_EQ(j50n("{\"a\":1}")["a"], "1");

  // a bare word starts with '-' or an ASCII letter or digit
  for (int c = 33; c <= 126; ++c)
  {
    if (c != '-' && !(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'Z') &&
      !(c >= 'a' && c <= 'z')) continue;

    std::string const doc(std::string("{\"a\":") + char(c) + "z,\"b\":2}");

    t::record(j50n(doc)["b"].get() == "2",
      "bare word starting with '" + std::string(1, char(c)) + "'", __LINE__);
  }

#ifdef NDEBUG
  // Malformed bytes are rejected: the result is empty (with assertions on, the
  // scanner aborts instead).  0x7F, 0xC0, 0xC1 and 0xF5..0xF7 are left out:
  // whether they are accepted is not a rule worth pinning.
  for (int b = 0; b < 256; ++b)
  {
    if (b == '"' || b == '\\' || (b >= 32 && b <= 126) || b == 0x7F ||
      (b >= 0xC0 && b <= 0xC1) || (b >= 0xC2 && b <= 0xF4) ||
      (b >= 0xF5 && b <= 0xF7)) continue;

    std::string const doc("{\"a\":\"" + std::string(1, char(b)) + "\",\"c\":2}");

    t::record(j50n(doc)["c"].is_empty(),
      "byte " + std::to_string(b) + " in a string is rejected", __LINE__);
  }

  // a lead byte needs all of its continuation bytes, and only those
  for (int b = 0xC2; b <= 0xF4; ++b)
  {
    int const need((b <= 0xDF) ? 1 : (b <= 0xEF) ? 2 : 3);
    std::string const lead(1, char(b));
    std::string const shortseq(lead + std::string(need - 1, '\x80'));
    std::string const wrong(shortseq + "a");          // not a continuation

    t::record(j50n("{\"a\":\"" + shortseq + "\",\"c\":2}")["c"].is_empty() &&
      j50n("{\"a\":\"" + wrong + "\",\"c\":2}")["c"].is_empty(),
      "UTF-8 sequence with lead byte " + std::to_string(b) + " truncated",
      __LINE__);
  }

  // bytes that cannot be inside a bare word
  for (int b = 0; b < 256; ++b)
  {
    if ((b >= 33 && b <= 126) || b == ' ' || b == '\t' || b == '\r' ||
      b == '\n' || b == 0x7F) continue;

    std::string const doc("{\"a\":x" + std::string(1, char(b)) + "y,\"b\":2}");

    t::record(j50n(doc)["b"].is_empty(),
      "byte " + std::to_string(b) + " in a bare word is rejected", __LINE__);
  }

  // printable characters that cannot start a bare word
  for (int c = 33; c <= 126; ++c)
  {
    if (c == '-' || c == '"' || c == '[' || c == '{' || c == ']' || c == '}' ||
      c == ',' || c == ':' || (c >= '0' && c <= '9') ||
      (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) continue;

    std::string const doc(std::string("{\"a\":") + char(c) + "z,\"b\":2}");

    t::record(j50n(doc)["b"].is_empty(),
      "'" + std::string(1, char(c)) + "' cannot start a bare word", __LINE__);
  }
#endif
}

static void test_whitespace_and_formatting()
{
  SECTION("whitespace and formatting");

  j50n const crlf("{\r\n\t\"a\"\t:\r\n 1 ,\t\"b\" : [ 1 ,\n2 ]\r\n}");
  CHECK_EQ(crlf["a"], "1");
  CHECK_EQ(crlf["b"].get(), "[ 1 ,\n2 ]");
  CHECK_EQ(crlf["b"].size(), 2u);
  CHECK_EQ(crlf["b"][1], "2");

  j50n const lead("  \n\t {\"a\":1}  \n");
  CHECK_EQ(lead["a"], "1");
  CHECK(lead.is_object());

  j50n const leadarr("\n [1,2] ");
  CHECK(leadarr.is_array());
  CHECK_EQ(leadarr.size(), 2u);
  CHECK_EQ(leadarr[1], "2");

  j50n const mini(R"j({"a":1,"b":[1,2],"c":{"d":4},"e":"s","f":true,"g":null})j");
  CHECK_EQ(mini["a"], "1");
  CHECK_EQ(mini["b"].get(), "[1,2]");
  CHECK_EQ(mini["c"]["d"], "4");
  CHECK_EQ(mini["e"], "s");
  CHECK(mini["f"].is_bool());
  CHECK(mini["g"].is_null());

  j50n const spaced("{ \"a\" : { \"b\" : [ { \"c\" : 1 } ] } }");
  CHECK_EQ(spaced.get("a", "b", 0, "c"), "1");

  // type predicates skip leading whitespace on a hand-built top-level value
  CHECK(j50n(" \"x\"").is_string());
  CHECK(j50n("\n\t\"x\"").is_string());
  CHECK(j50n(" [1]").is_array());
  CHECK(j50n(" {}").is_object());
  CHECK(j50n("  true").is_bool());
  CHECK(j50n("  false").is_bool());
  CHECK(j50n(" null").is_null());
  CHECK(!j50n(" [1]").is_string());
  CHECK(!j50n(" \"x\"").is_array());

  // a top-level scalar followed by a newline (typical file contents)
  CHECK_GET(j50n("42\n").get<int>(), 42);
  CHECK_GET(j50n("-1.5\r\n").get<double>(), -1.5);

  // is_bare() looks at the first non-blank character
  CHECK(!j50n(" [1]").is_bare());
  CHECK(!j50n(" {}").is_bare());
  CHECK(!j50n(" \"x\"").is_bare());
  CHECK(j50n(" 42 ").is_bare());

  // scalars with surrounding whitespace behave like tight ones
  CHECK(j50n(" 42").is_number());
  CHECK_GET(j50n(" 42").get<int>(), 42);
  CHECK_GET(j50n(" true").get<bool>(), true);
  CHECK_GET(j50n("\n false \n").get<bool>(), false);
  CHECK(j50n("true\n").is_bool());
  CHECK(j50n("\t false \r\n").is_bool());
  CHECK(j50n("null\n").is_null());
  CHECK(j50n(" null ").is_null());
  CHECK(!j50n(" nul ").is_null());
}

static void test_lenient_extensions()
{
  // The scanner treats ',' and ':' as whitespace and accepts bare words as
  // keys/values.  These pin the *current* (JSON5-flavoured) behaviour; update
  // them if the parser is ever made strict.
  SECTION("lenient / JSON5-style input (current behaviour)");

  j50n const j("{a:1, b:2, c:[1,2,3,],}");
  CHECK_EQ(j["a"], "1");                       // unquoted keys
  CHECK_EQ(j["b"], "2");
  CHECK_EQ(j["c"].size(), 3u);                 // trailing comma in array
  CHECK(j["d"].is_empty());                    // trailing comma in object

  j50n const sp("[NaN, Infinity]");
  CHECK(sp[0].is_number() && std::isnan(sp[0].get<double>().first));
  CHECK(sp[1].is_number() && std::isinf(sp[1].get<double>().first));
}

static void test_construction_and_assignment()
{
  SECTION("construction, copy, move, assignment");
  std::string const s(R"j({"a":7})j");

  CHECK_EQ(j50n(R"j({"a":8})j")["a"],               "8");   // char array
  CHECK_EQ(j50n(s)["a"],                            "7");   // std::string
  CHECK_EQ(j50n(std::string_view(s))["a"],          "7");   // string_view
  CHECK_EQ(j50n(s.data(), s.size())["a"],           "7");   // ptr + len
  CHECK_EQ(j50n(s.begin(), s.end())["a"],           "7");   // iterator pair
  CHECK_EQ(j50n(s.c_str())["a"],                    "7");   // const char*
  CHECK_EQ(j50n(JSON)["name"],                      "j50n-test");

  // char-array ctor must not include the terminating NUL
  j50n const lit("[1,2]");
  CHECK_EQ(lit.get(), "[1,2]");
  CHECK_EQ(lit.size(), 2u);

  // copy / move
  j50n const root(JSON.data(), JSON.size());
  j50n a(root["name"]);
  j50n b(a);
  CHECK_EQ(b.get(), "j50n-test");
  j50n c;
  c = a;
  CHECK_EQ(c.get(), "j50n-test");
  j50n d(std::move(b));
  CHECK_EQ(d.get(), "j50n-test");
  j50n e;
  e = std::move(d);
  CHECK_EQ(e.get(), "j50n-test");
  CHECK_EQ(a.get(), "j50n-test");              // source of copies unaffected

  // assignment from each source kind
  j50n j;
  j = std::string_view(s);
  CHECK_EQ(j["a"], "7");
  j = R"j({"a":9})j";                          // char array
  CHECK_EQ(j["a"], "9");
  j = s.c_str();                               // const char*
  CHECK_EQ(j["a"], "7");
  j = s;                                       // std::string
  CHECK_EQ(j["a"], "7");
  j = root["numbers"];                         // another j50n
  CHECK(j.is_array());
  j = j50n();
  CHECK(j.is_empty());

  // every constructor trims surrounding whitespace
  std::string const w("  \t[1,2]\r\n ");
  CHECK(j50n(w).is_array());
  CHECK_EQ(j50n(w).get(),                       "[1,2]");
  CHECK_EQ(j50n(w.data(), w.size()).get(),      "[1,2]");
  CHECK_EQ(j50n(w.begin(), w.end()).get(),      "[1,2]");
  CHECK_EQ(j50n(w.c_str()).get(),               "[1,2]");
  CHECK_EQ(j50n(std::string_view(w)).get(),     "[1,2]");
  CHECK_EQ(j50n("  [1,2] ").get(),              "[1,2]");
  CHECK_EQ(j50n(w).size(),                      2u);
  CHECK(j50n(" \t\r\n").is_empty());
  CHECK(j50n(std::string(" ")).is_empty());
  {
    j50n const j(w);                            // still zero-copy
    CHECK(j.get().data() == w.data() + 3);
  }

  // ... and so does assignment, whichever source it comes from
  {
    j50n j;
    j = std::string_view(w);
    CHECK(j.is_array() && j.size() == 2);
    CHECK_EQ(j.get(), "[1,2]");
    j = "  [1,2,3] ";                           // char array
    CHECK(j.is_array() && j.size() == 3);
    j = w.c_str();                              // const char*
    CHECK(j.is_array() && j.size() == 2);
    j = w;                                      // std::string
    CHECK(j.is_array() && j.size() == 2);
    j = " \t ";
    CHECK(j.is_empty() && !j.is_array());
    j = std::string_view("  42 ");
    CHECK_GET(j.get<int>(), 42);
    CHECK(j.is_number() && j.is_bare());
    j = "\n true\n";
    CHECK(j.is_bool());
    CHECK_GET(j.get<bool>(), true);
    j = "  null ";
    CHECK(j.is_null());
    j = " \"x \" ";
    CHECK(j.is_string());
    CHECK_EQ(j.get(), "x ");
  }

  // non-owning: results from an old document stay valid after re-pointing
  j50n doc(JSON.data(), JSON.size());
  j50n const name(doc["name"]);
  doc = std::string_view(JSON_NUMBERS);
  CHECK_EQ(doc["neg"], "-42");
  CHECK_EQ(name.get(), "j50n-test");
  CHECK(doc["name"].is_empty());
}

static void test_scale_and_nesting()
{
  SECTION("scale and deep nesting");

  // deeply nested objects: the scanner is iterative, so no stack overflow
  {
    constexpr int N = 10000;
    std::string s;
    for (int i = 0; i < N; ++i) s += "{\"k\":";
    s += "42";
    s.append(N, '}');
    j50n const d(s);
    CHECK_EQ(d["k"].get().size(), s.size() - 6);    // strip outer "{\"k\":" and "}"
  }
  {
    constexpr int N = 10000;
    std::string const s("[" + std::string(N, '[') + std::string(N, ']') + "]");
    j50n const d(s);
    CHECK_EQ(d.size(), 1u);
    CHECK_EQ(d[0].size(), 1u);
  }
  {
    // walk down a moderately deep document one key at a time
    constexpr int N = 200;
    std::string s;
    for (int i = 0; i < N; ++i) s += "{\"k\":";
    s += "\"bottom\"";
    s.append(N, '}');
    j50n cur(s);
    for (int i = 0; i < N; ++i) cur = cur["k"];
    CHECK_EQ(cur.get(), "bottom");
  }

  // many keys: first, last and absent
  {
    constexpr int N = 20000;
    std::string s("{");
    for (int i = 0; i < N; ++i)
    {
      if (i) s += ',';
      s += "\"key" + std::to_string(i) + "\":" + std::to_string(i);
    }
    s += '}';
    j50n const d(s);
    CHECK_EQ(d["key0"],     "0");
    CHECK_EQ(d["key19999"], "19999");
    CHECK_EQ(d["key12345"], "12345");
    CHECK(d["key20000"].is_empty());
    CHECK(d["key"].is_empty());
  }

  // long array.  (size() and feach() are O(n^2): each element access
  // rescans from the start; keep n modest.)
  {
    constexpr std::size_t N = 2000;
    std::string s("[");
    for (std::size_t i = 0; i < N; ++i)
    {
      if (i) s += ',';
      s += std::to_string(i);
    }
    s += ']';
    j50n const d(s);
    CHECK_EQ(d.size(), N);
    CHECK_EQ(d[N - 1], std::to_string(N - 1));
    CHECK(d[N].is_empty());
    std::size_t n{}, sum{};
    d.feach([&](j50n const& e, std::size_t i)
      { ++n; sum += i + e.get<std::size_t>().first; });
    CHECK_EQ(n, N);
    CHECK_EQ(sum, N * (N - 1));                  // sum(i) + sum(value)
  }
}

static void test_lazy_scanning()
{
  // The scanner stops as soon as the requested value is complete, so damage
  // *after* the target does not affect the result (in any build mode).
  SECTION("lazy scanning stops at the match");

  CHECK_EQ(j50n(R"j({"a":1,"b":"\x"})j")["a"], "1");
  CHECK_EQ(j50n(R"j({"a":"ok","b":@})j")["a"], "ok");
  CHECK_EQ(j50n(R"j({"a":[1,2],"b":'x'})j")["a"], "[1,2]");
  CHECK_EQ(j50n(R"j([1,2,@])j")[1], "2");
}

static void test_malformed()
{
  SECTION("malformed input");
#ifdef NDEBUG
  // With assertions disabled the scanner reports malformed input by returning
  // an empty view.  With assertions enabled it aborts instead (assert in
  // j50n::S::find), so these cases cannot run in a normal debug build.
  CHECK(j50n(R"j({"a":"unterminated)j")["a"].is_empty());
  CHECK(j50n(R"j({"a":"bad\xesc","b":2})j")["b"].is_empty());
  CHECK(j50n("{\"a\":\"raw\nnewline\",\"b\":2}")["b"].is_empty());
  CHECK(j50n("{\"a\":\"\x80\",\"b\":2}")["b"].is_empty());     // stray continuation
  CHECK(j50n("{\"a\":\"\xC3\",\"b\":2}")["b"].is_empty());     // truncated sequence
  CHECK(j50n("{\"a\":\"\xE2\x82\",\"b\":2}")["b"].is_empty());
  CHECK(j50n("{'a':1}")["a"].is_empty());                      // single quotes
  CHECK(j50n("{\"a\":@,\"b\":2}")["b"].is_empty());            // junk token
  CHECK(j50n("{{\"a\":1}")["a"].is_empty());                   // unbalanced
  CHECK(j50n("\xC3\xA9")["a"].is_empty());                     // UTF-8 outside string

  // documents that end inside a token
  CHECK(j50n("{\"a\":\"x\\")["a"].is_empty());                // after a '\'
  CHECK(j50n("{\"a\":\"\xC3")["a"].is_empty());                // inside a UTF-8 char
  CHECK(j50n("{\"a\":\"\xE2\x82")["a"].is_empty());
  CHECK(j50n("{\"a\":\"abc")["a"].is_empty());                // inside a string
  CHECK(j50n("{\"a\":12")["a"].is_empty());                    // inside a bare word
  CHECK(j50n("{\"a")["a"].is_empty());                         // inside a key
  CHECK(j50n("[1,2")[1].is_empty());
#else
  std::cout << "  (skipped: build with -DNDEBUG to run the malformed-input "
               "tests; with assertions enabled they abort by design)\n";
#endif
}

#ifdef J50N_TEST_ODR
// Defined in test2_odr.cpp, which also includes j50n.hpp.  Linking the two
// translation units fails ("multiple definition") if anything in the header,
// e.g. operator<<, is not inline.
std::string odr_other_tu();
#endif

static void test_odr()
{
#ifdef J50N_TEST_ODR
  SECTION("ODR (second translation unit)");
  CHECK_EQ(odr_other_tu(), "x|1");
  CHECK_EQ(str(j50n(R"j({"k":"x"})j")["k"]), "x");
#endif
}

// ── main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
  for (int i = 1; i < argc; ++i)
  {
    std::string_view const a(argv[i]);
    if (a == "-v" || a == "--verbose") t::g.verbose = true;
    else if (a == "--strict")          t::g.strict = true;
    else
    {
      std::cerr << "usage: " << argv[0] << " [-v|--verbose] [--strict]\n";
      return 2;
    }
  }

  test_basic_access();
  test_value_boundaries();
  test_typed_getters();
  test_get_or();
  test_type_predicates();
  test_empty_and_missing();
  test_arrays();
  test_feach();
  test_object_keys();
  test_path_access();
  test_strings_and_escapes();
  test_string_blanks();
  test_utf8();
  test_character_classes();
  test_whitespace_and_formatting();
  test_lenient_extensions();
  test_construction_and_assignment();
  test_scale_and_nesting();
  test_lazy_scanning();
  test_malformed();
  test_odr();

  std::cout << "\n== Summary ==\n"
    << t::g.checks << " checks, " << t::g.failures << " failed";
  if (t::g.known_open)
    std::cout << "; " << t::g.known_open << " known issue(s) still open";
  if (t::g.known_fixed)
    std::cout << "; " << t::g.known_fixed
      << " known issue(s) now FIXED (promote to CHECK)";
  std::cout << '\n';

  if (!t::g.failures)
    std::cout << "All checks PASSED.\n";

  return t::g.failures ? 1 : 0;
}
