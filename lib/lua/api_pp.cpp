/*!
 * @brief api_pp
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

#include "lua.hpp"

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/gc.h"
#include "lumen/object.h"
#include "lumen/state.h"
#include "lumen/tm.h"
#include "lumen/undump.h"
#include "lumen/vm.h"
#include "lumen/protected_call.h"

extern const char lua_ident[];

#define apiCheckElementCount(L, n)    LumenApiCheck(L, (n) <= (L->Top - L->Base))

#define apiCheckValidIndex(L, i)    LumenApiCheck(L, (i) != Lumen::NilObject)

#define apiIncrTop(L) \
LumenDo(                \
    LumenApiCheck(L, L->Top < L->CallInfo->Top); \
    L->Top++;         \
)

static Lumen::Value *index2addr(Lumen::State *L, int idx) {
    if (idx > 0) {
        Lumen::Value *o = L->Base + (idx - 1);
        LumenApiCheck(L, idx <= L->CallInfo->Top - L->Base);
        if (o >= L->Top) return cast(Lumen::Value *, Lumen::NilObject);
        else return o;
    } else if (idx > LUA_REGISTRYINDEX) {
        LumenApiCheck(L, idx != 0 && -idx <= L->Top - L->Base);
        return L->Top + idx;
    } else
        switch (idx) {  /* pseudo-indices */
            case LUA_REGISTRYINDEX:
                return LumenRegistry(L);
            case LUA_ENVIRONINDEX: {
                Lumen::Closure *func = LumenCurFunc(L);
                LumenSetTableValue(L, &L->Env, func->AsC.Env);
                return &L->Env;
            }
            case LUA_GLOBALSINDEX:
                return LumenGlobalTable(L);
            default: {
                Lumen::Closure *func = LumenCurFunc(L);
                idx = LUA_GLOBALSINDEX - idx;
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
    return L == nullptr ? nullptr : new Lua::State{L};
}

Lua::State *Lua::State::NewThread() {
    Lumen::State *L1;
    LumenLock(L);
    LumenGCCheckGC(L);
    L1 = Lumen::State::NewThread(L);
    LumenSetThreadValue(L, L->Top, L1);
    apiIncrTop(L);
    LumenUnlock(L);
    luai_userstatethread(L, L1);
    return L1 == nullptr ? nullptr : new Lua::State{L1};
}

Lua::Delegate Lua::State::AtPanic(Lua::Delegate pInvoke) {
    Lua::Delegate old;
    LumenLock(L);
    old = reinterpret_cast<Lua::Delegate>(LumenGlobal(L)->Panic);
    LumenGlobal(L)->Panic = reinterpret_cast<Lumen::Delegate>(pInvoke);
    LumenUnlock(L);
    return old;
}

// MARK: basic stack manipulation

int Lua::State::GetTop() {
    return cast_int(L->Top - L->Base);
}

