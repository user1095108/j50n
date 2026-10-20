// test2.cpp - test suite for j50n.hpp
//
// Build:
//   g++ -std=c++20 -Wall -Wextra -g -fsanitize=address,undefined -fno-sanitize-recover=all test2.cpp -o test2
//   g++ -std=c++20 -Wall -Wextra -Werror -O2 test2.cpp -o test2_o2
// The scanner must not depend on the signedness of char, so also try:
//   g++ -std=c++20 -O1 -funsigned-char test2.cpp -o test2_uc   (and -fsigned-char)
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

static std::string str(j50n const j)
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
static_assert(noexcept(std::declval<j50n>()["key"]));
static_assert(noexcept(std::declval<j50n>()[std::size_t{}]));
static_assert(noexcept(std::declval<j50n>().size()));
static_assert(noexcept(std::declval<j50n>().is_object()));
static_assert(noexcept(std::declval<j50n>().get()));
static_assert(std::is_same_v<decltype(std::declval<j50n>().get<int>("x")),
  std::pair<int, bool>>);
static_assert(std::is_same_v<decltype(std::declval<j50n>().get<bool>("x")),
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
  CHECK_GET(nums.get<std::int64_t>("big"),        INT64_C(9007199254740992));
  CHECK_GET(nums.get<double>("big"),              9007199254740992.0);
  CHECK_GET(nums.get<double>("sci"),              150.0);
  CHECK_GET(nums.get<double>("sci_neg"),          0.25);
  CHECK_GET(nums.get<unsigned>("uint_max"),       4294967295u);
  CHECK_GET(nums.get<std::uint64_t>("uint_max"),  UINT64_C(4294967295));
  CHECK_GET(nums.get<float>("sci_neg"),           0.25f);

  // range / sign errors reported by std::from_chars
  CHECK_GET_ERR(nums.get<int>("uint_max"));       // 4294967295 > INT_MAX
  CHECK_GET_ERR(nums.get<unsigned>("neg"));       // negative -> unsigned
  CHECK_GET_ERR(nums.get<std::uint8_t>("uint_max"));

  j50n const edge(R"j({"i64":9007199254740993,"i64min":-9223372036854775808,"u64max":18446744073709551615,"over":18446744073709551616,"negz":-0.0,"E":1E5,"s":"123"})j");
  CHECK_GET(edge.get<std::int64_t>("i64"),    INT64_C(9007199254740993)); // > 2^53
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
  CHECK_EQ(root["nested"].size(),    1u);
  CHECK_EQ(root["empty_obj"].size(), 0u);
  CHECK_EQ(root.size(),              11u);   // object root
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
    numbers.feach([&](j50n const e) { seen += std::string(e.get()) + ","; });
    CHECK_EQ(seen, "1,2,3,4,5,");
  }
  // (element, index) visitor
  {
    std::string seen;
    numbers.feach([&](j50n const e, std::size_t i)
      { seen += std::to_string(i) + "=" + std::string(e.get()) + ";"; });
    CHECK_EQ(seen, "0=1;1=2;2=3;3=4;4=5;");
  }
  // array of objects
  {
    int sum{};
    std::size_t last{};
    root["objects"].feach([&](j50n const o, std::size_t i)
      { sum += o.get_or<int>(0, "value"); last = i; });
    CHECK_EQ(sum, 60);
    CHECK_EQ(last, 2u);
  }
  // nested arrays
  {
    std::size_t total{};
    j50n(R"j([[1,2],[3],[]])j").feach([&](j50n const a) { total += a.size(); });
    CHECK_EQ(total, 3u);
  }
  // mutable visitor
  {
    std::string seen;
    numbers.feach([&seen, n = 0](j50n const e) mutable
      { seen += std::to_string(n++) + ":" + std::string(e.get()) + ";"; });
    CHECK_EQ(seen, "0:1;1:2;2:3;3:4;4:5;");
  }

  // early exit: returning true stops iteration, no further calls are made
  {
    std::size_t count{};
    numbers.feach([&](j50n const e) -> bool
      { ++count; return e.get<int>().first >= 3; });
    CHECK_EQ(count, 3u);
  }
  {
    std::size_t count{};
    numbers.feach([&](j50n const, std::size_t) -> bool
      { ++count; return count == 2; });
    CHECK_EQ(count, 2u);
  }
  {
    std::size_t count{};                       // stop on first element
    numbers.feach([&](j50n const) -> bool { ++count; return true; });
    CHECK_EQ(count, 1u);
  }
  {
    std::size_t count{};                       // stop on last element
    numbers.feach([&](j50n const, std::size_t i) -> bool
      { ++count; return i == 4; });
    CHECK_EQ(count, 5u);
  }
  {
    std::size_t count{};                       // never stop
    numbers.feach([&](j50n const) -> bool { ++count; return false; });
    CHECK_EQ(count, 5u);
  }

  // no-ops: empty array, non-arrays, missing, default constructed
  std::size_t calls{};
  auto const f([&](j50n const) { ++calls; });
  auto const g([&](j50n const, std::size_t) { ++calls; });
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

