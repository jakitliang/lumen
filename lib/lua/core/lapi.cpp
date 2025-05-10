/*!
 * @brief Lua API
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstring>

#define lapi_c
#define LUA_CORE

#include "lua.h"

#include "lua/api.h"
#include "lua/debug.h"
#include "lua/do.h"
#include "lua/gc.h"
#include "lua/mem.h"
#include "lua/object.h"
#include "lua/state.h"
#include "lua/string.h"
#include "lua/table.h"
#include "lua/tm.h"
#include "lua/undump.h"
#include "lua/vm.h"


const char lua_ident[] =
        "$Lua: " LUA_RELEASE " " LUA_COPYRIGHT " $\n"
        "$Authors: " LUA_AUTHORS " $\n"
        "$URL: www.lua.org $\n";


#define apiCheckElementCount(L, n)    LuaApiCheck(L, (n) <= (L->Top - L->Base))

#define apiCheckValidIndex(L, i)    LuaApiCheck(L, (i) != Lua::NilObject)

#define apiIncrTop(L) \
LuaDo(                \
    LuaApiCheck(L, L->Top < L->CallInfo->Top); \
    L->Top++;         \
)


static Lua::Value *index2addr(Lua::State *L, int idx) {
    if (idx > 0) {
        Lua::Value *o = L->Base + (idx - 1);
        LuaApiCheck(L, idx <= L->CallInfo->Top - L->Base);
        if (o >= L->Top) return cast(Lua::Value *, Lua::NilObject);
        else return o;
    } else if (idx > LUA_REGISTRYINDEX) {
        LuaApiCheck(L, idx != 0 && -idx <= L->Top - L->Base);
        return L->Top + idx;
    } else
        switch (idx) {  /* pseudo-indices */
            case LUA_REGISTRYINDEX:
                return LuaRegistry(L);
            case LUA_ENVIRONINDEX: {
                Lua::Closure *func = LuaCurFunc(L);
                LuaSetTableValue(L, &L->Env, func->AsC.Env);
                return &L->Env;
            }
            case LUA_GLOBALSINDEX:
                return LuaGlobalTable(L);
            default: {
                Lua::Closure *func = LuaCurFunc(L);
                idx = LUA_GLOBALSINDEX - idx;
                return (idx <= func->AsC.NUpValues)
                       ? &func->AsC.UpValues[idx - 1]
                       : cast(Lua::Value *, Lua::NilObject);
            }
        }
}


static Lua::Table *getCurEnv(Lua::State *L) {
    if (L->CallInfo == L->BaseCI)  /* no enclosing function? */
        return LuaTableValue(LuaGlobalTable(L));  /* use global table as environment */
    else {
        Lua::Closure *func = LuaCurFunc(L);
        return func->AsC.Env;
    }
}


void Lua::PushObject(Lua::State *L, const Lua::Value *o) {
    LuaSetObject2S(L, L->Top, o);
    apiIncrTop(L);
}


LUA_API int lua_checkstack(Lua::State *L, int size) {
    int res = 1;
    LuaLock(L);
    if (size > LUAI_MAXCSTACK || (L->Top - L->Base + size) > LUAI_MAXCSTACK)
        res = 0;  /* stack overflow */
    else if (size > 0) {
        LuaDoCheckStack(L, size);
        if (L->CallInfo->Top < L->Top + size)
            L->CallInfo->Top = L->Top + size;
    }
    LuaUnlock(L);
    return res;
}


LUA_API void lua_xmove(Lua::State *from, Lua::State *to, int n) {
    int i;
    if (from == to) return;
    LuaLock(to);
    apiCheckElementCount(from, n);
    LuaApiCheck(from, LuaGlobal(from) == LuaGlobal(to));
    LuaApiCheck(from, to->CallInfo->Top - to->Top >= n);
    from->Top -= n;
    for (i = 0; i < n; i++) {
        LuaSetObject2S(to, to->Top++, from->Top + i);
    }
    LuaUnlock(to);
}


LUA_API void lua_setlevel(Lua::State *from, Lua::State *to) {
    to->NCCalls = from->NCCalls;
}


