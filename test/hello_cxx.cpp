/*!
 * @brief hello_cxx
 * @author Jakit
 * @date 2025/5/29
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#include "lua.hpp"

int main() {
    auto L = Lua::Open();
    L->OpenLibs();
    L->PushLiteral("my_value");
    L->SetGlobal("my_key");
    L->DoString(R"(
print(my_key);
)");
    L->Close();

    return 0;
}
