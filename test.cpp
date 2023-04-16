#include <iostream>
#include <fstream>
#include <sstream>

#include "j50n.hpp"

int main()
{
  auto const s((std::ostringstream() << std::ifstream("test.json").rdbuf()).str());
  j50n j(s);

  std::cout << j["bool"].get() << std::endl;
  std::cout << j["foo"].get() << std::endl;
  std::cout << j["test"].get() << std::endl;
  std::cout << j["key"].get() << std::endl;
  std::cout << j["obj"].get() << std::endl;
  std::cout << j["array"].get() << " " << j["array"].size() << std::endl;
  std::cout << j.find("array", 0).get() << " " << j["array"][2].get() << std::endl;
  std::cout << j.find("obj", "true").get() << std::endl;

  return 0;
}