LUA_API lua_CFunction lua_atpanic(Lua::State *L, lua_CFunction fPanic) {
    lua_CFunction old;
    LuaLock(L);
    old = LuaGlobal(L)->Panic;
    LuaGlobal(L)->Panic = fPanic;
    LuaUnlock(L);
    return old;
}


LUA_API Lua::State *lua_newthread(lua_State *L) {
    lua_State *L1;
    LuaLock(L);
    LuaGCCheckGC(L);
    L1 = Lua::State::NewThread(L);
    LuaSetThreadValue(L, L->Top, L1);
    apiIncrTop(L);
    LuaUnlock(L);
    luai_userstatethread(L, L1);
    return L1;
}



/*
** basic stack manipulation
*/


LUA_API int lua_gettop(lua_State *L) {
    return cast_int(L->Top - L->Base);
}


LUA_API void lua_settop(lua_State *L, int idx) {
    LuaLock(L);
    if (idx >= 0) {
        LuaApiCheck(L, idx <= L->StackLast - L->Base);
        while (L->Top < L->Base + idx)
            LuaSetNilValue(L->Top++);
        L->Top = L->Base + idx;
    } else {
        LuaApiCheck(L, -(idx + 1) <= (L->Top - L->Base));
        L->Top += idx + 1;  /* `subtract' index (index is negative) */
    }
    LuaUnlock(L);
}


LUA_API void lua_remove(lua_State *L, int idx) {
    Lua::StkId p;
    LuaLock(L);
    p = index2addr(L, idx);
    apiCheckValidIndex(L, p);
    while (++p < L->Top) LuaSetObjectS2S(L, p - 1, p);
    L->Top--;
    LuaUnlock(L);
}


LUA_API void lua_insert(lua_State *L, int idx) {
    Lua::StkId p;
    Lua::StkId q;
    LuaLock(L);
    p = index2addr(L, idx);
    apiCheckValidIndex(L, p);
    for (q = L->Top; q > p; q--) LuaSetObjectS2S(L, q, q - 1);
    LuaSetObjectS2S(L, p, L->Top);
    LuaUnlock(L);
}


LUA_API void lua_replace(lua_State *L, int idx) {
    Lua::StkId o;
    LuaLock(L);
    /* explicit test for incompatible code */
    if (idx == LUA_ENVIRONINDEX && L->CallInfo == L->BaseCI)
        Lua::Debug::RunError(L, "no calling environment");
    apiCheckElementCount(L, 1);
    o = index2addr(L, idx);
    apiCheckValidIndex(L, o);
    if (idx == LUA_ENVIRONINDEX) {
        Lua::Closure *func = LuaCurFunc(L);
        LuaApiCheck(L, LuaTypeIsTable(L->Top - 1));
        func->AsC.Env = LuaTableValue(L->Top - 1);
        LuaGCBarrier(L, func, L->Top - 1);
    } else {
        LuaSetObject(L, o, L->Top - 1);
        if (idx < LUA_GLOBALSINDEX)  /* function upvalue? */
            LuaGCBarrier(L, LuaCurFunc(L), L->Top - 1);
    }
    L->Top--;
    LuaUnlock(L);
}


LUA_API void lua_pushvalue(lua_State *L, int idx) {
    LuaLock(L);
    LuaSetObject2S(L, L->Top, index2addr(L, idx));
    apiIncrTop(L);
    LuaUnlock(L);
}



/*
** access functions (stack -> C)
*/


LUA_API int lua_type(lua_State *L, int idx) {
    Lua::StkId o = index2addr(L, idx);
    return (o == Lua::NilObject) ? LUA_TNONE : LuaTypeOf(o);
}


LUA_API const char *lua_typename(lua_State *L, int t) {
    UNUSED(L);
    return (t == LUA_TNONE) ? "no value" : Lua::TM::TypeNames[t];
}


LUA_API int lua_iscfunction(lua_State *L, int idx) {
    Lua::StkId o = index2addr(L, idx);
    return LuaIsCFunction(o);
}


LUA_API int lua_isnumber(lua_State *L, int idx) {
    Lua::Value n;
    const Lua::Value *o = index2addr(L, idx);
    return LuaVMToNumber(o, &n);
}