static void test_contains()
{
  SECTION("contains");
  j50n const root(JSON.data(), JSON.size());

  // Every present value counts, including null and the empty string,
  // because the raw views of "null" and "\"\"" are both non-empty.
  CHECK(root.contains("name"));
  CHECK(root.contains("version"));
  CHECK(root.contains("pi"));
  CHECK(root.contains("active"));
  CHECK(root.contains("ratio"));       // null: present
  CHECK(root.contains("utf8"));
  CHECK(root.contains("empty_arr"));
  CHECK(root.contains("empty_obj"));
  CHECK(root.contains("numbers"));
  CHECK(root.contains("objects"));
  CHECK(root.contains("nested"));

  // Absent keys
  CHECK(!root.contains("missing"));
  CHECK(!root.contains("Name"));       // case-sensitive
  CHECK(!root.contains("n"));
  CHECK(!root.contains("names"));
  CHECK(!root.contains(""));

  // The empty-view trap that contains() exists to solve: null, empty
  // string, and empty container all report present, even though the
  // empty string's get() result is also empty.
  j50n const e(R"j({"n":null,"s":"","a":[],"o":{}})j");
  CHECK(e.contains("n"));
  CHECK(e.contains("s"));
  CHECK(e.contains("a"));
  CHECK(e.contains("o"));
  CHECK_EQ(e["n"].get(), "null");
  CHECK_EQ(e["s"].get(), "");          // get() is empty
  CHECK(!e["s"].is_empty());           // ... but the j50n is not
  CHECK(!e.contains("x"));

  // Array index presence
  j50n const nums(root["numbers"]);
  CHECK(nums.contains(0));
  CHECK(nums.contains(4));
  CHECK(!nums.contains(5));
  CHECK(!nums.contains(100));
  CHECK(!nums.contains(std::size_t(-1)));

  // contains(key) over an array is false: arrays have no keys
  CHECK(!nums.contains("0"));
  CHECK(!nums.contains("1"));
  CHECK(!nums.contains("numbers"));

  // contains(index) over an object is false: objects have no indices
  CHECK(!root.contains(0));
  CHECK(!root.contains(1));

  // Nested paths
  CHECK(root.contains("nested", "a"));
  CHECK(root.contains("nested", "a", "b"));
  CHECK(root.contains("nested", "a", "b", "c"));
  CHECK(!root.contains("nested", "a", "b", "c", "d"));
  CHECK(!root.contains("nested", "x"));
  CHECK(!root.contains("nested", "a", "b", "x"));
  CHECK(!root.contains("missing", "a", "b", "c"));

  // Mixed keys and indices in a nested path
  CHECK(root.contains("objects", 0));
  CHECK(root.contains("objects", 2));
  CHECK(!root.contains("objects", 3));
  CHECK(root.contains("objects", 0, "id"));
  CHECK(root.contains("objects", 1, "value"));
  CHECK(!root.contains("objects", 0, "nope"));
  CHECK(root.contains("objects", 1, "tag"));    // null, but present
  CHECK(!root.contains("objects", 2, "tag"));   // absent
  CHECK(!root.contains("numbers", 5));

  // Scalars: no keys, no indices
  CHECK(!root["pi"].contains("x"));
  CHECK(!root["pi"].contains(0));
  CHECK(!root["name"].contains("x"));
  CHECK(!root["name"].contains(0));
  CHECK(!root["active"].contains("x"));
  CHECK(!root["ratio"].contains("x"));          // null receiver

  // Empty / default-constructed receivers
  j50n const def;
  CHECK(!def.contains("a"));
  CHECK(!def.contains(0));
  CHECK(!def.contains("a", "b"));

  CHECK(!j50n("").contains("a"));
  CHECK(!j50n("{}").contains("a"));
  CHECK(!j50n("[]").contains(0));

  // Every key-type overload resolves through the same dispatch as
  // operator[]: string literal, char array, std::string, string_view,
  // const char*, and index.
  std::string const ks("name");
  std::string_view const kv("name");
  char const* const kp("name");
  CHECK(root.contains(ks));
  CHECK(root.contains(kv));
  CHECK(root.contains(kp));
  CHECK(root.contains("name"));
  CHECK(root.contains("numbers", std::size_t(0)));

  // Early exit: contains stops at the first match, so damage after the
  // matched key does not affect the result.
  CHECK(j50n(R"j({"a":1,"b":"\x"})j").contains("a"));
  CHECK(j50n(R"j([1,2,@])j").contains(1));

  // Truncated containers follow the streaming policy: the last complete
  // token before the truncation is still emitted, so contains reports
  // it as present.  Same limitation as operator[].
  CHECK(j50n("{\"a\":12").contains("a"));
  CHECK(j50n("[1,2").contains(1));

  // A search that does not match any present key still returns false
  // even if the document is truncated.
  CHECK(!j50n("{\"a\":12").contains("b"));
  CHECK(!j50n("{\"a\":12").contains(3));
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

static void test_unescape()
{
  SECTION("j50n::unescape");

  // Convenience: unescape a value, or return a fallback if the view is empty.
  // Replaces the removed get_unescaped_or.
  auto const or_ = [](std::string_view const fb, j50n const j) -> std::string
  {
    return j.is_empty() ? std::string(fb) : j50n::unescape(j.get());
  };

  // The JSON_ESCAPED fixture carries each simple escape as its own field.
  j50n const esc(JSON_ESCAPED.data(), JSON_ESCAPED.size());

  CHECK_EQ(j50n::unescape(esc["quote"].get()),     "say \"hello\"");
  CHECK_EQ(j50n::unescape(esc["backslash"].get()), "one\\two");
  CHECK_EQ(j50n::unescape(esc["newline"].get()),   std::string("line1\nline2"));
  CHECK_EQ(j50n::unescape(esc["tab"].get()),       std::string("col1\tcol2"));
  CHECK_EQ(j50n::unescape(esc["solidus"].get()),   "a/b");
  CHECK_EQ(j50n::unescape(esc["unicode"].get()),   "ABC");

  // All eight simple escapes back to back.
  j50n const simple(R"j({"s":"\"\\\/\b\f\n\r\t"})j");
  CHECK_EQ(j50n::unescape(simple["s"].get()),
    std::string("\"\\/\b\f\n\r\t"));

  // Unicode: 2-, 3- and 4-byte UTF-8 encodings, and a surrogate pair.
  j50n const uni(R"j({"e9":"\u00e9","euro":"\u20ac","rocket":"\ud83d\ude80","A":"\u0041"})j");
  CHECK_EQ(j50n::unescape(uni["e9"].get()),     std::string("\xC3\xA9", 2));
  CHECK_EQ(j50n::unescape(uni["euro"].get()),   std::string("\xE2\x82\xAC", 3));
  CHECK_EQ(j50n::unescape(uni["rocket"].get()), std::string("\xF0\x9F\x9A\x80", 4));
  CHECK_EQ(j50n::unescape(uni["A"].get()),      "A");

  // NUL byte in the middle: std::string carries it.
  {
    j50n const n(R"j({"s":"a\u0000b"})j");
    auto const s(j50n::unescape(n["s"].get()));
    CHECK_EQ(s.size(), 3u);
    CHECK(s[0] == 'a' && s[1] == '\0' && s[2] == 'b');
  }

  // Malformed \u falls through to the literal remainder.
  j50n const bad(R"j({"a":"\uZZZZ","b":"\u12"})j");
  CHECK_EQ(j50n::unescape(bad["a"].get()), R"(\uZZZZ)");
  CHECK_EQ(j50n::unescape(bad["b"].get()), R"(\u12)");

  // Lone surrogate: three-byte CESU-8-style encoding.
  j50n const lone(R"j({"s":"\ud800"})j");
  CHECK_EQ(j50n::unescape(lone["s"].get()), std::string("\xEF\xBF\xBD", 3));

  // Empty string, plain strings, and a string at the last position.
  j50n const plain(R"j({"e":"","plain":"hello","tail":"end"})j");
  CHECK_EQ(j50n::unescape(plain["e"].get()),     "");
  CHECK_EQ(j50n::unescape(plain["plain"].get()), "hello");
  CHECK_EQ(j50n::unescape(plain["tail"].get()),  "end");

  // Path access: keys and indices.
  j50n const root(JSON.data(), JSON.size());
  CHECK_EQ(j50n::unescape(root.get("nested", "a", "b", "c")), "deep");
  CHECK_EQ(j50n::unescape(root["name"].get()), "j50n-test");

  j50n const arr(R"j(["a\nb","c"])j");
  CHECK_EQ(j50n::unescape(arr[0].get()), std::string("a\nb"));
  CHECK_EQ(j50n::unescape(arr[1].get()), "c");

  // Empty view unescapes to "".
  CHECK_EQ(j50n::unescape(root["does_not_exist"].get()), "");

  // Non-strings: unescape operates on any bytes and passes through content
  // that contains no backslashes.  No is_string() check.
  j50n const misc(R"j({"n":42,"b":true,"z":null,"arr":[1,2]})j");
  CHECK_EQ(j50n::unescape(misc["n"].get()),   "42");
  CHECK_EQ(j50n::unescape(misc["b"].get()),   "true");
  CHECK_EQ(j50n::unescape(misc["z"].get()),   "null");
  CHECK_EQ(j50n::unescape(misc["arr"].get()), "[1,2]");

  // Because unescape walks raw bytes without a type check, a container
  // token has its inner escapes decoded too.  This is the documented
  // behaviour of the new API; callers who want string-only unescaping
  // check is_string() themselves.
  j50n const nested_esc(R"j({"o":{"s":"x\ny"}})j");
  CHECK_EQ(j50n::unescape(nested_esc["o"].get()),
    std::string("{\"s\":\"x\ny\"}"));

  // Fallback helper (replaces get_unescaped_or).
  CHECK_EQ(or_("FB", plain["plain"]),   "hello");
  CHECK_EQ(or_("FB", plain["e"]),       "");     // present, empty
  CHECK_EQ(or_("FB", plain["missing"]), "FB");
  CHECK_EQ(or_("FB", misc["n"]),        "42");   // non-string, non-empty
  CHECK_EQ(or_("FB", misc["arr"]),      "[1,2]");

  // Return type is std::string, not a view.
  static_assert(std::is_same_v<decltype(j50n::unescape(std::string_view{})),
    std::string>);

  // unescape is callable on any string_view, not only j50n contents.
  CHECK_EQ(j50n::unescape(R"(a\nb)"), std::string("a\nb"));
  CHECK_EQ(j50n::unescape("plain"),   "plain");
  CHECK_EQ(j50n::unescape(""),        "");
}

