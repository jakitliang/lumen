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
#include "lumen/api.h"
#include "lumen/version.h"

#include "luaconf.h"
#include "lua.h"

#define LumenApiCheckStack(L, i, ...) do { \
    if (!lua_checkstack(L, i))     \
        Lumen::Debug::RunError(L, __VA_ARGS__); \
} while (0)

const char lua_ident[] =
    "$Lua: " LUA_RELEASE " " LUMEN_COPYRIGHT " $\n"
    "$Authors: " LUMEN_AUTHORS " $\n"
    "$URL: www.lua.org $\n";

LUA_API lua_State *lua_newstate(Lumen::Allocator f, void *ud) {
    return Lumen::State::New(f, ud);
}

LUA_API void lua_close(lua_State *L) {
    Lumen::State::Close(L);
}

LUA_API lua_CFunction lua_atpanic(lua_State *L, lua_CFunction fPanic) {
    lua_CFunction old;
    LumenLock(L);
    old = reinterpret_cast<lua_CFunction>(LumenGlobalState(L)->Panic);
    LumenGlobalState(L)->Panic = reinterpret_cast<Lumen::Delegate>(fPanic);
    LumenUnlock(L);
    return old;
}

LUA_API const Lumen::Number *lua_version(lua_State *) {
    static const Lumen::Number lua_version_number = LUA_VERSION_NUM;
    return &lua_version_number;
}

LUA_API lua_State *lua_newthread(lua_State *L) {
    Lumen::State *L1;
    LumenLock(L);
    LumenGCCheckGC(L);
    L1 = Lumen::State::NewThread(L);
    LumenSetThreadValue(L, L->Top, L1);
    LumenApiIncrTop(L);
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
    LumenApiCheckElementCount(from, n);
    LumenApiCheck(from, LumenGlobalState(from) == LumenGlobalState(to));
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

LUA_API int lua_absindex(lua_State *L, int idx) {
    return (idx > 0 || LumenApiIsPseudo(idx))
           ? idx
           : cast_int((L->Top - L->Base) + 1 + idx);
}

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
    Lumen::Value p;
    LumenLock(L);
    p = L->ToObject(idx);
    LumenApiCheckValidIndex(L, p);
    while (++p < L->Top) LumenSetObjectS2S(L, p - 1, p);
    L->Top--;
    LumenUnlock(L);
}


LUA_API void lua_insert(lua_State *L, int idx) {
    Lumen::Value p;
    Lumen::Value q;
    LumenLock(L);
    p = L->ToObject(idx);
    LumenApiCheckValidIndex(L, p);
    for (q = L->Top; q > p; q--) LumenSetObjectS2S(L, q, q - 1);
    LumenSetObjectS2S(L, p, L->Top);
    LumenUnlock(L);
}

static void moveTo(lua_State *L, Lumen::Object *from, int idx) {
    Lumen::Object *to = L->ToObject(idx);
    LumenApiCheckValidIndex(L, to);
    if (idx == LUA_ENVIRONINDEX) {
        Lumen::Closure *func = LumenCurFunc(L);
        LumenApiCheck(L, LumenTypeIsTable(from));
        func->AsC.Env = LumenTableValue(from);
        LumenGCBarrier(L, func, from);
    } else {
        LumenSetObject(L, to, from);
        if (idx < LUA_GLOBALSINDEX)  /* function upvalue? */
            LumenGCBarrier(L, LumenCurFunc(L), from);
    }
}

LUA_API void lua_replace(lua_State *L, int idx) {
    LumenLock(L);
    /* explicit test for incompatible code */
    if (idx == LUA_ENVIRONINDEX && L->CallInfo == L->BaseCI)
        Lumen::Debug::RunError(L, "no calling environment");
    LumenApiCheckElementCount(L, 1);
    moveTo(L, L->Top - 1, idx);
    L->Top--;
    LumenUnlock(L);
}

static void compatReverse(lua_State *L, int a, int b) {
    for (; a < b; ++a, --b) {
        lua_pushvalue(L, a);
        lua_pushvalue(L, b);
        lua_replace(L, a);
        lua_replace(L, b);
    }
}

LUA_API void lua_rotate(lua_State *L, int idx, int n) {
    int n_elems = 0;
    idx = lua_absindex(L, idx);
    n_elems = lua_gettop(L) - idx + 1;
    if (n < 0)
        n += n_elems;
    if (n > 0 && n < n_elems) {
        if (!lua_checkstack(L, 2)) {
            lua_pushstring(L, "not enough stack slots available");
            lua_error(L);
        }
        n = n_elems - n;
        compatReverse(L, idx, idx + n - 1);
        compatReverse(L, idx + n, idx + n_elems - 1);
        compatReverse(L, idx, idx + n_elems - 1);
    }
}

