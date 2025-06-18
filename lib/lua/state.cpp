/*!
 * @brief Lua State API
 * @author Jakit
 * @date 2025/6/14
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
#include "lumen/api.h"

#include "lua.hpp"

#define LuaToLumen(L) reinterpret_cast<Lumen::State *>(L)
#define LumenToLua(L) reinterpret_cast<Lua::State *>(L)

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
    LumenApiIncrTop(L);
    LumenUnlock(L);
    luai_userstatethread(L, L1);
    return L1 == nullptr ? nullptr : LumenToLua(L1);
}

Lua::Delegate Lua::State::AtPanic(Lua::Delegate pInvoke) {
    auto L = LuaToLumen(this);
    Lua::Delegate old;
    LumenLock(L);
    old = reinterpret_cast<Lua::Delegate>(LumenGlobalState(L)->Panic);
    LumenGlobalState(L)->Panic = reinterpret_cast<Lumen::Delegate>(pInvoke);
    LumenUnlock(L);
    return old;
}

const Lua::Number *Lua::State::Version() {
    static const Lumen::Number lua_version_number = LUA_VERSION_NUM;
    return &lua_version_number;
}

// MARK: basic stack manipulation

int Lua::State::AbsIndex(int idx) {
    auto L = LuaToLumen(this);
    return (idx > 0 || LumenApiIsPseudo(idx))
           ? idx
           : cast_int((L->Top - L->Base) + 1 + idx);
}

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
    LumenSetObject2S(L, L->Top, L->ToObject(idx));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::Remove(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value p;
    LumenLock(L);
    p = L->ToObject(idx);
    LumenApiCheckValidIndex(L, p);
    while (++p < L->Top) LumenSetObjectS2S(L, p - 1, p);
    L->Top--;
    LumenUnlock(L);
}

void Lua::State::Insert(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value p;
    Lumen::Value q;
    LumenLock(L);
    p = L->ToObject(idx);
    LumenApiCheckValidIndex(L, p);
    for (q = L->Top; q > p; q--) LumenSetObjectS2S(L, q, q - 1);
    LumenSetObjectS2S(L, p, L->Top);
    LumenUnlock(L);
}

static void moveTo(Lumen::State *L, Lumen::Object *from, int idx) {
    Lumen::Object *to = L->ToObject(idx);
    LumenApiCheckValidIndex(L, to);
    if (idx == Lumen::EnvIndex) {
        Lumen::Closure *func = LumenCurFunc(L);
        LumenApiCheck(L, LumenTypeIsTable(from));
        func->AsC.Env = LumenTableValue(from);
        LumenGCBarrier(L, func, from);
    } else {
        LumenSetObject(L, to, from);
        if (idx < Lumen::GlobalIndex)  /* function upvalue? */
            LumenGCBarrier(L, LumenCurFunc(L), from);
    }
}

void Lua::State::Replace(int idx) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    /* explicit test for incompatible code */
    if (idx == Lumen::EnvIndex && L->CallInfo == L->BaseCI)
        Lumen::Debug::RunError(L, "no calling environment");
    LumenApiCheckElementCount(L, 1);
    moveTo(L, L->Top - 1, idx);
    L->Top--;
    LumenUnlock(L);
}

void Lua::State::Copy(int fromIdx, int toIdx) {
    auto L = LuaToLumen(this);
    Lumen::Object *from;
    LumenLock(L);
    if (toIdx == Lumen::EnvIndex && L->CallInfo == L->BaseCI)
        Lumen::Debug::RunError(L, "no calling environment");
    from = L->ToObject(fromIdx);
    moveTo(L, from, toIdx);
    LumenUnlock(L);
}

static void compatReverse(Lua::State *L, int a, int b) {
    for (; a < b; ++a, --b) {
        L->PushValue(a);
        L->PushValue(b);
        L->Replace(a);
        L->Replace(b);
    }
}

void Lua::State::Rotate(int idx, int n) {
    auto L = LuaToLumen(this);
    int n_elems = 0;
    idx = AbsIndex(idx);
    n_elems = GetTop() - idx + 1;
    if (n < 0)
        n += n_elems;
    if (n > 0 && n < n_elems) {
        if (!CheckStack(2)) {
            PushString("not enough stack slots available");
            Error();
        }
        n = n_elems - n;
        compatReverse(this, idx, idx + n - 1);
        compatReverse(this, idx + n, idx + n_elems - 1);
        compatReverse(this, idx, idx + n_elems - 1);
    }
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
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
    return LumenVMToNumber(o, &n);
}