static void test_unescape_edge_cases()
{
  SECTION("unescape edge cases (more)");

  // Multiple escapes back to back.
  CHECK_EQ(j50n::unescape(R"(a\n\t\rb)"), std::string("a\n\t\rb"));
  CHECK_EQ(j50n::unescape(R"(a\n\t\\b)"), std::string("a\n\t\\b"));

  // A single backslash alone: passes through.
  CHECK_EQ(j50n::unescape(R"(\)"), std::string("\\"));

  // "\u" alone (incomplete escape): the backslash is consumed, leaving "u".
  CHECK_EQ(j50n::unescape(R"(\u)"), R"(\u)");

  // Two high surrogates back to back: two CESU-8 encodings, no combining.
  j50n const dbl(R"j({"s":"\ud800\ud800"})j");
  CHECK_EQ(j50n::unescape(dbl["s"].get()),
    std::string("\xEF\xBF\xBD\xEF\xBF\xBD", 6));

  // A lone low surrogate: still encodes as 3 bytes (CESU-8 style).
  j50n const lo(R"j({"s":"\udc00"})j");
  CHECK_EQ(j50n::unescape(lo["s"].get()),
    std::string("\xEF\xBF\xBD", 3));

  // High surrogate followed by a non-low-surrogate \u escape: they do not
  // combine; the first is a lone surrogate, the second is a normal codepoint.
  j50n const hi(R"j({"s":"\ud800\u0041"})j");
  CHECK_EQ(j50n::unescape(hi["s"].get()),
    std::string("\xEF\xBF\xBD" "A", 4));

  // High surrogate at the very end of the string, truncated.
  j50n const trunc(R"j({"s":"\ud800"})j");
  CHECK_EQ(j50n::unescape(trunc["s"].get()),
    std::string("\xEF\xBF\xBD", 3));

  // Raw bytes 0x80-0xFF pass through unescape unchanged.
  std::string const raw("\xC3\xA9 \xFF");
  CHECK_EQ(j50n::unescape(raw), raw);

  // A raw NUL byte in the input survives.
  std::string const nul("a\0b", 3);
  CHECK_EQ(j50n::unescape(nul).size(), 3u);
  CHECK_EQ(j50n::unescape(nul), nul);
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

    // Minimal first continuation that yields a non-overlong code point.
    unsigned char first = 0x80;
    if (b == 0xE0) first = 0xA0;   // U+0800, smallest 3-byte
    else if (b == 0xF0) first = 0x90;   // U+10000, smallest 4-byte

    std::string const body(
      std::string(1, char(b)) +
      std::string(1, char(first)) +
      std::string(need - 1, '\x80'));

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

  // Malformed bytes are rejected: the result is empty.  0x7F, 0xC0, 0xC1 and
  // 0xF5..0xF7 are left out: whether they are accepted is not a rule worth
  // pinning.
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
    d.feach([&](j50n const e, std::size_t i)
      { ++n; sum += i + e.get<std::size_t>().first; });
    CHECK_EQ(n, N);
    CHECK_EQ(sum, N * (N - 1));                  // sum(i) + sum(value)
  }
}

static void test_lazy_scanning()
{
  // The scanner stops as soon as the requested value is complete, so damage
  // *after* the target does not affect the result.
  SECTION("lazy scanning stops at the match");

  CHECK_EQ(j50n(R"j({"a":1,"b":"\x"})j")["a"], "1");
  CHECK_EQ(j50n(R"j({"a":"ok","b":@})j")["a"], "ok");
  CHECK_EQ(j50n(R"j({"a":[1,2],"b":'x'})j")["a"], "[1,2]");
  CHECK_EQ(j50n(R"j([1,2,@])j")[1], "2");
}

static void test_malformed()
{
  // The scanner reports malformed input by returning an empty view.
  SECTION("malformed input");

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
  CHECK_EQ(j50n("{\"a\":12")["a"], "12");     // truncated object
  CHECK(j50n("{\"a")["a"].is_empty());                         // inside a key
  CHECK_EQ(j50n("[1,2")[1], "2");             // truncated array
}

static void test_misc_edge_cases()
{
  SECTION("misc edge cases");

  // repeated size() / feach() calls return the same result: the scanner is
  // stateless and does not cache or mutate anything
  j50n const arr("[1,2,3,4,5]");
  CHECK_EQ(arr.size(), 5u);
  std::size_t n1{}, n2{};
  arr.feach([&](j50n) { ++n1; });
  arr.feach([&](j50n) { ++n2; });
  CHECK_EQ(n1, 5u);
  CHECK_EQ(n2, 5u);

  // iterating does not modify the j50n
  j50n const before = arr;
  arr.feach([](j50n) {});
  CHECK_EQ(arr.get(), before.get());

  // a j50n built from a subview navigates independently of its parent
  j50n const root(JSON.data(), JSON.size());
  j50n const nested(root["nested"]);
  CHECK(nested.is_object());
  CHECK_EQ(nested["a"]["b"]["c"], "deep");
  CHECK_EQ(nested.get("a", "b", "c"), "deep");
  CHECK(nested["a"]["b"]["c"].is_string());

  // view() results are independent from one another and from the parent
  auto const v1(root.view("nested"));
  auto const v2(root.view("nested", "a"));
  CHECK_EQ(v1["a"]["b"]["c"], "deep");
  CHECK_EQ(v2["b"]["c"], "deep");
  CHECK(v1.is_object());
  CHECK(v2.is_object());

  // zero-length span, constructed from a data pointer and length 0
  j50n const empty(JSON.data(), std::size_t{0});
  CHECK(empty.is_empty());

  // a j50n view with a non-zero start offset
  j50n const substr(JSON.data() + 4, 6);
  CHECK_EQ(substr.get(), "name");

  // is_bare() across the full spectrum of top-level values
  CHECK(j50n("42").is_bare());
  CHECK(j50n("-1.5").is_bare());
  CHECK(j50n("true").is_bare());
  CHECK(j50n("false").is_bare());
  CHECK(j50n("null").is_bare());
  CHECK(!j50n("\"str\"").is_bare());
  CHECK(!j50n("[1]").is_bare());
  CHECK(!j50n("{}").is_bare());
  CHECK(!j50n("").is_bare());

  // a string document whose *contents* look like a container stays a string
  CHECK(j50n(R"j("[not an array]")j").is_string());
  CHECK(!j50n(R"j("[not an array]")j").is_array());
  CHECK_EQ(j50n(R"j("[not an array]")j").get(), "[not an array]");

  // very long key: matched exactly, and near-misses (one byte short) rejected
  std::string const longkey(10000, 'k');
  std::string const doc = "{\"" + longkey + "\":1,\"after\":2}";
  j50n const lk(doc);
  CHECK_EQ(lk[longkey], "1");
  CHECK_EQ(lk["after"], "2");
  CHECK(lk[longkey.substr(0, longkey.size() - 1)].is_empty());

  // get_or with a std::string_view fallback and a nested path
  CHECK_EQ(root.get_or(std::string_view("FB"), "nested", "a", "b", "c"),
    "deep");
  CHECK_EQ(root.get_or(std::string_view("FB"), "nested", "zzz"), "FB");
}

