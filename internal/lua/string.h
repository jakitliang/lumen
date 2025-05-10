/*!
 * @brief String table (keep all strings handled by Lua)
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_STRING_H
#define LUA_STRING_H

#include "lua/gc.h"
#include "lua/object.h"
#include "lua/state.h"

#define LuaStringSize(s)    (sizeof(Lua::String)+((s)->Length+1)*sizeof(char))

#define LuaUserdataSize(u)    (sizeof(Lua::Userdata)+(u)->Length)

#define LuaStringNewLiteral(L, s) \
    (Lua::String::New(L, "" s, (sizeof(s)/sizeof(char))-1))

#define LuaStringFix(s)    LuaGCLSetBit((s)->Marked, Lua::GC::MarkFixedBit)

#endif
