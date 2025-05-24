/*!
 * @brief Lua virtual machine
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_VM_H
#define LUMEN_VM_H


#include "lumen/do.h"
#include "lumen/object.h"
#include "lumen/tm.h"

namespace Lumen::VM {
    int LessThan(Lumen::State *L, const Lumen::Value *l, const Lumen::Value *r);

    int EqualVal(Lumen::State *L, const Lumen::Value *t1, const Lumen::Value *t2);

    const Lumen::Value *ToNumber(const Lumen::Value *obj, Lumen::Value *n);

    int ToString(Lumen::State *L, Lumen::StkId obj);

    void GetTable(Lumen::State *L, const Lumen::Value *t, Lumen::Value *key,
                  Lumen::StkId val);

    void SetTable(Lumen::State *L, const Lumen::Value *t, Lumen::Value *key,
                  Lumen::StkId val);

    void Execute(Lumen::State *L, int nExecCalls);

    void Concat(Lumen::State *L, int total, int last);
}

#define LumenVMToString(L, o) ((LumenTypeOf(o) == LUA_TSTRING) || (Lumen::VM::ToString(L, o)))

#define LumenVMToNumber(o, n)    (LumenTypeOf(o) == LUA_TNUMBER || \
                         (((o) = Lumen::VM::ToNumber(o,n)) != nullptr))

#define LumenVMEqualObj(L, o1, o2)    (LumenTypeOf(o1) == LumenTypeOf(o2) && Lumen::VM::EqualVal(L, o1, o2))

#endif