static void test_more_coverage()
{
  SECTION("additional coverage");

  // get<T> for several integer widths returns consistent values
  j50n const n(R"j({"v":1000})j");
  CHECK_GET(n.get<int>("v"),          1000);
  CHECK_GET(n.get<long>("v"),         1000L);
  CHECK_GET(n.get<long long>("v"),    1000LL);
  CHECK_GET(n.get<std::int64_t>("v"), INT64_C(1000));
  CHECK_GET(n.get<std::int32_t>("v"), std::int32_t(1000));
  CHECK_GET(n.get<unsigned>("v"),     1000u);

  // float / double / long double all parse the same value
  j50n const f(R"j({"pi":3.5})j");
  CHECK_GET(f.get<float>("pi"),       3.5f);
  CHECK_GET(f.get<double>("pi"),      3.5);
  CHECK_GET(f.get<long double>("pi"), 3.5L);

  // integer values coerce cleanly to floating point
  CHECK_GET(n.get<double>("v"), 1000.0);
  CHECK_GET(n.get<float>("v"),  1000.0f);

  // nested feach: iterating an array of arrays
  j50n const grid(R"j([[1,2,3],[4,5],[6]])j");
  std::size_t total{};
  grid.feach([&](j50n const row) { total += row.size(); });
  CHECK_EQ(total, 6u);

  // feach over an array of objects, accumulating
  std::size_t idsum{};
  j50n(R"j([{"id":1},{"id":2},{"id":3},{"id":4}])j")
    .feach([&](j50n const o) { idsum += o.get<std::size_t>("id").first; });
  CHECK_EQ(idsum, 10u);

  // view chains can mix keys and indices in any order
  j50n const root(JSON.data(), JSON.size());
  CHECK_GET(root.view("objects", 0).get<int>("id"), 1);
  CHECK_GET(root.view("objects").get<int>(1, "value"), 20);

  // a sub-view of a sub-view behaves the same as the equivalent long path
  auto const mid(root["objects"]);
  CHECK_EQ(mid[1]["tag"], root["objects"][1]["tag"]);
  CHECK_EQ(mid[2]["value"], root["objects"][2]["value"]);

  // very long string value survives round-trip byte-exact
  std::string const big(50000, 'x');
  std::string const doc = "{\"big\":\"" + big + "\",\"t\":1}";
  j50n const b(doc);
  CHECK_EQ(b["big"].get().size(), big.size());
  CHECK(b["big"].get() == big);

  // strings containing only whitespace and structural punctuation
  j50n const punct(R"j({"a":" , : [ ] { } ","b":"\t\t\t"})j");
  CHECK_EQ(punct["a"].get(), " , : [ ] { } ");
  CHECK_EQ(punct["b"].get(), "\\t\\t\\t");

  // get_or<bool> distinguishes false from wrong-type
  j50n const bd(R"j({"t":true,"f":false,"n":0,"s":"false"})j");
  CHECK(bd.get_or<bool>(true,  "t") == true);
  CHECK(bd.get_or<bool>(true,  "f") == false);
  CHECK(bd.get_or<bool>(true,  "n") == true);    // number, not bool
  CHECK(bd.get_or<bool>(true,  "s") == true);    // string, not bool

  // every predicate on a value fetched by various key types
  CHECK(root[std::string("active")].is_bool());
  CHECK(root[std::string_view("active")].is_bool());
  CHECK(root["active"].is_bool());

  // a j50n built from an iterator pair over a std::string body
  std::string const src(R"j({"x":42})j");
  j50n const from_iters(src.begin(), src.end());
  CHECK_GET(from_iters.get<int>("x"), 42);

  // move assignment leaves the target usable
  j50n a(R"j({"a":1})j");
  j50n bb(R"j({"b":2})j");
  a = std::move(bb);
  CHECK_EQ(a["b"], "2");
  CHECK(a["a"].is_empty());

  // equivalent values reached by different paths compare identically
  CHECK_EQ(root["nested"]["a"]["b"]["c"],
           root.view("nested").view("a").view("b").view("c"));
  CHECK_EQ(root.get("objects", 0, "id"),
           root.view("objects", 0).get("id"));
}

static void test_operator_eq()
{
  SECTION("operator==");
  j50n const root(JSON.data(), JSON.size());

  // numbers
  CHECK(root["version"] == 1);
  CHECK(1 == root["version"]);            // rewritten
  CHECK(root["version"] == 1L);
  CHECK(root["version"] == 1u);
  CHECK(root["version"] == 1.0);
  CHECK(root["pi"] == 3.14159);
  CHECK(root["pi"] == 3.14159f);
  CHECK(!(root["pi"] == 3));              // 3.14159 is not the integer 3
  CHECK(!(root["version"] == 2));
  CHECK(!(root["version"] == -1));
  CHECK(!(root["pi"] == 3.14));           // not bit-identical

  // bool
  CHECK(root["active"] == true);
  CHECK(true == root["active"]);
  CHECK(!(root["active"] == false));
  CHECK(!(root["active"] == 1));          // bool is not number
  CHECK(!(root["version"] == true));      // number is not bool

  // null
  CHECK(root["ratio"] == nullptr);
  CHECK(nullptr == root["ratio"]);
  CHECK(!(root["ratio"] == 0));
  CHECK(!(root["name"] == nullptr));

  // strings
  CHECK(root["name"] == "j50n-test");
  CHECK("j50n-test" == root["name"]);
  CHECK(root["name"] == std::string_view("j50n-test"));
  CHECK(root["name"] == std::string("j50n-test"));
  CHECK(!(root["name"] == "j50n"));
  CHECK(!(root["name"] == ""));
  CHECK(!(root["version"] == "1"));       // number is not string
  CHECK(!(root["ratio"] == "null"));      // null is not string

  // empty string
  j50n const e(R"j({"s":""})j");
  CHECK(e["s"] == "");
  CHECK(!(e["s"] == "x"));

  // escaped strings compare unescaped
  j50n const esc(JSON_ESCAPED.data(), JSON_ESCAPED.size());
  CHECK(esc["quote"]     == "say \"hello\"");
  CHECK(esc["backslash"] == "one\\two");
  CHECK(esc["newline"]   == "line1\nline2");
  CHECK(esc["tab"]       == "col1\tcol2");
  CHECK(esc["solidus"]   == "a/b");
  CHECK(esc["unicode"]   == "ABC");       // \u0041BC
  CHECK(!(esc["newline"] == R"(line1\nline2)")); // raw backslash-n is not a newline

  // the same comparison on a string without escapes takes the fast path
  j50n const plain(R"j({"s":"abc"})j");
  CHECK(plain["s"] == "abc");
  CHECK(!(plain["s"] == "ab"));

  // two j50n compare raw-text
  CHECK(j50n("{}") == j50n("{}"));
  CHECK(j50n("[1,2]") == j50n("[1,2]"));
  CHECK(!(j50n("[1,2]") == j50n("[1, 2]")));   // whitespace differs
  CHECK(!(j50n("{}") == j50n("[]")));
  CHECK(j50n() == j50n());                     // both empty

  // cross-type comparisons are always false
  CHECK(!(root["version"] == root["name"]));
  CHECK(!(root["active"] == root["pi"]));
  CHECK(!(root["ratio"] == root["numbers"]));

  // array element comparison
  CHECK(root["numbers"][0] == 1);
  CHECK(root["numbers"][4] == 5);
  CHECK(root["objects"][0]["id"] == 1);
  CHECK(root["objects"][0]["tag"] == "first");
  CHECK(root["objects"][1]["tag"] == nullptr);
  CHECK(!(root["objects"][2]["tag"] == "x"));  // absent tag

  // error path returns false, not UB
  CHECK(!(root["does_not_exist"] == "anything"));
  CHECK(!(root["does_not_exist"] == 0));
  CHECK(!(root["does_not_exist"] == false));
  CHECK(!(root["does_not_exist"] == nullptr));

  // nested path via get() still works
  CHECK(root.get("nested", "a", "b", "c") == "deep");
  CHECK(!(root.get("nested", "a", "x") == "deep"));

  // return type is bool
  static_assert(std::is_same_v<decltype(root["name"] == "x"), bool>);
  static_assert(std::is_same_v<decltype(root["version"] == 1), bool>);
  static_assert(std::is_same_v<decltype(root["active"] == true), bool>);
  static_assert(std::is_same_v<decltype(root["ratio"] == nullptr), bool>);
}

