# j50n
This repository implements ideas related to the fantastic [js0n](https://github.com/quartzjer/js0n) library and its [fork](https://github.com/nigoroll/js0n/tree/pr_case).

The essence of js0n is its [FSM](https://en.wikipedia.org/wiki/Finite-state_machine)  implementation. This implementation could be improved, but, for now, we rely on the original.
# build instructions
``g++ -std=c++20 test.cpp -o t``

# j50n --- Minimal Zero-Allocation JSON View

## Overview

`j50n` is a lightweight, non-owning JSON accessor built on top of
`std::string_view`.\
It enables zero-allocation navigation and on-demand lookup within JSON
text.

### Key Characteristics

-   Zero dynamic memory allocation
-   Non-owning (`std::string_view`-based)
-   Depth-scanning structural lookup
-   Optional numeric extraction via `std::from_chars()`
-   Functional-style array iteration

This is **not** a full JSON parser: - No DOM construction - No mutation
support - No full validation - Assumes structurally valid JSON (enforced
via `assert`)

------------------------------------------------------------------------

## Storage Model

``` cpp
std::string_view s_;
```

`j50n` stores only a view into externally owned memory.

⚠ The underlying JSON buffer must outlive the `j50n` instance.

------------------------------------------------------------------------

## Construction

### Default

``` cpp
j50n() = default;
```

### From String Literal

``` cpp
template <std::size_t N>
j50n(char const(&a)[N]) noexcept;
```

### Generic Constructor

``` cpp
j50n(auto&& ...a);
```

Enabled when constructible as `std::string_view`.

------------------------------------------------------------------------

## Navigation

### Object Member Lookup

``` cpp
j["key"]
```

Returns a new `j50n` referencing the associated value.

### Array Index Lookup

``` cpp
j[index]
```

Returns the element at `index`.

### Chained Lookup

``` cpp
auto result = j.get("a", "b", 0, "c");
```

Equivalent to:

``` cpp
auto result = j["a"]["b"][0]["c"].get();
```

------------------------------------------------------------------------

## Type Introspection

``` cpp
bool is_empty() const noexcept;
bool is_array() const noexcept;
bool is_object() const noexcept;
```

-   `is_empty()` --- no value found
-   `is_array()` --- first character `'['`
-   `is_object()` --- first character `'{'`

------------------------------------------------------------------------

## Numeric Extraction

``` cpp
template <typename U>
auto get(auto&& ...a) const noexcept;
```

For arithmetic types (excluding `bool`).

Example:

``` cpp
auto [value, error] = j.get<int>("a", "b");
```

Returns:

``` cpp
std::pair<U, bool>
```

Uses `std::from_chars()` (no locale, no allocation).

------------------------------------------------------------------------

## Array Utilities

### Size

``` cpp
auto size() const noexcept;
```

⚠ Worst-case complexity: O(n²)

### Iteration

``` cpp
void feach(auto f) const;
```

Supports:

-   `f(element)`
-   `f(element, index)`

Returning `true` stops iteration early.

⚠ Worst-case complexity: O(n²)

------------------------------------------------------------------------

## Performance Notes

| Operation | Complexity |
| :--- | :--- |
| Key lookup | O(n) |
| Index lookup | O(n) |
| size() | O(n²) |
| feach() | O(n²) |

Best suited for small, trusted JSON payloads.

------------------------------------------------------------------------

## Example

``` cpp
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

    auto [id, err] = j.get<int>("user", "id");
    if (!err)
        std::cout << "ID: " << id << "\n";

    j["values"].feach([](j50n const& e, std::size_t i){
        std::cout << i << ": " << e << "\n";
    });
}
```

------------------------------------------------------------------------

## Limitations

-   No mutation
-   No schema validation
-   No JSON generation
-   Relies on `assert` for structural correctness

------------------------------------------------------------------------

## Summary

`j50n` is a compact, zero-allocation JSON view designed for minimal
overhead and trusted input scenarios.
