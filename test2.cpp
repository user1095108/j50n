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

int main()
{
  // Safer initialization with explicit size
  j50n root(JSON.data(), JSON.size());
  
  std::cout << std::boolalpha << std::fixed << std::setprecision(5);

  std::cout << "== Basic object access ==\n";
  std::cout << "name:    " << root["name"]    << "\n";
  std::cout << "version: " << root["version"] << "\n";
  std::cout << "pi:      " << root["pi"]      << "\n";
  std::cout << "utf8:    " << root["utf8"]    << "\n";
  std::cout << "ratio:   " << (root["ratio"].is_null() ? "null" : "not null") << "\n\n";

  std::cout << "== Nested access (operator[]) ==\n";
  std::cout << "nested.a.b.c: " << root["nested"]["a"]["b"]["c"] << "\n\n";

  std::cout << "== Nested access (get()) ==\n";
  std::cout << "nested.a.b.c: " << root.get("nested", "a", "b", "c") << "\n\n";

  std::cout << "== Typed extraction with error handling ==\n";
  {
    auto const [v, err] = root.get<int>("version");
    std::cout << "version int   = " << v << " (error=" << err << ")\n";
  }
  {
    auto const [a, err] = root.get<bool>("active");
    std::cout << "active bool   = " << a << " (error=" << err << ")\n";
  }
  {
    auto const [p, err] = root.get<double>("pi");
    std::cout << "pi double     = " << p << " (error=" << err << ")\n";
  }
  {
    // Test error case: string to int conversion should fail
    auto const [n, err] = root.get<int>("name");
    std::cout << "name as int   = " << n << " (error=" << err << ")\n\n";
  }

  std::cout << "== Array access ==\n";
  j50n numbers = root["numbers"];
  std::cout << "numbers.size() = " << numbers.size() << "\n";
  for (std::size_t i = 0; i < numbers.size(); ++i)
    std::cout << "  numbers[" << i << "] = " << numbers[i] << "\n";

  std::cout << "\n== feach(element) ==\n";
  numbers.feach([](j50n const& e) { std::cout << e << " "; });
  std::cout << "\n";

  std::cout << "\n== feach(element, index) ==\n";
  numbers.feach([](j50n const& e, std::size_t i) {
    std::cout << "[" << i << "]=" << e << " ";
  });
  std::cout << "\n";

  std::cout << "\n== Array of objects ==\n";
  root["objects"].feach([](j50n const& obj, std::size_t i) {
    auto const [id, ierr] = obj.get<int>("id");
    auto const [val, verr] = obj.get<int>("value");
    std::cout << "  objects[" << i << "]: id=" << id << " (err=" << ierr << ")";
    std::cout << ", value=" << val << " (err=" << verr << ")";
    if (obj["tag"].is_null()) std::cout << ", tag=null";
    else if (obj["tag"].is_string()) std::cout << ", tag=" << obj["tag"];
    std::cout << "\n";
  });

  std::cout << "\n== Type predicates ==\n";
  std::cout << "name     is_string: " << root["name"].is_string()  << "\n";
  std::cout << "pi       is_bare:   " << root["pi"].is_bare()      << "\n";
  std::cout << "pi       is_number: " << root["pi"].is_number()    << "\n";
  std::cout << "active   is_bool:   " << root["active"].is_bool()  << "\n";
  std::cout << "ratio    is_null:   " << root["ratio"].is_null()   << "\n";
  std::cout << "numbers  is_array:  " << root["numbers"].is_array()<< "\n";
  std::cout << "root     is_object: " << root.is_object()          << "\n";

  std::cout << "\n== Empty / missing handling ==\n";
  std::cout << "missing key:       " << (root["does_not_exist"].is_empty() ? "empty (OK)" : "FAIL") << "\n";
  std::cout << "out-of-range idx:  " << (numbers[100].is_empty()           ? "empty (OK)" : "FAIL") << "\n";
  std::cout << "empty array size:  " << root["empty_arr"].size() << "\n";
  std::cout << "empty object size: " << root["empty_obj"].size() << "\n";

  std::cout << "\n== Deep missing key ==\n";
  std::cout << "nested.a.x: " << (root.view("nested", "a", "x").is_empty() ? "empty (OK)" : "FAIL") << "\n";

  return 0;
}