bool Lua::State::IsString(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value o = L->ToObject(idx);
    return LumenTypeIsString(o);
}

bool Lua::State::IsDelegate(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value o = L->ToObject(idx);
    return LumenIsCFunction(o);
}

bool Lua::State::IsUserdata(int idx) {
    auto L = LuaToLumen(this);
    const Lumen::Object *o = L->ToObject(idx);
    return (LumenTypeIsUData(o) || LumenTypeIsLUData(o));
}

Lua::Type Lua::State::Type(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value o = L->ToObject(idx);
    return (o == Lumen::NilObject) ? Lua::TypeNone : LumenTypeOf(o);
}

const char *Lua::State::TypeId(int tp) {
    return (tp == Lua::TypeNone) ? "no value" : Lumen::TM::TypeNames[tp];
}

void Lua::State::Arith(Lua::ArithOp op) {
    auto L = LuaToLumen(this);
    Lumen::Value o1;  /* 1st operand */
    Lumen::Value o2;  /* 2nd operand */
    LumenLock(L);
    if (op != Lua::ArithOpUnm) /* all other operations expect two operands */
        LumenApiCheckElementCount(L, 2);
    else {  /* for unary minus, add fake 2nd operand */
        LumenApiCheckElementCount(L, 1);
        LumenSetObjectS2S(L, L->Top, L->Top - 1);
        L->Top++;
    }
    o1 = L->Top - 2;
    o2 = L->Top - 1;
    if (LumenTypeIsNumber(o1) && LumenTypeIsNumber(o2)) {
        LumenSetNumberValue(o1, Lumen::Arith(op, LumenNumberValue(o1), LumenNumberValue(o2)));
    } else
        Lumen::VM::ArithValue(L, o1, o1, o2, cast(Lumen::TM::Name, op - Lua::ArithOpAdd + Lumen::TM::NameAdd));
    L->Top--;
    LumenUnlock(L);
}

int Lua::State::Compare(int idx1, int idx2, Lua::ArithOp op) {
    auto L = LuaToLumen(this);
    Lumen::Value o1, o2;
    int i = 0;
    LumenLock(L);  /* may call tag method */
    o1 = L->ToObject(idx1);
    o2 = L->ToObject(idx2);
    if (LumenApiIsValid(o1) && LumenApiIsValid(o2)) {
        switch (op) {
            case Lua::CompareOpEQ:
                i = Lumen::VM::EqualObject(L, o1, o2);
                break;
            case Lua::CompareOpLT:
                i = Lumen::VM::LessThan(L, o1, o2);
                break;
            case Lua::CompareOpLE:
                i = Lumen::VM::LessEqual(L, o1, o2);
                break;
            default:
                LumenApiCheck(L, 0);
        }
    }
    LumenUnlock(L);
    return i;
}

