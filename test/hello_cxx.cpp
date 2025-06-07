/*!
 * @brief hello_cxx
 * @author Jakit
 * @date 2025/5/29
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#include "lua.hpp"
#include <iostream>

int main() {
    auto L = Lua::Open();
    L->OpenLibs();
    L->PushLiteral("my_value");
    L->SetGlobal("my_key");
    L->DoString(R"(
print(my_key);
)");

    L->PushDelegate([](Lua::State *l) {
        auto n = l->OptNumber(1, 123);
        std::cout << "Got number: " << n << std::endl;
        return 1;
    });
    L->SetGlobal("TestNumber");
    L->DoString<0>(R"(
TestNumber(555);
)");
    Lua::Close(L);

    return 0;
}