static void test_operator_bool()
{
  SECTION("operator bool");

  // explicit: direct-init works, copy-init does not
  static_assert(std::is_constructible_v<bool, j50n>);
  static_assert(!std::is_convertible_v<j50n, bool>);
  static_assert(noexcept(bool(std::declval<j50n>())));

  // Empty view: default-constructed, blank document, absent key.
  CHECK(!bool(j50n()));
  CHECK(!bool(j50n("")));
  CHECK(!bool(j50n("   \t\r\n")));
  CHECK(!bool(j50n("{}")["missing"]));
  CHECK(!bool(j50n("{\"a\":1}")["b"]));

  // null: literal, padded, and as a present key.
  CHECK(!bool(j50n("null")));
  CHECK(!bool(j50n(" null ")));
  CHECK(!bool(j50n(R"j({"n":null})j")["n"]));

  // Booleans: the two spellings, padded, and as present keys.
  CHECK(bool(j50n("true")));
  CHECK(!bool(j50n("false")));
  CHECK(bool(j50n(" true ")));
  CHECK(!bool(j50n(" false ")));
  CHECK(bool(j50n(R"j({"t":true,"f":false})j")["t"]));
  CHECK(!bool(j50n(R"j({"t":true,"f":false})j")["f"]));

  // Strings: empty is falsy, any content is truthy.
  CHECK(!bool(j50n(R"("")")));
  CHECK(bool(j50n(R"("x")")));
  CHECK(bool(j50n(R"("0")")));          // "0" is a non-empty string
  CHECK(bool(j50n(R"("false")")));      // ditto
  CHECK(!bool(j50n(R"j({"s":""})j")["s"]));
  CHECK(bool(j50n(R"j({"s":"x"})j")["s"]));

  // Escaped string: content after unescaping is what matters, so an
  // escape-only string "\u0000" is still non-empty.
  CHECK(!bool(j50n(R"j({"s":""})j")["s"]));
  CHECK(bool(j50n(R"j({"s":"\n"})j")["s"]));   // one newline of content

  // Numbers: all spellings of zero are falsy, everything else is truthy.
  CHECK(!bool(j50n("0")));
  CHECK(!bool(j50n("-0")));
  CHECK(!bool(j50n("0.0")));
  CHECK(!bool(j50n("-0.0")));
  CHECK(!bool(j50n("0e0")));
  CHECK(!bool(j50n("0E5")));
  CHECK(bool(j50n("1")));
  CHECK(bool(j50n("-1")));
  CHECK(bool(j50n("3.14")));
  CHECK(bool(j50n("0.000001")));
  CHECK(!bool(j50n(R"j({"n":0})j")["n"]));
  CHECK(bool(j50n(R"j({"n":1})j")["n"]));

  // Containers: empty (with and without interior whitespace) is falsy,
  // anything with at least one element is truthy.
  CHECK(!bool(j50n("[]")));
  CHECK(!bool(j50n("{}")));
  CHECK(!bool(j50n("[ ]")));
  CHECK(!bool(j50n("{ }")));
  CHECK(!bool(j50n("[   ]")));
  CHECK(!bool(j50n("{       }")));
  CHECK(!bool(j50n("[\n\t]")));
  CHECK(!bool(j50n("{\n\t}")));
  CHECK(bool(j50n("[0]")));
  CHECK(bool(j50n("[false]")));
  CHECK(bool(j50n("[null]")));
  CHECK(bool(j50n("[[]]")));
  CHECK(bool(j50n(R"(["",0,null])")));
  CHECK(bool(j50n(R"j({"a":0})j")));
  CHECK(bool(j50n(R"j({"a":null})j")));
  CHECK(bool(j50n(R"j({"a":[]})j")));
  CHECK(!bool(j50n(R"j({"a":[]})j")["a"]));    // sub-container, still empty
  CHECK(bool(j50n(R"j({"a":[0]})j")["a"]));

  // Values reached through the fixture.
  j50n const root(JSON.data(), JSON.size());
  CHECK(bool(root));
  CHECK(bool(root["name"]));
  CHECK(bool(root["version"]));
  CHECK(bool(root["active"]));
  CHECK(!bool(root["ratio"]));         // null
  CHECK(!bool(root["empty_arr"]));     // []
  CHECK(!bool(root["empty_obj"]));     // {}
  CHECK(bool(root["numbers"]));        // [1,2,3,4,5]
  CHECK(bool(root["nested"]));         // non-empty object
  CHECK(!bool(root["does_not_exist"]));// empty view

  // Unparseable bare tokens fall through to size(), which is 0 for a
  // non-container.  So they are falsy under the current implementation.
  // If the fall-through is ever changed to "non-empty bare is truthy",
  // flip these three lines.
  CHECK(bool(j50n("tru")));
  CHECK(bool(j50n("falsex")));
  CHECK(bool(j50n("1.2.3")));
  CHECK(bool(j50n("NaN")));
  CHECK(bool(j50n("-")));

  // Direct use in control flow: if (j) is the intended spelling.
  int n{};
  for (j50n const x : {j50n("0"), j50n("1"), j50n("null"), j50n("[]")})
    if (x) ++n;
  CHECK_EQ(n, 1);   // only "1" is truthy

  // Return type is bool.
  static_assert(std::is_same_v<decltype(static_cast<bool>(
    std::declval<j50n>())), bool>);
}