void Lua::State::SetTop(int idx) {
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
    LumenLock(L);
    LumenSetObject2S(L, L->Top, index2addr(L, idx));
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::Remove(int idx) {
    Lumen::StkId p;
    LumenLock(L);
    p = index2addr(L, idx);
    apiCheckValidIndex(L, p);
    while (++p < L->Top) LumenSetObjectS2S(L, p - 1, p);
    L->Top--;
    LumenUnlock(L);
}

void Lua::State::Insert(int idx) {
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
    Lumen::StkId o;
    LumenLock(L);
    /* explicit test for incompatible code */
    if (idx == LUA_ENVIRONINDEX && L->CallInfo == L->BaseCI)
        Lumen::Debug::RunError(L, "no calling environment");
    apiCheckElementCount(L, 1);
    o = index2addr(L, idx);
    apiCheckValidIndex(L, o);
    if (idx == LUA_ENVIRONINDEX) {
        Lumen::Closure *func = LumenCurFunc(L);
        LumenApiCheck(L, LumenTypeIsTable(L->Top - 1));
        func->AsC.Env = LumenTableValue(L->Top - 1);
        LumenGCBarrier(L, func, L->Top - 1);
    } else {
        LumenSetObject(L, o, L->Top - 1);
        if (idx < LUA_GLOBALSINDEX)  /* function upvalue? */
            LumenGCBarrier(L, LumenCurFunc(L), L->Top - 1);
    }
    L->Top--;
    LumenUnlock(L);
}

int Lua::State::CheckStack(int size) {
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

int Lua::State::IsNumber(int idx) {
    Lumen::Value n;
    const Lumen::Value *o = index2addr(L, idx);
    return LumenVMToNumber(o, &n);
}

int Lua::State::IsString(int idx) {
    int t = lua_type(L, idx);
    return (t == LUA_TSTRING || t == LUA_TNUMBER);
}

int Lua::State::IsDelegate(int idx) {
    Lumen::StkId o = index2addr(L, idx);
    return LumenIsCFunction(o);
}

int Lua::State::IsUserdata(int idx) {
    const Lumen::Value *o = index2addr(L, idx);
    return (LumenTypeIsUData(o) || LumenTypeIsLUData(o));
}

int Lua::State::Type(int idx) {
    Lumen::StkId o = index2addr(L, idx);
    return (o == Lumen::NilObject) ? LUA_TNONE : LumenTypeOf(o);
}

const char *Lua::State::TypeName(int t) {
    UNUSED(L);
    return (t == LUA_TNONE) ? "no value" : Lumen::TM::TypeNames[t];
}

int Lua::State::Equal(int idx1, int idx2) {
    Lumen::StkId o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = index2addr(L, idx1);
    o2 = index2addr(L, idx2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0 : LumenVMEqualObj(L, o1, o2);
    LumenUnlock(L);
    return i;
}

int Lua::State::RawEqual(int idx1, int idx2) {
    Lumen::StkId o1 = index2addr(L, idx1);
    Lumen::StkId o2 = index2addr(L, idx2);
    return (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0
                                                              : Lumen::RawEqualObject(o1, o2);
}

int Lua::State::LessThan(int idx1, int idx2) {
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
    Lumen::Value n;
    const Lumen::Value *o = index2addr(L, idx);
    if (LumenVMToNumber(o, &n))
        return LumenNumberValue(o);
    else
        return 0;
}

Lua::Integer Lua::State::ToInteger(int idx) {
    Lumen::Value n;
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
    const Lumen::Value *o = index2addr(L, idx);
    return !LumenIsFalse(o);
}

const char *Lua::State::ToString(int idx, size_t *len) {
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

size_t Lua::State::ObjectLength(int idx) {
    Lumen::StkId o = index2addr(L, idx);
    switch (LumenTypeOf(o)) {
        case LUA_TSTRING:
            return LumenStringValue(o)->Length;
        case LUA_TUSERDATA:
            return LumenUDataValue(o)->Length;
        case LUA_TTABLE:
            return Lumen::Table::GetN(LumenTableValue(o));
        case LUA_TNUMBER: {
            size_t l;
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
    Lumen::StkId o = index2addr(L, idx);
    return (!LumenIsCFunction(o)) ? nullptr : reinterpret_cast<Lua::Delegate>(LumenClosureValue(o)->AsC.Func);
}

Lua::Function Lua::State::ToFunction(int idx) {
    Lumen::StkId o = index2addr(L, idx);
    return (!LumenIsCFunction(o)) ? nullptr : reinterpret_cast<Lua::Function>(LumenClosureValue(o)->AsC.Func);
}

void *Lua::State::ToUserdata(int idx) {
    Lumen::StkId o = index2addr(L, idx);
    switch (LumenTypeOf(o)) {
        case LUA_TUSERDATA:
            return (LumenUDataValue(o) + 1);
        case LUA_TLIGHTUSERDATA:
            return LumenLUDataValue(o);
        default:
            return nullptr;
    }
}

Lua::State Lua::State::ToThread(int idx) {
    Lumen::StkId o = index2addr(L, idx);
    return (!LumenTypeIsThread(o)) ? Lua::State{nullptr} : Lua::State{LumenThreadValue(o)};
}

const void *Lua::State::ToPointer(int idx) {
    Lumen::StkId o = index2addr(L, idx);
    switch (LumenTypeOf(o)) {
        case LUA_TTABLE:
            return LumenTableValue(o);
        case LUA_TFUNCTION:
            return LumenClosureValue(o);
        case LUA_TTHREAD:
            return LumenThreadValue(o);
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            return lua_touserdata(L, idx);
        default:
            return nullptr;
    }
}

// MARK: push functions (C -> stack)

void Lua::State::PushNil() {
    LumenLock(L);
    LumenSetNilValue(L->Top);
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushNumber(Lua::Number n) {
    LumenLock(L);
    LumenSetNumberValue(L->Top, n);
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushInteger(Lua::Integer n) {
    LumenLock(L);
    LumenSetNumberValue(L->Top, cast_num(n));
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushString(const char *s, size_t length) {
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenSetStringValue2S(L, L->Top,
                          Lumen::String::New(L, s, length));
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushString(const char *s) {
    if (s == nullptr)
        lua_pushnil(L);
    else
        lua_pushlstring(L, s, strlen(s));
}

const char *Lua::State::PushVFString(const char *fmt, va_list argP) {
    const char *ret;
    LumenLock(L);
    LumenGCCheckGC(L);
    ret = Lumen::PushVFString(L, fmt, argP);
    LumenUnlock(L);
    return ret;
}

const char *Lua::State::PushFString(const char *fmt, ...) {
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
    LumenLock(L);
    LumenSetBoolValue(L->Top, (b != 0));  /* ensure that true is 1 */
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::PushLightUserdata(void *p) {
    LumenLock(L);
    LumenSetLUDataValue(L->Top, p);
    apiIncrTop(L);
    LumenUnlock(L);
}

int Lua::State::PushThread() {
    LumenLock(L);
    LumenSetThreadValue(L, L->Top, L);
    apiIncrTop(L);
    LumenUnlock(L);
    return (LumenGlobal(L)->MainThread == L);
}

// MARK: get functions (LuaToState(this)ua -> stack)

void Lua::State::GetTable(int idx) {
    Lumen::StkId t;
    LumenLock(L);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    Lumen::VM::GetTable(L, t, L->Top - 1, L->Top - 1);
    LumenUnlock(L);
}

void Lua::State::GetField(int idx, const char *k) {
    Lumen::StkId t;
    Lumen::Value key;
    LumenLock(L);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    LumenSetStringValue(L, &key, Lumen::String::New(L, k));
    Lumen::VM::GetTable(L, t, &key, L->Top);
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::RawGet(int idx) {
    Lumen::StkId t;
    LumenLock(L);
    t = index2addr(L, idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    LumenSetObject2S(L, L->Top - 1, Lumen::Table::Get(LumenTableValue(t), L->Top - 1));
    LumenUnlock(L);
}

void Lua::State::RawGetAt(int idx, int n) {
    Lumen::StkId o;
    LumenLock(L);
    o = index2addr(L, idx);
    LumenApiCheck(L, LumenTypeIsTable(o));
    LumenSetObject2S(L, L->Top, Lumen::Table::GetNum(LumenTableValue(o), n));
    apiIncrTop(L);
    LumenUnlock(L);
}

void Lua::State::CreateTable(int nArray, int nRec) {
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenSetTableValue(L, L->Top, Lumen::Table::New(L, nArray, nRec));
    apiIncrTop(L);
    LumenUnlock(L);
}

void *Lua::State::NewUserdata(size_t size) {
    Lumen::Userdata *u;
    LumenLock(L);
    LumenGCCheckGC(L);
    u = Lumen::Userdata::New(L, size, getCurEnv(L));
    LumenSetUDataValue(L, L->Top, u);
    apiIncrTop(L);
    LumenUnlock(L);
    return u + 1;
}

int Lua::State::GetMetatable(int objIndex) {
    const Lumen::Value *obj;
    Lumen::Table *mt = nullptr;
    int res;
    LumenLock(L);
    obj = index2addr(L, objIndex);
    switch (LumenTypeOf(obj)) {
        case LUA_TTABLE:
            mt = LumenTableValue(obj)->Metatable;
            break;
        case LUA_TUSERDATA:
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
    Lumen::StkId o;
    LumenLock(L);
    o = index2addr(L, idx);
    apiCheckValidIndex(L, o);
    switch (LumenTypeOf(o)) {
        case LUA_TFUNCTION:
            LumenSetTableValue(L, L->Top, LumenClosureValue(o)->AsC.Env);
            break;
        case LUA_TUSERDATA:
            LumenSetTableValue(L, L->Top, LumenUDataValue(o)->Env);
            break;
        case LUA_TTHREAD:
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
    Lumen::StkId t;
    Lumen::Value key;
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

int Lua::State::SetMetatable(int objIndex) {
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
        case LUA_TTABLE: {
            LumenTableValue(obj)->Metatable = mt;
            if (mt)
                LumenGCObjectBarrierTable(L, LumenTableValue(obj), mt);
            break;
        }
        case LUA_TUSERDATA: {
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
    return 1;
}

int Lua::State::SetFEnv(int idx) {
    Lumen::StkId o;
    int res = 1;
    LumenLock(L);
    apiCheckElementCount(L, 1);
    o = index2addr(L, idx);
    apiCheckValidIndex(L, o);
    LumenApiCheck(L, LumenTypeIsTable(L->Top - 1));
    switch (LumenTypeOf(o)) {
        case LUA_TFUNCTION:
            LumenClosureValue(o)->AsC.Env = LumenTableValue(L->Top - 1);
            break;
        case LUA_TUSERDATA:
            LumenUDataValue(o)->Env = LumenTableValue(L->Top - 1);
            break;
        case LUA_TTHREAD:
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
    if (nRes == LUA_MULTRET && L->Top >= L->CallInfo->Top) \
        L->CallInfo->Top = L->Top;   \
)

#define checkResults(L, na, nr) \
     LumenApiCheck(L, (nr) == LUA_MULTRET || (L->CallInfo->Top - L->Top >= (nr) - (na)))


void Lua::State::Call(int nargs, int nResults) {
    Lumen::StkId func;
    LumenLock(L);
    apiCheckElementCount(L, nargs + 1);
    checkResults(L, nargs, nResults);
    func = L->Top - (nargs + 1);
    Lumen::Do::Call(L, func, nResults);
    adjustResults(L, nResults);
    LumenUnlock(L);
}

int Lua::State::TryCall(int nargs, int nResults, int errFunc) {
    Lumen::ProtectedCall c;
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

int Lua::State::TryCall(Lua::Delegate invoke, void *userdata) {
    Lumen::ProtectedCCall c;
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

int Lua::State::TryCall(Lua::Function invoke, void *userdata) {
    Lumen::ProtectedCCall c;
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

int Lua::State::Load(Lua::Reader reader, void *data, const char *chunkName) {
    Lumen::ZIO z;
    int status;
    LumenLock(L);
    if (!chunkName) chunkName = "?";
    Lumen::ZIO::Init(L, &z, reinterpret_cast<Lumen::Reader>(reader), data);
    status = Lumen::Do::ProtectedParser(L, &z, chunkName);
    LumenUnlock(L);
    return status;
}

int Lua::State::Dump(Lua::Writer writer, void *data) {
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

int Lua::State::Yield(int nResults) {
    luai_userstateyield(L, nResults);
    LumenLock(L);
    if (L->NCCalls > L->BaseCCalls)
        Lumen::Debug::RunError(L, "attempt to yield across metaMethod/C-call boundary");
    L->Base = L->Top - nResults;  /* protect stack slots below */
    L->Status = LUA_YIELD;
    LumenUnlock(L);
    return -1;
}

int Lua::State::Resume(int nArgs) {
    int status;
    LumenLock(L);
    if (L->Status != LUA_YIELD && (L->Status != 0 || L->CallInfo != L->BaseCI))
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

int Lua::State::Status() {
    return L->Status;
}

// MARK: garbage-collection function and options

int Lua::State::GC(int what, int data) {
    int res = 0;
    Lumen::GlobalState *g;
    LumenLock(L);
    g = LumenGlobal(L);
    switch (what) {
        case LUA_GCSTOP: {
            g->GCThreshold = Lumen::MaxUMemory;
            break;
        }
        case LUA_GCRESTART: {
            g->GCThreshold = g->TotalBytes;
            break;
        }
        case LUA_GCCOLLECT: {
            Lumen::GC::FullGC(L);
            break;
        }
        case LUA_GCCOUNT: {
            /* GC values are expressed in KBytes: #bytes/2^10 */
            res = cast_int(g->TotalBytes >> 10);
            break;
        }
        case LUA_GCCOUNTB: {
            res = cast_int(g->TotalBytes & 0x3ff);
            break;
        }
        case LUA_GCSTEP: {
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
        case LUA_GCSETPAUSE: {
            res = g->GCPause;
            g->GCPause = data;
            break;
        }
        case LUA_GCSETSTEPMUL: {
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

int Lua::State::Error() {
    LumenLock(L);
    apiCheckElementCount(L, 1);
    Lumen::Debug::ErrorMessage(L);
    LumenUnlock(L);
    return 0;  /* to avoid warnings */
}

int Lua::State::Next(int idx) {
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
    Lumen::Allocator f;
    LumenLock(L);
    if (ud) *ud = LumenGlobal(L)->ReAllocatorUData;
    f = LumenGlobal(L)->ReAllocator;
    LumenUnlock(L);
    return f;
}

void Lua::State::SetAllocator(Lua::Allocator f, void *ud) {
    LumenLock(L);
    LumenGlobal(L)->ReAllocatorUData = ud;
    LumenGlobal(L)->ReAllocator = f;
    LumenUnlock(L);
}

// MARK: Debug APIs

int Lua::State::GetStack(int level, Lua::DebugInfo *ar) {
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

int Lua::State::GetInfo(const char *what, Lua::DebugInfo *ar) {
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
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name)
        lua_PushObject(L, ci->Base + (n - 1));
    LumenUnlock(L);
    return name;
}

const char *Lua::State::SetLocal(const Lua::DebugInfo *ar, int n) {
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

int Lua::State::SetHook(Lua::Hook func, int mask, int count) {
    if (func == nullptr || mask == 0) {  /* turn off hooks? */
        mask = 0;
        func = nullptr;
    }
    L->Hook = reinterpret_cast<Lumen::Hook>(func);
    L->BaseHookCount = count;
    LumenDebugResetHookCount(L);
    L->HookMask = cast_byte(mask);
    return 1;
}

Lua::Hook Lua::State::GetHook() {
    return reinterpret_cast<Lua::Hook>(L->Hook);
}

int Lua::State::GetHookMask() {
    return L->HookMask;
}

int Lua::State::GetHookCount() {
    return L->BaseHookCount;
}

Lua::State *Lua::Open() {
    return Lua::State::New();
}

void Lua::Close(Lua::State *(&L)) {
    if (L != nullptr) {
        if (L->L != nullptr) {
            Lumen::State::Close(L->L);
            L->L = nullptr;
        }

        delete L;
        L = nullptr;
    }
}

void Lua::XMove(Lua::State *from, Lua::State *to, int n) {
    int i;
    if (from->L == to->L) return;
    LumenLock(to->L);
    apiCheckElementCount(from->L, n);
    LumenApiCheck(from->L, LumenGlobal(from->L) == LumenGlobal(to->L));
    LumenApiCheck(from->L, to->L->CallInfo->Top - to->L->Top >= n);
    from->L->Top -= n;
    for (i = 0; i < n; i++) {
        LumenSetObject2S(to->L, to->L->Top++, from->L->Top + i);
    }
    LumenUnlock(to->L);
}

void Lua::SetLevel(Lua::State *from, Lua::State *to) {
    to->L->NCCalls = from->L->NCCalls;
}
