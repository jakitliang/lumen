/*!
 * @brief Lua API
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
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

#include "luaconf.h"
#include "lua.h"

const char lua_ident[] =
    "$Lua: " LUA_RELEASE " " LUA_COPYRIGHT " $\n"
    "$Authors: " LUA_AUTHORS " $\n"
    "$URL: www.lua.org $\n";


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


void lua_PushObject(Lumen::State *L, const Lumen::Value *o) {
    LumenSetObject2S(L, L->Top, o);
    apiIncrTop(L);
}

LUA_API lua_State *lua_newstate(Lumen::Allocator f, void *ud) {
    return Lumen::State::New(f, ud);
}

LUA_API void lua_close(lua_State *L) {
    Lumen::State::Close(L);
}

LUA_API lua_CFunction lua_atpanic(lua_State *L, lua_CFunction fPanic) {
    lua_CFunction old;
    LumenLock(L);
    old = reinterpret_cast<lua_CFunction>(LumenGlobal(L)->Panic);
    LumenGlobal(L)->Panic = reinterpret_cast<Lumen::Delegate>(fPanic);
    LumenUnlock(L);
    return old;
}

LUA_API lua_State *lua_newthread(lua_State *L) {
    Lumen::State *L1;
    LumenLock(L);
    LumenGCCheckGC(L);
    L1 = Lumen::State::NewThread(L);
    LumenSetThreadValue(L, L->Top, L1);
    apiIncrTop(L);
    LumenUnlock(L);
    luai_userstatethread(L, L1);
    return L1;
}

LUA_API int lua_checkstack(lua_State *L, int size) {
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


LUA_API void lua_xmove(lua_State *from, lua_State *to, int n) {
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


LUA_API void lua_setlevel(lua_State *from, lua_State *to) {
    to->NCCalls = from->NCCalls;
}


/*
** basic stack manipulation
*/


LUA_API int lua_gettop(lua_State *L) {
    return cast_int(L->Top - L->Base);
}


