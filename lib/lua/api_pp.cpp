/*!
 * @brief Lumen C++ FrontEnd API for Lua
 * @author Jakit
 * @date 2025/6/7
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#include <cassert>
#include <cstdarg>
#include <cstring>

#define LUA_CORE

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/gc.h"
#include "lumen/object.h"
#include "lumen/state.h"
#include "lumen/tm.h"
#include "lumen/undump.h"
#include "lumen/vm.h"
#include "lumen/protected_call.h"

#include "lua.hpp"

#define apiCheckElementCount(L, n)    LumenApiCheck(L, (n) <= (L->Top - L->Base))

#define apiCheckValidIndex(L, i)    LumenApiCheck(L, (i) != Lumen::NilObject)

#define apiIncrTop(L) \
LumenDo(                \
    LumenApiCheck(L, L->Top < L->CallInfo->Top); \
    L->Top++;         \
)

/* limit for table tag-method chains (to avoid loops) */
#define LumenMaxTagLoop    100

#define LuaToLumen(L) reinterpret_cast<Lumen::State *>(L)
#define LumenToLua(L) reinterpret_cast<Lua::State *>(L)

static Lumen::Value *index2addr(Lumen::State *L, int idx) {
    if (idx > 0) {
        Lumen::Value *o = L->Base + (idx - 1);
        LumenApiCheck(L, idx <= L->CallInfo->Top - L->Base);
        if (o >= L->Top) return cast(Lumen::Value *, Lumen::NilObject);
        else return o;
    } else if (idx > Lua::RegistryIndex) {
        LumenApiCheck(L, idx != 0 && -idx <= L->Top - L->Base);
        return L->Top + idx;
    } else
        switch (idx) {  /* pseudo-indices */
            case Lua::RegistryIndex:
                return LumenRegistry(L);
            case Lua::EnvIndex: {
                Lumen::Closure *func = LumenCurFunc(L);
                LumenSetTableValue(L, &L->Env, func->AsC.Env);
                return &L->Env;
            }
            case Lua::GlobalIndex:
                return LumenGlobalTable(L);
            default: {
                Lumen::Closure *func = LumenCurFunc(L);
                idx = Lua::GlobalIndex - idx;
                return (idx <= func->AsC.NUpValues)
                       ? &func->AsC.UpValues[idx - 1]
                       : cast(Lumen::Value *, Lumen::NilObject);
            }
        }
}

static Lumen::Table *getCurEnv(Lumen::State *L) {
    if (L->CallInfo == L->BaseCI)  /* no enclosing function? */
        return LumenTableValue(LumenGlobalTable(L));  /* use global table as environment */
    else {
        Lumen::Closure *func = LumenCurFunc(L);
        return func->AsC.Env;
    }
}

void lua_PushObject(Lumen::State *L, const Lumen::Value *o);

// MARK: state manipulation

Lua::State *Lua::State::New(Lua::Allocator allocator, void *userdata) {
    auto L = Lumen::State::New(allocator, userdata);
    return L == nullptr ? nullptr : LumenToLua(L);
}

Lua::State *Lua::State::NewThread() {
    auto L = LuaToLumen(this);
    Lumen::State *L1;
    LumenLock(L);
    LumenGCCheckGC(L);
    L1 = Lumen::State::NewThread(L);
    LumenSetThreadValue(L, L->Top, L1);
    apiIncrTop(L);
    LumenUnlock(L);
    luai_userstatethread(L, L1);
    return L1 == nullptr ? nullptr : LumenToLua(L1);
}

Lua::Delegate Lua::State::AtPanic(Lua::Delegate pInvoke) {
    auto L = LuaToLumen(this);
    Lua::Delegate old;
    LumenLock(L);
    old = reinterpret_cast<Lua::Delegate>(LumenGlobal(L)->Panic);
    LumenGlobal(L)->Panic = reinterpret_cast<Lumen::Delegate>(pInvoke);
    LumenUnlock(L);
    return old;
}

// MARK: basic stack manipulation

int Lua::State::GetTop() {
    auto L = LuaToLumen(this);
    return cast_int(L->Top - L->Base);
}

void Lua::State::SetTop(int idx) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    if (idx >= 0) {
        LumenApiCheck(L, idx <= L->StackLast - L->Base);
        while (L->Top < L->Base + idx)
            LumenSetNilValue(L->Top++);
        L->Top = L->Base + idx;
    } else {
        LumenApiCheck(L, -(idx + 1) <= (L->Top - L->Base));
        L->Top += idx + 1;  /* `subtract` index (index is negative) */
    }
    LumenUnlock(L);
}

void Lua::State::PushValue(int idx) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetObject2S(L, L->Top, index2addr(L, idx));
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::Remove(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId p;
    LumenLock(L);
    p = index2addr(L, idx);
    apiCheckValidIndex(L, p);
    while (++p < L->Top) LumenSetObjectS2S(L, p - 1, p);
    L->Top--;
    LumenUnlock(L);
}

