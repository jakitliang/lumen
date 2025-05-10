/*!
 * @brief Lua tables (hash table)
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_TABLE_H
#define LUA_TABLE_H

#include "lua/object.h"

#define LuaTableGetNode(t,i)       (&(t)->Nodes[i])
#define LuaTableGetKey(n)          (&(n)->Key.KeyNext)
#define LuaTableGetValue(n)        (&(n)->Value)
#define LuaTableGetNext(n)         ((n)->Key.KeyNext.Next)
#define LuaTableKey2KeyValue(n)    (&(n)->Key.KeyValue)

#endif
