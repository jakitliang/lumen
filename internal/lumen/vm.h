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

#include <cstdio>

#include "lumen/do.h"
#include "lumen/object.h"
#include "lumen/tm.h"

namespace Lumen::VM {
    int LessThan(Lumen::State *L, const Lumen::Object *l, const Lumen::Object *r);

    int LessEqual(Lumen::State *L, const Lumen::Object *l, const Lumen::Object *r);

    int EqualObject(Lumen::State *L, const Lumen::Object *t1, const Lumen::Object *t2);

    const Lumen::Object *ToNumber(const Lumen::Object *obj, Lumen::Object *n);

    int ToString(Lumen::State *L, Lumen::Value obj);

    void GetTable(Lumen::State *L, const Lumen::Object *t, Lumen::Object *key,
                  Lumen::Value val);

    void SetTable(Lumen::State *L, const Lumen::Object *t, Lumen::Object *key,
                  Lumen::Value val);

    void Execute(Lumen::State *L, int nExecCalls);

    void Concat(Lumen::State *L, int total, int last);

    void ArithValue(Lumen::State *L, Lumen::Value ra, const Lumen::Object *rb,
                    const Lumen::Object *rc, Lumen::TM::Name op);
}

#define LumenVMToString(L, o) ((LumenTypeOf(o) == Lumen::TypeString) || (Lumen::VM::ToString(L, o)))

#define LumenVMToNumber(o, n)    (LumenTypeOf(o) == Lumen::TypeNumber || \
                         (((o) = Lumen::VM::ToNumber(o,n)) != nullptr))

#define LumenVMEqualObj(L, o1, o2)    (LumenTypeOf(o1) == LumenTypeOf(o2) && Lumen::VM::EqualObject(L, o1, o2))

inline const Lumen::Object *Lumen::VM::ToNumber(const Lumen::Object *obj, Lumen::Object *n) {
    Lumen::Number num;
    if (LumenTypeIsNumber(obj)) return obj;
    if (LumenTypeIsString(obj) && Lumen::String2Decimal(LumenStringValue2CString(obj), &num)) {
        LumenSetNumberValue(n, num);
        return n;
    } else
        return nullptr;
}

inline int Lumen::VM::ToString(Lumen::State *L, Lumen::Value obj) {
    if (!LumenTypeIsNumber(obj))
        return 0;
    else {
        char s[LUAI_MAXNUMBER2STR];
        Lumen::Number n = LumenNumberValue(obj);
        lua_number2str(s, n);
        LumenSetStringValue2S(L, obj, Lumen::String::New(L, s));
        return 1;
    }
}

#endif