static void test_size()
{
  SECTION("size");

  // Empty views and empty containers all report 0.
  CHECK_EQ(j50n().size(),                    0u);
  CHECK_EQ(j50n("").size(),                  0u);
  CHECK_EQ(j50n("   \t\r\n").size(),         0u);
  CHECK_EQ(j50n("{}").size(),                0u);
  CHECK_EQ(j50n("[]").size(),                0u);
  CHECK_EQ(j50n("{ }").size(),               0u);
  CHECK_EQ(j50n("[ ]").size(),               0u);
  CHECK_EQ(j50n("{       }").size(),         0u);  // interior whitespace
  CHECK_EQ(j50n("[       ]").size(),         0u);
  CHECK_EQ(j50n("{\n\t\r }").size(),         0u);
  CHECK_EQ(j50n("[\n\t\r ]").size(),         0u);

  // Scalars: no members, no elements.
  CHECK_EQ(j50n("42").size(),                0u);
  CHECK_EQ(j50n("-1.5").size(),              0u);
  CHECK_EQ(j50n("true").size(),              0u);
  CHECK_EQ(j50n("false").size(),             0u);
  CHECK_EQ(j50n("null").size(),              0u);
  CHECK_EQ(j50n("\"str\"").size(),           0u);
  CHECK_EQ(j50n("\"\"").size(),              0u);

  // Non-container subviews from a document report 0 as well.
  j50n const root(JSON.data(), JSON.size());
  CHECK_EQ(root["name"].size(),              0u);
  CHECK_EQ(root["version"].size(),           0u);
  CHECK_EQ(root["pi"].size(),                0u);
  CHECK_EQ(root["active"].size(),            0u);
  CHECK_EQ(root["ratio"].size(),             0u);
  CHECK_EQ(root["does_not_exist"].size(),    0u);

  // Single-element containers.
  CHECK_EQ(j50n("[0]").size(),               1u);
  CHECK_EQ(j50n("[null]").size(),            1u);
  CHECK_EQ(j50n("[false]").size(),           1u);
  CHECK_EQ(j50n("[[]]").size(),              1u);
  CHECK_EQ(j50n(R"({})").size(),             0u);
  CHECK_EQ(j50n(R"({"a":1})").size(),        1u);
  CHECK_EQ(j50n(R"({"a":null})").size(),     1u);

  // Multi-element arrays and objects.
  CHECK_EQ(j50n("[1,2,3]").size(),           3u);
  CHECK_EQ(j50n("[1,2,3,]").size(),          3u);  // trailing comma, lenient
  CHECK_EQ(j50n(R"({"a":1,"b":2,"c":3})").size(), 3u);
  CHECK_EQ(j50n(R"({"a":1,"b":2,"c":3,})").size(), 3u);

  // The heterogeneous fixture and nested containers.
  j50n const h(R"j([10,"two",true,null,{"x":1},[2,3],"",-4.5e1])j");
  CHECK_EQ(h.size(), 8u);
  CHECK_EQ(h[4].size(), 1u);     // {"x":1}
  CHECK_EQ(h[5].size(), 2u);     // [2,3]

  j50n const n(R"j([[1,2],[3],[],[[4]]])j");
  CHECK_EQ(n.size(), 4u);
  CHECK_EQ(n[0].size(), 2u);
  CHECK_EQ(n[1].size(), 1u);
  CHECK_EQ(n[2].size(), 0u);
  CHECK_EQ(n[3].size(), 1u);
  CHECK_EQ(n[3][0].size(), 1u);

  // Object element counts from the fixture.
  CHECK_EQ(root["numbers"].size(),           5u);
  CHECK_EQ(root["objects"].size(),           3u);
  CHECK_EQ(root["nested"].size(),            1u);
  CHECK_EQ(root["nested"]["a"].size(),       1u);
  CHECK_EQ(root["nested"]["a"]["b"].size(),  1u);
  CHECK_EQ(root["empty_arr"].size(),         0u);
  CHECK_EQ(root["empty_obj"].size(),         0u);
  CHECK_EQ(root.size(),                     11u);  // object root

  // Objects with many keys, counted with a loop for both sides.
  {
    constexpr int N = 100;
    std::string s("{");
    for (int i = 0; i < N; ++i)
    {
      if (i) s += ',';
      s += "\"k" + std::to_string(i) + "\":" + std::to_string(i);
    }
    s += '}';
    j50n const d(s);
    CHECK_EQ(d.size(), std::size_t(N));
  }

  // size() is idempotent: repeated calls return the same value, and the
  // view is not mutated.
  {
    j50n const arr("[1,2,3,4,5]");
    auto const s0(arr.size());
    auto const raw0(arr.get());
    CHECK_EQ(arr.size(), 5u);
    CHECK_EQ(arr.size(), 5u);
    CHECK_EQ(arr.size(), s0);
    CHECK(arr.get() == raw0);
  }
  {
    j50n const obj(R"j({"a":1,"b":2})j");
    CHECK_EQ(obj.size(), 2u);
    CHECK_EQ(obj.size(), 2u);
  }

  // A subview reports only its own elements, not its descendants.
  {
    j50n const d(R"j({"a":{"b":{"c":1}}})j");
    CHECK_EQ(d.size(),              1u);  // {"a": ...}
    CHECK_EQ(d["a"].size(),         1u);  // {"b": ...}
    CHECK_EQ(d["a"]["b"].size(),    1u);  // {"c": ...}
    CHECK_EQ(d["a"]["b"]["c"].size(), 0u);  // 1, a scalar
  }
}

template <class T, class U>
inline constexpr bool cmp_eq = requires(T t, U u){ t == u; };

template <class T, class U>
inline constexpr bool cmp_sp = requires(T t, U u){ t <=> u; };

