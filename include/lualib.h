/*!
 * @brief Lua standard libraries
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 1994-2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef lualib_h
#define lualib_h

#include "lua.h"


/* Key to file-handle type */
#define LUA_FILEHANDLE        "FILE*"

LUALIB_API int (luaopen_base)(lua_State *L);

#define LUA_COLIBNAME    "coroutine"

LUALIB_API int (luaopen_coroutine)(lua_State *L);

#define LUA_TABLIBNAME    "table"

LUALIB_API int (luaopen_table)(lua_State *L);

#define LUA_IOLIBNAME    "io"

LUALIB_API int (luaopen_io)(lua_State *L);

#define LUA_OSLIBNAME    "os"

LUALIB_API int (luaopen_os)(lua_State *L);

#define LUA_STRLIBNAME    "string"

LUALIB_API int (luaopen_string)(lua_State *L);

#define LUA_UTF8LIBNAME    "utf8"

LUALIB_API int (luaopen_utf8)(lua_State *L);

#define LUA_MATHLIBNAME    "math"

LUALIB_API int (luaopen_math)(lua_State *L);

#define LUA_DBLIBNAME    "debug"

LUALIB_API int (luaopen_debug)(lua_State *L);

#define LUA_BITLIBNAME    "bit"

LUALIB_API int (luaopen_bit)(lua_State *L);

#define LUA_LOADLIBNAME    "package"

LUALIB_API int (luaopen_package)(lua_State *L);

/* open all previous libraries */
LUALIB_API void (luaL_openlibs)(lua_State *L);


#ifndef lua_assert
#define lua_assert(x)    ((void)0)
#endif


#endif