void Lua::State::Insert(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId p;
    Lumen::StkId q;
    LumenLock(L);
    p = index2addr(L, idx);
    apiCheckValidIndex(L, p);
    for (q = L->Top; q > p; q--) LumenSetObjectS2S(L, q, q - 1);
    LumenSetObjectS2S(L, p, L->Top);
    LumenUnlock(L);
}

void Lua::State::Replace(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o;
    LumenLock(L);
    /* explicit test for incompatible code */
    if (idx == Lua::EnvIndex && L->CallInfo == L->BaseCI)
        Lumen::Debug::RunError(L, "no calling environment");
    apiCheckElementCount(L, 1);
    o = index2addr(L, idx);
    apiCheckValidIndex(L, o);
    if (idx == Lua::EnvIndex) {
        Lumen::Closure *func = LumenCurFunc(L);
        LumenApiCheck(L, LumenTypeIsTable(L->Top - 1));
        func->AsC.Env = LumenTableValue(L->Top - 1);
        LumenGCBarrier(L, func, L->Top - 1);
    } else {
        LumenSetObject(L, o, L->Top - 1);
        if (idx < Lua::GlobalIndex)  /* function upvalue? */
            LumenGCBarrier(L, LumenCurFunc(L), L->Top - 1);
    }
    L->Top--;
    LumenUnlock(L);
}

bool Lua::State::CheckStack(int size) {
    auto L = LuaToLumen(this);
    int res = 1;
    LumenLock(L);
    if (size > LUAI_MAXCSTACK || (L->Top - L->Base + size) > LUAI_MAXCSTACK)
        res = 0;  /* stack overflow */
    else if (size > 0) {
        LumenDoCheckStack(L, size);
        if (L->CallInfo->Top < L->Top + size)
            L->CallInfo->Top = L->Top + size;
    }
    LumenUnlock(L);
    return res;
}

// MARK: access functions (stack -> C)

bool Lua::State::IsNumber(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value n; // NOLINT
    const Lumen::Value *o = index2addr(L, idx);
    return LumenVMToNumber(o, &n);
}

bool Lua::State::IsString(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o = index2addr(L, idx);
    return LumenTypeIsString(o);
}

bool Lua::State::IsDelegate(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o = index2addr(L, idx);
    return LumenIsCFunction(o);
}

bool Lua::State::IsUserdata(int idx) {
    auto L = LuaToLumen(this);
    const Lumen::Value *o = index2addr(L, idx);
    return (LumenTypeIsUData(o) || LumenTypeIsLUData(o));
}

Lua::Type Lua::State::Type(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o = index2addr(L, idx);
    return (o == Lumen::NilObject) ? Lua::TypeNone : LumenTypeOf(o);
}

const char *Lua::State::TypeName(int t) const { // NOLINT
    return (t == Lua::TypeNone) ? "no value" : Lumen::TM::TypeNames[t];
}

