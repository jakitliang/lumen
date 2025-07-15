/*!
 * @brief hello_cxx
 * @author Jakit
 * @date 2025/5/29
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#include "lumen.h"
#include <iostream>

struct Test {
    int a;
};

int main() {
    auto L = Lumen::Open();
    L->OpenLibs();
    L->PushDelegate([](Lumen::IState *l) {
        std::cout << "HelloCXX" << std::endl;
        return 0;
    });
    L->Call(0, 0);
    L->PushLiteral("my_value");
    L->SetGlobal("my_key");
    L->DoString(R"(
print(my_key);
)");

    L->PushDelegate([](Lumen::IState *l) {
        auto n = l->OptNumber(1, 123);
        std::cout << "Got number: " << n << std::endl;
        return 1;
    });
    L->SetGlobal("TestNumber");
    L->DoString<0>(R"(
TestNumber(555);
)");

    L->DoString<0>(R"(
Super = {a = 123}
Super.__index = Super
)");
    L->GetGlobal("Super");
    L->PushLightUserdata(new int{1});
    L->GetGlobal("Super");
    L->SetMetatable(-2);

    std::cout << L->InstanceOf(-1, -2) << std::endl;

    L->NewMetatable("Test");
    L->PushValue(-1);
    L->SetField(-2, "__index");
    L->PushDelegate([](Lumen::IState *l) {
        auto t = (Test *) l->ToUserdata(1);
        l->PushNumber(t->a);
        return 1;
    });
    L->SetField(-2, "GetA");
    auto test = reinterpret_cast<Test *>(L->NewUserdata(sizeof(Test)));
    L->PushValue(-2);
    L->SetMetatable(-2);
    L->PushValue(-1);
    L->SetGlobal("Test");
    if (L->DoString<0>(R"(
print("Test.GetA", Test:GetA())
)") != Lumen::RetOK) {
        std::cout << L->ToString(-1) << std::endl;
        L->Pop();
    }

    {
        L->PushString("Hello world");
        auto str = Lumen::IString::Get(L, -1);
        std::cout << str->CString() << std::endl;
        L->Pop();
    }

//    {
//        char tmp;
//        std::cout << "ready? ";
//        std::cin >> tmp;
//        Lumen::UInteger iter = 100000000;
//        while (iter--) {
//            auto t = L->CheckUserdata(-1, "Test");
//            if (t == nullptr) L->Error("Test Failed");
//        }
//        std::cout << std::endl << "exit? ";
//        std::cin >> tmp;
//        std::cout << std::endl << "done" << std::endl;
//    }

    Lumen::Close(L);

    return 0;
}