LUA_API void lua_copy(lua_State *L, int fromIdx, int toIdx) {
    Lumen::Object *from;
    LumenLock(L);
    if (toIdx == LUA_ENVIRONINDEX && L->CallInfo == L->BaseCI)
        Lumen::Debug::RunError(L, "no calling environment");
    from = L->ToObject(fromIdx);
    moveTo(L, from, toIdx);
    LumenUnlock(L);
}

LUA_API void lua_pushvalue(lua_State *L, int idx) {
    LumenLock(L);
    LumenSetObject2S(L, L->Top, L->ToObject(idx));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}



/*
** access functions (stack -> C)
*/


LUA_API int lua_type(lua_State *L, int idx) {
    Lumen::Value o = L->ToObject(idx);
    return (o == Lumen::NilObject) ? LUA_TNONE : LumenTypeOf(o);
}


LUA_API const char *lua_typename(lua_State *L, int t) {
    UNUSED(L);
    return (t == LUA_TNONE) ? "no value" : Lumen::TM::TypeNames[t];
}


LUA_API int lua_iscfunction(lua_State *L, int idx) {
    Lumen::Value o = L->ToObject(idx);
    return LumenIsCFunction(o);
}


LUA_API int lua_isnumber(lua_State *L, int idx) {
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
    return LumenVMToNumber(o, &n);
}


LUA_API int lua_isstring(lua_State *L, int idx) {
    int t = lua_type(L, idx);
    return (t == LUA_TSTRING || t == LUA_TNUMBER);
}


LUA_API int lua_isuserdata(lua_State *L, int idx) {
    const Lumen::Object *o = L->ToObject(idx);
    return (LumenTypeIsUData(o) || LumenTypeIsLUData(o));
}


