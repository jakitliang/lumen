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
        std::cout << "Try call" << std::endl;
        return 0;
    });
    L->Call(0, 0);
    Lua::Close(L);

    return 0;
}
