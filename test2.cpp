#include "j50n.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main()
{
  // Load JSON file into a string (must outlive j50n)
  auto const json((std::ostringstream() << std::ifstream("test2.json").rdbuf()).str());
  j50n root(json);

  std::cout << "== Basic object access ==\n";
  std::cout << "name: " << root["name"] << "\n";
  std::cout << "version: " << root["version"] << "\n";
  std::cout << "pi: " << root["pi"] << "\n";
  std::cout << "utf8: " << root["utf8"] << "\n\n";

  std::cout << "== Nested access (operator[]) ==\n";
  std::cout << "nested.a.b.c: "
            << root["nested"]["a"]["b"]["c"] << "\n\n";

  std::cout << "== Nested access (get()) ==\n";
  std::cout << "nested.a.b.c: "
            << root.get("nested", "a", "b", "c") << "\n\n";

  std::cout << "== Numeric parsing ==\n";
  {
    auto [v, err] = root.get<int>("version");
    std::cout << "version int = " << v
              << " (error=" << err << ")\n";

    auto [p, perr] = root.get<double>("pi");
    std::cout << "pi double = " << p
              << " (error=" << perr << ")\n\n";
  }

  std::cout << "== Array access ==\n";
  j50n numbers = root["numbers"];
  std::cout << "numbers.size() = " << numbers.size() << "\n";

  for (std::size_t i = 0; i < numbers.size(); ++i)
    std::cout << "numbers[" << i << "] = "
              << numbers[i] << "\n";

  std::cout << "\n== feach(element) ==\n";
  numbers.feach([](j50n const& e) {
    std::cout << e << " ";
  });
  std::cout << "\n\n";

  std::cout << "== feach(element, index) ==\n";
  numbers.feach([](j50n const& e, std::size_t i) {
    std::cout << "[" << i << "]=" << e << " ";
  });
  std::cout << "\n\n";

  std::cout << "== Array of objects ==\n";
  j50n objects = root["objects"];

  objects.feach([](j50n const& obj, std::size_t i) {
    std::cout << "objects[" << i << "]: "
              << "id=" << obj["id"]
              << ", value=" << obj["value"]
              << "\n";
  });

  std::cout << "\n== Failure / empty cases ==\n";
  if (root["does_not_exist"].is_empty())
    std::cout << "missing key -> empty (OK)\n";

  if (numbers[100].is_empty())
    std::cout << "out-of-range index -> empty (OK)\n";

  return 0;
}
