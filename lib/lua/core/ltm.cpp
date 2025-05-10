/*!
 * @brief Tagged methods
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstring>

#define ltm_c
#define LUA_CORE

#include "lua.h"

#include "lua/object.h"
#include "lua/state.h"
#include "lua/string.h"
#include "lua/table.h"
#include "lua/tm.h"



const char *const Lua::TM::TypeNames[] = {
  "nil", "boolean", "userdata", "number",
  "string", "table", "function", "userdata", "thread",
  "proto", "upval"
};


void Lua::TM::Init (Lua::State *L) {
  static const char *const luaT_eventname[] = {  /* ORDER TM */
    "__index", "__newindex",
    "__gc", "__mode", "__eq",
    "__add", "__sub", "__mul", "__div", "__mod",
    "__pow", "__unm", "__len", "__lt", "__le",
    "__concat", "__call"
  };
  int i;
  for (i=0; i<Lua::TM::NameN; i++) {
    LuaGlobal(L)->MetatableName[i] = Lua::String::New(L, luaT_eventname[i]);
    LuaStringFix(LuaGlobal(L)->MetatableName[i]);  /* never collect these names */
  }
}


/*
** function to be used with macro "LuaTMGetFast": optimized for absence of
** tag methods
*/
const Lua::Value *Lua::TM::Get (Lua::Table *events, Lua::TM::Name event, Lua::String *ename) {
  const Lua::Value *tm = Lua::Table::GetString(events, ename);
  lua_assert(event <= Lua::TM::NameEQ);
  if (LuaTypeIsNil(tm)) {  /* no tag method? */
    events->Flags |= cast_byte(1u << event);  /* cache this fact */
    return NULL;
  }
  else return tm;
}


const Lua::Value *Lua::TM::GetByObject (Lua::State *L, const Lua::Value *o, Lua::TM::Name event) {
  Lua::Table *mt;
  switch (LuaTypeOf(o)) {
    case LUA_TTABLE:
      mt = LuaTableValue(o)->Metatable;
      break;
    case LUA_TUSERDATA:
      mt = LuaUDataValue(o)->Metatable;
      break;
    default:
      mt = LuaGlobal(L)->Metatable[LuaTypeOf(o)];
  }
  return (mt ? Lua::Table::GetString(mt, LuaGlobal(L)->MetatableName[event]) : Lua::NilObject);
}

