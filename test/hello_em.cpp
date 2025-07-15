/*!
 * @brief hello_em
 * @author Jakit
 * @date 2025/7/15
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#include <iostream>
#include <emscripten.h>
#include "lumen.h"

struct Welcome {
    int Counter;

    void Hello() {
        Counter++;
        std::cout << "Welcome to Lumen! x " << Counter << std::endl;
    }
};

int main() {
    std::cout << LUMEN_RELEASE " -- " LUMEN_COPYRIGHT << std::endl;
    auto L = Lumen::Open();
    std::cout << "Lumen: load libs" << std::endl;
    L->OpenLibs();

    // Make an example library "Welcome"
    {
        constexpr auto libName = "Welcome";
        auto tWelcome = static_cast<Welcome *>(L->NewUserdata(sizeof(Welcome)));
        *tWelcome = Welcome{0}; // Initialize, counter = 0
        L->NewMetatable(libName);
        L->PushValue(-1);
        L->SetField(-2, "__index");
        L->PushDelegate([](Lumen::IState *l) {
            auto w = static_cast<Welcome *>(l->CheckUserdata(1, "Welcome"));
            w->Hello();
            return 0;
        });
        L->SetField(-2, "Hello");
        L->SetMetatable(-2);
        L->SetGlobal("Welcome");
    }

    // Scripting with Lua
    if (L->DoString<0>(R"(
-- Calling the Welcome library x 10 times
for i = 1, 10 do
  Welcome:Hello()
end
)") != Lumen::RetOK) {
        std::cout << L->ToString(-1) << std::endl;
        L->Pop();
    }

    Lumen::Close(L);
    std::cout << "Lumen: stop" << std::endl;
}
