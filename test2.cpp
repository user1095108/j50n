#include "j50n.hpp"
#include <iostream>

static auto const& JSON(R"(  {
  "name": "j50n-test",
  "version": 1,
  "pi": 3.14159,
  "active": true,
  "utf8": "héllø 🌍",
  "numbers": [1, 2, 3, 4, 5],
  "objects": [
    { "id": 1, "value": 10 },
    { "id": 2, "value": 20 },
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
  j50n root(JSON, sizeof(JSON) - 1);

  std::cout << "== Basic object access ==\n";
  std::cout << "name: "    << root["name"]    << "\n";
  std::cout << "version: " << root["version"] << "\n";
  std::cout << "pi: "      << root["pi"]      << "\n";
  std::cout << "utf8: "    << root["utf8"]    << "\n\n";

  std::cout << "== Nested access (operator[]) ==\n";
  std::cout << "nested.a.b.c: " << root["nested"]["a"]["b"]["c"] << "\n\n";

  std::cout << "== Nested access (get()) ==\n";
  std::cout << "nested.a.b.c: " << root.get("nested", "a", "b", "c") << "\n\n";

  std::cout << "== Numeric parsing ==\n";
  {
    auto const [v, err] = root.get<int>("version");
    std::cout << "version int    = " << v << " (error=" << err << ")\n";
  }
  {
    auto const [a, err] = root.get<bool>("active");
    std::cout << "active bool    = " << a << " (error=" << err << ")\n";
  }
  {
    auto const [p, err] = root.get<double>("pi");
    std::cout << "pi double      = " << p << " (error=" << err << ")\n";
  }
  {
    auto const [q, err] = root["pi"].get<double>();
    std::cout << "pi double (2)  = " << q << " (error=" << err << ")\n\n";
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
    auto const [id,   ierr] = obj.get<int>("id");
    auto const [val, verr2] = obj.get<int>("value");
    std::cout << "  objects[" << i << "]: id=" << id
              << " (err=" << ierr << ")"
              << ", value=" << val
              << " (err=" << verr2 << ")\n";
  });

  std::cout << "\n== Type predicates ==\n";
  std::cout << "name  is_string: " << root["name"].is_string()  << "\n";
  std::cout << "pi    is_bare:   " << root["pi"].is_bare()      << "\n";
  std::cout << "pi    is_number: " << root["pi"].is_number()    << "\n";
  std::cout << "active is_bool:  " << root["active"].is_bool()  << "\n";
  std::cout << "nums  is_array:  " << root["numbers"].is_array()<< "\n";
  std::cout << "root  is_object: " << root.is_object()<< "\n";

  std::cout << "\n== Empty / missing ==\n";
  std::cout << "missing key:       " << (root["does_not_exist"].is_empty() ? "empty (OK)" : "FAIL") << "\n";
  std::cout << "out-of-range idx:  " << (numbers[100].is_empty()           ? "empty (OK)" : "FAIL") << "\n";

  return 0;
}