LUA_API int lua_rawequal(lua_State *L, int index1, int index2) {
    Lumen::Value o1 = L->ToObject(index1);
    Lumen::Value o2 = L->ToObject(index2);
    return (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0
                                                              : Lumen::RawEqualObject(o1, o2);
}

LUA_API void lua_arith(lua_State *L, int op) {
    Lumen::Value o1;  /* 1st operand */
    Lumen::Value o2;  /* 2nd operand */
    LumenLock(L);
    if (op != LUA_OPUNM) /* all other operations expect two operands */
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
        Lumen::VM::ArithValue(L, o1, o1, o2, cast(Lumen::TM::Name, op - LUA_OPADD + Lumen::TM::NameAdd));
    L->Top--;
    LumenUnlock(L);
}

LUA_API int lua_compare(lua_State *L, int idx1, int idx2, int op) {
    Lumen::Value o1, o2;
    int i = 0;
    LumenLock(L);  /* may call tag method */
    o1 = L->ToObject(idx1);
    o2 = L->ToObject(idx2);
    if (LumenApiIsValid(o1) && LumenApiIsValid(o2)) {
        switch (op) {
            case LUA_OPEQ:
                i = Lumen::VM::EqualObject(L, o1, o2);
                break;
            case LUA_OPLT:
                i = Lumen::VM::LessThan(L, o1, o2);
                break;
            case LUA_OPLE:
                i = Lumen::VM::LessEqual(L, o1, o2);
                break;
            default:
                LumenApiCheck(L, 0);
        }
    }
    LumenUnlock(L);
    return i;
}

LUA_API int lua_equal(lua_State *L, int index1, int index2) {
    Lumen::Value o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = L->ToObject(index1);
    o2 = L->ToObject(index2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0 : LumenVMEqualObj(L, o1, o2);
    LumenUnlock(L);
    return i;
}


LUA_API int lua_lessthan(lua_State *L, int index1, int index2) {
    Lumen::Value o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = L->ToObject(index1);
    o2 = L->ToObject(index2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0
                                                           : Lumen::VM::LessThan(L, o1, o2);
    LumenUnlock(L);
    return i;
}



LUA_API Lumen::Number lua_tonumber(lua_State *L, int idx) {
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
    if (LumenVMToNumber(o, &n))
        return LumenNumberValue(o);
    else
        return 0;
}


LUA_API Lumen::Integer lua_tointeger(lua_State *L, int idx) {
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


LUA_API int lua_toboolean(lua_State *L, int idx) {
    const Lumen::Object *o = L->ToObject(idx);
    return !LumenIsFalse(o);
}


LUA_API const char *lua_tolstring(lua_State *L, int idx, size_t *len) {
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


LUA_API size_t lua_objlen(lua_State *L, int idx) {
    Lumen::Value o = L->ToObject(idx);
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
    Lumen::Value o = L->ToObject(idx);
    return (!LumenIsCFunction(o)) ? nullptr : reinterpret_cast<lua_CFunction>(LumenClosureValue(o)->AsC.Func);
}


LUA_API void *lua_touserdata(lua_State *L, int idx) {
    Lumen::Value o = L->ToObject(idx);
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
    Lumen::Value o = L->ToObject(idx);
    return (!LumenTypeIsThread(o)) ? nullptr : LumenThreadValue(o);
}


LUA_API const void *lua_topointer(lua_State *L, int idx) {
    Lumen::Value o = L->ToObject(idx);
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
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

LUA_API void lua_pushnumber(lua_State *L, Lumen::Number n) {
    LumenLock(L);
    LumenSetNumberValue(L->Top, n);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

LUA_API void lua_pushinteger(lua_State *L, Lumen::Integer n) {
    LumenLock(L);
    LumenSetNumberValue(L->Top, cast_num(n));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

LUA_API void lua_pushlstring(lua_State *L, const char *s, size_t len) {
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenSetStringValue2S(L, L->Top, Lumen::String::New(L, s, len));
    LumenApiIncrTop(L);
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
    LumenApiCheckElementCount(L, n);
    cl = Lumen::CClosure::New(L, n, L->GetCurrentEnv());
    cl->AsC.Func = reinterpret_cast<Lumen::Delegate>(fn);
    L->Top -= n;
    while (n--)
        LumenSetObject2N(L, &cl->AsC.UpValues[n], L->Top + n);
    LumenSetClosureValue(L, L->Top, cl);
    LumenAssert(LumenGCIsWhite(LumenObject2GCObject(cl)));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_pushboolean(lua_State *L, int b) {
    LumenLock(L);
    LumenSetBoolValue(L->Top, (b != 0));  /* ensure that true is 1 */
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_pushlightuserdata(lua_State *L, void *p) {
    LumenLock(L);
    LumenSetLUDataValue(L->Top, p);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


LUA_API int lua_pushthread(lua_State *L) {
    LumenLock(L);
    LumenSetThreadValue(L, L->Top, L);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return (LumenGlobalState(L)->MainThread == L);
}


/*
** get functions (Lua -> stack)
*/


LUA_API void lua_gettable(lua_State *L, int idx) {
    Lumen::Value t;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheckValidIndex(L, t);
    Lumen::VM::GetTable(L, t, L->Top - 1, L->Top - 1);
    LumenUnlock(L);
}


LUA_API void lua_getfield(lua_State *L, int idx, const char *k) {
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


LUA_API void lua_rawget(lua_State *L, int idx) {
    Lumen::Value t;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheck(L, LumenTypeIsTable(t));
    LumenSetObject2S(L, L->Top - 1, Lumen::Table::Get(LumenTableValue(t), L->Top - 1));
    LumenUnlock(L);
}


LUA_API void lua_rawgeti(lua_State *L, int idx, int n) {
    Lumen::Value o;
    LumenLock(L);
    o = L->ToObject(idx);
    LumenApiCheck(L, LumenTypeIsTable(o));
    LumenSetObject2S(L, L->Top, Lumen::Table::GetNum(LumenTableValue(o), n));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

LUA_API void lua_rawgetp(lua_State *L, int idx, const void *p) {
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

LUA_API void lua_createtable(lua_State *L, int nArray, int nRec) {
    LumenLock(L);
    LumenGCCheckGC(L);
    LumenSetTableValue(L, L->Top, Lumen::Table::New(L, nArray, nRec));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


LUA_API int lua_getmetatable(lua_State *L, int objIndex) {
    const Lumen::Object *obj;
    Lumen::Table *mt = nullptr;
    int res;
    LumenLock(L);
    obj = L->ToObject(objIndex);
    switch (LumenTypeOf(obj)) {
        case LUA_TTABLE:
            mt = LumenTableValue(obj)->Metatable;
            break;
        case LUA_TUSERDATA:
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


LUA_API void lua_getfenv(lua_State *L, int idx) {
    Lumen::Value o;
    LumenLock(L);
    o = L->ToObject(idx);
    LumenApiCheckValidIndex(L, o);
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
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


/*
** set functions (stack -> Lua)
*/


LUA_API void lua_settable(lua_State *L, int idx) {
    Lumen::Value t;
    LumenLock(L);
    LumenApiCheckElementCount(L, 2);
    t = L->ToObject(idx);
    LumenApiCheckValidIndex(L, t);
    Lumen::VM::SetTable(L, t, L->Top - 2, L->Top - 1);
    L->Top -= 2;  /* pop index and value */
    LumenUnlock(L);
}


LUA_API void lua_setfield(lua_State *L, int idx, const char *k) {
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


LUA_API void lua_rawset(lua_State *L, int idx) {
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


LUA_API void lua_rawseti(lua_State *L, int idx, int n) {
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

LUA_API void lua_rawsetp(lua_State *L, int idx, const void *p) {
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

LUA_API int lua_setmetatable(lua_State *L, int objIndex) {
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
            LumenGlobalState(L)->Metatable[LumenTypeOf(obj)] = mt;
            break;
        }
    }
    L->Top--;
    LumenUnlock(L);
    return 1;
}


LUA_API int lua_setfenv(lua_State *L, int idx) {
    Lumen::Value o;
    int res = 1;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    o = L->ToObject(idx);
    LumenApiCheckValidIndex(L, o);
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

LUA_API void lua_call(lua_State *L, int nargs, int nResults) {
    Lumen::Value func;
    LumenLock(L);
    LumenApiCheckElementCount(L, nargs + 1);
    LumenApiCheckResults(L, nargs, nResults);
    func = L->Top - (nargs + 1);
    Lumen::Do::Call(L, func, nResults);
    LumenApiAdjustResults(L, nResults);
    LumenUnlock(L);
}

LUA_API int lua_pcall(lua_State *L, int nargs, int nResults, int errFunc) {
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

LUA_API int lua_cpcall(lua_State *L, lua_CFunction func, void *ud) {
    Lumen::ProtectedCCall c; // NOLINT
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
    Lumen::ZIO z; // NOLINT
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
    g = LumenGlobalState(L);
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
    LumenApiCheckElementCount(L, 1);
    Lumen::Debug::ErrorMessage(L);
    LumenUnlock(L);
    return 0;  /* to avoid warnings */
}


LUA_API int lua_next(lua_State *L, int idx) {
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


LUA_API void lua_concat(lua_State *L, int n) {
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

LUA_API void lua_len(lua_State *L, int idx) {
    Lumen::Value t;
    LumenLock(L);
    t = L->ToObject(idx);
    Lumen::VM::ObjectLength(L, L->Top, t);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


LUA_API Lumen::Allocator lua_getallocf(lua_State *L, void **ud) {
    Lumen::Allocator f;
    LumenLock(L);
    if (ud) *ud = LumenGlobalState(L)->ReAllocatorUData;
    f = LumenGlobalState(L)->ReAllocator;
    LumenUnlock(L);
    return f;
}


LUA_API void lua_setallocf(lua_State *L, Lumen::Allocator f, void *ud) {
    LumenLock(L);
    LumenGlobalState(L)->ReAllocatorUData = ud;
    LumenGlobalState(L)->ReAllocator = f;
    LumenUnlock(L);
}


LUA_API void *lua_newuserdata(lua_State *L, size_t size) {
    Lumen::Userdata *u;
    LumenLock(L);
    LumenGCCheckGC(L);
    u = Lumen::Userdata::New(L, size, L->GetCurrentEnv());
    LumenSetUDataValue(L, L->Top, u);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return u + 1;
}

// MARK: Debug API

LUA_API const char *lua_getlocal(lua_State *L, const lua_Debug *ar, int n) {
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name)
        L->PushObject(ci->Base + (n - 1));
    LumenUnlock(L);
    return name;
}


LUA_API const char *lua_setlocal(lua_State *L, const lua_Debug *ar, int n) {
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name)
        LumenSetObjectS2S(L, ci->Base + (n - 1), L->Top - 1);
    L->Top--;  /* pop value */
    LumenUnlock(L);
    return name;
}

LUA_API const char *lua_getupvalue(lua_State *L, int funcIndex, int n) {
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


LUA_API const char *lua_setupvalue(lua_State *L, int funcIndex, int n) {
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

static Lumen::UpValue **getUpValueRef(lua_State *L, int fIdx, int n, Lumen::LClosure **pf) {
    Lumen::LClosure *f;
    Lumen::Value fi = L->ToObject(fIdx);
    LumenApiCheck(L, LumenIsLFunction(fi));
    f = LumenLClosureValue(fi);
    LumenApiCheck(L, (1 <= n && n <= f->Func->UpValuesCount));
    if (pf) *pf = f;
    return &f->UpValues[n - 1];  /* get its upvalue pointer */
}

LUA_API void *lua_upvalueid(lua_State *L, int fIdx, int n) {
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

LUA_API void lua_upvaluejoin(lua_State *L, int fIdx1, int n1,
                             int fIdx2, int n2) {
    Lumen::LClosure *f1;
    Lumen::UpValue **up1 = getUpValueRef(L, fIdx1, n1, &f1);
    Lumen::UpValue **up2 = getUpValueRef(L, fIdx2, n2, nullptr);
    *up1 = *up2;
    LumenGCObjectBarrier(L, f1, *up2);
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

LUA_API int lua_loadx(lua_State *L, lua_Reader reader, void *data,
                      const char *chunkName, const char *mode) {
    (void) mode;  /* Lua 5.1 Can't specify mode */
    return lua_load(L, reader, data, chunkName);
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