bool Lua::State::Equal(int idx1, int idx2) {
    auto L = LuaToLumen(this);
    Lumen::Value o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = L->ToObject(idx1);
    o2 = L->ToObject(idx2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0 : LumenVMEqualObj(L, o1, o2);
    LumenUnlock(L);
    return i;
}

bool Lua::State::RawEqual(int idx1, int idx2) {
    auto L = LuaToLumen(this);
    Lumen::Value o1 = L->ToObject(idx1);
    Lumen::Value o2 = L->ToObject(idx2);
    return !(o1 == Lumen::NilObject || o2 == Lumen::NilObject) && Lumen::RawEqualObject(o1, o2);
}

bool Lua::State::LessThan(int idx1, int idx2) {
    auto L = LuaToLumen(this);
    Lumen::Value o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = L->ToObject(idx1);
    o2 = L->ToObject(idx2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0
                                                           : Lumen::VM::LessThan(L, o1, o2);
    LumenUnlock(L);
    return i;
}

Lua::Number Lua::State::ToNumber(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
    if (LumenVMToNumber(o, &n))
        return LumenNumberValue(o);
    else
        return 0;
}

Lua::Integer Lua::State::ToInteger(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
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
    const Lumen::Object *o = L->ToObject(idx);
    return !LumenIsFalse(o);
}

const char *Lua::State::ToString(int idx, Lua::UInteger *len) {
    auto L = LuaToLumen(this);
    Lumen::Value o = L->ToObject(idx);
    if (!LumenTypeIsString(o)) {
        LumenLock(L);  /* `Lumen::VM::ToString' may create a new string */
        if (!Lumen::VM::ToString(L, o)) {  /* conversion failed? */
            if (len != nullptr) *len = 0;
            LumenUnlock(L);
            return nullptr;
        }
        LumenGCCheckGC(L);
        o = L->ToObject(idx);  /* previous call may reallocate the stack */
        LumenUnlock(L);
    }
    if (len != nullptr) *len = LumenStringValue(o)->Length;
    return LumenStringValue2CString(o);
}

bool Lua::State::InstanceOf(int idxChild, int idxSuper) {
    auto L = LuaToLumen(this);
    const Lumen::Object *oChild = L->ToObject(idxChild);
    const Lumen::Object *oSuper = L->ToObject(idxSuper);
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
    Lumen::Value o = L->ToObject(idx);
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
    Lumen::Value o = L->ToObject(idx);
    return (!LumenIsCFunction(o)) ? nullptr : reinterpret_cast<Lua::Delegate>(LumenClosureValue(o)->AsC.Func);
}

Lua::Function Lua::State::ToFunction(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value o = L->ToObject(idx);
    return (!LumenIsCFunction(o)) ? nullptr : reinterpret_cast<Lua::Function>(LumenClosureValue(o)->AsC.Func);
}

void *Lua::State::ToUserdata(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value o = L->ToObject(idx);
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
    Lumen::Value o = L->ToObject(idx);
    return (!LumenTypeIsThread(o)) ? nullptr : LumenToLua(LumenThreadValue(o));
}

const void *Lua::State::ToPointer(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value o = L->ToObject(idx);
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
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushNumber(Lua::Number n) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetNumberValue(L->Top, n);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushInteger(Lua::Integer n) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetNumberValue(L->Top, cast_num(n));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushString(const char *s, Lua::UInteger length) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenSetStringValue2S(L, L->Top,
                          Lumen::String::New(L, s, length));
    LumenApiIncrTop(L);
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
    LumenApiCheckElementCount(L, n);
    cl = Lumen::CClosure::New(L, n, L->GetCurrentEnv());
    cl->AsC.Func = reinterpret_cast<Lumen::Delegate>(invoke);
    L->Top -= n;
    while (n--)
        LumenSetObject2N(L, &cl->AsC.UpValues[n], L->Top + n);
    LumenSetClosureValue(L, L->Top, cl);
    LumenAssert(LumenGCIsWhite(LumenObject2GCObject(cl)));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushFunction(Lua::Function invoke, int n) {
    auto L = LuaToLumen(this);
    Lumen::Closure *cl;
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenApiCheckElementCount(L, n);
    cl = Lumen::CClosure::New(L, n, L->GetCurrentEnv());
    cl->AsC.Func = reinterpret_cast<Lumen::Delegate>(invoke);
    L->Top -= n;
    while (n--)
        LumenSetObject2N(L, &cl->AsC.UpValues[n], L->Top + n);
    LumenSetClosureValue(L, L->Top, cl);
    LumenAssert(LumenGCIsWhite(LumenObject2GCObject(cl)));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushBoolean(int b) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetBoolValue(L->Top, (b != 0));  /* ensure that true is 1 */
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushLightUserdata(void *p) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetLUDataValue(L->Top, p);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

int Lua::State::PushThread() {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenSetThreadValue(L, L->Top, L);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return (LumenGlobalState(L)->MainThread == L);
}

// MARK: get functions (LuaToState(this)ua -> stack)

void Lua::State::GetTable(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value t;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheckValidIndex(L, t);
    Lumen::VM::GetTable(L, t, L->Top - 1, L->Top - 1);
    LumenUnlock(L);
}

void Lua::State::GetField(int idx, const char *k) {
    auto L = LuaToLumen(this);
    Lumen::Value t;
    Lumen::Object key; // NOLINT
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheckValidIndex(L, t);
    LumenSetStringValue(L, &key, Lumen::String::New(L, k));
    Lumen::VM::GetTable(L, t, &key, L->Top);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::RawGet(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value t;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    LumenSetObject2S(L, L->Top - 1, Lumen::Table::Get(LumenTableValue(t), L->Top - 1));
    LumenUnlock(L);
}

void Lua::State::RawGetAt(int idx, int n) {
    auto L = LuaToLumen(this);
    Lumen::Value o;
    LumenLock(L);
    o = L->ToObject(idx);
    LumenApiCheck(L, LumenTypeIsTable(o));
    LumenSetObject2S(L, L->Top, Lumen::Table::GetNum(LumenTableValue(o), n));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::RawGetPtr(int idx, int n, const void *p) {
    auto L = LuaToLumen(this);
    Lumen::Value t;
    Lumen::Object k; // NOLINT
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    LumenSetLUDataValue(&k, cast(void * , p));
    LumenSetObject2S(L, L->Top, Lumen::Table::Get(LumenTableValue(t), &k));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::CreateTable(int nArray, int nRec) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenSetTableValue(L, L->Top, Lumen::Table::New(L, nArray, nRec));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void *Lua::State::NewUserdata(Lua::UInteger size) {
    auto L = LuaToLumen(this);
    Lumen::Userdata *u;
    LumenLock(L);
    LumenGCCheckGC(L);
    u = Lumen::Userdata::New(L, size, L->GetCurrentEnv());
    LumenSetUDataValue(L, L->Top, u);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return u + 1;
}

bool Lua::State::GetMetatable(int objIndex) {
    auto L = LuaToLumen(this);
    const Lumen::Object *obj;
    Lumen::Table *mt;
    int res;
    LumenLock(L);
    obj = L->ToObject(objIndex);
    switch (LumenTypeOf(obj)) {
        case Lua::TypeTable:
            mt = LumenTableValue(obj)->Metatable;
            break;
        case Lua::TypeUserdata:
            mt = LumenUDataValue(obj)->Metatable;
            break;
        default:
            mt = LumenGlobalState(L)->Metatable[LumenTypeOf(obj)];
            break;
    }
    if (mt == nullptr)
        res = 0;
    else {
        LumenSetTableValue(L, L->Top, mt);
        LumenApiIncrTop(L);
        res = 1;
    }
    LumenUnlock(L);
    return res;
}

void Lua::State::GetFEnv(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value o;
    LumenLock(L);
    o = L->ToObject(idx);
    LumenApiCheckValidIndex(L, o);
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
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

// MARK: set functions (stack -> Lua)

void Lua::State::SetTable(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value t;
    LumenLock(L);
    LumenApiCheckElementCount(L, 2);
    t = L->ToObject(idx);
    LumenApiCheckValidIndex(L, t);
    Lumen::VM::SetTable(L, t, L->Top - 2, L->Top - 1);
    L->Top -= 2;  /* pop index and value */
    LumenUnlock(L);
}

void Lua::State::SetField(int idx, const char *k) {
    auto L = LuaToLumen(this);
    Lumen::Value t;
    Lumen::Object key; // NOLINT
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    t = L->ToObject(idx);
    LumenApiCheckValidIndex(L, t);
    LumenSetStringValue(L, &key, Lumen::String::New(L, k));
    Lumen::VM::SetTable(L, t, &key, L->Top - 1);
    L->Top--;  /* pop value */
    LumenUnlock(L);
}

void Lua::State::RawSet(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value t;
    LumenLock(L);
    LumenApiCheckElementCount(L, 2);
    t = L->ToObject(idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    LumenSetObject2T(L, Lumen::Table::Set(L, LumenTableValue(t), L->Top - 2), L->Top - 1);
    LumenGCBarrierTable(L, LumenTableValue(t), L->Top - 1);
    L->Top -= 2;
    LumenUnlock(L);
}

void Lua::State::RawSetAt(int idx, int n) {
    auto L = LuaToLumen(this);
    Lumen::Value o;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    o = L->ToObject(idx);
    LumenApiCheck(L, LumenTypeIsTable(o));
    LumenSetObject2T(L, Lumen::Table::SetNum(L, LumenTableValue(o), n), L->Top - 1);
    LumenGCBarrierTable(L, LumenTableValue(o), L->Top - 1);
    L->Top--;
    LumenUnlock(L);
}

void Lua::State::RawSetPtr(int idx, int n, const void *p) {
    auto L = LuaToLumen(this);
    Lumen::Value t;
    Lumen::Object k; // NOLINT
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    t = L->ToObject(idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    LumenSetLUDataValue(&k, cast(void * , p));
    LumenSetObject2T(L, Lumen::Table::Set(L, LumenTableValue(t), &k), L->Top - 1);
    LumenGCBarrierTable(L, LumenTableValue(t), L->Top - 1);
    L->Top--;
    LumenUnlock(L);
}

bool Lua::State::SetMetatable(int objIndex) {
    auto L = LuaToLumen(this);
    Lumen::Object *obj;
    Lumen::Table *mt;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    obj = L->ToObject(objIndex);
    LumenApiCheckValidIndex(L, obj);
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
            LumenGlobalState(L)->Metatable[LumenTypeOf(obj)] = mt;
            break;
        }
    }
    L->Top--;
    LumenUnlock(L);
    return true;
}

bool Lua::State::SetFEnv(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value o;
    int res = 1;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    o = L->ToObject(idx);
    LumenApiCheckValidIndex(L, o);
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

void Lua::State::Call(int nargs, int nResults) {
    auto L = LuaToLumen(this);
    Lumen::Value func;
    LumenLock(L);
    LumenApiCheckElementCount(L, nargs + 1);
    LumenApiCheckResults(L, nargs, nResults);
    func = L->Top - (nargs + 1);
    Lumen::Do::Call(L, func, nResults);
    LumenApiAdjustResults(L, nResults);
    LumenUnlock(L);
}

Lua::Ret Lua::State::TryCall(int nargs, int nResults, int errFunc) {
    auto L = LuaToLumen(this);
    Lumen::ProtectedCall c; // NOLINT
    int status;
    ptrdiff_t func;
    LumenLock(L);
    LumenApiCheckElementCount(L, nargs + 1);
    LumenApiCheckResults(L, nargs, nResults);
    if (errFunc == 0)
        func = 0;
    else {
        Lumen::Value o = L->ToObject(errFunc);
        LumenApiCheckValidIndex(L, o);
        func = LumenSaveStack(L, o);
    }
    c.Func = L->Top - (nargs + 1);  /* function to be called */
    c.NResults = nResults;
    status = Lumen::Do::PCall(L,
                              &Lumen::ProtectedCall::Call, &c,
                              LumenSaveStack(L, c.Func), func);
    LumenApiAdjustResults(L, nResults);
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
    Lumen::Object *o;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
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

bool Lua::State::CanYield() {
    return LuaToLumen(this)->NCCalls == 0;
}

// MARK: garbage-collection function and options

int Lua::State::GC(Lua::GCAction what, int data) {
    auto L = LuaToLumen(this);
    int res = 0;
    Lumen::GlobalState *g;
    LumenLock(L);
    g = LumenGlobalState(L);
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
    LumenApiCheckElementCount(L, 1);
    Lumen::Debug::ErrorMessage(L);
    LumenUnlock(L);
    return 0;  /* to avoid warnings */
}

bool Lua::State::Next(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value t;
    int more;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    more = Lumen::Table::Next(L, LumenTableValue(t), L->Top - 1);
    if (more) {
        LumenApiIncrTop(L);
    } else  /* no more elements */
        L->Top -= 1;  /* remove key */
    LumenUnlock(L);
    return more;
}

void Lua::State::Concat(int n) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenApiCheckElementCount(L, n);
    if (n >= 2) {
        LumenGCCheckGC(L);
        Lumen::VM::Concat(L, n, cast_int(L->Top - L->Base) - 1);
        L->Top -= (n - 1);
    } else if (n == 0) {  /* push empty string */
        LumenSetStringValue2S(L, L->Top, Lumen::String::New(L, "", 0));
        LumenApiIncrTop(L);
    }
    /* else n == 1; nothing to do */
    LumenUnlock(L);
}

void Lua::State::LengthOf(int idx) {
    auto L = LuaToLumen(this);
    Lumen::Value t;
    LumenLock(L);
    t = L->ToObject(idx);
    Lumen::VM::ObjectLength(L, L->Top, t);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

Lua::Allocator Lua::State::GetAllocator(void **ud) {
    auto L = LuaToLumen(this);
    Lumen::Allocator f;
    LumenLock(L);
    if (ud) *ud = LumenGlobalState(L)->ReAllocatorUData;
    f = LumenGlobalState(L)->ReAllocator;
    LumenUnlock(L);
    return f;
}

void Lua::State::SetAllocator(Lua::Allocator f, void *ud) {
    auto L = LuaToLumen(this);
    LumenLock(L);
    LumenGlobalState(L)->ReAllocatorUData = ud;
    LumenGlobalState(L)->ReAllocator = f;
    LumenUnlock(L);
}

// MARK: Debug APIs

bool Lua::State::GetStack(int level, Lua::DebugInfo *ar) {
    auto L = LuaToLumen(this);
    bool status;
    Lumen::CallInfo *ci;
    LumenLock(L);
    for (ci = L->CallInfo; level > 0 && ci > L->BaseCI; ci--) {
        level--;
        if (LumenCIFuncIsLua(ci))  /* Lua function? */
            level -= ci->NTailCalls;  /* skip lost tail calls */
    }
    if (level == 0 && ci > L->BaseCI) {  /* level found? */
        status = true;
        reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI = cast_int(ci - L->BaseCI);
    } else if (level < 0) {  /* level is of a lost tail call? */
        status = true;
        reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI = 0;
    } else status = false;  /* no such level */
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
        Lumen::Value func = L->Top - 1;
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
        L->PushObject(ci->Base + (n - 1));
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

const char *Lua::State::GetUpValue(int funcIndex, int n) {
    auto L = LuaToLumen(this);
    const char *name;
    Lumen::Object *val;
    LumenLock(L);
    name = L->ToObject(funcIndex)->GetUpValueInfo(n, &val);
    if (name) {
        LumenSetObject2S(L, L->Top, val);
        LumenApiIncrTop(L);
    }
    LumenUnlock(L);
    return name;
}

const char *Lua::State::SetUpValue(int funcIndex, int n) {
    auto L = LuaToLumen(this);
    const char *name;
    Lumen::Object *val;
    Lumen::Value fi;
    LumenLock(L);
    fi = L->ToObject(funcIndex);
    LumenApiCheckElementCount(L, 1);
    name = fi->GetUpValueInfo(n, &val);
    if (name) {
        L->Top--;
        LumenSetObject(L, val, L->Top);
        LumenGCBarrier(L, LumenClosureValue(fi), L->Top);
    }
    LumenUnlock(L);
    return name;
}

static Lumen::UpValue **getUpValueRef(Lumen::State *L, int fIdx, int n, Lumen::LClosure **pf) {
    Lumen::LClosure *f;
    Lumen::Value fi = L->ToObject(fIdx);
    LumenApiCheck(L, LumenIsLFunction(fi));
    f = LumenLClosureValue(fi);
    LumenApiCheck(L, (1 <= n && n <= f->Func->UpValuesCount));
    if (pf) *pf = f;
    return &f->UpValues[n - 1];  /* get its upvalue pointer */
}

void *Lua::State::GetUpValueId(int fIdx, int n) {
    auto L = LuaToLumen(this);
    Lumen::Value fi = L->ToObject(fIdx);
    if (LumenIsLFunction(fi)) { /* lua closure */
        return *getUpValueRef(L, fIdx, n, nullptr);
    } else if (LumenIsCFunction(fi)) { /* C closure */
        Lumen::CClosure *f = LumenCClosureValue(fi);
        LumenApiCheck(L, 1 <= n && n <= f->NUpValues);
        return &f->UpValues[n - 1];
    } else {
        LumenApiCheck(L, 0);
        return nullptr;
    }
}

void Lua::State::JoinUpValue(int fIdx1, int n1, int fIdx2, int n2) {
    auto L = LuaToLumen(this);
    Lumen::LClosure *f1;
    Lumen::UpValue **up1 = getUpValueRef(L, fIdx1, n1, &f1);
    Lumen::UpValue **up2 = getUpValueRef(L, fIdx2, n2, nullptr);
    *up1 = *up2;
    LumenGCObjectBarrier(L, f1, *up2);
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


