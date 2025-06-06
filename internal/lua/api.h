/*!
 * @brief Internal auxiliary functions from Lua API
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */

#ifndef LUMEN_API_H
#define LUMEN_API_H


#include "lumen/object.h"

namespace Lumen {
    void PushObject (Lumen::State *L, const Lumen::Value *o);
}

#endif
