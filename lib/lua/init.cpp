/*!
 * @brief Initialization of libraries for lua.c
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#define LUA_LIB

#include "lua.h"

#include "lualib.h"
#include "lauxlib.h"

#include "lua.hpp"

void Lua::State::OpenLibs() {
    static const Lua::Interface luaLibs[] = {
        {"",              Lua::Open<Lua::Base>},
        {LUA_COLIBNAME,   Lua::Open<Lua::Coroutine>},
        {LUA_LOADLIBNAME, Lua::Open<Lua::Package>},
        {LUA_TABLIBNAME,  Lua::Open<Lua::Table>},
        {LUA_IOLIBNAME,   Lua::Open<Lua::IO>},
        {LUA_OSLIBNAME,   Lua::Open<Lua::OS>},
        {LUA_STRLIBNAME,  Lua::Open<Lua::String>},
        {LUA_MATHLIBNAME, Lua::Open<Lua::Math>},
        {LUA_UTF8LIBNAME, Lua::Open<Lua::UTF8>},
        {LUA_BITLIBNAME,  Lua::Open<Lua::Bit>},
        {LUA_DBLIBNAME,   Lua::Open<Lua::Debug>},
        {nullptr,         nullptr}
    };

    auto lib = luaLibs;
    for (; lib->Invoke; lib++) {
        PushDelegate(lib->Invoke);
        PushString(lib->Name);
        Call(1, 0);
    }
}

LUALIB_API void luaL_openlibs(lua_State *L) {
    static const luaL_Reg luaLibs[] = {
        {"",              luaopen_base},
        {LUA_LOADLIBNAME, luaopen_package},
        {LUA_TABLIBNAME,  luaopen_table},
        {LUA_IOLIBNAME,   luaopen_io},
        {LUA_OSLIBNAME,   luaopen_os},
        {LUA_STRLIBNAME,  luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},
        {LUA_BITLIBNAME,  luaopen_bit},
        {LUA_DBLIBNAME,   luaopen_debug},
        {nullptr, nullptr}
    };
    
    const luaL_Reg *lib = luaLibs;
    for (; lib->func; lib++) {
        lua_pushcfunction(L, lib->func);
        lua_pushstring(L, lib->name);
        lua_call(L, 1, 0);
    }
}