LUA_API void lua_settop(lua_State *L, int idx) {
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


LUA_API void lua_remove(lua_State *L, int idx) {
    Lumen::StkId p;
    LumenLock(L);
    p = index2addr(L, idx);
    apiCheckValidIndex(L, p);
    while (++p < L->Top) LumenSetObjectS2S(L, p - 1, p);
    L->Top--;
    LumenUnlock(L);
}


LUA_API void lua_insert(lua_State *L, int idx) {
    Lumen::StkId p;
    Lumen::StkId q;
    LumenLock(L);
    p = index2addr(L, idx);
    apiCheckValidIndex(L, p);
    for (q = L->Top; q > p; q--) LumenSetObjectS2S(L, q, q - 1);
    LumenSetObjectS2S(L, p, L->Top);
    LumenUnlock(L);
}


LUA_API void lua_replace(lua_State *L, int idx) {
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


LUA_API void lua_pushvalue(lua_State *L, int idx) {
    LumenLock(L);
    LumenSetObject2S(L, L->Top, index2addr(L, idx));
    apiIncrTop(L);
    LumenUnlock(L);
}



/*
** access functions (stack -> C)
*/


LUA_API int lua_type(lua_State *L, int idx) {
    Lumen::StkId o = index2addr(L, idx);
    return (o == Lumen::NilObject) ? LUA_TNONE : LumenTypeOf(o);
}


LUA_API const char *lua_typename(lua_State *L, int t) {
    UNUSED(L);
    return (t == LUA_TNONE) ? "no value" : Lumen::TM::TypeNames[t];
}


LUA_API int lua_iscfunction(lua_State *L, int idx) {
    Lumen::StkId o = index2addr(L, idx);
    return LumenIsCFunction(o);
}


LUA_API int lua_isnumber(lua_State *L, int idx) {
    Lumen::Value n;
    const Lumen::Value *o = index2addr(L, idx);
    return LumenVMToNumber(o, &n);
}


LUA_API int lua_isstring(lua_State *L, int idx) {
    int t = lua_type(L, idx);
    return (t == LUA_TSTRING || t == LUA_TNUMBER);
}


LUA_API int lua_isuserdata(lua_State *L, int idx) {
    const Lumen::Value *o = index2addr(L, idx);
    return (LumenTypeIsUData(o) || LumenTypeIsLUData(o));
}


LUA_API int lua_rawequal(lua_State *L, int index1, int index2) {
    Lumen::StkId o1 = index2addr(L, index1);
    Lumen::StkId o2 = index2addr(L, index2);
    return (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0
                                                              : Lumen::RawEqualObject(o1, o2);
}


LUA_API int lua_equal(lua_State *L, int index1, int index2) {
    Lumen::StkId o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = index2addr(L, index1);
    o2 = index2addr(L, index2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0 : LumenVMEqualObj(L, o1, o2);
    LumenUnlock(L);
    return i;
}


LUA_API int lua_lessthan(lua_State *L, int index1, int index2) {
    Lumen::StkId o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = index2addr(L, index1);
    o2 = index2addr(L, index2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0
                                                           : Lumen::VM::LessThan(L, o1, o2);
    LumenUnlock(L);
    return i;
}



LUA_API Lumen::Number lua_tonumber(lua_State *L, int idx) {
    Lumen::Value n;
    const Lumen::Value *o = index2addr(L, idx);
    if (LumenVMToNumber(o, &n))
        return LumenNumberValue(o);
    else
        return 0;
}


LUA_API Lumen::Integer lua_tointeger(lua_State *L, int idx) {
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


LUA_API int lua_toboolean(lua_State *L, int idx) {
    const Lumen::Value *o = index2addr(L, idx);
    return !LumenIsFalse(o);
}


LUA_API const char *lua_tolstring(lua_State *L, int idx, size_t *len) {
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


LUA_API size_t lua_objlen(lua_State *L, int idx) {
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


LUA_API lua_CFunction lua_tocfunction(lua_State *L, int idx) {
    Lumen::StkId o = index2addr(L, idx);
    return (!LumenIsCFunction(o)) ? nullptr : reinterpret_cast<lua_CFunction>(LumenClosureValue(o)->AsC.Func);
}


LUA_API void *lua_touserdata(lua_State *L, int idx) {
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


LUA_API lua_State *lua_tothread(lua_State *L, int idx) {
    Lumen::StkId o = index2addr(L, idx);
    return (!LumenTypeIsThread(o)) ? nullptr : LumenThreadValue(o);
}


LUA_API const void *lua_topointer(lua_State *L, int idx) {
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



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushnil(lua_State *L) {
    LumenLock(L);
    LumenSetNilValue(L->Top);
    apiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_pushnumber(lua_State *L, Lumen::Number n) {
    LumenLock(L);
    LumenSetNumberValue(L->Top, n);
    apiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_pushinteger(lua_State *L, Lumen::Integer n) {
    LumenLock(L);
    LumenSetNumberValue(L->Top, cast_num(n));
    apiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_pushlstring(lua_State *L, const char *s, size_t len) {
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenSetStringValue2S(L, L->Top, Lumen::String::New(L, s, len));
    apiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_pushstring(lua_State *L, const char *s) {
    if (s == nullptr)
        lua_pushnil(L);
    else
        lua_pushlstring(L, s, Lumen::String::LengthOf(s));
}


LUA_API const char *lua_pushvfstring(lua_State *L, const char *fmt,
                                     va_list argP) {
    const char *ret;
    LumenLock(L);
    LumenGCCheckGC(L);
    ret = Lumen::PushVFString(L, fmt, argP);
    LumenUnlock(L);
    return ret;
}


LUA_API const char *lua_pushfstring(lua_State *L, const char *fmt, ...) {
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


LUA_API void lua_pushcclosure(lua_State *L, lua_CFunction fn, int n) {
    Lumen::Closure *cl;
    LumenLock(L);
    LumenGCCheckGC(L);
    apiCheckElementCount(L, n);
    cl = Lumen::CClosure::New(L, n, getCurEnv(L));
    cl->AsC.Func = reinterpret_cast<Lumen::Delegate>(fn);
    L->Top -= n;
    while (n--)
        LumenSetObject2N(L, &cl->AsC.UpValues[n], L->Top + n);
    LumenSetClosureValue(L, L->Top, cl);
    LumenAssert(LumenGCIsWhite(LumenObject2GCObject(cl)));
    apiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_pushboolean(lua_State *L, int b) {
    LumenLock(L);
    LumenSetBoolValue(L->Top, (b != 0));  /* ensure that true is 1 */
    apiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_pushlightuserdata(lua_State *L, void *p) {
    LumenLock(L);
    LumenSetLUDataValue(L->Top, p);
    apiIncrTop(L);
    LumenUnlock(L);
}


LUA_API int lua_pushthread(lua_State *L) {
    LumenLock(L);
    LumenSetThreadValue(L, L->Top, L);
    apiIncrTop(L);
    LumenUnlock(L);
    return (LumenGlobal(L)->MainThread == L);
}


/*
** get functions (Lua -> stack)
*/


LUA_API void lua_gettable(lua_State *L, int idx) {
    Lumen::StkId t;
    LumenLock(L);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    Lumen::VM::GetTable(L, t, L->Top - 1, L->Top - 1);
    LumenUnlock(L);
}


LUA_API void lua_getfield(lua_State *L, int idx, const char *k) {
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


LUA_API void lua_rawget(lua_State *L, int idx) {
    Lumen::StkId t;
    LumenLock(L);
    t = index2addr(L, idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    LumenSetObject2S(L, L->Top - 1, Lumen::Table::Get(LumenTableValue(t), L->Top - 1));
    LumenUnlock(L);
}


LUA_API void lua_rawgeti(lua_State *L, int idx, int n) {
    Lumen::StkId o;
    LumenLock(L);
    o = index2addr(L, idx);
    LumenApiCheck(L, LumenTypeIsTable(o));
    LumenSetObject2S(L, L->Top, Lumen::Table::GetNum(LumenTableValue(o), n));
    apiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_createtable(lua_State *L, int nArray, int nRec) {
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenSetTableValue(L, L->Top, Lumen::Table::New(L, nArray, nRec));
    apiIncrTop(L);
    LumenUnlock(L);
}


LUA_API int lua_getmetatable(lua_State *L, int objIndex) {
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


LUA_API void lua_getfenv(lua_State *L, int idx) {
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


/*
** set functions (stack -> Lua)
*/


LUA_API void lua_settable(lua_State *L, int idx) {
    Lumen::StkId t;
    LumenLock(L);
    apiCheckElementCount(L, 2);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    Lumen::VM::SetTable(L, t, L->Top - 2, L->Top - 1);
    L->Top -= 2;  /* pop index and value */
    LumenUnlock(L);
}


LUA_API void lua_setfield(lua_State *L, int idx, const char *k) {
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


LUA_API void lua_rawset(lua_State *L, int idx) {
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


LUA_API void lua_rawseti(lua_State *L, int idx, int n) {
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


LUA_API int lua_setmetatable(lua_State *L, int objIndex) {
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


LUA_API int lua_setfenv(lua_State *L, int idx) {
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


/*
** `load' and `call' functions (run Lua code)
*/


#define adjustResults(L, nRes) \
LumenDo(                         \
    if (nRes == LUA_MULTRET && L->Top >= L->CallInfo->Top) \
        L->CallInfo->Top = L->Top;   \
)


#define checkResults(L, na, nr) \
     LumenApiCheck(L, (nr) == LUA_MULTRET || (L->CallInfo->Top - L->Top >= (nr) - (na)))


LUA_API void lua_call(lua_State *L, int nargs, int nResults) {
    Lumen::StkId func;
    LumenLock(L);
    apiCheckElementCount(L, nargs + 1);
    checkResults(L, nargs, nResults);
    func = L->Top - (nargs + 1);
    Lumen::Do::Call(L, func, nResults);
    adjustResults(L, nResults);
    LumenUnlock(L);
}

LUA_API int lua_pcall(lua_State *L, int nargs, int nResults, int errFunc) {
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

LUA_API int lua_cpcall(lua_State *L, lua_CFunction func, void *ud) {
    Lumen::ProtectedCCall c;
    int status;
    LumenLock(L);
    c.Func = reinterpret_cast<Lumen::Delegate>(func);
    c.UData = ud;
    status = Lumen::Do::PCall(L,
                              &Lumen::ProtectedCCall::Call, &c,
                              LumenSaveStack(L, L->Top), 0);
    LumenUnlock(L);
    return status;
}


LUA_API int lua_load(lua_State *L, lua_Reader reader, void *data,
                     const char *chunkName) {
    Lumen::ZIO z;
    int status;
    LumenLock(L);
    if (!chunkName) chunkName = "?";
    Lumen::ZIO::Init(L, &z, reinterpret_cast<Lumen::Reader>(reader), data);
    status = Lumen::Do::ProtectedParser(L, &z, chunkName);
    LumenUnlock(L);
    return status;
}


LUA_API int lua_dump(lua_State *L, lua_Writer writer, void *data) {
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

// MARK: Coroutine

LUA_API int lua_resume(lua_State *L, int nArgs) {
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

LUA_API int lua_yield(lua_State *L, int nResults) {
    luai_userstateyield(L, nResults);
    LumenLock(L);
    if (L->NCCalls > L->BaseCCalls)
        Lumen::Debug::RunError(L, "attempt to yield across metaMethod/C-call boundary");
    L->Base = L->Top - nResults;  /* protect stack slots below */
    L->Status = LUA_YIELD;
    LumenUnlock(L);
    return -1;
}

LUA_API int lua_status(lua_State *L) {
    return L->Status;
}


/*
** Garbage-collection function
*/

LUA_API int lua_gc(lua_State *L, int what, int data) {
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
            /* GC values are expressed in Kbytes: #bytes/2^10 */
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



/*
** miscellaneous functions
*/


LUA_API int lua_error(lua_State *L) {
    LumenLock(L);
    apiCheckElementCount(L, 1);
    Lumen::Debug::ErrorMessage(L);
    LumenUnlock(L);
    return 0;  /* to avoid warnings */
}


LUA_API int lua_next(lua_State *L, int idx) {
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


LUA_API void lua_concat(lua_State *L, int n) {
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


LUA_API Lumen::Allocator lua_getallocf(lua_State *L, void **ud) {
    Lumen::Allocator f;
    LumenLock(L);
    if (ud) *ud = LumenGlobal(L)->ReAllocatorUData;
    f = LumenGlobal(L)->ReAllocator;
    LumenUnlock(L);
    return f;
}


LUA_API void lua_setallocf(lua_State *L, Lumen::Allocator f, void *ud) {
    LumenLock(L);
    LumenGlobal(L)->ReAllocatorUData = ud;
    LumenGlobal(L)->ReAllocator = f;
    LumenUnlock(L);
}


LUA_API void *lua_newuserdata(lua_State *L, size_t size) {
    Lumen::Userdata *u;
    LumenLock(L);
    LumenGCCheckGC(L);
    u = Lumen::Userdata::New(L, size, getCurEnv(L));
    LumenSetUDataValue(L, L->Top, u);
    apiIncrTop(L);
    LumenUnlock(L);
    return u + 1;
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

// MARK: Debug API

LUA_API const char *lua_getlocal(lua_State *L, const lua_Debug *ar, int n) {
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name)
        lua_PushObject(L, ci->Base + (n - 1));
    LumenUnlock(L);
    return name;
}


LUA_API const char *lua_setlocal(lua_State *L, const lua_Debug *ar, int n) {
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name)
        LumenSetObjectS2S (L, ci->Base + (n - 1), L->Top - 1);
    L->Top--;  /* pop value */
    LumenUnlock(L);
    return name;
}

LUA_API const char *lua_getupvalue(lua_State *L, int funcIndex, int n) {
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


LUA_API const char *lua_setupvalue(lua_State *L, int funcIndex, int n) {
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


/*
** this function can be called asynchronous (e.g. during a signal)
*/
LUA_API int lua_sethook(lua_State *L, lua_Hook func, int mask, int count) {
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


LUA_API lua_Hook lua_gethook(lua_State *L) {
    return reinterpret_cast<lua_Hook>(L->Hook);
}


LUA_API int lua_gethookmask(lua_State *L) {
    return L->HookMask;
}


LUA_API int lua_gethookcount(lua_State *L) {
    return L->BaseHookCount;
}


LUA_API int lua_getstack(lua_State *L, int level, lua_Debug *ar) {
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

LUA_API int lua_getinfo(lua_State *L, const char *what, lua_Debug *ar) {
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

LUA_API void *lua_upvalueid(lua_State *L, int idx, int n) {
    Lumen::StkId func = index2addr(L, idx);
    if (!LumenTypeIsFunction(func)) return nullptr;

    auto cl = LumenClosureValue(func);
    if (LumenIsCFunction(func)) {
        if (n <= 0 || n > cl->AsC.NUpValues)
            return nullptr;
        return &cl->AsC.UpValues[n - 1];
    } else {
        if (n <= 0 || n > cl->AsLua.NUpValues)
            return nullptr;
        return cl->AsLua.UpValues[n - 1];
    }
}

LUA_API void lua_upvaluejoin(lua_State *L, int idx1, int n1, int idx2, int n2) {
    auto o1 = index2addr(L, idx1);
    auto o2 = index2addr(L, idx2);
    LumenAssert(LumenTypeIsFunction(o1) && LumenTypeIsFunction(o2));
    auto cl1 = LumenClosureValue(o1);
    auto cl2 = LumenClosureValue(o2);
    LumenAssert(cl1->AsC.NUpValues >= n1 && cl2->AsC.NUpValues >= n2);
    if (LumenIsCFunction(o1) && LumenIsCFunction(o2)) {
        cl1->AsC.UpValues[n1 - 1] = cl2->AsC.UpValues[n2 - 1];
    } else if (LumenIsLFunction(o1) && LumenIsLFunction(o2)) {
        cl1->AsLua.UpValues[n1 - 1] = cl2->AsLua.UpValues[n2 - 1];
    } else {
        LumenAssert(0 && "mismatched function types in lua_upvaluejoin");
    }
}

LUA_API int lua_loadx(lua_State *L, lua_Reader reader, void *data,
                      const char *chunkName, const char *mode) {
    (void) mode;  /* Lua 5.1 Can't specify mode */
    return lua_load(L, reader, data, chunkName);
}

static const Lumen::Number lua_version_number = 5.1;

LUA_API const Lumen::Number *lua_version(lua_State *L) {
    (void) L;
    return &lua_version_number;
}

LUA_API int lua_absindex(lua_State *L, int i) {
    if (i < 0 && i > LUA_REGISTRYINDEX)
        i += lua_gettop(L) + 1;
    return i;
}

#define checkStack(L, i, ...) do { \
    if (!lua_checkstack(L, i))     \
        Lumen::Debug::RunError(L, __VA_ARGS__); \
} while (0)

LUA_API void lua_copy(lua_State *L, int fromIdx, int toIdx) {
    int absTo = lua_absindex(L, toIdx);
    checkStack(L, 1, "stack overflow (%s)", "not enough stack slots");
    lua_pushvalue(L, fromIdx);
    lua_replace(L, absTo);
}

LUA_API Lumen::Number lua_tonumberx(lua_State *L, int idx, int *isNum) {
    Lumen::Number n = lua_tonumber(L, idx);
    if (isNum) *isNum = (n != 0 || lua_isnumber(L, idx));
    return n;
}

LUA_API Lumen::Integer lua_tointegerx(lua_State *L, int idx, int *isNum) {
    Lumen::Integer n = lua_tointeger(L, idx);
    if (isNum) *isNum = (n != 0 || lua_isnumber(L, idx));
    return n;
}

LUA_API int lua_isyieldable(lua_State *L) {
    return (L->NCCalls == 0);
}
