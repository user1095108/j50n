#include <iostream>
#include <fstream>
#include <sstream>

#include "j50n.hpp"

int main()
{
  auto const s((std::ostringstream() << std::ifstream("test.json").rdbuf()).str());
  j50n j(s);

  std::cout << j["bool"] << std::endl;
  std::cout << j["foo"] << std::endl;
  std::cout << j["test"] << std::endl;
  std::cout << j["key"] << std::endl;
  std::cout << j["obj"] << " " << j.find("obj", "true") << std::endl;
  std::cout << j["array"] << " " << j["array"].size() << std::endl;
  std::cout << j.find("array", 0) << " " << j["array"][2] << std::endl;

  return 0;
}
