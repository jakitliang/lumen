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
#include "lumen/common.inl"
#include "lumen/state.h"
#include "lumen/tm.h"
#include "lumen/undump.h"
#include "lumen/vm.h"
#include "lumen/protected_call.h"
#include "lumen/api.h"

#include "lua.h"

#define ToState(L) reinterpret_cast<Lumen::State *>(L)
#define ToLua(L) reinterpret_cast<lua_State *>(L)

const char lua_ident[] =
    "$Lua: " LUA_RELEASE " " LUMEN_COPYRIGHT " $\n"
    "$Authors: " LUMEN_AUTHORS " $\n"
    "$URL: www.lua.org $\n";

LUA_API lua_State *lua_newstate(Lumen::Allocator f, void *ud) {
    return ToLua(Lumen::State::New(f, ud));
}

LUA_API void lua_close(lua_State *l) {
    auto L = ToState(l);
    Lumen::State::Close(L);
}

LUA_API lua_CFunction lua_atpanic(lua_State *l, lua_CFunction fPanic) {
    auto L = ToState(l);
    lua_CFunction old;
    LumenLock(L);
    old = reinterpret_cast<lua_CFunction>(LumenGlobalState(L)->Panic);
    LumenGlobalState(L)->Panic = reinterpret_cast<Lumen::Delegate>(fPanic);
    LumenUnlock(L);
    return old;
}

LUA_API const lua_Number *lua_version(lua_State *) {
    static const Lumen::Number lua_version_number = LUA_VERSION_NUM;
    return &lua_version_number;
}

LUA_API lua_State *lua_newthread(lua_State *l) {
    auto L = ToState(l);
    Lumen::State *L1;
    LumenLock(L);
    L->CheckGC();
    L1 = Lumen::State::NewThread(L);
    L->Top->SetThread(L, L1);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    luai_userstatethread(L, L1);
    return ToLua(L1);
}

LUA_API int lua_checkstack(lua_State *l, int size) {
    auto L = ToState(l);
    int res = 1;
    LumenLock(L);
    if (size > LUA_MAX_C_STACK || (L->Top - L->Base + size) > LUA_MAX_C_STACK)
        res = 0;  /* stack overflow */
    else if (size > 0) {
        LumenDoCheckStack(L, size);
        if (L->CallInfo->Top < L->Top + size)
            L->CallInfo->Top = L->Top + size;
    }
    LumenUnlock(L);
    return res;
}


