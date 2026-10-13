#include <iostream>
#include "j50n.hpp"

int main()
{
    j50n j = R"({
        "user": { "id": 123, "name": "alice" },
        "values": [10, 20, 30]
    })";

    auto name = j.get("user", "name");
    std::cout << "Name: " << name << '\n';

    if (auto [id, err] = j.get<int>("user", "id"); !err)
        std::cout << "ID: " << id << '\n';

    std::cout << j.get_or<int>(-1, "fail") << '\n';

    j.feach([](std::string_view const k, j50n const e){
        std::cout << k << ": " << e << '\n';
    });

    j["values"].feach([](j50n const e, std::size_t i){
        std::cout << i << ": " << e << '\n';
    });

    std::cout << j50n(R"("\ud83c\udf08 \ud83e\udd8b \u2728 \ud83c\udf3b \ud83d\udc96")").get_unescaped() << std::endl;

    return 0;
}
