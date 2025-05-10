/*!
 * @brief Auxiliary functions from Debug Interface module
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_DEBUG_H
#define LUA_DEBUG_H


#include "lua/state.h"


#define LuaDebugPCRel(pc, p)         (cast(int, (pc) - (p)->Code) - 1)
#define LuaDebugGetLine(f, pc)       (((f)->LineInfo) ? (f)->LineInfo[pc] : 0)
#define LuaDebugResetHookCount(L)    (L->HookCount = L->BaseHookCount)

namespace Lua::Debug {
    void TypeError(Lua::State *L, const Lua::Value *o,
                               const char *opname);

    void ConcatError(Lua::State *L, Lua::StkId p1, Lua::StkId p2);

    void ArithError(Lua::State *L, const Lua::Value *p1,
                                const Lua::Value *p2);

    int OrderError(Lua::State *L, const Lua::Value *p1,
                               const Lua::Value *p2);

    void RunError(Lua::State *L, const char *fmt, ...);

    void ErrorMessage(Lua::State *L);

    int CheckCode(const Lua::Proto *pt);

    int CheckOpenOP(Lua::Instruction i);
}

#endif
