//  j50n_example.cpp — a guided tour of j50n.hpp
//
//  j50n is a single-header, zero-dependency JSON *reader* for C++20.
//  A j50n object is just a std::string_view over JSON text you already own:
//  there is no DOM, no copying, no allocation and no exceptions — values are
//  located lazily while you navigate. It is read-only: there is no serializer.
//
//  Build:  g++ -std=c++20 -O2 -Wall -Wextra -o j50n_example j50n_example.cpp

#include <iostream>
#include <string>

#include "j50n.hpp"

int main()
{
    std::cout << std::boolalpha; // print true/false instead of 1/0

    // 1 ─ The document ────────────────────────────────────────────────────────
    // A j50n never owns text. Here the JSON lives in a string literal with
    // static storage duration, so the view stays valid for the whole run.
    // (Detail: `{` must be the first character — literals are not trimmed;
    // constructing from a std::string_view does trim.)
    j50n const doc{R"({
        "name":     "Milestone X200",
        "version":  2,
        "enabled":  true,
        "rating":   4.75,
        "tags":     ["iot", "sensor", "ultra-low-power"],
        "owner":    null,
        "server":   {
            "host": "10.0.0.7",
            "port": 8080,
            "tls":  false
        },
        "sensors": [
            { "id": "s1", "temp": 21.5, "online": true  },
            { "id": "s2", "temp": 19.8, "online": false },
            { "id": "s3", "temp": 23.1, "online": true  }
        ],
        "greeting": "H\u00e9llo, \"w\u00f6rld\"\n"
    })"};

    // 2 ─ Navigating ──────────────────────────────────────────────────────────
    // operator[] takes a key (object) or an index (array) and returns another
    // lightweight view. Missing keys yield an *empty* view, and empty views
    // chain safely — no crash, no exception, just more emptiness.
    std::cout << doc["name"]                 << '\n'; // Milestone X200
    std::cout << doc["sensors"][1]["id"]     << '\n'; // s2
    std::cout << doc.view("server", "port")  << '\n'; // 8080 (whole path in one call)
    std::cout << doc.get("sensors", 0, "id") << '\n'; // s1   (get() takes a path too)

    std::cout << doc.contains("server", "tls")    << '\n'; // true
    std::cout << doc.contains("sensors", 0, "id") << '\n'; // true — keys and indexes mix
    std::cout << doc.contains("server", "user")   << '\n'; // false

    j50n const ghost = doc["ghost"]["deeper"];             // empty view…
    std::cout << ghost.is_empty() << '\n';                 // true — and totally harmless

    // 3 ─ Typed reads ─────────────────────────────────────────────────────────
    // get<T>() returns {value, err}: err == false means success.
    auto const [port,   port_err]   = doc.get<int>("server", "port");
    auto const [ver,    ver_err]    = doc.get<long>("version");
    auto const [rating, rating_err] = doc.get<double>("rating");
    auto const [wifi,   wifi_err]   = doc.get<bool>("enabled");

    std::cout << port << ' ' << ver << ' ' << rating << ' ' << wifi << '\n';
    //   → 8080 2 4.75 true

    // get_or() supplies a fallback when a key is absent — for strings…
    std::cout << doc.get_or("en-GB", "locale")   << '\n'; // en-GB           (missing → default)
    std::cout << doc.get_or("anonymous", "name") << '\n'; // Milestone X200  (present → value)
    // …and for numbers:
    std::cout << doc.get_or(1500, "server", "timeout_ms") << '\n'; // 1500 (missing → default)
    std::cout << doc.get_or(9999, "server", "port")       << '\n'; // 8080 (present → value)

    // Careful: null is a *value*, so it is no reason to fall back:
    std::cout << doc.get_or("n/a", "owner") << '\n';      // null — the key exists!
    std::cout << (doc["owner"] == nullptr)  << '\n';      // true — test for null like this

    // 4 ─ Truthiness & comparisons ────────────────────────────────────────────
    // explicit operator bool(): false, null, 0, "", [], {} and missing keys
    // are all falsy — handy for `if (doc["flag"])`-style code.
    std::cout << bool(doc["enabled"]) << '\n'; // true  (boolean true)
    std::cout << bool(doc["rating"])  << '\n'; // true  (non-zero number)
    std::cout << bool(doc["tags"])    << '\n'; // true  (non-empty array)
    std::cout << bool(doc["owner"])   << '\n'; // false (null)
    std::cout << bool(doc["ghost"])   << '\n'; // false (missing key)

    // Numbers compare numerically, strings compare by decoded content,
    // and there is no silent coercion between the two worlds.
    std::cout << (doc["server"]["port"] == 8080)   << '\n'; // true
    std::cout << (doc["rating"] > 4.5)             << '\n'; // true
    std::cout << (doc["name"] == "Milestone X200") << '\n'; // true
    std::cout << (doc["version"] == "2")           << '\n'; // false — it's a number, not a string

    // 5 ─ Iterating ───────────────────────────────────────────────────────────
    // Objects: feach(key, value). Arrays: feach(item) or feach(item, index).
    std::cout << "server:\n";
    doc["server"].feach([](std::string_view key, j50n val)
    {
        std::cout << "  " << key << " = " << val << '\n';
    });

    std::cout << "tags:\n";
    doc["tags"].feach([](j50n tag, std::size_t i)
    {
        std::cout << "  " << i << ": " << tag << '\n';
    });

    // A callback may return bool: true stops the walk early.
    std::cout << "first offline sensor: ";
    doc["sensors"].feach([](j50n s)
    {
        if (s["online"]) return false; // keep going
        std::cout << s["id"] << '\n';  // → s2
        return true;                   // found it — stop
    });

    // Views compose with everything above — a tiny aggregation:
    double      sum = 0.0;
    std::size_t n   = 0;
    doc["sensors"].feach([&](j50n s)
    {
        if (auto const [t, err] = s.get<double>("temp"); !err)
        {
            sum += t;
            ++n;
        }
        return false; // keep going
    });
    std::cout << "average temp: " << sum / n << " over " << n << " sensors\n";

    // 6 ─ Sizes and type queries ──────────────────────────────────────────────
    std::cout << doc.size()            << " top-level members\n"; // 9
    std::cout << doc["sensors"].size() << " sensors\n";           // 3
    std::cout << doc["tags"].size()    << " tags\n";              // 3

    std::cout << doc["name"].is_string()    << '\n'; // true
    std::cout << doc["version"].is_number() << '\n'; // true
    std::cout << doc["enabled"].is_bool()   << '\n'; // true
    std::cout << doc["owner"].is_null()     << '\n'; // true
    std::cout << doc["tags"].is_array()     << '\n'; // true
    std::cout << doc["server"].is_object()  << '\n'; // true

    // 7 ─ Strings: raw vs decoded ─────────────────────────────────────────────
    // operator<< and get() hand you the *raw* slice — escape sequences stay
    // intact, because decoding would require an allocation:
    std::cout << doc["greeting"] << '\n';
    //   → H\u00e9llo, \"w\u00f6rld\"\n

    // unescape() decodes on demand into a std::string (UTF-8 aware,
    // surrogate pairs included). The decoded string ends with '\n',
    // so the extra << '\n' adds the blank line you can see in the output:
    std::cout << j50n::unescape(doc["greeting"].get()) << '\n';
    //   → Héllo, "wörld"

    // operator== against strings decodes automatically:
    std::cout << (doc["greeting"] == "Héllo, \"wörld\"\n") << '\n'; // true

    // 8 ─ Any sub-view is a document ──────────────────────────────────────────
    // A slice — say, one array element — is itself a fully usable j50n.
    // operator<< on a compound value prints the raw JSON slice:
    j50n const s2 = doc["sensors"][1];
    std::cout << s2 << '\n';
    //   → { "id": "s2", "temp": 19.8, "online": false }
    std::cout << s2["id"] << " @ " << s2["temp"] << " °C\n";

    return 0;
}