LUA_API void lua_xmove(lua_State *fromL, lua_State *toL, int n) {
    auto from = ToState(fromL);
    auto to = ToState(toL);
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


LUA_API void lua_setlevel(lua_State *fromL, lua_State *toL) {
    auto from = ToState(fromL);
    auto to = ToState(toL);
    to->NCCalls = from->NCCalls;
}


/*
** basic stack manipulation
*/

LUA_API int lua_absindex(lua_State *l, int idx) {
    auto L = ToState(l);
    return (idx > 0 || LumenApiIsPseudo(idx))
           ? idx
           : cast_int((L->Top - L->Base) + 1 + idx);
}

LUA_API int lua_gettop(lua_State *l) {
    auto L = ToState(l);
    return cast_int(L->Top - L->Base);
}

LUA_API void lua_settop(lua_State *l, int idx) {
    auto L = ToState(l);
    LumenLock(L);
    if (idx >= 0) {
        LumenApiCheck(L, idx <= L->StackLast - L->Base);
        while (L->Top < L->Base + idx)
            (L->Top++)->SetNil();
        L->Top = L->Base + idx;
    } else {
        LumenApiCheck(L, -(idx + 1) <= (L->Top - L->Base));
        L->Top += idx + 1;  /* `subtract` index (index is negative) */
    }
    LumenUnlock(L);
}


LUA_API void lua_remove(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value p;
    LumenLock(L);
    p = L->ToObject(idx);
    LumenApiCheckValidIndex(L, p);
    while (++p < L->Top) LumenSetObjectS2S(L, p - 1, p);
    L->Top--;
    LumenUnlock(L);
}


LUA_API void lua_insert(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value p;
    Lumen::Value q;
    LumenLock(L);
    p = L->ToObject(idx);
    LumenApiCheckValidIndex(L, p);
    for (q = L->Top; q > p; q--) LumenSetObjectS2S(L, q, q - 1);
    LumenSetObjectS2S(L, p, L->Top);
    LumenUnlock(L);
}

static void moveTo(Lumen::State *l, Lumen::Object *from, int idx) {
    auto L = ToState(l);
    Lumen::Object *to = L->ToObject(idx);
    LumenApiCheckValidIndex(L, to);
    if (idx == Lumen::EnvIndex) {
        Lumen::Closure *func = L->GetCurrentFunction();
        LumenApiCheck(L, from->IsTable());
        func->AsC.Env = from->GetTable();
        L->Barrier(func, from);
    } else {
        to->SetObject(L, from);
        if (idx < Lumen::GlobalIndex)  /* function upvalue? */
            L->Barrier(L->GetCurrentFunction(), from);
    }
}

LUA_API void lua_replace(lua_State *l, int idx) {
    auto L = ToState(l);
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

LUA_API void lua_copy(lua_State *l, int fromIdx, int toIdx) {
    auto L = ToState(l);
    Lumen::Object *from;
    LumenLock(L);
    if (toIdx == LUA_ENVIRONINDEX && L->CallInfo == L->BaseCI)
        Lumen::Debug::RunError(L, "no calling environment");
    from = L->ToObject(fromIdx);
    moveTo(L, from, toIdx);
    LumenUnlock(L);
}

LUA_API void lua_pushvalue(lua_State *l, int idx) {
    auto L = ToState(l);
    LumenLock(L);
    LumenSetObject2S(L, L->Top, L->ToObject(idx));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

/*
** access functions (stack -> C)
*/


LUA_API int lua_type(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value o = L->ToObject(idx);
    return (o == Lumen::NilObject) ? LUA_TNONE : o->Type;
}


LUA_API const char *lua_typename(lua_State *l, int t) {
    auto L = ToState(l);
    UNUSED(L);
    return (t == LUA_TNONE) ? "no value" : Lumen::MetaMethod::TypeNames[t];
}


LUA_API int lua_iscfunction(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value o = L->ToObject(idx);
    return o->IsCFunction();
}


LUA_API int lua_isnumber(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
    return Lumen::VM::FastToNumber(o, &n);
}


LUA_API int lua_isstring(lua_State *L, int idx) {
    int t = lua_type(L, idx);
    return (t == LUA_TSTRING || t == LUA_TNUMBER);
}


LUA_API int lua_isuserdata(lua_State *l, int idx) {
    auto L = ToState(l);
    const Lumen::Object *o = L->ToObject(idx);
    return (o->IsUData() || o->IsLUData());
}


LUA_API int lua_rawequal(lua_State *l, int index1, int index2) {
    auto L = ToState(l);
    Lumen::Value o1 = L->ToObject(index1);
    Lumen::Value o2 = L->ToObject(index2);
    return (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0
                                                              : Lumen::RawEqualObject(o1, o2);
}

LUA_API void lua_arith(lua_State *l, int op) {
    auto L = ToState(l);
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
    if (o1->IsNumber() && o2->IsNumber()) {
        o1->SetNumber(Lumen::Arith(op, o1->GetNumber(), o2->GetNumber()));
    } else
        Lumen::VM::ArithValue(L, o1, o1, o2, cast(Lumen::MetaMethod::Name, op - Lumen::ArithOpAdd + Lumen::MetaMethod::NameAdd));
    L->Top--;
    LumenUnlock(L);
}

LUA_API int lua_compare(lua_State *l, int idx1, int idx2, int op) {
    auto L = ToState(l);
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

LUA_API int lua_equal(lua_State *l, int index1, int index2) {
    auto L = ToState(l);
    Lumen::Value o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = L->ToObject(index1);
    o2 = L->ToObject(index2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0 : Lumen::VM::FastEqualObject(L, o1, o2);
    LumenUnlock(L);
    return i;
}


LUA_API int lua_lessthan(lua_State *l, int index1, int index2) {
    auto L = ToState(l);
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



LUA_API Lumen::Number lua_tonumber(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
    if (Lumen::VM::FastToNumber(o, &n))
        return o->GetNumber();
    else
        return 0;
}


LUA_API Lumen::Integer lua_tointeger(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
    if (Lumen::VM::FastToNumber(o, &n)) {
        Lumen::Integer res;
        Lumen::Number num = o->GetNumber();
        lua_number2integer(res, num);
        return res;
    } else
        return 0;
}


LUA_API int lua_toboolean(lua_State *l, int idx) {
    auto L = ToState(l);
    const Lumen::Object *o = L->ToObject(idx);
    return !o->IsFalse();
}


LUA_API const char *lua_tolstring(lua_State *l, int idx, size_t *len) {
    auto L = ToState(l);
    Lumen::Value o = L->ToObject(idx);
    if (!o->IsString()) {
        LumenLock(L);  /* `Lumen::VM::ToString' may create a new string */
        if (!Lumen::VM::ToString(L, o)) {  /* conversion failed? */
            if (len != nullptr) *len = 0;
            LumenUnlock(L);
            return nullptr;
        }
        L->CheckGC();
        o = L->ToObject(idx);  /* previous call may reallocate the stack */
        LumenUnlock(L);
    }
    if (len != nullptr) *len = o->GetString()->Length;
    return o->ToCString();
}


LUA_API size_t lua_objlen(lua_State *state, int idx) {
    auto L = ToState(state);
    Lumen::Value o = L->ToObject(idx);
    switch (o->Type) {
        case Lumen::TypeString:
            return o->GetString()->Length;
        case Lumen::TypeUserdata:
            return o->GetUData()->Length;
        case Lumen::TypeTable:
            return Lumen::Table::GetN(o->GetTable());
        case Lumen::TypeNumber: {
            Lumen::UInteger l;
            LumenLock(L);  /* `Lumen::VM::ToString' may create a new string */
            l = (Lumen::VM::ToString(L, o) ? o->GetString()->Length : 0);
            LumenUnlock(L);
            return l;
        }
        default:
            return 0;
    }
}


LUA_API lua_CFunction lua_tocfunction(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value o = L->ToObject(idx);
    return (!o->IsCFunction()) ? nullptr : reinterpret_cast<lua_CFunction>(o->GetClosure()->AsC.Func);
}


LUA_API void *lua_touserdata(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value o = L->ToObject(idx);
    switch (o->Type) {
        case Lumen::TypeUserdata:
            return (o->GetUData() + 1);
        case Lumen::TypeLightUserdata:
            return o->GetLUData();
        default:
            return nullptr;
    }
}


LUA_API lua_State *lua_tothread(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value o = L->ToObject(idx);
    return (!o->IsThread()) ? nullptr : ToLua(o->GetThread());
}


LUA_API const void *lua_topointer(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value o = L->ToObject(idx);
    switch (o->Type) {
        case Lumen::TypeTable:
            return o->GetTable();
        case Lumen::TypeFunction:
            return o->GetClosure();
        case Lumen::TypeThread:
            return o->GetThread();
        case Lumen::TypeUserdata:
        case Lumen::TypeLightUserdata:
            return lua_touserdata(l, idx);
        default:
            return nullptr;
    }
}


/*
** push functions (C -> stack)
*/

LUA_API void lua_pushnil(lua_State *l) {
    auto L = ToState(l);
    LumenLock(L);
    L->Top->SetNil();
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

LUA_API void lua_pushnumber(lua_State *l, Lumen::Number n) {
    auto L = ToState(l);
    LumenLock(L);
    L->Top->SetNumber(n);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

LUA_API void lua_pushinteger(lua_State *l, Lumen::Integer n) {
    auto L = ToState(l);
    LumenLock(L);
    L->Top->SetNumber(cast_num(n));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

LUA_API const char *lua_pushlstring(lua_State *l, const char *s, size_t len) {
    auto L = ToState(l);
    LumenLock(L);
    L->CheckGC();
    auto str = Lumen::String::New(L, s, len);
    LumenSetStringValue2S(L, L->Top, str);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return str->CString();
}

LUA_API const char *lua_pushstring(lua_State *L, const char *s) {
    if (s == nullptr) {
        lua_pushnil(L);
        return s;
    }
    return lua_pushlstring(L, s, Lumen::String::LengthOf(s));
}

LUA_API const char *lua_pushvfstring(lua_State *l, const char *fmt,
                                     va_list argP) {
    auto L = ToState(l);
    const char *ret;
    LumenLock(L);
    L->CheckGC();;
    ret = Lumen::PushVFString(L, fmt, argP);
    LumenUnlock(L);
    return ret;
}

LUA_API const char *lua_pushfstring(lua_State *l, const char *fmt, ...) {
    auto L = ToState(l);
    const char *ret;
    va_list argP;
    LumenLock(L);
    L->CheckGC();;
        va_start(argP, fmt);
    ret = Lumen::PushVFString(L, fmt, argP);
        va_end(argP);
    LumenUnlock(L);
    return ret;
}


LUA_API void lua_pushcclosure(lua_State *l, lua_CFunction fn, int n) {
    auto L = ToState(l);
    Lumen::Closure *cl;
    LumenLock(L);
    L->CheckGC();;
    LumenApiCheckElementCount(L, n);
    cl = Lumen::CClosure::New(L, n, L->GetCurrentEnv());
    cl->AsC.Func = reinterpret_cast<Lumen::Delegate>(fn);
    L->Top -= n;
    while (n--)
        LumenSetObject2N(L, &cl->AsC.UpValues[n], L->Top + n);
    L->Top->SetClosure(L, cl);
    LumenAssert(LumenObject2GCObject(cl)->IsWhite());
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_pushboolean(lua_State *l, int b) {
    auto L = ToState(l);
    LumenLock(L);
    L->Top->SetBool(b != 0);  /* ensure that true is 1 */
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


LUA_API void lua_pushlightuserdata(lua_State *l, void *p) {
    auto L = ToState(l);
    LumenLock(L);
    L->Top->SetLUData(p);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


LUA_API int lua_pushthread(lua_State *l) {
    auto L = ToState(l);
    LumenLock(L);
    L->Top->SetThread(L, L);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return (LumenGlobalState(L)->MainThread == L);
}


/*
** get functions (Lua -> stack)
*/


LUA_API int lua_gettable(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value t;
    const Lumen::Object *key;
    const Lumen::Object *slot;
    LumenLock(L);
    t = L->ToObject(idx);
    key = L->Top - 1;
    LumenApiCheckValidIndex(L, t);
    if (key->IsNumber()
        ? LumenVMFastGetTable(L, t, cast_int(key->GetNumber()), slot, Lumen::Table::GetNum)
        : LumenVMFastGetTable(L, t, key, slot, Lumen::Table::Get)) {
        LumenSetObject2S(L, (L->Top - 1), slot);
    } else {
        Lumen::VM::FinishGetTable(L, t, L->Top - 1, L->Top - 1, slot);
    }
    LumenUnlock(L);
    return (L->Top - 1)->Type;
}


LUA_API int lua_getfield(lua_State *l, int idx, const char *k) {
    auto L = ToState(l);
    Lumen::Value t;
    Lumen::Object key; // NOLINT
    const Lumen::Object *slot;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheckValidIndex(L, t);
    key.SetString(L, Lumen::String::New(L, k));
    if (LumenVMFastGetTable(L, t, (&key), slot, Lumen::Table::Get)) {
        LumenSetObject2S(L, L->Top, slot);
    } else {
        Lumen::VM::FinishGetTable(L, t, &key, L->Top, slot);
    }
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return (L->Top - 1)->Type;
}


LUA_API int lua_rawget(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value t;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheck(L, t->IsTable());
    LumenSetObject2S(L, L->Top - 1, Lumen::Table::Get(t->GetTable(), L->Top - 1));
    LumenUnlock(L);
    return (L->Top - 1)->Type;
}


LUA_API int lua_rawgeti(lua_State *l, int idx, int n) {
    auto L = ToState(l);
    Lumen::Value o;
    LumenLock(L);
    o = L->ToObject(idx);
    LumenApiCheck(L, o->IsTable());
    LumenSetObject2S(L, L->Top, Lumen::Table::GetNum(o->GetTable(), n));
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return (L->Top - 1)->Type;
}

LUA_API int lua_rawgetp(lua_State *l, int idx, const void *p) {
    auto L = ToState(l);
    Lumen::Value t;
    Lumen::Object k; // NOLINT
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheck(L, t->IsTable());
    k.SetLUData(cast(void *, p));
    LumenSetObject2S(L, L->Top, Lumen::Table::Get(t->GetTable(), &k));
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return (L->Top - 1)->Type;
}

LUA_API void lua_createtable(lua_State *l, int nArray, int nRec) {
    auto L = ToState(l);
    LumenLock(L);
    L->CheckGC();;
    L->Top->SetTable(L, Lumen::Table::New(L, nArray, nRec));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


LUA_API int lua_getmetatable(lua_State *l, int objIndex) {
    auto L = ToState(l);
    const Lumen::Object *obj;
    Lumen::Table *mt = nullptr;
    int res;
    LumenLock(L);
    obj = L->ToObject(objIndex);
    switch (obj->Type) {
        case LUA_TTABLE:
            mt = obj->GetTable()->Metatable;
            break;
        case LUA_TUSERDATA:
            mt = obj->GetUData()->Metatable;
            break;
        default:
            mt = LumenGlobalState(L)->Metatable[obj->Type];
            break;
    }
    if (mt == nullptr)
        res = 0;
    else {
        L->Top->SetTable(L, mt);
        LumenApiIncrTop(L);
        res = 1;
    }
    LumenUnlock(L);
    return res;
}


LUA_API void lua_getfenv(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value o;
    LumenLock(L);
    o = L->ToObject(idx);
    LumenApiCheckValidIndex(L, o);
    switch (o->Type) {
        case LUA_TFUNCTION:
            L->Top->SetTable(L, o->GetClosure()->AsC.Env);
            break;
        case LUA_TUSERDATA:
            L->Top->SetTable(L, o->GetUData()->Env);
            break;
        case LUA_TTHREAD:
            LumenSetObject2S(L, L->Top, LumenGlobalTable(o->GetThread()));
            break;
        default:
            L->Top->SetNil();
            break;
    }
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


/*
** set functions (stack -> Lua)
*/


LUA_API void lua_settable(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value t;
    const Lumen::Object *key;
    const Lumen::Object *slot;
    LumenLock(L);
    LumenApiCheckElementCount(L, 2);
    t = L->ToObject(idx);
    key = L->Top - 2;
    LumenApiCheckValidIndex(L, t);
    if (key->IsNumber()
        ? LumenVMFastGetTable(L, t, cast_int(key->GetNumber()), slot, Lumen::Table::GetNum)
        : LumenVMFastGetTable(L, t, key, slot, Lumen::Table::Get)) {
        LumenVMFastSetTable(L, t->GetTable(), slot, L->Top - 1);
    } else {
        Lumen::VM::FinishSetTable(L, t, L->Top - 2, L->Top - 1, slot);
    }
    L->Top -= 2;  /* pop index and value */
    LumenUnlock(L);
}


LUA_API void lua_setfield(lua_State *l, int idx, const char *k) {
    auto L = ToState(l);
    Lumen::Value t;
    Lumen::Object key; // NOLINT
    const Lumen::Object *slot;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    t = L->ToObject(idx);
    LumenApiCheckValidIndex(L, t);
    key.SetString(L, Lumen::String::New(L, k));
    if (LumenVMFastGetTable(L, t, (&key), slot, Lumen::Table::Get)) {
        LumenVMFastSetTable(L, t->GetTable(), slot, L->Top - 1);
    } else {
        Lumen::VM::FinishSetTable(L, t, &key, L->Top - 1, slot);
    }
    L->Top--;  /* pop value */
    LumenUnlock(L);
}


LUA_API void lua_rawset(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value t;
    LumenLock(L);
    LumenApiCheckElementCount(L, 2);
    t = L->ToObject(idx);
    LumenApiCheck(L, t->IsTable());
    LumenSetObject2T(L, Lumen::Table::Set(L, t->GetTable(), L->Top - 2), L->Top - 1);
    L->BarrierTable(t->GetTable(), L->Top - 1);
    L->Top -= 2;
    LumenUnlock(L);
}


LUA_API void lua_rawseti(lua_State *l, int idx, int n) {
    auto L = ToState(l);
    Lumen::Value o;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    o = L->ToObject(idx);
    LumenApiCheck(L, o->IsTable());
    LumenSetObject2T(L, Lumen::Table::SetNum(L, o->GetTable(), n), L->Top - 1);
    L->BarrierTable(o->GetTable(), L->Top - 1);
    L->Top--;
    LumenUnlock(L);
}

LUA_API void lua_rawsetp(lua_State *l, int idx, const void *p) {
    auto L = ToState(l);
    Lumen::Value t;
    Lumen::Object k; // NOLINT
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    t = L->ToObject(idx);
    LumenApiCheck(L, t->IsTable());
    k.SetLUData(cast(void *, p));
    LumenSetObject2T(L, Lumen::Table::Set(L, t->GetTable(), &k), L->Top - 1);
    L->BarrierTable(t->GetTable(), L->Top - 1);
    L->Top--;
    LumenUnlock(L);
}

LUA_API int lua_setmetatable(lua_State *l, int objIndex) {
    auto L = ToState(l);
    Lumen::Object *obj;
    Lumen::Table *mt;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    obj = L->ToObject(objIndex);
    LumenApiCheckValidIndex(L, obj);
    if ((L->Top - 1)->IsNil())
        mt = nullptr;
    else {
        LumenApiCheck(L, (L->Top - 1)->IsTable());
        mt = (L->Top - 1)->GetTable();
    }
    switch (obj->Type) {
        case LUA_TTABLE: {
            obj->GetTable()->Metatable = mt;
            if (mt)
                L->BarrierGCObjectTable(obj->GetTable(), mt);
            break;
        }
        case LUA_TUSERDATA: {
            obj->GetUData()->Metatable = mt;
            if (mt)
                L->BarrierGCObject(obj->GetUData(), mt);
            break;
        }
        default: {
            LumenGlobalState(L)->Metatable[obj->Type] = mt;
            break;
        }
    }
    L->Top--;
    LumenUnlock(L);
    return 1;
}


LUA_API int lua_setfenv(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value o;
    int res = 1;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    o = L->ToObject(idx);
    LumenApiCheckValidIndex(L, o);
    LumenApiCheck(L, (L->Top - 1)->IsTable());
    switch (o->Type) {
        case LUA_TFUNCTION:
            o->GetClosure()->AsC.Env = (L->Top - 1)->GetTable();
            break;
        case LUA_TUSERDATA:
            o->GetUData()->Env = (L->Top - 1)->GetTable();
            break;
        case LUA_TTHREAD:
            LumenGlobalTable(o->GetThread())->SetTable(L, (L->Top - 1)->GetTable());
            break;
        default:
            res = 0;
            break;
    }
    if (res) L->BarrierGCObject(o->GetGCObject(), (L->Top - 1)->GetTable());
    L->Top--;
    LumenUnlock(L);
    return res;
}


/*
** `load' and `call' functions (run Lua code)
*/

LUA_API void lua_call(lua_State *l, int nargs, int nResults) {
    auto L = ToState(l);
    Lumen::Value func;
    LumenLock(L);
    LumenApiCheckElementCount(L, nargs + 1);
    LumenApiCheckResults(L, nargs, nResults);
    func = L->Top - (nargs + 1);
    Lumen::Do::Call(L, func, nResults);
    LumenApiAdjustResults(L, nResults);
    LumenUnlock(L);
}

LUA_API int lua_pcall(lua_State *l, int nargs, int nResults, int errFunc) {
    auto L = ToState(l);
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

LUA_API int lua_cpcall(lua_State *l, lua_CFunction func, void *ud) {
    auto L = ToState(l);
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


LUA_API int lua_load(lua_State *l, lua_Reader reader, void *data,
                     const char *chunkName) {
    auto L = ToState(l);
    Lumen::ZIO z; // NOLINT
    int status;
    LumenLock(L);
    if (!chunkName) chunkName = "?";
    Lumen::ZIO::Init(L, &z, reinterpret_cast<Lumen::Reader>(reader), data);
    status = Lumen::Do::ProtectedParser(L, &z, chunkName);
    LumenUnlock(L);
    return status;
}


LUA_API int lua_dump(lua_State *l, lua_Writer writer, void *data) {
    auto L = ToState(l);
    int status;
    Lumen::Object *o;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    o = L->Top - 1;
    if (o->IsLFunction())
        status = Lumen::Dumper::Dump(L,
                                     o->GetClosure()->AsLua.Func,
                                     reinterpret_cast<Lumen::Writer>(writer), data, 0);
    else
        status = 1;
    LumenUnlock(L);
    return status;
}

// MARK: Coroutine

LUA_API int lua_resume(lua_State *l, int nArgs) {
    auto L = ToState(l);
    int status;
    LumenLock(L);
    if (L->Status != LUA_YIELD && (L->Status != 0 || L->CallInfo != L->BaseCI))
        return Lumen::Do::ResumeError(L, "cannot resume non-suspended coroutine");
    if (L->NCCalls >= LUA_MAX_C_CALLS)
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

LUA_API int lua_yield(lua_State *l, int nResults) {
    auto L = ToState(l);
    luai_userstateyield(L, nResults);
    LumenLock(L);
    if (L->NCCalls > L->BaseCCalls)
        Lumen::Debug::RunError(L, "attempt to yield across metaMethod/C-call boundary");
    L->Base = L->Top - nResults;  /* protect stack slots below */
    L->Status = LUA_YIELD;
    LumenUnlock(L);
    return -1;
}

LUA_API int lua_status(lua_State *l) {
    auto L = ToState(l);
    return L->Status;
}


/*
** Garbage-collection function
*/

LUA_API int lua_gc(lua_State *l, int what, int data) {
    auto L = ToState(l);
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


LUA_API int lua_error(lua_State *l) {
    auto L = ToState(l);
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    Lumen::Debug::ErrorMessage(L);
    LumenUnlock(L);
    return 0;  /* to avoid warnings */
}


LUA_API int lua_next(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value t;
    int more;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheck(L, t->IsTable());
    more = Lumen::Table::Next(L, t->GetTable(), L->Top - 1);
    if (more) {
        LumenApiIncrTop(L);
    } else  /* no more elements */
        L->Top -= 1;  /* remove key */
    LumenUnlock(L);
    return more;
}


LUA_API void lua_concat(lua_State *l, int n) {
    auto L = ToState(l);
    LumenLock(L);
    LumenApiCheckElementCount(L, n);
    if (n >= 2) {
        L->CheckGC();;
        Lumen::VM::Concat(L, n, cast_int(L->Top - L->Base) - 1);
        L->Top -= (n - 1);
    } else if (n == 0) {  /* push empty string */
        LumenSetStringValue2S(L, L->Top, Lumen::String::New(L, "", 0));
        LumenApiIncrTop(L);
    }
    /* else n == 1; nothing to do */
    LumenUnlock(L);
}

LUA_API void lua_len(lua_State *l, int idx) {
    auto L = ToState(l);
    Lumen::Value t;
    LumenLock(L);
    t = L->ToObject(idx);
    Lumen::VM::ObjectLength(L, L->Top, t);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}


LUA_API Lumen::Allocator lua_getallocf(lua_State *l, void **ud) {
    auto L = ToState(l);
    Lumen::Allocator f;
    LumenLock(L);
    if (ud) *ud = LumenGlobalState(L)->ReAllocatorUData;
    f = LumenGlobalState(L)->ReAllocator;
    LumenUnlock(L);
    return f;
}


LUA_API void lua_setallocf(lua_State *l, Lumen::Allocator f, void *ud) {
    auto L = ToState(l);
    LumenLock(L);
    LumenGlobalState(L)->ReAllocatorUData = ud;
    LumenGlobalState(L)->ReAllocator = f;
    LumenUnlock(L);
}


LUA_API void *lua_newuserdata(lua_State *l, size_t size) {
    auto L = ToState(l);
    Lumen::Userdata *u;
    LumenLock(L);
    L->CheckGC();;
    u = Lumen::Userdata::New(L, size, L->GetCurrentEnv());
    L->Top->SetUData(L, u);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return u + 1;
}

// MARK: Debug API

LUA_API const char *lua_getlocal(lua_State *l, const lua_Debug *ar, int n) {
    auto L = ToState(l);
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name) {
        reinterpret_cast<Lumen::IState *>(L)->PushObject(reinterpret_cast<Lumen::IObject *>(ci->Base + (n - 1)));
    }
    LumenUnlock(L);
    return name;
}


LUA_API const char *lua_setlocal(lua_State *l, const lua_Debug *ar, int n) {
    auto L = ToState(l);
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name)
        LumenSetObjectS2S(L, ci->Base + (n - 1), L->Top - 1);
    L->Top--;  /* pop value */
    LumenUnlock(L);
    return name;
}

LUA_API const char *lua_getupvalue(lua_State *l, int funcIndex, int n) {
    auto L = ToState(l);
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


LUA_API const char *lua_setupvalue(lua_State *l, int funcIndex, int n) {
    auto L = ToState(l);
    const char *name;
    Lumen::Object *val;
    Lumen::Value fi;
    LumenLock(L);
    fi = L->ToObject(funcIndex);
    LumenApiCheckElementCount(L, 1);
    name = fi->GetUpValueInfo(n, &val);
    if (name) {
        L->Top--;
        val->SetObject(L, L->Top);
        L->Barrier(fi->GetClosure(), L->Top);
    }
    LumenUnlock(L);
    return name;
}

static Lumen::UpValue **getUpValueRef(Lumen::State *L, int fIdx, int n, Lumen::LClosure **pf) {
    Lumen::LClosure *f;
    Lumen::Value fi = L->ToObject(fIdx);
    LumenApiCheck(L, fi->IsLFunction());
    f = fi->GetLClosure();
    LumenApiCheck(L, (1 <= n && n <= f->Func->UpValuesCount));
    if (pf) *pf = f;
    return &f->UpValues[n - 1];  /* get its upvalue pointer */
}

LUA_API void *lua_upvalueid(lua_State *l, int fIdx, int n) {
    auto L = ToState(l);
    Lumen::Value fi = L->ToObject(fIdx);
    if (fi->IsLFunction()) { /* lua closure */
        return *getUpValueRef(L, fIdx, n, nullptr);
    } else if (fi->IsCFunction()) { /* C closure */
        Lumen::CClosure *f = fi->GetCClosure();
        LumenApiCheck(L, 1 <= n && n <= f->NUpValues);
        return &f->UpValues[n - 1];
    } else {
        LumenApiCheck(L, 0);
        return nullptr;
    }
}

LUA_API void lua_upvaluejoin(lua_State *l, int fIdx1, int n1,
                             int fIdx2, int n2) {
    auto L = ToState(l);
    Lumen::LClosure *f1;
    Lumen::UpValue **up1 = getUpValueRef(L, fIdx1, n1, &f1);
    Lumen::UpValue **up2 = getUpValueRef(L, fIdx2, n2, nullptr);
    *up1 = *up2;
    L->BarrierGCObject(f1, *up2);
}

/*
** this function can be called asynchronous (e.g. during a signal)
*/
LUA_API int lua_sethook(lua_State *l, lua_Hook func, int mask, int count) {
    auto L = ToState(l);
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


LUA_API lua_Hook lua_gethook(lua_State *l) {
    auto L = ToState(l);
    return reinterpret_cast<lua_Hook>(L->Hook);
}


LUA_API int lua_gethookmask(lua_State *l) {
    auto L = ToState(l);
    return L->HookMask;
}


LUA_API int lua_gethookcount(lua_State *l) {
    auto L = ToState(l);
    return L->BaseHookCount;
}


LUA_API int lua_getstack(lua_State *l, int level, lua_Debug *ar) {
    auto L = ToState(l);
    int status;
    Lumen::CallInfo *ci;
    LumenLock(L);
    for (ci = L->CallInfo; level > 0 && ci > L->BaseCI; ci--) {
        level--;
        if (ci->IsLuaFunction())  /* Lua function? */
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

LUA_API int lua_getinfo(lua_State *l, const char *what, lua_Debug *ar) {
    auto L = ToState(l);
    int status;
    Lumen::Closure *f = nullptr;
    Lumen::CallInfo *ci = nullptr;
    LumenLock(L);
    if (*what == '>') {
        Lumen::Value func = L->Top - 1;
        LumenApiCheck(L, func->IsFunction());
        what++;  /* skip the '>' */
        f = func->GetClosure();
        L->Top--;  /* pop function */
    } else if (reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI != 0) {  /* no tail call? */
        ci = L->BaseCI + reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI;
        LumenAssert(ci->Func->IsFunction());
        f = ci->Func->GetClosure();
    }
    status = Lumen::Debug::GetInfo(L, what, reinterpret_cast<Lumen::DebugInfo *>(ar), f, ci);
    if (strchr(what, 'f')) {
        if (f == nullptr) L->Top->SetNil();
        else
            L->Top->SetClosure(L, f);
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

LUA_API int lua_isyieldable(lua_State *l) {
    auto L = ToState(l);
    return (L->NCCalls == 0);
}
