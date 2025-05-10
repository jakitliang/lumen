/*!
 * @brief Lua virtual machine
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_VM_H
#define LUA_VM_H


#include "lua/do.h"
#include "lua/object.h"
#include "lua/tm.h"

namespace Lua::VM {
    int LessThan(Lua::State *L, const Lua::Value *l, const Lua::Value *r);

    int EqualVal(Lua::State *L, const Lua::Value *t1, const Lua::Value *t2);

    const Lua::Value *ToNumber(const Lua::Value *obj, Lua::Value *n);

    int ToString(Lua::State *L, Lua::StkId obj);

    void GetTable(Lua::State *L, const Lua::Value *t, Lua::Value *key,
                  Lua::StkId val);

    void SetTable(Lua::State *L, const Lua::Value *t, Lua::Value *key,
                  Lua::StkId val);

    void Execute(Lua::State *L, int nExecCalls);

    void Concat(Lua::State *L, int total, int last);
}

#define LuaVMToString(L, o) ((LuaTypeOf(o) == LUA_TSTRING) || (Lua::VM::ToString(L, o)))

#define LuaVMToNumber(o, n)    (LuaTypeOf(o) == LUA_TNUMBER || \
                         (((o) = Lua::VM::ToNumber(o,n)) != nullptr))

#define LuaVMEqualObj(L, o1, o2)    (LuaTypeOf(o1) == LuaTypeOf(o2) && Lua::VM::EqualVal(L, o1, o2))

#endif