LUA_API int lua_isstring(lua_State *L, int idx) {
    int t = lua_type(L, idx);
    return (t == LUA_TSTRING || t == LUA_TNUMBER);
}


LUA_API int lua_isuserdata(lua_State *L, int idx) {
    const Lua::Value *o = index2addr(L, idx);
    return (LuaTypeIsUData(o) || LuaTypeIsLUData(o));
}


LUA_API int lua_rawequal(lua_State *L, int index1, int index2) {
    Lua::StkId o1 = index2addr(L, index1);
    Lua::StkId o2 = index2addr(L, index2);
    return (o1 == Lua::NilObject || o2 == Lua::NilObject) ? 0
                                                          : Lua::RawEqualObject(o1, o2);
}


LUA_API int lua_equal(lua_State *L, int index1, int index2) {
    Lua::StkId o1, o2;
    int i;
    LuaLock(L);  /* may call tag method */
    o1 = index2addr(L, index1);
    o2 = index2addr(L, index2);
    i = (o1 == Lua::NilObject || o2 == Lua::NilObject) ? 0 : LuaVMEqualObj(L, o1, o2);
    LuaUnlock(L);
    return i;
}


LUA_API int lua_lessthan(lua_State *L, int index1, int index2) {
    Lua::StkId o1, o2;
    int i;
    LuaLock(L);  /* may call tag method */
    o1 = index2addr(L, index1);
    o2 = index2addr(L, index2);
    i = (o1 == Lua::NilObject || o2 == Lua::NilObject) ? 0
                                                       : Lua::VM::LessThan(L, o1, o2);
    LuaUnlock(L);
    return i;
}



LUA_API Lua::Number lua_tonumber(lua_State *L, int idx) {
    Lua::Value n;
    const Lua::Value *o = index2addr(L, idx);
    if (LuaVMToNumber(o, &n))
        return LuaNumberValue(o);
    else
        return 0;
}


LUA_API lua_Integer lua_tointeger(lua_State *L, int idx) {
    Lua::Value n;
    const Lua::Value *o = index2addr(L, idx);
    if (LuaVMToNumber(o, &n)) {
        lua_Integer res;
        Lua::Number num = LuaNumberValue(o);
        lua_number2integer(res, num);
        return res;
    } else
        return 0;
}


LUA_API int lua_toboolean(lua_State *L, int idx) {
    const Lua::Value *o = index2addr(L, idx);
    return !LuaIsFalse(o);
}


LUA_API const char *lua_tolstring(lua_State *L, int idx, size_t *len) {
    Lua::StkId o = index2addr(L, idx);
    if (!LuaTypeIsString(o)) {
        LuaLock(L);  /* `Lua::VM::ToString' may create a new string */
        if (!Lua::VM::ToString(L, o)) {  /* conversion failed? */
            if (len != nullptr) *len = 0;
            LuaUnlock(L);
            return nullptr;
        }
        LuaGCCheckGC(L);
        o = index2addr(L, idx);  /* previous call may reallocate the stack */
        LuaUnlock(L);
    }
    if (len != nullptr) *len = LuaStringValue(o)->Length;
    return LuaStringValue2CString(o);
}


LUA_API size_t lua_objlen(lua_State *L, int idx) {
    Lua::StkId o = index2addr(L, idx);
    switch (LuaTypeOf(o)) {
        case LUA_TSTRING:
            return LuaStringValue(o)->Length;
        case LUA_TUSERDATA:
            return LuaUDataValue(o)->Length;
        case LUA_TTABLE:
            return Lua::Table::GetN(LuaTableValue(o));
        case LUA_TNUMBER: {
            size_t l;
            LuaLock(L);  /* `Lua::VM::ToString' may create a new string */
            l = (Lua::VM::ToString(L, o) ? LuaStringValue(o)->Length : 0);
            LuaUnlock(L);
            return l;
        }
        default:
            return 0;
    }
}


LUA_API lua_CFunction lua_tocfunction(lua_State *L, int idx) {
    Lua::StkId o = index2addr(L, idx);
    return (!LuaIsCFunction(o)) ? nullptr : LuaClosureValue(o)->AsC.Func;
}


