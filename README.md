# j50n

A lightweight, header-only C++ JSON parser built on `std::string_view`. Zero allocations, zero dependencies beyond the standard library, zero exceptions. It implements ideas related to the fantastic [js0n](https://github.com/quartzjer/js0n) library and its [fork](https://github.com/nigoroll/js0n/tree/pr_case).

The essence of [js0n](https://github.com/quartzjer/js0n) is its [FSM](https://en.wikipedia.org/wiki/Finite-state_machine) implementation. This FSM could be improved, but, for now, we rely on the original.

## Features

- **Header-only** — just drop `j50n.hpp` into your project
- **Zero-copy** — operates entirely on `std::string_view`; no heap allocations
- **Non-validating** — designed for speed and simplicity over strict [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259.html) compliance (see [Limitations](#limitations))
- **UTF-8 aware** — handles multi-byte sequences inside strings

## Requirements

- C++20 compiler

## Installation

Copy `j50n.hpp` into your include path. No build system integration required.

```cpp
#include "j50n.hpp"
```

## Usage

### Construction

```cpp
j50n j(R"({"name":"Alice","age":30,"scores":[10,20,30]})");
```

`j50n` wraps any type constructible as a `std::string_view`, including string literals,
`std::string`, and `std::string_view` itself. The source string must outlive all `j50n`
instances that reference it.

### Accessing object fields

```cpp
auto name = j["name"];   // j50n wrapping "Alice"
auto age  = j["age"];    // j50n wrapping 30
```

### Accessing array elements

```cpp
auto first = j["scores"][0];   // j50n wrapping 10
```

### Chained access

```cpp
auto sv = j.get("scores", 1);           // std::string_view "20"
auto sv = j.get_or("40", "scores", 3);  // std::string_view "40" (key not found)
```

Use `view(...)` when you want to continue working with the result as a `j50n`;
use `get(...)` when you want a `std::string_view` directly.

```cpp
j50n  sub = j.view("scores");          // j50n — supports further [] / feach / etc.
auto  sv  = j.get("scores");           // std::string_view — terminal extraction
```

### String values and escaping

> **Important:** `get()` returns the raw JSON content of a string value without
> decoding escape sequences. The JSON text `"say \"hello\""` will return
> `say \"hello\"` — with literal backslashes — not `say "hello"`.
> Callers that compare or display string values must account for this.

```cpp
j50n j(R"({"msg":"say \"hello\""})");
auto sv = j.get("msg");   // sv == R"(say \"hello\")"  — not  say "hello"
```

### Numeric extraction

```cpp
auto [value, error] = j.get<int>("age");  // error == false on success; value == 30
auto value = j.get_or<int>(40, "scores", 3);  // 40 — index out of range
```

`get<U>()` uses `std::from_chars()` internally and returns `std::pair<U, bool>`.
The second element is `true` on **failure** — always check `error` before using `value`.

`get_or()` and `get_or<U>()` return a caller-supplied default on any failure.

### Iterating arrays

```cpp
// Without index
j["scores"].feach([](j50n const& elem) {
    std::cout << elem << '\n';
});

// With index
j["scores"].feach([](j50n const& elem, std::size_t i) {
    std::cout << i << ": " << elem << '\n';
});
```

Both overloads support early termination by returning `bool`:

```cpp
j["scores"].feach([](j50n const& elem) -> bool {
    if (elem.get() == "30") return true;  // stop iteration
    std::cout << elem << '\n';
    return false;
});
```

`feach` is a no-op on non-array values, including empty/missing keys.

### Array size

```cpp
auto n = j["scores"].size();  // 3
```

### Type checks

```cpp
j["scores"].is_array();    // true
j["scores"].is_object();   // false
j["name"].is_string();     // true
j["name"].is_empty();      // true means key not found or parse failure
j["active"].is_bool();     // true if value is exactly "true" or "false"
j["ratio"].is_null();      // true if value is exactly "null"
```

Note: `is_null()` and `is_bool()` match bare values exactly. `null` is also visible
via `is_bare()` and its raw text is returned by `get()` as a bare string `null`.

### Output

```cpp
std::cout << j["name"] << '\n';  // prints: Alice
```

## Example

```cpp
#include <iostream>
#include "j50n.hpp"

int main()
{
    j50n j = R"({
        "user": { "id": 123, "name": "alice" },
        "values": [10, 20, 30]
    })";

    auto name = j.get("user", "name");
    std::cout << "Name: " << name << '\n';

    if (auto [id, err] = j.get<int>("user", "id"); !err)
        std::cout << "ID: " << id << '\n';

    std::cout << j.get_or<int>(-1, "fail") << '\n';

    j["values"].feach([](j50n const& e, std::size_t i){
        std::cout << i << ": " << e << '\n';
    });

    return 0;
}
```

## API Reference

| Method | Description |
|---|---|
| `operator[](key)` | Access object field by string key |
| `operator[](n)` | Access array element by zero-based index |
| `get()` | Returns the raw underlying `std::string_view` (no unescaping) |
| `get(...)` | Chained key/index access, returns `std::string_view` |
| `get_or(def, ...)` | Chained key/index access, returns `std::string_view` or `def` if not found |
| `get<U>(...)` | Chained numeric extraction via `std::from_chars()`, returns `std::pair<U, bool>` where second is `true` on failure |
| `get_or<U>(def, ...)` | Chained numeric extraction, returns `U` or `def` on error |
| `view()` | Returns `*this` |
| `view(...)` | Chained key/index access, returns `j50n` |
| `size()` | Number of elements in an array; O(n) scans, O(n²) total — see Performance |
| `is_empty()` | True if the view is empty (key not found or parse error) |
| `is_array()` | True if the value starts with `[` |
| `is_object()` | True if the value starts with `{` (skips leading whitespace) |
| `is_string()` | True if the value starts with `"` |
| `is_bare()` | True if the value does not start with `[`, `{`, or `"` |
| `is_bool()` | True if the value is exactly `true` or `false` |
| `is_null()` | True if the value is exactly `null` |
| `is_number()` | True if the value parses as `long double` via `std::from_chars` — see compiler note |
| `feach(f)` | Iterate array elements; supports early exit via `bool` return; no-op on non-arrays |

## Performance Notes

| Operation | Complexity | Notes |
| :--- | :--- | :--- |
| Type predicate | O(1) | |
| Key lookup | O(n) | Scans the JSON text |
| Index lookup | O(n) | Scans from array start |
| `size()` | O(n²) | Calls index lookup once per element |
| `feach()` | O(n²) | Each step rescans from array start |

## Limitations

- **Read-only** — no serialization or mutation
- **Non-owning** — the source string must outlive any `j50n` instance referencing it
- **Not strictly validating** — malformed JSON may silently produce empty or incorrect results rather than errors
- **No unescaping** — `get()` returns raw JSON string content; escape sequences such as `\"`, `\\`, and `\n` are not decoded
- **Duplicate keys** — only the first matching key is matched
- **Object iteration** — there is no equivalent of `feach` for objects; structured access requires knowing keys in advance
