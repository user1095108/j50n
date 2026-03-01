# j50n

A lightweight, header-only C++ JSON parser built on `std::string_view`. Zero allocations, zero dependencies beyond the standard library, zero exceptions. It implements ideas related to the fantastic [js0n](https://github.com/quartzjer/js0n) library and its [fork](https://github.com/nigoroll/js0n/tree/pr_case).

The essence of [js0n](https://github.com/quartzjer/js0n) is its [FSM](https://en.wikipedia.org/wiki/Finite-state_machine)  implementation. This FSM could be improved, but, for now, we rely on the original.

## Features

- **Header-only** — just drop `j50n.hpp` into your project
- **Zero-copy** — operates entirely on `std::string_view`; no heap allocations
- **Non-validating** — designed for speed and simplicity over strict RFC 8259 compliance
- **UTF-8 aware** — handles multi-byte sequences inside strings

## Requirements

- C++20 compiler

## Installation

Copy `j50n.hpp` into your include path. No build system integration required.
```c++
#include "j50n.hpp"
```

## Usage

### Construction
```c++
j50n j(R"({"name":"Alice","age":30,"scores":[10,20,30]})");
```

`j50n` wraps any type constructible as a `std::string_view`, including string literals, `std::string`, and `std::string_view` itself.

### Accessing object fields
```c++
auto name = j["name"];   // j50n wrapping "Alice"
auto age  = j["age"];    // j50n wrapping 30
```

### Accessing array elements
```c++
auto first = j["scores"][0];   // j50n wrapping 10
```

### Chained access
```c++
auto sv = j.get("scores", 1);  // std::string_view "20"
```

### Numeric extraction
```c++
auto [value, error] = j.get<int>("age");
// value == 30, error == false on success
```

`get<U>()` uses `std::from_chars()` internally and returns a `std::pair<U, bool>` where the second element is `true` on failure.

### Iterating arrays
```c++
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
```c++
j["scores"].feach([](j50n const& elem) -> bool {
    if (elem.get() == "30") return true;  // stop
    std::cout << elem << '\n';
    return false;
});
```

### Array size
```c++
auto n = j["scores"].size();  // 3
```

### Type checks
```c++
j["scores"].is_array();    // true
j["scores"].is_object();   // false
j["name"].is_string();     // true
j["name"].is_empty();      // false — empty means parse failed / key not found
```

### Output
```c++
std::cout << j["name"] << '\n';  // prints: Alice
```

## Example

``` c++
#include "j50n.hpp"
#include <iostream>

int main()
{
    j50n j = R"({
        "user": { "id": 123, "name": "alice" },
        "values": [10, 20, 30]
    })";

    auto name = j.get("user", "name");
    std::cout << "Name: " << name << "\n";

    if (auto [id, err] = j.get<int>("user", "id"); !err)
        std::cout << "ID: " << id << "\n";

    j["values"].feach([](j50n const& e, std::size_t i){
        std::cout << i << ": " << e << "\n";
    });

    return 0;
}
```
## API Reference

| Method | Description |
|---|---|
| `operator[](key)` | Access object field by string key |
| `operator[](n)` | Access array element by zero-based index |
| `get()` | Returns the underlying `std::string_view` |
| `get(...)` | Chained key/index access, returns `std::string_view` |
| `get<U>(...)` | Chained numeric extraction via `std::from_chars()`, returns `std::pair<U, bool>` |
| `view()` | Returns the underlying `j50n` |
| `view(...)` | Chained key/index access, returns `j50n` |
| `size()` | Number of elements in an array (O(n²)) |
| `is_empty()` | True if the view is empty (key not found or parse error) |
| `is_array()` | True if the view starts with `[` |
| `is_object()` | True if the view starts with `{` |
| `is_string()` | True if the view starts with `"` |
| `is_bare()` | True if the view does not start with `[`, `{` or `"` |
| `is_bool()` | True if the view is `true` or `false` |
| `is_null()` | True if the view is `null` |
| `feach(f)` | Iterate array elements (O(n²)); supports early exit via `bool` return |
## Performance Notes

| Operation | Time Complexity |
| :--- | :--- |
| Key lookup | O(n) |
| Index lookup | O(n) |
| size() | O(n²) |
| feach() | O(n²) |
## Limitations

- **Read-only** — no serialization or mutation
- **Non-owning** — the source string must outlive any `j50n` instance derived from it
- **Not strictly validating** — malformed JSON may produce unexpected results rather than errors
- Duplicate keys: the first matching key is returned
- No support for `null` as a distinct type — it parses as a bare value string `"null"`
