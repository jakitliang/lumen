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

    void FinishGetTable(Lumen::State *L, const Lumen::Object *t,
                        Lumen::Object *key, Lumen::Value val,
                        const Lumen::Object *cachedSlot);

    void SetTable(Lumen::State *L, const Lumen::Object *t, Lumen::Object *key,
                  Lumen::Value val);

    void FinishSetTable(Lumen::State *L, const Lumen::Object *t,
                        Lumen::Object *key, Lumen::Value val,
                        const Lumen::Object *cachedSlot);

    void Execute(Lumen::State *L, int nExecCalls);

    void Concat(Lumen::State *L, int total, int last);

    void ArithValue(Lumen::State *L, Lumen::Value ra, const Lumen::Object *rb,
                    const Lumen::Object *rc, Lumen::MetaMethod::Name op);

    void ObjectLength(Lumen::State *L, Lumen::Value ra, const Lumen::Object *rb);

    bool FastToString(Lumen::State *L, Lumen::Value obj);

    bool FastToNumber(const Lumen::Object *&obj, Lumen::Object *n);

    bool FastEqualObject(Lumen::State *L, const Lumen::Object *o1, const Lumen::Object *o2);

    void CallTMRes(Lumen::State *L, Lumen::Value res, const Lumen::Object *f,
                   const Lumen::Object *p1, const Lumen::Object *p2);

    void CallTM(Lumen::State *L, const Lumen::Object *f, const Lumen::Object *p1,
                const Lumen::Object *p2, const Lumen::Object *p3);
}

inline bool Lumen::VM::FastToString(Lumen::State *L, Lumen::Value obj) {
    return (obj->Type == Lumen::TypeString) || (Lumen::VM::ToString(L, obj));
}

inline bool Lumen::VM::FastToNumber(const Lumen::Object *&obj, Lumen::Object *n) {
    return obj->Type == Lumen::TypeNumber || ((obj = Lumen::VM::ToNumber(obj, n)) != nullptr);
}

inline bool Lumen::VM::FastEqualObject(Lumen::State *L, const Lumen::Object *o1, const Lumen::Object *o2) {
    return (o1->Type == o2->Type && Lumen::VM::EqualObject(L, o1, o2));
}

inline const Lumen::Object *Lumen::VM::ToNumber(const Lumen::Object *obj, Lumen::Object *n) {
    Lumen::Number num;
    if (obj->IsNumber()) return obj;
    if (obj->IsString() && Lumen::String2Decimal(obj->ToCString(), &num)) {
        n->SetNumber(num);
        return n;
    } else
        return nullptr;
}

#define LumenVMFastGetTable(L, t, k, slot, f) \
    (!t->IsTable()                            \
    ? (slot = nullptr, 0)  /* not a table; 'slot' is NULL and result is false */ \
    : (slot = f(t->GetTable(), k),  /* else, do raw access */                \
    !slot->IsNil()))  /* result not empty? */

#define LumenVMFastFetchTable(L, t, k, slot, f) \
    (slot = f(t->GetTable(), k),  /* else, do raw access */                \
    !slot->IsNil())  /* result not empty? */

#define LumenVMFastSetTable(L, t, slot, v) \
LumenDo(                                   \
    LumenSetObject2T(L, cast(Lumen::Object *, slot), v); \
    t->Flags = 0;                          \
    L->BarrierTable(t, v);    \
)

#endif
