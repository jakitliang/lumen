/*!
 * @brief Tagged methods
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstring>

#define LUA_CORE

#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/state.h"
#include "lumen/string.h"
#include "lumen/tm.h"


const char *const Lumen::TM::TypeNames[] = {
        "nil", "boolean", "userdata", "number",
        "string", "table", "function", "userdata", "thread",
        "proto", "upval"
};


void Lumen::TM::Init(Lumen::State *L) {
    static const char *const luaT_eventname[] = {  /* ORDER TM */
            "__index", "__newindex",
            "__gc", "__mode", "__eq",
            "__add", "__sub", "__mul", "__div", "__mod",
            "__pow", "__unm", "__len", "__lt", "__le",
            "__concat", "__call"
    };
    int i;
    for (i = 0; i < Lumen::TM::NameN; i++) {
        LumenGlobalState(L)->MetatableName[i] = Lumen::String::New(L, luaT_eventname[i]);
        LumenStringFix(LumenGlobalState(L)->MetatableName[i]);  /* never collect these names */
    }
}

const Lumen::Object *Lumen::TM::GetByObject(Lumen::State *L, const Lumen::Object *o, Lumen::TM::Name event) {
    Lumen::Table *mt;
    switch (o->Type) {
        case Lumen::TypeTable:
            mt = o->GetTable()->Metatable;
            break;
        case Lumen::TypeUserdata:
            mt = o->GetUData()->Metatable;
            break;
        default:
            mt = LumenGlobalState(L)->Metatable[(o)->Type];
    }
    return (mt ? Lumen::Table::GetString(mt, LumenGlobalState(L)->MetatableName[event]) : Lumen::NilObject);
}

