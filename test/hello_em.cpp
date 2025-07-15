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

int main() {
    std::cout << LUMEN_RELEASE " -- " LUMEN_COPYRIGHT << std::endl;
    auto L = Lumen::Open();
    std::cout << "Lumen: load libs" << std::endl;
    L->OpenLibs();
    std::cout << "Lumen: ready" << std::endl;

    Lumen::Close(L);
    std::cout << "Lumen: stop" << std::endl;
}
