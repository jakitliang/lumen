/*!
 * @brief Internal auxiliary functions from Lua API
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */

#ifndef LUA_API_H
#define LUA_API_H


#include "lua/object.h"

namespace Lua {
    void PushObject (lua_State *L, const Lua::Value *o);
}

#endif