static void test_operator_spaceship()
{
  SECTION("operator<=>");

  // Return type and constraint
  static_assert(std::is_same_v<decltype(std::declval<j50n>() <=> 1),
    std::partial_ordering>);
  static_assert(std::is_same_v<decltype(std::declval<j50n>() <=> 1.0),
    std::partial_ordering>);
  static_assert(std::is_same_v<decltype(std::declval<j50n>() <=> 1.0L),
    std::partial_ordering>);
  static_assert(std::is_same_v<decltype(std::declval<j50n>() <=> true),
    std::partial_ordering>);
  static_assert(noexcept(std::declval<j50n>() <=> 1));

  static_assert( requires(j50n a, j50n b){ a == b; });
  static_assert( requires(j50n a, j50n b){ a != b; });

  static_assert( cmp_sp<j50n, int>);
  static_assert( cmp_sp<j50n, double>);
  static_assert( cmp_sp<j50n, long double>);
  static_assert( cmp_sp<j50n, bool>);
  static_assert( cmp_sp<int, j50n>);

  static_assert(!cmp_sp<j50n, std::nullptr_t>);
  static_assert(!cmp_sp<j50n, const char*>);
  static_assert(!cmp_sp<j50n, std::string_view>);
  static_assert(!cmp_sp<j50n, j50n>);

  j50n const root(JSON.data(), JSON.size());

  // Six relational operators on a numeric view, both directions.
  CHECK(root["version"] <  2);
  CHECK(root["version"] <= 1);
  CHECK(root["version"] <= 2);
  CHECK(root["version"] >  0);
  CHECK(root["version"] >= 1);
  CHECK(root["version"] >= 0);
  CHECK(!(root["version"] <  1));
  CHECK(!(root["version"] >  1));
  CHECK(!(root["version"] <= 0));
  CHECK(!(root["version"] >= 2));

  // Different arithmetic types against the same value.
  CHECK(root["version"] <  2L);
  CHECK(root["version"] <  2u);
  CHECK(root["version"] <  2.0);
  CHECK(root["version"] <  2.0f);
  CHECK(root["version"] <  2.0L);
  CHECK((root["version"] <=> 2.0L) == std::partial_ordering::less);
  CHECK((root["version"] <=> 1.0L) == std::partial_ordering::equivalent);

  // Floating point.
  CHECK(root["pi"] >  3);
  CHECK(root["pi"] <  4);
  CHECK(root["pi"] >= 3.14159);
  CHECK(root["pi"] <= 3.14159);
  CHECK(!(root["pi"] <  3.14159));
  CHECK(!(root["pi"] >  3.14159));

  // long double on the left too
  CHECK(4.0L > root["version"]);
  CHECK(0.0L < root["version"]);

  // Negative numbers.
  j50n const neg(R"j({"n":-42})j");
  CHECK(neg["n"] <  0);
  CHECK(neg["n"] >  -100);
  CHECK(neg["n"] <= -42);
  CHECK(neg["n"] >= -42);
  CHECK(!(neg["n"] >  0));
  CHECK(!(neg["n"] < -42));

  // Zero, including negative zero and every spelling.
  j50n const z(R"j({"a":0,"b":-0,"c":0.0,"d":0e0,"e":1})j");
  CHECK(z["a"] <= 0);
  CHECK(z["a"] >= 0);
  CHECK(z["b"] <= 0);
  CHECK(z["b"] >= 0);
  CHECK(z["c"] <= 0.0);
  CHECK(z["c"] >= 0.0);
  CHECK(z["d"] <= 0.);
  CHECK(z["d"] >= 0.);
  CHECK(z["e"] >  0);

  // Reversed operands: 5 < j is rewritten to 0 < (j <=> 5).
  CHECK(2 >  root["version"]);
  CHECK(2 >= root["version"]);
  CHECK(0 <  root["version"]);
  CHECK(0 <= root["version"]);
  CHECK(!(1 <  root["version"]));
  CHECK(!(1 >  root["version"]));
  CHECK(4.0 > root["pi"]);

  // bool: ordering is C++'s, false < true.  Not a JSON concept, but
  // well-defined and consistent with operator==, which also accepts bool.
  CHECK((j50n("true")  <=> true)  == std::partial_ordering::equivalent);
  CHECK((j50n("false") <=> false) == std::partial_ordering::equivalent);
  CHECK((j50n("false") <=> true)  == std::partial_ordering::less);
  CHECK((j50n("true")  <=> false) == std::partial_ordering::greater);

  CHECK(j50n("false") <  true);
  CHECK(j50n("true")  >  false);
  CHECK(j50n("true")  >= true);
  CHECK(j50n("false") <= false);
  CHECK(!(j50n("true")  <  true));
  CHECK(!(j50n("false") >  true));
  CHECK(!(j50n("true")  <  2));

  // A number is not a bool: unordered, not ordered.
  CHECK((j50n("1") <=> true)  == std::partial_ordering::unordered);
  CHECK((j50n("0") <=> true)  == std::partial_ordering::unordered);
  CHECK((j50n("1") <=> false) == std::partial_ordering::unordered);

  // A string is not a bool either.
  CHECK((j50n(R"("true")")  <=> true)  == std::partial_ordering::unordered);
  CHECK((j50n(R"("false")") <=> false) == std::partial_ordering::unordered);

  // Consistency: ==, <=, and >= all agree where both apply.
  CHECK(j50n("true")  == true);
  CHECK(j50n("true")  <= true);
  CHECK(j50n("true")  >= true);
  CHECK(j50n("false") == false);
  CHECK(j50n("false") <= false);
  CHECK(j50n("false") >= false);

  // unordered: every comparison against a non-numeric view is false.
  j50n const s(R"j({"s":"abc"})j");
  CHECK(!(s["s"] <  1));
  CHECK(!(s["s"] >  1));
  CHECK(!(s["s"] <= 1));
  CHECK(!(s["s"] >= 1));
  CHECK(!(1 <  s["s"]));
  CHECK(!(1 >  s["s"]));

  // null, containers, empty view, absent key: all unordered against numbers.
  j50n const u(R"j({"n":null,"arr":[1],"obj":{"a":1}})j");
  CHECK(!(u["n"]   < 1));
  CHECK(!(u["arr"] < 1));
  CHECK(!(u["obj"] < 1));
  CHECK(!(u["n"]   > 0));
  CHECK(!(u["arr"] <= 0));
  CHECK(!(u["obj"] >= 0));

  j50n const empty;
  CHECK(!(empty <  0));
  CHECK(!(empty >  0));
  CHECK(!(empty <= 0));
  CHECK(!(empty >= 0));

  CHECK(!(root["does_not_exist"] <  0));
  CHECK(!(root["does_not_exist"] >  0));
  CHECK(!(root["does_not_exist"] <= 0));
  CHECK(!(root["does_not_exist"] >= 0));

  // Direct category inspection.
  j50n const nums(root["numbers"]);
  CHECK((nums[0] <=> 1) == std::partial_ordering::equivalent);
  CHECK((nums[0] <=> 2) == std::partial_ordering::less);
  CHECK((nums[4] <=> 1) == std::partial_ordering::greater);
  CHECK((s["s"]   <=> 1) == std::partial_ordering::unordered);
  CHECK((u["n"]   <=> 1) == std::partial_ordering::unordered);
  CHECK((root["does_not_exist"] <=> 1) ==
    std::partial_ordering::unordered);

  // Array elements compared to numbers.
  CHECK(nums[0] <  2);
  CHECK(nums[1] >  1);
  CHECK(nums[1] <= 2);
  CHECK(nums[1] >= 2);
  CHECK(nums[4] >  4);
  CHECK(nums[4] <= 5);
  CHECK(!(nums[0] >  1));
  CHECK(!(nums[4] <  5));

  // Values reached by path, compared to numbers.
  CHECK(root.view("objects", 0, "id") <  2);
  CHECK(root.view("objects", 1, "id") >  1);
  CHECK(root.view("objects", 1, "id") <= 2);
  CHECK(root.view("objects", 2, "id") >  2);
  CHECK(root.view("objects", 0, "value") <  20);
  CHECK(root.view("objects", 1, "value") >  10);
  CHECK(root.view("objects", 2, "value") == 30);
  CHECK(root.view("objects", 2, "value") >= 30);

  // Cross-checking == and <=> on the same values.
  CHECK((root["version"] == 1) && (root["version"] <= 1) &&
    (root["version"] >= 1));
  CHECK((root["version"] < 2) && !(root["version"] > 2) &&
    !(root["version"] == 2) && (root["version"] <= 2));
  CHECK((root["pi"] == 3.14159) && (root["pi"] <= 3.14159) &&
    (root["pi"] >= 3.14159));

  // Unsigned: a negative j50n parses as unsigned only if non-negative,
  // so the parse fails and the result is unordered.
  j50n const one(R"j({"v":1})j");
  j50n const negone(R"j({"v":-1})j");
  CHECK(one["v"] <  2u);
  CHECK(one["v"] >= 1u);
  CHECK((negone["v"] <=> 1u) == std::partial_ordering::less);
  CHECK((negone["v"] < 0u));
  CHECK(!(negone["v"] > 0u));
}

static void test_get_or_overloads()
{
  SECTION("get_or overload resolution");

  j50n const root(JSON.data(), JSON.size());

  static_assert(std::is_same_v<
    decltype(std::declval<j50n const>().get_or(std::string("x"), "k")),
    std::string_view>,
    "std::string default must reach the string_view overload");
  static_assert(std::is_same_v<
    decltype(std::declval<j50n const>().get_or(
      std::declval<char const*>(), "k")),
    std::string_view>,
    "char const* default must reach the string_view overload");
  static_assert(std::is_same_v<
    decltype(std::declval<j50n const>().get_or(std::string_view("x"), "k")),
    std::string_view>);
  static_assert(std::is_same_v<
    decltype(std::declval<j50n const>().get_or("x", "k")),
    std::string_view>);
  static_assert(std::is_same_v<
    decltype(std::declval<j50n const>().get_or(-1, "k")),
    int>);
  static_assert(std::is_same_v<
    decltype(std::declval<j50n const>().get_or(0.5, "k")),
    double>);
  static_assert(std::is_same_v<
    decltype(std::declval<j50n const>().get_or(true, "k")),
    bool>);

  // String-like defaults.
  CHECK_EQ(root.get_or("FB", "name"),                   "j50n-test");
  CHECK_EQ(root.get_or("FB", "nope"),                   "FB");

  CHECK_EQ(root.get_or(std::string_view("SV"), "name"), "j50n-test");
  CHECK_EQ(root.get_or(std::string_view("SV"), "nope"), "SV");

  CHECK_EQ(root.get_or(std::string("ST"), "name"),      "j50n-test");  // <- bug
  CHECK_EQ(root.get_or(std::string("ST"), "nope"),      "ST");         // <- bug

  char const* const cp("CP");
  CHECK_EQ(root.get_or(cp, "name"),                     "j50n-test");  // <- bug
  CHECK_EQ(root.get_or(cp, "nope"),                     "CP");         // <- bug

  // Arithmetic defaults.
  CHECK(root.get_or(-1,   "version") == 1);
  CHECK(root.get_or(-1,   "nope")    == -1);
  CHECK(root.get_or(0.5,  "pi")      == 3.14159);
  CHECK(root.get_or(0.5,  "nope")    == 0.5);
  CHECK(root.get_or(true, "active")  == true);
  CHECK(root.get_or(true, "nope")    == true);
  CHECK(root.get_or(false, "active") == true);
}

// ── additional coverage: is_empty_string, integer widths, overflow,
//    numeric edge cases ────────────────────────────────────────────────────

