
#include "j50n.hpp"
#include <iostream>
#include <iomanip>
#include <string_view>

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

// Additional JSON fixtures used in the extended tests
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

// ── tiny helper that prints PASS / FAIL ──────────────────────────────────────
static int g_failures = 0;

static void check(bool ok, std::string_view label)
{
  if (ok)
    std::cout << "  [PASS] " << label << '\n';
  else
  {
    std::cout << "  [FAIL] " << label << '\n';
    ++g_failures;
  }
}

// ── main ─────────────────────────────────────────────────────────────────────
int main()
{
  std::cout << std::boolalpha << std::fixed << std::setprecision(5);

  j50n root(JSON.data(), JSON.size());

  // ── 1. original suite (kept verbatim) ─────────────────────────────────────
  std::cout << "== Basic object access ==\n";
  std::cout << "name:    " << root["name"]    << '\n';
  std::cout << "version: " << root["version"] << '\n';
  std::cout << "pi:      " << root["pi"]      << '\n';
  std::cout << "utf8:    " << root["utf8"]    << '\n';
  std::cout << "ratio:   " << (root["ratio"].is_null() ? "null" : "not null") << "\n\n";

  std::cout << "== Nested access (operator[]) ==\n";
  std::cout << "nested.a.b.c: " << root["nested"]["a"]["b"]["c"] << "\n\n";

  std::cout << "== Nested access (get()) ==\n";
  std::cout << "nested.a.b.c: " << root.get("nested", "a", "b", "c") << "\n\n";

  std::cout << "== Typed extraction with error handling ==\n";
  { auto const [v,e]=root.get<int>("version");     std::cout<<"version int   = "<<v<<" (error="<<e<<")\n"; }
  { auto const [a,e]=root.get<bool>("active");     std::cout<<"active bool   = "<<a<<" (error="<<e<<")\n"; }
  { auto const [p,e]=root.get<double>("pi");       std::cout<<"pi double     = "<<p<<" (error="<<e<<")\n"; }
  { auto const [n,e]=root.get<int>("name");        std::cout<<"name as int   = "<<n<<" (error="<<e<<")\n\n"; }

  std::cout << "== Array access ==\n";
  j50n numbers = root["numbers"];
  std::cout << "numbers.size() = " << numbers.size() << '\n';
  for (std::size_t i = 0; i < numbers.size(); ++i)
    std::cout << "  numbers[" << i << "] = " << numbers[i] << '\n';

  std::cout << "\n== feach(element) ==\n";
  numbers.feach([](j50n const& e){ std::cout << e << " "; });
  std::cout << '\n';

  std::cout << "\n== feach(element, index) ==\n";
  numbers.feach([](j50n const& e, std::size_t i){
    std::cout << "[" << i << "]=" << e << " ";
  });
  std::cout << '\n';

  std::cout << "\n== Array of objects ==\n";
  root["objects"].feach([](j50n const& obj, std::size_t i){
    auto const [id, ie]  = obj.get<int>("id");
    auto const [val, ve] = obj.get<int>("value");
    std::cout << "  objects[" << i << "]: id=" << id << " (err=" << ie << ")";
    std::cout << ", value=" << val << " (err=" << ve << ")";
    if      (obj["tag"].is_null())   std::cout << ", tag=null";
    else if (obj["tag"].is_string()) std::cout << ", tag=" << obj["tag"];
    std::cout << '\n';
  });

  std::cout << "\n== Type predicates ==\n";
  std::cout << "name     is_string: " << root["name"].is_string()  << '\n';
  std::cout << "pi       is_bare:   " << root["pi"].is_bare()      << '\n';
  std::cout << "pi       is_number: " << root["pi"].is_number()    << '\n';
  std::cout << "active   is_bool:   " << root["active"].is_bool()  << '\n';
  std::cout << "ratio    is_null:   " << root["ratio"].is_null()   << '\n';
  std::cout << "numbers  is_array:  " << root["numbers"].is_array()<< '\n';
  std::cout << "root     is_object: " << root.is_object()          << '\n';

  std::cout << "\n== Empty / missing handling ==\n";
  std::cout << "missing key:       " << (root["does_not_exist"].is_empty() ? "empty (OK)" : "FAIL") << '\n';
  std::cout << "out-of-range idx:  " << (numbers[100].is_empty()           ? "empty (OK)" : "FAIL") << '\n';
  std::cout << "empty array size:  " << root["empty_arr"].size() << '\n';
  std::cout << "empty object size: " << root["empty_obj"].size() << '\n';

  std::cout << "\n== Deep missing key ==\n";
  std::cout << "nested.a.x: " << (root.view("nested","a","x").is_empty() ? "empty (OK)" : "FAIL") << '\n';

  // ── 2. get_or – string fallback ────────────────────────────────────────────
  std::cout << "\n== get_or (string) ==\n";
  check(root.get_or("DEFAULT", "name")           == "j50n-test", "existing key returns real value");
  check(root.get_or("DEFAULT", "does_not_exist") == "DEFAULT",   "missing key returns default");
  check(root.get_or("FB", "nested","a","b","c")  == "deep",      "nested existing key");
  check(root.get_or("FB", "nested","a","NOPE")   == "FB",        "nested missing key");

  // ── 3. get_or – numeric fallback ──────────────────────────────────────────
  std::cout << "\n== get_or (numeric) ==\n";
  check(root.get_or<int>(-1, "version")          == 1,           "existing int");
  check(root.get_or<int>(-1, "does_not_exist")   == -1,          "missing int → default");
  check(root.get_or<int>(-1, "name")             == -1,          "non-numeric string → default");
  check(root.get_or<double>(0.0, "pi")           > 3.14,         "existing double");

  // ── 4. get_or – bool fallback ─────────────────────────────────────────────
  std::cout << "\n== get_or (bool via get<bool>) ==\n";
  { auto const [v,e]=root.get<bool>("active"); check(!e && v,  "true parses correctly"); }
  { auto const [v,e]=root.get<bool>("does_not_exist"); check(e, "missing bool → error flag"); }

  // ── 5. is_object on nested values ─────────────────────────────────────────
  std::cout << "\n== is_object (nested) ==\n";
  check(root["nested"].is_object(),        "nested is object");
  check(root["nested"]["a"].is_object(),   "nested.a is object");
  check(!root["name"].is_object(),         "string is not object");
  check(!root["numbers"].is_object(),      "array is not object");
  check(!root["does_not_exist"].is_object(),"missing is not object");

  // ── 6. feach early exit (bool return) ─────────────────────────────────────
  std::cout << "\n== feach early exit ==\n";
  {
    std::size_t count = 0;
    numbers.feach([&](j50n const& e) -> bool {
      ++count;
      auto const [v,err] = e.get<int>();
      return (!err && v >= 3); // stop when we hit 3
    });
    check(count == 3, "feach stops after element value == 3 (visited 3 elements)");
  }
  {
    std::size_t count = 0;
    numbers.feach([&](j50n const& e, std::size_t) -> bool {
      ++count;
      return count == 2; // stop after index 1
    });
    check(count == 2, "feach(el,idx) stops after 2 elements");
  }

  // ── 7. feach on empty arrays ──────────────────────────────────────────────
  std::cout << "\n== feach on empty array ==\n";
  {
    std::size_t count = 0;
    root["empty_arr"].feach([&](j50n const&){ ++count; });
    check(count == 0, "feach on empty_arr never calls lambda");
  }
  {
    // feach on a non-array (e.g. a string) should also be a no-op
    std::size_t count = 0;
    root["name"].feach([&](j50n const&){ ++count; });
    check(count == 0, "feach on non-array is a no-op");
  }

  // ── 8. view() explicit usage ──────────────────────────────────────────────
  std::cout << "\n== view() ==\n";
  {
    j50n v = root.view("nested","a","b");
    check(v.is_object(),                           "view() returns nested object");
    check(v["c"].get() == "deep",                  "further access from view() result");
  }
  {
    j50n v = root.view("numbers");
    check(v.is_array(),                            "view() of array");
    check(v.size() == 5,                           "view() array has correct size");
  }
  {
    j50n v = root.view("does_not_exist");
    check(v.is_empty(),                            "view() of missing key is empty");
  }

  // ── 9. copy / move / assignment ───────────────────────────────────────────
  std::cout << "\n== copy & assignment ==\n";
  {
    j50n a = root["name"];
    j50n b = a;                         // copy constructor
    check(b.get() == "j50n-test",       "copy-constructed j50n has same value");

    j50n c;
    c = a;                              // copy assignment
    check(c.get() == "j50n-test",       "copy-assigned j50n has same value");

    j50n d = std::move(b);              // move constructor
    check(d.get() == "j50n-test",       "move-constructed j50n has same value");

    j50n e2;
    e2 = std::move(d);                  // move assignment
    check(e2.get() == "j50n-test",      "move-assigned j50n has same value");
  }
  {
    // Assign from string_view
    j50n j;
    std::string_view sv = JSON;
    j = sv;
    check(j["name"].get() == "j50n-test", "assign from string_view");
  }

  // ── 10. negative & edge-case numbers ──────────────────────────────────────
  std::cout << "\n== Numbers (negative, scientific, large) ==\n";
  {
    j50n nums(JSON_NUMBERS.data(), JSON_NUMBERS.size());

    { auto const [v,e]=nums.get<int>("neg");           check(!e && v==-42,         "negative int"); }
    { auto const [v,e]=nums.get<double>("negfloat");   check(!e && v<-3.0,         "negative float"); }
    { auto const [v,e]=nums.get<int>("zero");          check(!e && v==0,           "zero"); }
    { auto const [v,e]=nums.get<long long>("big");     check(!e && v>9007199254LL, "large int"); }
    { auto const [v,e]=nums.get<double>("sci");        check(!e && v==150.0,       "scientific notation"); }
    { auto const [v,e]=nums.get<double>("sci_neg");    check(!e && v==0.25,        "negative exponent"); }
    { auto const [v,e]=nums.get<unsigned>("uint_max"); check(!e && v==4294967295u, "uint32 max"); }

    check(nums["neg"].is_number(),       "negative bare value is_number");
    check(nums["big"].is_bare(),         "large int is_bare");
    check(!nums["neg"].is_bool(),        "negative int is not bool");
    check(!nums["neg"].is_null(),        "negative int is not null");
  }

  // ── 11. bool predicates corner cases ──────────────────────────────────────
  std::cout << "\n== Bool predicates corner cases ==\n";
  {
    j50n bools(JSON_BOOLS.data(), JSON_BOOLS.size());

    check( bools["yes"].is_bool(),          "true  is_bool");
    check( bools["no"].is_bool(),           "false is_bool");
    check(!bools["num"].is_bool(),          "1 is not bool");
    check(!bools["yes"].is_number(),        "true is not number");
    check(!bools["yes"].is_null(),          "true is not null");

    { auto const [v,e]=bools.get<bool>("yes"); check(!e && v,  "get<bool> true");  }
    { auto const [v,e]=bools.get<bool>("no");  check(!e && !v, "get<bool> false"); }
    { auto const [v,e]=bools.get<bool>("num"); check(e,        "get<bool> from 1 → error"); }
  }

  // ── 12. escaped strings ───────────────────────────────────────────────────
  // j50n returns the raw JSON string content (including escape sequences);
  // it does not unescape – so we verify the raw bytes are preserved intact.
  std::cout << "\n== Escaped strings (raw content preserved) ==\n";
  {
    j50n esc(JSON_ESCAPED.data(), JSON_ESCAPED.size());

    check(!esc["quote"].is_empty(),      "escaped quote key found");
    check( esc["quote"].is_string(),     "escaped quote is_string");
    check(!esc["backslash"].is_empty(),  "backslash key found");
    check(!esc["newline"].is_empty(),    "newline key found");
    check(!esc["tab"].is_empty(),        "tab key found");
    check(!esc["solidus"].is_empty(),    "solidus key found");
    check(!esc["unicode"].is_empty(),    "unicode escape key found");
    // raw get() strips the leading '"' but keeps the rest as-is
    check(esc["quote"].get().find("\\\"") != std::string_view::npos,
          "raw \\\" escape bytes are present in get() output");
  }

  // ── 13. string_view lifetime – j50n is non-owning ─────────────────────────
  std::cout << "\n== Non-owning / string_view semantics ==\n";
  {
    // j50n wraps a view into JSON; if we keep JSON alive the views are stable
    std::string_view sv(JSON.data(), JSON.size());
    j50n j(sv);
    j50n name = j["name"];
    check(name.get() == "j50n-test", "j50n wraps external storage correctly");
    // Reassign j to a different document; name still valid (owns its own view)
    j = std::string_view(JSON_NUMBERS.data(), JSON_NUMBERS.size());
    check(j["neg"].get() == "-42",   "reassignment works, old sub-view unaffected");
  }

  // ── 14. operator[] with std::string_view key ──────────────────────────────
  std::cout << "\n== operator[] with std::string_view ==\n";
  {
    std::string_view k = "pi";
    auto val = root[k];
    check(!val.is_empty(),          "lookup by string_view key works");
    check(val.is_number(),          "string_view key returns correct value");

    std::string_view missing = "xyzzy";
    check(root[missing].is_empty(), "missing string_view key returns empty");
  }

  // ── 15. size() on non-arrays ──────────────────────────────────────────────
  std::cout << "\n== size() on non-array types ==\n";
  check(root["name"].size()        == 0, "size() on string returns 0");
  check(root["pi"].size()          == 0, "size() on number returns 0");
  check(root["active"].size()      == 0, "size() on bool returns 0");
  check(root["ratio"].size()       == 0, "size() on null returns 0");
  check(root["empty_obj"].size()   == 0, "size() on empty object returns 0");
  check(root["numbers"].size()     == 5, "size() on 5-element array returns 5");
  check(root["objects"].size()     == 3, "size() on 3-element objects array returns 3");

  // ── summary ───────────────────────────────────────────────────────────────
  std::cout << "\n== Summary ==\n";
  if (g_failures == 0)
    std::cout << "All checks PASSED.\n";
  else
    std::cout << g_failures << " check(s) FAILED.\n";

  return g_failures ? 1 : 0;
}
