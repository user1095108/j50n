#include <iostream>
#include <fstream>
#include <sstream>

#include "j50n.hpp"

int main()
{
  auto const s(
    (std::ostringstream() << std::ifstream("test.json").rdbuf()).str()
  );

  j50n j(s);

  std::cout << j["test"].raw() << std::endl;
  std::cout << j["key"].raw() << std::endl;
  std::cout << j["obj"].raw() << std::endl;
  std::cout << j["array"].raw() << " " << j["array"].size() << std::endl;
  std::cout << j.find("array", 0).get() << " " << j["array"][2].get() << std::endl;

  return 0;
}