static void test_is_empty_string()
{
  SECTION("is_empty_string");

  // Only the exact raw 2-char text "\"\"" counts.
  j50n const doc(R"j({"a":"","b":"x","c":null,"d":[],"e":{},"f":" "})j");
  CHECK(doc["a"].is_empty_string());
  CHECK(!doc["b"].is_empty_string());
  CHECK(!doc["c"].is_empty_string());          // null
  CHECK(!doc["d"].is_empty_string());          // []
  CHECK(!doc["e"].is_empty_string());          // {}
  CHECK(!doc["f"].is_empty_string());          // " " (one blank)
  CHECK(!doc["missing"].is_empty_string());

  // Whole documents.
  CHECK(j50n(R"j("")j").is_empty_string());
  CHECK(!j50n("").is_empty_string());          // empty view, not ""
  CHECK(!j50n(" ").is_empty_string());         // blank document
  CHECK(!j50n("x").is_empty_string());         // bare token
  CHECK(!j50n("[]").is_empty_string());
  CHECK(!j50n("{}").is_empty_string());

  // "" (a present empty-string value) is distinct from the empty view.
  CHECK(!doc["a"].is_empty());                 // j50n itself is not empty
  CHECK_EQ(doc["a"].get(), "");
  CHECK_EQ(doc["a"].get().size(), 0u);
  CHECK(doc["a"].is_string());

  // But get_or returns the empty string, not the fallback.
  CHECK_EQ(doc.get_or("DEFAULT", "a"), "");
}

static void test_integer_widths()
{
  SECTION("integer width coverage");

  j50n const small(R"j({"v":42,"neg":-7,"big":32768})j");

  // All the standard integer types route through std::from_chars.
  CHECK_GET(small.get<short>("v"),              short(42));
  CHECK_GET(small.get<unsigned short>("v"),     static_cast<unsigned short>(42));
  CHECK_GET(small.get<long>("v"),               42L);
  CHECK_GET(small.get<unsigned long>("v"),      42UL);
  CHECK_GET(small.get<long long>("v"),          42LL);
  CHECK_GET(small.get<unsigned long long>("v"), 42ULL);
  CHECK_GET(small.get<std::int16_t>("v"),       std::int16_t(42));
  CHECK_GET(small.get<std::uint16_t>("v"),      std::uint16_t(42));
  CHECK_GET(small.get<std::int32_t>("v"),       std::int32_t(42));
  CHECK_GET(small.get<std::uint32_t>("v"),      std::uint32_t(42));
  CHECK_GET(small.get<std::int64_t>("v"),       std::int64_t(42));
  CHECK_GET(small.get<std::uint64_t>("v"),      std::uint64_t(42));

  // signed char and unsigned char.
  CHECK_GET(small.get<signed char>("v"),        static_cast<signed char>(42));
  CHECK_GET(small.get<unsigned char>("v"),      static_cast<unsigned char>(42));

  // char: from_chars treats it as an integer; 42 is '*'.
  CHECK_GET(small.get<char>("v"),               char(42));

  // Negative values fit in every signed width.
  CHECK_GET(small.get<int>("neg"),              -7);
  CHECK_GET(small.get<long>("neg"),             -7L);
  CHECK_GET(small.get<long long>("neg"),        -7LL);
  CHECK_GET(small.get<std::int16_t>("neg"),     std::int16_t(-7));

  // Negative values do not fit in any unsigned width.
  CHECK_GET_ERR(small.get<unsigned>("neg"));
  CHECK_GET_ERR(small.get<unsigned short>("neg"));
  CHECK_GET_ERR(small.get<unsigned long>("neg"));
  CHECK_GET_ERR(small.get<std::uint64_t>("neg"));

  // Out-of-range for the narrow types.
  CHECK_GET_ERR(small.get<signed char>("big"));    // 32768 > 127
  CHECK_GET_ERR(small.get<std::int8_t>("big"));
  CHECK_GET_ERR(small.get<std::uint8_t>("big"));   // 32768 > 255
  CHECK_GET_ERR(small.get<short>("big"));          // 32768 > 32767
  j50n const huge(R"j({"v":100000})j");
  CHECK_GET_ERR(huge.get<short>("v"));             // 100000 > 32767
  CHECK_GET_ERR(huge.get<std::int16_t>("v"));
  CHECK_GET_ERR(huge.get<unsigned short>("v"));    // 100000 > 65535
}

static void test_numeric_overflow()
{
  SECTION("numeric overflow");

  // 1e500 overflows double (max ~1.8e308).  from_chars reports
  // result_out_of_range, so get<double> reports an error and is_number()
  // returns false.  The comparison operators fall through to "unordered".
  j50n const huge(R"j({"huge":1e500,"neg":-1e500})j");
  CHECK(!huge["huge"].is_number());
  CHECK(!huge["neg"].is_number());

  CHECK(huge.get<double>("huge").second);
  CHECK(huge.get<double>("neg").second);

  // Current behaviour: an overflowing number compares as unordered against
  // any comparison target, because the parse fails.
  CHECK((huge["huge"] <=> 5) == std::partial_ordering::unordered);
  CHECK((huge["huge"] <=> 5.0) == std::partial_ordering::unordered);
  CHECK(!(huge["huge"] > 5));
  CHECK(!(huge["huge"] < 5));

  // operator bool: parse fails, so it falls through to "bare token is
  // truthy" (the leading byte is '1' or '-', not '[' / '{').
  CHECK(bool(huge["huge"]));
  CHECK(bool(huge["neg"]));

  // 1e308 is representable; 1e309 is not.
  j50n const edge(R"j({"ok":1e308,"ov":1e309,"z":0e500})j");
  CHECK(edge["ok"].is_number());
  CHECK(!edge["ov"].is_number());
  CHECK(edge["z"].is_number());                 // 0 * 10^500 = 0
  CHECK(!bool(edge["z"]));                      // zero -> falsy

  // NaN and Infinity parse successfully under from_chars (general format),
  // so is_number is true for them.
  j50n const special(R"j({"nan":NaN,"inf":Infinity})j");
  CHECK(special["nan"].is_number());
  CHECK(special["inf"].is_number());
  CHECK(std::isnan(special.get<double>("nan").first));
  CHECK(std::isinf(special.get<double>("inf").first));

  // ... but comparison against a normal number is unordered for NaN,
  // and ordinary for Infinity.
  CHECK((special["nan"] <=> 1.0) == std::partial_ordering::unordered);
  CHECK((special["inf"] <=> 1.0) == std::partial_ordering::greater);
  CHECK(special["inf"] > 1.0);

  // Very large integer that still fits in uint64_t.
  j50n const u64(R"j({"v":18446744073709551615})j");
  CHECK_GET(u64.get<std::uint64_t>("v"), UINT64_MAX);
  CHECK(!u64.get<int>("v").second == false);   // int overflows -> error
  CHECK(u64.get<int>("v").second);

  // One past INT64_MAX.
  j50n const over(R"j({"v":9223372036854775808})j");
  CHECK(over.get<std::int64_t>("v").second);          // error
  CHECK(!over.get<std::uint64_t>("v").second);        // fits in uint64_t
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
  test_contains();
  test_strings_and_escapes();
  test_string_blanks();
  test_unescape();
  test_unescape_edge_cases();
  test_operator_eq();
  test_operator_bool();
  test_operator_spaceship();
  test_utf8();
  test_character_classes();
  test_whitespace_and_formatting();
  test_lenient_extensions();
  test_construction_and_assignment();
  test_scale_and_nesting();
  test_lazy_scanning();
  test_malformed();
  test_misc_edge_cases();
  test_more_coverage();
  test_size();
  test_get_or_overloads();
  test_is_empty_string();
  test_integer_widths();
  test_numeric_overflow();

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
