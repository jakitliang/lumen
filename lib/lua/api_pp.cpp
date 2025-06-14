/*!
 * @brief Lumen C++ FrontEnd API for Lua
 * @author Jakit
 * @date 2025/6/7
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#include <cassert>
#include <cstdarg>
#include <cstring>

#define LUA_CORE

#include "lumen/object.h"
#include "lumen/state.h"
#include "lumen/api.h"

#include "lua.hpp"

#define LuaToLumen(L) reinterpret_cast<Lumen::State *>(L)
#define LumenToLua(L) reinterpret_cast<Lua::State *>(L)

void Lua::Close(Lua::State *(&state)) {
    auto L = LuaToLumen(state);
    if (L != nullptr) {
        Lumen::State::Close(L);
    }
    state = nullptr;
}

void Lua::XMove(Lua::State *fromL, Lua::State *toL, int n) {
    auto from = LuaToLumen(fromL);
    auto to = LuaToLumen(toL);
    int i;
    if (from == to) return;
    LumenLock(to);
    LumenApiCheckElementCount(from, n);
    LumenApiCheck(from, LumenGlobalState(from) == LumenGlobalState(to));
    LumenApiCheck(from, to->CallInfo->Top - to->Top >= n);
    from->Top -= n;
    for (i = 0; i < n; i++) {
        LumenSetObject2S(to, to->Top++, from->Top + i);
    }
    LumenUnlock(to);
}

void Lua::SetLevel(Lua::State *fromL, Lua::State *toL) {
    auto from = LuaToLumen(fromL);
    auto to = LuaToLumen(toL);
    to->NCCalls = from->NCCalls;
}
