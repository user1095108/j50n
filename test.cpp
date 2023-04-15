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

  std::cout << j["test"].view() << std::endl;
  std::cout << j["key"].view() << std::endl;
  std::cout << j["obj"].view() << std::endl;
  std::cout << j["array"].view() << " " << j["array"].size() << std::endl;
  std::cout << j["array"][0].view() << " " << j["array"][2].view() << std::endl;

  return 0;
}