LUA_API void *lua_touserdata(lua_State *L, int idx) {
    Lua::StkId o = index2addr(L, idx);
    switch (LuaTypeOf(o)) {
        case LUA_TUSERDATA:
            return (LuaUDataValue(o) + 1);
        case LUA_TLIGHTUSERDATA:
            return LuaLUDataValue(o);
        default:
            return nullptr;
    }
}


LUA_API lua_State *lua_tothread(lua_State *L, int idx) {
    Lua::StkId o = index2addr(L, idx);
    return (!LuaTypeIsThread(o)) ? nullptr : LuaThreadValue(o);
}


LUA_API const void *lua_topointer(lua_State *L, int idx) {
    Lua::StkId o = index2addr(L, idx);
    switch (LuaTypeOf(o)) {
        case LUA_TTABLE:
            return LuaTableValue(o);
        case LUA_TFUNCTION:
            return LuaClosureValue(o);
        case LUA_TTHREAD:
            return LuaThreadValue(o);
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
    LuaLock(L);
    LuaSetNilValue(L->Top);
    apiIncrTop(L);
    LuaUnlock(L);
}


LUA_API void lua_pushnumber(lua_State *L, Lua::Number n) {
    LuaLock(L);
    LuaSetNumberValue(L->Top, n);
    apiIncrTop(L);
    LuaUnlock(L);
}


LUA_API void lua_pushinteger(lua_State *L, lua_Integer n) {
    LuaLock(L);
    LuaSetNumberValue(L->Top, cast_num(n));
    apiIncrTop(L);
    LuaUnlock(L);
}


LUA_API void lua_pushlstring(lua_State *L, const char *s, size_t len) {
    LuaLock(L);
    LuaGCCheckGC(L);
    LuaSetStringValue2S(L, L->Top, Lua::String::New(L, s, len));
    apiIncrTop(L);
    LuaUnlock(L);
}


LUA_API void lua_pushstring(lua_State *L, const char *s) {
    if (s == nullptr)
        lua_pushnil(L);
    else
        lua_pushlstring(L, s, strlen(s));
}


LUA_API const char *lua_pushvfstring(lua_State *L, const char *fmt,
                                     va_list argp) {
    const char *ret;
    LuaLock(L);
    LuaGCCheckGC(L);
    ret = Lua::PushVFString(L, fmt, argp);
    LuaUnlock(L);
    return ret;
}


LUA_API const char *lua_pushfstring(lua_State *L, const char *fmt, ...) {
    const char *ret;
    va_list argp;
    LuaLock(L);
    LuaGCCheckGC(L);
            va_start(argp, fmt);
    ret = Lua::PushVFString(L, fmt, argp);
            va_end(argp);
    LuaUnlock(L);
    return ret;
}


LUA_API void lua_pushcclosure(lua_State *L, lua_CFunction fn, int n) {
    Lua::Closure *cl;
    LuaLock(L);
    LuaGCCheckGC(L);
    apiCheckElementCount(L, n);
    cl = Lua::CClosure::New(L, n, getCurEnv(L));
    cl->AsC.Func = fn;
    L->Top -= n;
    while (n--)
        LuaSetObject2N(L, &cl->AsC.UpValues[n], L->Top + n);
    LuaSetClosureValue(L, L->Top, cl);
    lua_assert(LuaGCIsWhite(LuaObject2GCObject(cl)));
    apiIncrTop(L);
    LuaUnlock(L);
}


LUA_API void lua_pushboolean(lua_State *L, int b) {
    LuaLock(L);
    LuaSetBoolValue(L->Top, (b != 0));  /* ensure that true is 1 */
    apiIncrTop(L);
    LuaUnlock(L);
}


LUA_API void lua_pushlightuserdata(lua_State *L, void *p) {
    LuaLock(L);
    LuaSetLUDataValue(L->Top, p);
    apiIncrTop(L);
    LuaUnlock(L);
}


LUA_API int lua_pushthread(lua_State *L) {
    LuaLock(L);
    LuaSetThreadValue(L, L->Top, L);
    apiIncrTop(L);
    LuaUnlock(L);
    return (LuaGlobal(L)->MainThread == L);
}



/*
** get functions (Lua -> stack)
*/


LUA_API void lua_gettable(lua_State *L, int idx) {
    Lua::StkId t;
    LuaLock(L);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    Lua::VM::GetTable(L, t, L->Top - 1, L->Top - 1);
    LuaUnlock(L);
}


LUA_API void lua_getfield(lua_State *L, int idx, const char *k) {
    Lua::StkId t;
    Lua::Value key;
    LuaLock(L);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    LuaSetStringValue(L, &key, Lua::String::New(L, k));
    Lua::VM::GetTable(L, t, &key, L->Top);
    apiIncrTop(L);
    LuaUnlock(L);
}


LUA_API void lua_rawget(lua_State *L, int idx) {
    Lua::StkId t;
    LuaLock(L);
    t = index2addr(L, idx);
    LuaApiCheck(L, LuaTypeIsTable(t));
    LuaSetObject2S(L, L->Top - 1, Lua::Table::Get(LuaTableValue(t), L->Top - 1));
    LuaUnlock(L);
}


LUA_API void lua_rawgeti(lua_State *L, int idx, int n) {
    Lua::StkId o;
    LuaLock(L);
    o = index2addr(L, idx);
    LuaApiCheck(L, LuaTypeIsTable(o));
    LuaSetObject2S(L, L->Top, Lua::Table::GetNum(LuaTableValue(o), n));
    apiIncrTop(L);
    LuaUnlock(L);
}


LUA_API void lua_createtable(lua_State *L, int narray, int nrec) {
    LuaLock(L);
    LuaGCCheckGC(L);
    LuaSetTableValue(L, L->Top, Lua::Table::New(L, narray, nrec));
    apiIncrTop(L);
    LuaUnlock(L);
}


LUA_API int lua_getmetatable(lua_State *L, int objindex) {
    const Lua::Value *obj;
    Lua::Table *mt = nullptr;
    int res;
    LuaLock(L);
    obj = index2addr(L, objindex);
    switch (LuaTypeOf(obj)) {
        case LUA_TTABLE:
            mt = LuaTableValue(obj)->Metatable;
            break;
        case LUA_TUSERDATA:
            mt = LuaUDataValue(obj)->Metatable;
            break;
        default:
            mt = LuaGlobal(L)->Metatable[LuaTypeOf(obj)];
            break;
    }
    if (mt == nullptr)
        res = 0;
    else {
        LuaSetTableValue(L, L->Top, mt);
        apiIncrTop(L);
        res = 1;
    }
    LuaUnlock(L);
    return res;
}


LUA_API void lua_getfenv(lua_State *L, int idx) {
    Lua::StkId o;
    LuaLock(L);
    o = index2addr(L, idx);
    apiCheckValidIndex(L, o);
    switch (LuaTypeOf(o)) {
        case LUA_TFUNCTION:
            LuaSetTableValue(L, L->Top, LuaClosureValue(o)->AsC.Env);
            break;
        case LUA_TUSERDATA:
            LuaSetTableValue(L, L->Top, LuaUDataValue(o)->Env);
            break;
        case LUA_TTHREAD:
            LuaSetObject2S(L, L->Top, LuaGlobalTable(LuaThreadValue(o)));
            break;
        default:
            LuaSetNilValue(L->Top);
            break;
    }
    apiIncrTop(L);
    LuaUnlock(L);
}


/*
** set functions (stack -> Lua)
*/


LUA_API void lua_settable(lua_State *L, int idx) {
    Lua::StkId t;
    LuaLock(L);
    apiCheckElementCount(L, 2);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    Lua::VM::SetTable(L, t, L->Top - 2, L->Top - 1);
    L->Top -= 2;  /* pop index and value */
    LuaUnlock(L);
}


LUA_API void lua_setfield(lua_State *L, int idx, const char *k) {
    Lua::StkId t;
    Lua::Value key;
    LuaLock(L);
    apiCheckElementCount(L, 1);
    t = index2addr(L, idx);
    apiCheckValidIndex(L, t);
    LuaSetStringValue(L, &key, Lua::String::New(L, k));
    Lua::VM::SetTable(L, t, &key, L->Top - 1);
    L->Top--;  /* pop value */
    LuaUnlock(L);
}


LUA_API void lua_rawset(lua_State *L, int idx) {
    Lua::StkId t;
    LuaLock(L);
    apiCheckElementCount(L, 2);
    t = index2addr(L, idx);
    LuaApiCheck(L, LuaTypeIsTable(t));
    LuaSetObject2T(L, Lua::Table::Set(L, LuaTableValue(t), L->Top - 2), L->Top - 1);
    LuaGCBarrierTable(L, LuaTableValue(t), L->Top - 1);
    L->Top -= 2;
    LuaUnlock(L);
}


LUA_API void lua_rawseti(lua_State *L, int idx, int n) {
    Lua::StkId o;
    LuaLock(L);
    apiCheckElementCount(L, 1);
    o = index2addr(L, idx);
    LuaApiCheck(L, LuaTypeIsTable(o));
    LuaSetObject2T(L, Lua::Table::SetNum(L, LuaTableValue(o), n), L->Top - 1);
    LuaGCBarrierTable(L, LuaTableValue(o), L->Top - 1);
    L->Top--;
    LuaUnlock(L);
}


LUA_API int lua_setmetatable(lua_State *L, int objindex) {
    Lua::Value *obj;
    Lua::Table *mt;
    LuaLock(L);
    apiCheckElementCount(L, 1);
    obj = index2addr(L, objindex);
    apiCheckValidIndex(L, obj);
    if (LuaTypeIsNil(L->Top - 1))
        mt = nullptr;
    else {
        LuaApiCheck(L, LuaTypeIsTable(L->Top - 1));
        mt = LuaTableValue(L->Top - 1);
    }
    switch (LuaTypeOf(obj)) {
        case LUA_TTABLE: {
            LuaTableValue(obj)->Metatable = mt;
            if (mt)
                LuaGCObjectBarrierTable(L, LuaTableValue(obj), mt);
            break;
        }
        case LUA_TUSERDATA: {
            LuaUDataValue(obj)->Metatable = mt;
            if (mt)
                LuaGCObjectBarrier(L, LuaUDataValue(obj), mt);
            break;
        }
        default: {
            LuaGlobal(L)->Metatable[LuaTypeOf(obj)] = mt;
            break;
        }
    }
    L->Top--;
    LuaUnlock(L);
    return 1;
}


LUA_API int lua_setfenv(lua_State *L, int idx) {
    Lua::StkId o;
    int res = 1;
    LuaLock(L);
    apiCheckElementCount(L, 1);
    o = index2addr(L, idx);
    apiCheckValidIndex(L, o);
    LuaApiCheck(L, LuaTypeIsTable(L->Top - 1));
    switch (LuaTypeOf(o)) {
        case LUA_TFUNCTION:
            LuaClosureValue(o)->AsC.Env = LuaTableValue(L->Top - 1);
            break;
        case LUA_TUSERDATA:
            LuaUDataValue(o)->Env = LuaTableValue(L->Top - 1);
            break;
        case LUA_TTHREAD:
            LuaSetTableValue(L, LuaGlobalTable(LuaThreadValue(o)), LuaTableValue(L->Top - 1));
            break;
        default:
            res = 0;
            break;
    }
    if (res) LuaGCObjectBarrier(L, LuaGCValue(o), LuaTableValue(L->Top - 1));
    L->Top--;
    LuaUnlock(L);
    return res;
}


/*
** `load' and `call' functions (run Lua code)
*/


#define adjustResults(L, nRes) \
LuaDo(                         \
    if (nRes == LUA_MULTRET && L->Top >= L->CallInfo->Top) \
        L->CallInfo->Top = L->Top;   \
)


#define checkResults(L, na, nr) \
     LuaApiCheck(L, (nr) == LUA_MULTRET || (L->CallInfo->Top - L->Top >= (nr) - (na)))


LUA_API void lua_call(lua_State *L, int nargs, int nResults) {
    Lua::StkId func;
    LuaLock(L);
    apiCheckElementCount(L, nargs + 1);
    checkResults(L, nargs, nResults);
    func = L->Top - (nargs + 1);
    Lua::Do::Call(L, func, nResults);
    adjustResults(L, nResults);
    LuaUnlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to `f_call' */
    Lua::StkId Func;
    int NResults;
};


static void f_call(lua_State *L, void *ud) {
    struct CallS *c = cast(struct CallS *, ud);
    Lua::Do::Call(L, c->Func, c->NResults);
}


LUA_API int lua_pcall(lua_State *L, int nargs, int nResults, int errfunc) {
    CallS c;
    int status;
    ptrdiff_t func;
    LuaLock(L);
    apiCheckElementCount(L, nargs + 1);
    checkResults(L, nargs, nResults);
    if (errfunc == 0)
        func = 0;
    else {
        Lua::StkId o = index2addr(L, errfunc);
        apiCheckValidIndex(L, o);
        func = LuaSaveStack(L, o);
    }
    c.Func = L->Top - (nargs + 1);  /* function to be called */
    c.NResults = nResults;
    status = Lua::Do::PCall(L, f_call, &c, LuaSaveStack(L, c.Func), func);
    adjustResults(L, nResults);
    LuaUnlock(L);
    return status;
}


/*
** Execute a protected C call.
*/
struct CCallS {  /* data to `f_Ccall' */
    lua_CFunction func;
    void *ud;
};


static void f_Ccall(lua_State *L, void *ud) {
    CCallS *c = cast(struct CCallS *, ud);
    Lua::Closure *cl;
    cl = Lua::CClosure::New(L, 0, getCurEnv(L));
    cl->AsC.Func = c->func;
    LuaSetClosureValue(L, L->Top, cl);  /* push function */
    apiIncrTop(L);
    LuaSetLUDataValue(L->Top, c->ud);  /* push only argument */
    apiIncrTop(L);
    Lua::Do::Call(L, L->Top - 2, 0);
}


LUA_API int lua_cpcall(lua_State *L, lua_CFunction func, void *ud) {
    CCallS c;
    int status;
    LuaLock(L);
    c.func = func;
    c.ud = ud;
    status = Lua::Do::PCall(L, f_Ccall, &c, LuaSaveStack(L, L->Top), 0);
    LuaUnlock(L);
    return status;
}


LUA_API int lua_load(lua_State *L, Lua::Reader reader, void *data,
                     const char *chunkname) {
    Lua::ZIO z;
    int status;
    LuaLock(L);
    if (!chunkname) chunkname = "?";
    Lua::ZIO::Init(L, &z, reader, data);
    status = Lua::Do::ProtectedParser(L, &z, chunkname);
    LuaUnlock(L);
    return status;
}


LUA_API int lua_dump(lua_State *L, Lua::Writer writer, void *data) {
    int status;
    Lua::Value *o;
    LuaLock(L);
    apiCheckElementCount(L, 1);
    o = L->Top - 1;
    if (LuaIsLFunction(o))
        status = Lua::Dumper::Dump(L, LuaClosureValue(o)->AsLua.Func, writer, data, 0);
    else
        status = 1;
    LuaUnlock(L);
    return status;
}


LUA_API int lua_status(lua_State *L) {
    return L->Status;
}


/*
** Garbage-collection function
*/

LUA_API int lua_gc(lua_State *L, int what, int data) {
    int res = 0;
    Lua::GlobalState *g;
    LuaLock(L);
    g = LuaGlobal(L);
    switch (what) {
        case LUA_GCSTOP: {
            g->GCThreshold = Lua::MaxUMemory;
            break;
        }
        case LUA_GCRESTART: {
            g->GCThreshold = g->TotalBytes;
            break;
        }
        case LUA_GCCOLLECT: {
            Lua::GC::FullGC(L);
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
            Lua::MemorySize a = (cast(Lua::MemorySize, data) << 10);
            if (a <= g->TotalBytes)
                g->GCThreshold = g->TotalBytes - a;
            else
                g->GCThreshold = 0;
            while (g->GCThreshold <= g->TotalBytes) {
                Lua::GC::Step(L);
                if (g->GCState == Lua::GC::StatePause) {  /* end of cycle? */
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
    LuaUnlock(L);
    return res;
}



/*
** miscellaneous functions
*/


LUA_API int lua_error(lua_State *L) {
    LuaLock(L);
    apiCheckElementCount(L, 1);
    Lua::Debug::ErrorMessage(L);
    LuaUnlock(L);
    return 0;  /* to avoid warnings */
}


LUA_API int lua_next(lua_State *L, int idx) {
    Lua::StkId t;
    int more;
    LuaLock(L);
    t = index2addr(L, idx);
    LuaApiCheck(L, LuaTypeIsTable(t));
    more = Lua::Table::Next(L, LuaTableValue(t), L->Top - 1);
    if (more) {
        apiIncrTop(L);
    } else  /* no more elements */
        L->Top -= 1;  /* remove key */
    LuaUnlock(L);
    return more;
}


LUA_API void lua_concat(lua_State *L, int n) {
    LuaLock(L);
    apiCheckElementCount(L, n);
    if (n >= 2) {
        LuaGCCheckGC(L);
        Lua::VM::Concat(L, n, cast_int(L->Top - L->Base) - 1);
        L->Top -= (n - 1);
    } else if (n == 0) {  /* push empty string */
        LuaSetStringValue2S(L, L->Top, Lua::String::New(L, "", 0));
        apiIncrTop(L);
    }
    /* else n == 1; nothing to do */
    LuaUnlock(L);
}


LUA_API lua_Alloc lua_getallocf(lua_State *L, void **ud) {
    lua_Alloc f;
    LuaLock(L);
    if (ud) *ud = LuaGlobal(L)->ReAllocatorUData;
    f = LuaGlobal(L)->ReAllocator;
    LuaUnlock(L);
    return f;
}


LUA_API void lua_setallocf(lua_State *L, lua_Alloc f, void *ud) {
    LuaLock(L);
    LuaGlobal(L)->ReAllocatorUData = ud;
    LuaGlobal(L)->ReAllocator = f;
    LuaUnlock(L);
}


LUA_API void *lua_newuserdata(lua_State *L, size_t size) {
    Lua::Userdata *u;
    LuaLock(L);
    LuaGCCheckGC(L);
    u = Lua::Userdata::New(L, size, getCurEnv(L));
    LuaSetUDataValue(L, L->Top, u);
    apiIncrTop(L);
    LuaUnlock(L);
    return u + 1;
}


static const char *aux_upvalue(Lua::StkId fi, int n, Lua::Value **val) {
    Lua::Closure *f;
    if (!LuaTypeIsFunction(fi)) return nullptr;
    f = LuaClosureValue(fi);
    if (f->AsC.IsC) {
        if (!(1 <= n && n <= f->AsC.NUpValues)) return nullptr;
        *val = &f->AsC.UpValues[n - 1];
        return "";
    } else {
        Lua::Proto *p = f->AsLua.Func;
        if (!(1 <= n && n <= p->UpValuesCount)) return nullptr;
        *val = f->AsLua.UpValues[n - 1]->SelfValue;
        return LuaStringCString(p->UpValues[n - 1]);
    }
}


LUA_API const char *lua_getupvalue(lua_State *L, int funcindex, int n) {
    const char *name;
    Lua::Value *val;
    LuaLock(L);
    name = aux_upvalue(index2addr(L, funcindex), n, &val);
    if (name) {
        LuaSetObject2S(L, L->Top, val);
        apiIncrTop(L);
    }
    LuaUnlock(L);
    return name;
}


LUA_API const char *lua_setupvalue(lua_State *L, int funcindex, int n) {
    const char *name;
    Lua::Value *val;
    Lua::StkId fi;
    LuaLock(L);
    fi = index2addr(L, funcindex);
    apiCheckElementCount(L, 1);
    name = aux_upvalue(fi, n, &val);
    if (name) {
        L->Top--;
        LuaSetObject(L, val, L->Top);
        LuaGCBarrier(L, LuaClosureValue(fi), L->Top);
    }
    LuaUnlock(L);
    return name;
}