bool Lua::State::Equal(int idx1, int idx2) {
    auto L = LuaToLumen(this);
    Lumen::StkId o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = index2addr(L, idx1);
    o2 = index2addr(L, idx2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0 : LumenVMEqualObj(L, o1, o2);
    LumenUnlock(L);
    return i;
}

bool Lua::State::RawEqual(int idx1, int idx2) {
    auto L = LuaToLumen(this);
    Lumen::StkId o1 = index2addr(L, idx1);
    Lumen::StkId o2 = index2addr(L, idx2);
    return (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? false
                                                              : Lumen::RawEqualObject(o1, o2);
}

bool Lua::State::LessThan(int idx1, int idx2) {
    auto L = LuaToLumen(this);
    Lumen::StkId o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = index2addr(L, idx1);
    o2 = index2addr(L, idx2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0
                                                           : Lumen::VM::LessThan(L, o1, o2);
    LumenUnlock(L);
    return i;
}

Lua::Number Lua::State::ToNumber(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value n; // NOLINT
    const Lumen::Value *o = index2addr(L, idx);
    if (LumenVMToNumber(o, &n))
        return LumenNumberValue(o);
    else
        return 0;
}

Lua::Integer Lua::State::ToInteger(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value n; // NOLINT
    const Lumen::Value *o = index2addr(L, idx);
    if (LumenVMToNumber(o, &n)) {
        Lumen::Integer res;
        Lumen::Number num = LumenNumberValue(o);
        lua_number2integer(res, num);
        return res;
    } else
        return 0;
}

bool Lua::State::ToBoolean(int idx) {
    auto L = LuaToLumen(this);
    const Lumen::Value *o = index2addr(L, idx);
    return !LumenIsFalse(o);
}

const char *Lua::State::ToString(int idx, Lua::UInteger *len) {
    auto L = LuaToLumen(this);
    Lumen::StkId o = index2addr(L, idx);
    if (!LumenTypeIsString(o)) {
        LumenLock(L);  /* `Lumen::VM::ToString' may create a new string */
        if (!Lumen::VM::ToString(L, o)) {  /* conversion failed? */
            if (len != nullptr) *len = 0;
            LumenUnlock(L);
            return nullptr;
        }
        LumenGCCheckGC(L);
        o = index2addr(L, idx);  /* previous call may reallocate the stack */
        LumenUnlock(L);
    }
    if (len != nullptr) *len = LumenStringValue(o)->Length;
    return LumenStringValue2CString(o);
}

bool Lua::State::InstanceOf(int idxChild, int idxSuper) {
    auto L = LuaToLumen(this);
    const Lumen::Value *oChild = index2addr(L, idxChild);
    const Lumen::Value *oSuper = index2addr(L, idxSuper);
    if (oChild == Lumen::NilObject || oSuper == Lumen::NilObject) return false;
    int loop;
    for (loop = 0; loop < LumenMaxTagLoop; loop++) {
        if (LumenTypeIsTable(oChild)) {  /* `t` is a table? */
            Lumen::Table *h = LumenTableValue(oChild);
            if (Lumen::RawEqualObject(oChild, oSuper)) {
                return true;
            }
            if ((oChild = LumenTMGetFast(L, h->Metatable, Lumen::TM::NameIndex)) == nullptr) { /* no TM? */
                return false;
            }
            /* else will try the tag method */
        } else if (LumenTypeIsNil(oChild = Lumen::TM::GetByObject(L, oChild, Lumen::TM::NameIndex))) {
            Lumen::Debug::TypeError(L, oChild, "index");
        }
    }
    Lumen::Debug::RunError(L, "loop in gettable");
    return false;
}

Lua::UInteger Lua::State::ObjectLength(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o = index2addr(L, idx);
    switch (LumenTypeOf(o)) {
        case Lua::TypeString:
            return LumenStringValue(o)->Length;
        case Lua::TypeUserdata:
            return LumenUDataValue(o)->Length;
        case Lua::TypeTable:
            return Lumen::Table::GetN(LumenTableValue(o));
        case Lua::TypeNumber: {
            Lua::UInteger l;
            LumenLock(L);  /* `Lumen::VM::ToString' may create a new string */
            l = (Lumen::VM::ToString(L, o) ? LumenStringValue(o)->Length : 0);
            LumenUnlock(L);
            return l;
        }
        default:
            return 0;
    }
}

Lua::Delegate Lua::State::ToDelegate(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o = index2addr(L, idx);
    return (!LumenIsCFunction(o)) ? nullptr : reinterpret_cast<Lua::Delegate>(LumenClosureValue(o)->AsC.Func);
}

Lua::Function Lua::State::ToFunction(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o = index2addr(L, idx);
    return (!LumenIsCFunction(o)) ? nullptr : reinterpret_cast<Lua::Function>(LumenClosureValue(o)->AsC.Func);
}

void *Lua::State::ToUserdata(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o = index2addr(L, idx);
    switch (LumenTypeOf(o)) {
        case Lua::TypeUserdata:
            return (LumenUDataValue(o) + 1);
        case Lua::TypeLightUserdata:
            return LumenLUDataValue(o);
        default:
            return nullptr;
    }
}

Lua::State *Lua::State::ToThread(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o = index2addr(L, idx);
    return (!LumenTypeIsThread(o)) ? nullptr : LumenToLua(LumenThreadValue(o));
}

const void *Lua::State::ToPointer(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o = index2addr(L, idx);
    switch (LumenTypeOf(o)) {
        case Lua::TypeTable:
            return LumenTableValue(o);
        case Lua::TypeFunction:
            return LumenClosureValue(o);
        case Lua::TypeThread:
            return LumenThreadValue(o);
        case Lua::TypeUserdata:
        case Lua::TypeLightUserdata:
            return ToUserdata(idx);
        default:
            return nullptr;
    }
}

// MARK: push functions (C -> stack)

void Lua::State::PushNil() {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetNilValue(L->Top);
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushNumber(Lua::Number n) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetNumberValue(L->Top, n);
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushInteger(Lua::Integer n) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetNumberValue(L->Top, cast_num(n));
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushString(const char *s, Lua::UInteger length) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenSetStringValue2S(L, L->Top,
                          Lumen::String::New(L, s, length));
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushString(const char *s) {
    if (s == nullptr)
        PushNil();
    else
        PushString(s, Lumen::String::LengthOf(s));
}

const char *Lua::State::PushVFString(const char *fmt, va_list argP) {
    auto L = LuaToLumen(this);
    const char *ret;
    LumenLock(L);
    LumenGCCheckGC(L);
    ret = Lumen::PushVFString(L, fmt, argP);
    LumenUnlock(L);
    return ret;
}

const char *Lua::State::PushFString(const char *fmt, ...) {
    auto L = LuaToLumen(this);
    const char *ret;
    va_list argP;
    LumenLock(L);
    LumenGCCheckGC(L);
        va_start(argP, fmt);
    ret = Lumen::PushVFString(L, fmt, argP);
        va_end(argP);
    LumenUnlock(L);
    return ret;
}

void Lua::State::PushDelegate(Lua::Delegate invoke, int n) {
    auto L = LuaToLumen(this);
    Lumen::Closure *cl;
    LumenLock(L);
    LumenGCCheckGC(L);
    apiCheckElementCount(L, n);
    cl = Lumen::CClosure::New(L, n, getCurEnv(L));
    cl->AsC.Func = reinterpret_cast<Lumen::Delegate>(invoke);
    L->Top -= n;
    while (n--)
        LumenSetObject2N(L, &cl->AsC.UpValues[n], L->Top + n);
    LumenSetClosureValue(L, L->Top, cl);
    LumenAssert(LumenGCIsWhite(LumenObject2GCObject(cl)));
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushFunction(Lua::Function invoke, int n) {
    auto L = LuaToLumen(this);
    Lumen::Closure *cl;
    LumenLock(L);
    LumenGCCheckGC(L);
    apiCheckElementCount(L, n);
    cl = Lumen::CClosure::New(L, n, getCurEnv(L));
    cl->AsC.Func = reinterpret_cast<Lumen::Delegate>(invoke);
    L->Top -= n;
    while (n--)
        LumenSetObject2N(L, &cl->AsC.UpValues[n], L->Top + n);
    LumenSetClosureValue(L, L->Top, cl);
    LumenAssert(LumenGCIsWhite(LumenObject2GCObject(cl)));
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushBoolean(int b) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetBoolValue(L->Top, (b != 0));  /* ensure that true is 1 */
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushLightUserdata(void *p) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetLUDataValue(L->Top, p);
    apiIncrTop(L);
    LumenUnlock(L);
}

int Lua::State::PushThread() {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetThreadValue(L, L->Top, L);
    apiIncrTop(L);
    LumenUnlock(L);
    return (LumenGlobal(L)->MainThread == L);
}

// MARK: get functions (LuaToState(this)ua -> stack)

void Lua::State::GetTable(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId t;
    LumenLock(L);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    Lumen::VM::GetTable(L, t, L->Top - 1, L->Top - 1);
    LumenUnlock(L);
}

void Lua::State::GetField(int idx, const char *k) {
    auto L = LuaToLumen(this);
    Lumen::StkId t;
    Lumen::Value key; // NOLINT
    LumenLock(L);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    LumenSetStringValue(L, &key, Lumen::String::New(L, k));
    Lumen::VM::GetTable(L, t, &key, L->Top);
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::RawGet(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId t;
    LumenLock(L);
    t = index2addr(L, idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    LumenSetObject2S(L, L->Top - 1, Lumen::Table::Get(LumenTableValue(t), L->Top - 1));
    LumenUnlock(L);
}

void Lua::State::RawGetAt(int idx, int n) {
    auto L = LuaToLumen(this);
    Lumen::StkId o;
    LumenLock(L);
    o = index2addr(L, idx);
    LumenApiCheck(L, LumenTypeIsTable(o));
    LumenSetObject2S(L, L->Top, Lumen::Table::GetNum(LumenTableValue(o), n));
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::CreateTable(int nArray, int nRec) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenSetTableValue(L, L->Top, Lumen::Table::New(L, nArray, nRec));
    apiIncrTop(L);
    LumenUnlock(L);
}

void *Lua::State::NewUserdata(Lua::UInteger size) {
    auto L = LuaToLumen(this);
    Lumen::Userdata *u;
    LumenLock(L);
    LumenGCCheckGC(L);
    u = Lumen::Userdata::New(L, size, getCurEnv(L));
    LumenSetUDataValue(L, L->Top, u);
    apiIncrTop(L);
    LumenUnlock(L);
    return u + 1;
}

bool Lua::State::GetMetatable(int objIndex) {
    auto L = LuaToLumen(this);
    const Lumen::Value *obj;
    Lumen::Table *mt = nullptr;
    int res;
    LumenLock(L);
    obj = index2addr(L, objIndex);
    switch (LumenTypeOf(obj)) {
        case Lua::TypeTable:
            mt = LumenTableValue(obj)->Metatable;
            break;
        case Lua::TypeUserdata:
            mt = LumenUDataValue(obj)->Metatable;
            break;
        default:
            mt = LumenGlobal(L)->Metatable[LumenTypeOf(obj)];
            break;
    }
    if (mt == nullptr)
        res = 0;
    else {
        LumenSetTableValue(L, L->Top, mt);
        apiIncrTop(L);
        res = 1;
    }
    LumenUnlock(L);
    return res;
}

void Lua::State::GetFEnv(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o;
    LumenLock(L);
    o = index2addr(L, idx);
    apiCheckValidIndex(L, o);
    switch (LumenTypeOf(o)) {
        case Lua::TypeFunction:
            LumenSetTableValue(L, L->Top, LumenClosureValue(o)->AsC.Env);
            break;
        case Lua::TypeUserdata:
            LumenSetTableValue(L, L->Top, LumenUDataValue(o)->Env);
            break;
        case Lua::TypeThread:
            LumenSetObject2S(L, L->Top, LumenGlobalTable(LumenThreadValue(o)));
            break;
        default:
            LumenSetNilValue(L->Top);
            break;
    }
    apiIncrTop(L);
    LumenUnlock(L);
}

// MARK: set functions (stack -> Lua)

void Lua::State::SetTable(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId t;
    LumenLock(L);
    apiCheckElementCount(L, 2);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    Lumen::VM::SetTable(L, t, L->Top - 2, L->Top - 1);
    L->Top -= 2;  /* pop index and value */
    LumenUnlock(L);
}

void Lua::State::SetField(int idx, const char *k) {
    auto L = LuaToLumen(this);
    Lumen::StkId t;
    Lumen::Value key; // NOLINT
    LumenLock(L);
    apiCheckElementCount(L, 1);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    LumenSetStringValue(L, &key, Lumen::String::New(L, k));
    Lumen::VM::SetTable(L, t, &key, L->Top - 1);
    L->Top--;  /* pop value */
    LumenUnlock(L);
}

void Lua::State::RawSet(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId t;
    LumenLock(L);
    apiCheckElementCount(L, 2);
    t = index2addr(L, idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    LumenSetObject2T(L, Lumen::Table::Set(L, LumenTableValue(t), L->Top - 2), L->Top - 1);
    LumenGCBarrierTable(L, LumenTableValue(t), L->Top - 1);
    L->Top -= 2;
    LumenUnlock(L);
}

void Lua::State::RawSetAt(int idx, int n) {
    auto L = LuaToLumen(this);
    Lumen::StkId o;
    LumenLock(L);
    apiCheckElementCount(L, 1);
    o = index2addr(L, idx);
    LumenApiCheck(L, LumenTypeIsTable(o));
    LumenSetObject2T(L, Lumen::Table::SetNum(L, LumenTableValue(o), n), L->Top - 1);
    LumenGCBarrierTable(L, LumenTableValue(o), L->Top - 1);
    L->Top--;
    LumenUnlock(L);
}

bool Lua::State::SetMetatable(int objIndex) {
    auto L = LuaToLumen(this);
    Lumen::Value *obj;
    Lumen::Table *mt;
    LumenLock(L);
    apiCheckElementCount(L, 1);
    obj = index2addr(L, objIndex);
    apiCheckValidIndex(L, obj);
    if (LumenTypeIsNil(L->Top - 1))
        mt = nullptr;
    else {
        LumenApiCheck(L, LumenTypeIsTable(L->Top - 1));
        mt = LumenTableValue(L->Top - 1);
    }
    switch (LumenTypeOf(obj)) {
        case Lua::TypeTable: {
            LumenTableValue(obj)->Metatable = mt;
            if (mt)
                LumenGCObjectBarrierTable(L, LumenTableValue(obj), mt);
            break;
        }
        case Lua::TypeUserdata: {
            LumenUDataValue(obj)->Metatable = mt;
            if (mt)
                LumenGCObjectBarrier(L, LumenUDataValue(obj), mt);
            break;
        }
        default: {
            LumenGlobal(L)->Metatable[LumenTypeOf(obj)] = mt;
            break;
        }
    }
    L->Top--;
    LumenUnlock(L);
    return true;
}

bool Lua::State::SetFEnv(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId o;
    int res = 1;
    LumenLock(L);
    apiCheckElementCount(L, 1);
    o = index2addr(L, idx);
    apiCheckValidIndex(L, o);
    LumenApiCheck(L, LumenTypeIsTable(L->Top - 1));
    switch (LumenTypeOf(o)) {
        case Lua::TypeFunction:
            LumenClosureValue(o)->AsC.Env = LumenTableValue(L->Top - 1);
            break;
        case Lua::TypeUserdata:
            LumenUDataValue(o)->Env = LumenTableValue(L->Top - 1);
            break;
        case Lua::TypeThread:
            LumenSetTableValue(L, LumenGlobalTable(LumenThreadValue(o)), LumenTableValue(L->Top - 1));
            break;
        default:
            res = 0;
            break;
    }
    if (res) LumenGCObjectBarrier(L, LumenGCValue(o), LumenTableValue(L->Top - 1));
    L->Top--;
    LumenUnlock(L);
    return res;
}

// MARK: `load' and `call' functions (load and run Lua code)

#define adjustResults(L, nRes) \
LumenDo(                         \
    if (nRes == Lua::RetMul && L->Top >= L->CallInfo->Top) \
        L->CallInfo->Top = L->Top;   \
)

#define checkResults(L, na, nr) \
     LumenApiCheck(L, (nr) == Lua::RetMul || (L->CallInfo->Top - L->Top >= (nr) - (na)))


void Lua::State::Call(int nargs, int nResults) {
    auto L = LuaToLumen(this);
    Lumen::StkId func;
    LumenLock(L);
    apiCheckElementCount(L, nargs + 1);
    checkResults(L, nargs, nResults);
    func = L->Top - (nargs + 1);
    Lumen::Do::Call(L, func, nResults);
    adjustResults(L, nResults);
    LumenUnlock(L);
}

Lua::Ret Lua::State::TryCall(int nargs, int nResults, int errFunc) {
    auto L = LuaToLumen(this);
    Lumen::ProtectedCall c; // NOLINT
    int status;
    ptrdiff_t func;
    LumenLock(L);
    apiCheckElementCount(L, nargs + 1);
    checkResults(L, nargs, nResults);
    if (errFunc == 0)
        func = 0;
    else {
        Lumen::StkId o = index2addr(L, errFunc);
        apiCheckValidIndex(L, o);
        func = LumenSaveStack(L, o);
    }
    c.Func = L->Top - (nargs + 1);  /* function to be called */
    c.NResults = nResults;
    status = Lumen::Do::PCall(L,
                              &Lumen::ProtectedCall::Call, &c,
                              LumenSaveStack(L, c.Func), func);
    adjustResults(L, nResults);
    LumenUnlock(L);
    return status;
}

Lua::Ret Lua::State::TryCall(Lua::Delegate invoke, void *userdata) {
    auto L = LuaToLumen(this);
    Lumen::ProtectedCCall c; // NOLINT
    int status;
    LumenLock(L);
    c.Func = reinterpret_cast<Lumen::Delegate>(invoke);
    c.UData = userdata;
    status = Lumen::Do::PCall(L,
                              &Lumen::ProtectedCCall::Call, &c,
                              LumenSaveStack(L, L->Top), 0);
    LumenUnlock(L);
    return status;
}

Lua::Ret Lua::State::TryCall(Lua::Function invoke, void *userdata) {
    auto L = LuaToLumen(this);
    Lumen::ProtectedCCall c; // NOLINT
    int status;
    LumenLock(L);
    c.Func = invoke;
    c.UData = userdata;
    status = Lumen::Do::PCall(L,
                              &Lumen::ProtectedCCall::Call, &c,
                              LumenSaveStack(L, L->Top), 0);
    LumenUnlock(L);
    return status;
}

Lua::Ret Lua::State::Load(Lua::Reader reader, void *data, const char *chunkName) {
    auto L = LuaToLumen(this);
    Lumen::ZIO z; // NOLINT
    int status;
    LumenLock(L);
    if (!chunkName) chunkName = "?";
    Lumen::ZIO::Init(L, &z, reinterpret_cast<Lumen::Reader>(reader), data);
    status = Lumen::Do::ProtectedParser(L, &z, chunkName);
    LumenUnlock(L);
    return status;
}

Lua::Ret Lua::State::Dump(Lua::Writer writer, void *data) {
    auto L = LuaToLumen(this);
    int status;
    Lumen::Value *o;
    LumenLock(L);
    apiCheckElementCount(L, 1);
    o = L->Top - 1;
    if (LumenIsLFunction(o))
        status = Lumen::Dumper::Dump(L,
                                     LumenClosureValue(o)->AsLua.Func,
                                     reinterpret_cast<Lumen::Writer>(writer), data, 0);
    else
        status = 1;
    LumenUnlock(L);
    return status;
}

// MARK: coroutine functions

Lua::Ret Lua::State::Yield(int nResults) {
    auto L = LuaToLumen(this);
    luai_userstateyield(L, nResults);
    LumenLock(L);
    if (L->NCCalls > L->BaseCCalls)
        Lumen::Debug::RunError(L, "attempt to yield across metaMethod/C-call boundary");
    L->Base = L->Top - nResults;  /* protect stack slots below */
    L->Status = Lua::RetYield;
    LumenUnlock(L);
    return -1;
}

Lua::Ret Lua::State::Resume(int nArgs) {
    auto L = LuaToLumen(this);
    int status;
    LumenLock(L);
    if (L->Status != Lua::RetYield && (L->Status != 0 || L->CallInfo != L->BaseCI))
        return Lumen::Do::ResumeError(L, "cannot resume non-suspended coroutine");
    if (L->NCCalls >= LUAI_MAXCCALLS)
        return Lumen::Do::ResumeError(L, "C stack overflow");
    luai_userstateresume(L, nArgs);
    LumenAssert(L->ErrFunc == 0);
    L->BaseCCalls = ++L->NCCalls;
    status = Lumen::Do::RawRunProtected(L, Lumen::Do::Resume, L->Top - nArgs);
    if (status != 0) {  /* error? */
        L->Status = cast_byte(status);  /* mark thread as `dead' */
        Lumen::Do::SetErrorObject(L, status, L->Top);
        L->CallInfo->Top = L->Top;
    } else {
        LumenAssert(L->NCCalls == L->BaseCCalls);
        status = L->Status;
    }
    --L->NCCalls;
    LumenUnlock(L);
    return status;
}

Lua::Ret Lua::State::Status() {
    return LuaToLumen(this)->Status;
}

// MARK: garbage-collection function and options

int Lua::State::GC(Lua::GCAction what, int data) {
    auto L = LuaToLumen(this);
    int res = 0;
    Lumen::GlobalState *g;
    LumenLock(L);
    g = LumenGlobal(L);
    switch (what) {
        case Lua::GCStop: {
            g->GCThreshold = Lumen::MaxUMemory;
            break;
        }
        case Lua::GCRestart: {
            g->GCThreshold = g->TotalBytes;
            break;
        }
        case Lua::GCCollect: {
            Lumen::GC::FullGC(L);
            break;
        }
        case Lua::GCCount: {
            /* GC values are expressed in KBytes: #bytes/2^10 */
            res = cast_int(g->TotalBytes >> 10);
            break;
        }
        case Lua::GCCountB: {
            res = cast_int(g->TotalBytes & 0x3ff);
            break;
        }
        case Lua::GCStep: {
            Lumen::MemorySize a = (cast(Lumen::MemorySize, data) << 10);
            if (a <= g->TotalBytes)
                g->GCThreshold = g->TotalBytes - a;
            else
                g->GCThreshold = 0;
            while (g->GCThreshold <= g->TotalBytes) {
                Lumen::GC::Step(L);
                if (g->GCState == Lumen::GC::StatePause) {  /* end of cycle? */
                    res = 1;  /* signal it */
                    break;
                }
            }
            break;
        }
        case Lua::GCSetPause: {
            res = g->GCPause;
            g->GCPause = data;
            break;
        }
        case Lua::GCSetStepMul: {
            res = g->GCStepMul;
            g->GCStepMul = data;
            break;
        }
        default:
            res = -1;  /* invalid option */
    }
    LumenUnlock(L);
    return res;
}

// MARK: miscellaneous functions

Lua::Ret Lua::State::Error() {
    auto L = LuaToLumen(this);
    LumenLock(L);
    apiCheckElementCount(L, 1);
    Lumen::Debug::ErrorMessage(L);
    LumenUnlock(L);
    return 0;  /* to avoid warnings */
}

bool Lua::State::Next(int idx) {
    auto L = LuaToLumen(this);
    Lumen::StkId t;
    int more;
    LumenLock(L);
    t = index2addr(L, idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    more = Lumen::Table::Next(L, LumenTableValue(t), L->Top - 1);
    if (more) {
        apiIncrTop(L);
    } else  /* no more elements */
        L->Top -= 1;  /* remove key */
    LumenUnlock(L);
    return more;
}

void Lua::State::Concat(int n) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    apiCheckElementCount(L, n);
    if (n >= 2) {
        LumenGCCheckGC(L);
        Lumen::VM::Concat(L, n, cast_int(L->Top - L->Base) - 1);
        L->Top -= (n - 1);
    } else if (n == 0) {  /* push empty string */
        LumenSetStringValue2S(L, L->Top, Lumen::String::New(L, "", 0));
        apiIncrTop(L);
    }
    /* else n == 1; nothing to do */
    LumenUnlock(L);
}

Lua::Allocator Lua::State::GetAllocator(void **ud) {
    auto L = LuaToLumen(this);
    Lumen::Allocator f;
    LumenLock(L);
    if (ud) *ud = LumenGlobal(L)->ReAllocatorUData;
    f = LumenGlobal(L)->ReAllocator;
    LumenUnlock(L);
    return f;
}

void Lua::State::SetAllocator(Lua::Allocator f, void *ud) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenGlobal(L)->ReAllocatorUData = ud;
    LumenGlobal(L)->ReAllocator = f;
    LumenUnlock(L);
}

// MARK: Debug APIs

bool Lua::State::GetStack(int level, Lua::DebugInfo *ar) {
    auto L = LuaToLumen(this);
    int status;
    Lumen::CallInfo *ci;
    LumenLock(L);
    for (ci = L->CallInfo; level > 0 && ci > L->BaseCI; ci--) {
        level--;
        if (LumenCIFuncIsLua(ci))  /* Lua function? */
            level -= ci->NTailCalls;  /* skip lost tail calls */
    }
    if (level == 0 && ci > L->BaseCI) {  /* level found? */
        status = 1;
        reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI = cast_int(ci - L->BaseCI);
    } else if (level < 0) {  /* level is of a lost tail call? */
        status = 1;
        reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI = 0;
    } else status = 0;  /* no such level */
    LumenUnlock(L);
    return status;
}

bool Lua::State::GetInfo(const char *what, Lua::DebugInfo *ar) {
    auto L = LuaToLumen(this);
    int status;
    Lumen::Closure *f = nullptr;
    Lumen::CallInfo *ci = nullptr;
    LumenLock(L);
    if (*what == '>') {
        Lumen::StkId func = L->Top - 1;
        LumenApiCheck(L, LumenTypeIsFunction(func));
        what++;  /* skip the '>' */
        f = LumenClosureValue(func);
        L->Top--;  /* pop function */
    } else if (reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI != 0) {  /* no tail call? */
        ci = L->BaseCI + reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI;
        LumenAssert(LumenTypeIsFunction(ci->Func));
        f = LumenClosureValue(ci->Func);
    }
    status = Lumen::Debug::GetInfo(L, what, reinterpret_cast<Lumen::DebugInfo *>(ar), f, ci);
    if (strchr(what, 'f')) {
        if (f == nullptr) LumenSetNilValue(L->Top);
        else
            LumenSetClosureValue(L, L->Top, f);
        LumenIncrTop(L);
    }
    if (strchr(what, 'L'))
        Lumen::Debug::CollectValidLines(L, f);
    LumenUnlock(L);
    return status;
}

const char *Lua::State::GetLocal(const Lua::DebugInfo *ar, int n) {
    auto L = LuaToLumen(this);
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name)
        lua_PushObject(L, ci->Base + (n - 1));
    LumenUnlock(L);
    return name;
}

const char *Lua::State::SetLocal(const Lua::DebugInfo *ar, int n) {
    auto L = LuaToLumen(this);
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name)
        LumenSetObjectS2S (L, ci->Base + (n - 1), L->Top - 1);
    L->Top--;  /* pop value */
    LumenUnlock(L);
    return name;
}

static const char *aux_upvalue(Lumen::StkId fi, int n, Lumen::Value **val) {
    Lumen::Closure *f;
    if (!LumenTypeIsFunction(fi)) return nullptr;
    f = LumenClosureValue(fi);
    if (f->AsC.IsC) {
        if (!(1 <= n && n <= f->AsC.NUpValues)) return nullptr;
        *val = &f->AsC.UpValues[n - 1];
        return "";
    } else {
        Lumen::Proto *p = f->AsLua.Func;
        if (!(1 <= n && n <= p->UpValuesCount)) return nullptr;
        *val = f->AsLua.UpValues[n - 1]->SelfValue;
        return LumenStringCString(p->UpValues[n - 1]);
    }
}

const char *Lua::State::GetUpValue(int funcIndex, int n) {
    auto L = LuaToLumen(this);
    const char *name;
    Lumen::Value *val;
    LumenLock(L);
    name = aux_upvalue(index2addr(L, funcIndex), n, &val);
    if (name) {
        LumenSetObject2S(L, L->Top, val);
        apiIncrTop(L);
    }
    LumenUnlock(L);
    return name;
}

const char *Lua::State::SetUpValue(int funcIndex, int n) {
    auto L = LuaToLumen(this);
    const char *name;
    Lumen::Value *val;
    Lumen::StkId fi;
    LumenLock(L);
    fi = index2addr(L, funcIndex);
    apiCheckElementCount(L, 1);
    name = aux_upvalue(fi, n, &val);
    if (name) {
        L->Top--;
        LumenSetObject(L, val, L->Top);
        LumenGCBarrier(L, LumenClosureValue(fi), L->Top);
    }
    LumenUnlock(L);
    return name;
}

bool Lua::State::SetHook(Lua::Hook func, Lua::HookMask mask, int count) {
    auto L = LuaToLumen(this);
    if (func == nullptr || mask == 0) {  /* turn off hooks? */
        mask = 0;
        func = nullptr;
    }
    L->Hook = reinterpret_cast<Lumen::Hook>(func);
    L->BaseHookCount = count;
    LumenDebugResetHookCount(L);
    L->HookMask = cast_byte(mask);
    return true;
}

Lua::Hook Lua::State::GetHook() {
    return reinterpret_cast<Lua::Hook>(LuaToLumen(this)->Hook);
}

int Lua::State::GetHookMask() {
    return LuaToLumen(this)->HookMask;
}

int Lua::State::GetHookCount() {
    return LuaToLumen(this)->BaseHookCount;
}

void Lua::Close(Lua::State *(&state)) {
    auto L = LuaToLumen(state);
    if (L != nullptr) {
        Lumen::State::Close(L);
    }
    state = nullptr;
}

void Lua::XMove(Lua::State *fromL, Lua::State *toL, int n) {
    auto from = LuaToLumen(fromL);
    auto to = LuaToLumen(toL);
    int i;
    if (from == to) return;
    LumenLock(to);
    apiCheckElementCount(from, n);
    LumenApiCheck(from, LumenGlobal(from) == LumenGlobal(to));
    LumenApiCheck(from, to->CallInfo->Top - to->Top >= n);
    from->Top -= n;
    for (i = 0; i < n; i++) {
        LumenSetObject2S(to, to->Top++, from->Top + i);
    }
    LumenUnlock(to);
}

void Lua::SetLevel(Lua::State *fromL, Lua::State *toL) {
    auto from = LuaToLumen(fromL);
    auto to = LuaToLumen(toL);
    to->NCCalls = from->NCCalls;
}
