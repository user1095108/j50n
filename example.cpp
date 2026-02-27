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
