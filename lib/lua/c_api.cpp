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

#define LUA_LIB

#include "lumen.h"
#include "lua.h"

struct lua_State : Lumen::IState {

};

#define LuaToPP(L) reinterpret_cast<Lumen::IState *>(L)
#define PPToLua(L) reinterpret_cast<lua_State *>(L)

const char lua_ident[] =
    "$Lua: " LUA_RELEASE " " LUMEN_COPYRIGHT " $\n"
    "$Authors: " LUMEN_AUTHORS " $\n"
    "$URL: www.lua.org $\n";

LUA_API lua_State *lua_newstate(lua_Alloc f, void *ud) {
    return PPToLua(Lumen::IState::New());
}

LUA_API void lua_close(lua_State *l) {
    auto L = LuaToPP(l);
    Lumen::Close(L);
}

LUA_API lua_CFunction lua_atpanic(lua_State *L, lua_CFunction fPanic) {
    auto old = L->AtPanic(reinterpret_cast<Lumen::Delegate>(fPanic));
    return reinterpret_cast<lua_CFunction>(old);
}

LUA_API const lua_Number *lua_version(lua_State *) {
    static const lua_Number lua_version_number = LUA_VERSION_NUM;
    return &lua_version_number;
}

LUA_API lua_State *lua_newthread(lua_State *L) {
    return PPToLua(L->NewThread());
}

LUA_API int lua_checkstack(lua_State *L, int size) {
    return L->CheckStack(size);
}

LUA_API void lua_xmove(lua_State *from, lua_State *to, int n) {
    Lumen::XMove(from, to, n);
}

LUA_API void lua_setlevel(lua_State *from, lua_State *to) {
    Lumen::SetLevel(from, to);
}

/*
** basic stack manipulation
*/

LUA_API int lua_absindex(lua_State *L, int idx) {
    return L->AbsIndex(idx);
}

LUA_API int lua_gettop(lua_State *L) {
    return L->GetTop();
}

LUA_API void lua_settop(lua_State *L, int idx) {
    L->SetTop(idx);
}

LUA_API void lua_remove(lua_State *L, int idx) {
    L->Remove(idx);
}

LUA_API void lua_insert(lua_State *L, int idx) {
    L->Insert(idx);
}

LUA_API void lua_replace(lua_State *L, int idx) {
    L->Replace(idx);
}

LUA_API void lua_rotate(lua_State *L, int idx, int n) {
    L->Rotate(idx, n);
}

LUA_API void lua_copy(lua_State *L, int fromIdx, int toIdx) {
    L->Copy(fromIdx, toIdx);
}

LUA_API void lua_pushvalue(lua_State *L, int idx) {
    L->PushValue(idx);
}

/*
** access functions (stack -> C)
*/

LUA_API int lua_type(lua_State *L, int idx) {
    return L->TypeId(idx);
}

LUA_API const char *lua_typename(lua_State *L, int t) {
    return L->TypeName(t);
}

LUA_API int lua_iscfunction(lua_State *L, int idx) {
    return L->IsDelegate(idx);
}

LUA_API int lua_isnumber(lua_State *L, int idx) {
    return L->IsNumber(idx);
}

LUA_API int lua_isstring(lua_State *L, int idx) {
    return L->IsString(idx);
}

LUA_API int lua_isuserdata(lua_State *L, int idx) {
    return L->IsUserdata(idx);
}

LUA_API int lua_rawequal(lua_State *L, int index1, int index2) {
    return L->RawEqual(index1, index2);
}

LUA_API void lua_arith(lua_State *L, int op) {
    L->Arith(op);
}

LUA_API int lua_compare(lua_State *L, int idx1, int idx2, int op) {
    return L->Compare(idx1, idx2, op);
}

LUA_API int lua_equal(lua_State *L, int index1, int index2) {
    return L->Equal(index1, index2);
}

LUA_API int lua_lessthan(lua_State *L, int index1, int index2) {
    return L->LessThan(index1, index2);
}

LUA_API lua_Number lua_tonumber(lua_State *L, int idx) {
    return L->ToNumber(idx);
}

LUA_API lua_Integer lua_tointeger(lua_State *L, int idx) {
    return L->ToInteger(idx);
}

LUA_API int lua_toboolean(lua_State *L, int idx) {
    return L->ToBoolean(idx);
}

LUA_API const char *lua_tolstring(lua_State *L, int idx, size_t *len) {
    return L->ToString(idx, len);
}

LUA_API size_t lua_objlen(lua_State *L, int idx) {
    return L->ObjectLength(idx);
}

LUA_API lua_CFunction lua_tocfunction(lua_State *L, int idx) {
    return reinterpret_cast<lua_CFunction>(L->ToDelegate(idx));
}

LUA_API void *lua_touserdata(lua_State *L, int idx) {
    return L->ToUserdata(idx);
}

LUA_API lua_State *lua_tothread(lua_State *L, int idx) {
    return PPToLua(L->ToThread(idx));
}

LUA_API const void *lua_topointer(lua_State *L, int idx) {
    return L->ToPointer(idx);
}

/*
** push functions (C -> stack)
*/

LUA_API void lua_pushnil(lua_State *L) {
    L->PushNil();
}

LUA_API void lua_pushnumber(lua_State *L, lua_Number n) {
    L->PushNumber(n);
}

LUA_API void lua_pushinteger(lua_State *L, lua_Integer n) {
    L->PushInteger(n);
}

LUA_API const char *lua_pushlstring(lua_State *L, const char *s, size_t len) {
    return L->PushString(s, len);
}

LUA_API const char *lua_pushstring(lua_State *L, const char *s) {
    return L->PushString(s);
}

LUA_API const char *lua_pushvfstring(lua_State *L, const char *fmt,
                                     va_list argP) {
    return L->PushVFString(fmt, argP);
}

LUA_API const char *lua_pushfstring(lua_State *L, const char *fmt, ...) {
    va_list argP;
        va_start(argP, fmt);
    auto s = L->PushVFString(fmt, argP);
        va_end(argP);
    return s;
}

LUA_API void lua_pushcclosure(lua_State *L, lua_CFunction fn, int n) {
    L->PushDelegate(reinterpret_cast<Lumen::Delegate>(fn), n);
}

LUA_API void lua_pushboolean(lua_State *L, int b) {
    L->PushBoolean(b);
}

LUA_API void lua_pushlightuserdata(lua_State *L, void *p) {
    L->PushLightUserdata(p);
}

LUA_API int lua_pushthread(lua_State *L) {
    return L->PushThread();
}

/*
** get functions (Lua -> stack)
*/

LUA_API int lua_gettable(lua_State *L, int idx) {
    return L->GetTable(idx);
}

LUA_API int lua_getfield(lua_State *L, int idx, const char *k) {
    return L->GetField(idx, k);
}

LUA_API int lua_rawget(lua_State *L, int idx) {
    return L->RawGet(idx);
}

LUA_API int lua_rawgeti(lua_State *L, int idx, int n) {
    return L->RawGetAt(idx, n);
}

LUA_API int lua_rawgetp(lua_State *L, int idx, const void *p) {
    return L->RawGetPtr(idx, p);
}

LUA_API void lua_createtable(lua_State *L, int nArray, int nRec) {
    L->CreateTable(nArray, nRec);
}

LUA_API int lua_getmetatable(lua_State *L, int objIndex) {
    return L->GetMetatable(objIndex);
}

LUA_API void lua_getfenv(lua_State *L, int idx) {
    L->GetFEnv(idx);
}

/*
** set functions (stack -> Lua)
*/

LUA_API void lua_settable(lua_State *L, int idx) {
    L->SetTable(idx);
}

LUA_API void lua_setfield(lua_State *L, int idx, const char *k) {
    L->SetField(idx, k);
}

LUA_API void lua_rawset(lua_State *L, int idx) {
    L->RawSet(idx);
}

LUA_API void lua_rawseti(lua_State *L, int idx, int n) {
    L->RawSetAt(idx, n);
}

LUA_API void lua_rawsetp(lua_State *L, int idx, const void *p) {
    L->RawSetPtr(idx, p);
}

LUA_API int lua_setmetatable(lua_State *L, int objIndex) {
    return L->SetMetatable(objIndex);
}

LUA_API int lua_setfenv(lua_State *L, int idx) {
    return L->SetFEnv(idx);
}


/*
** `load' and `call' functions (run Lua code)
*/

LUA_API void lua_call(lua_State *L, int nargs, int nResults) {
    L->Call(nargs, nResults);
}

LUA_API int lua_pcall(lua_State *L, int nargs, int nResults, int errFunc) {
    return L->TryCall(nargs, nResults, errFunc);
}

LUA_API int lua_cpcall(lua_State *L, lua_CFunction func, void *ud) {
    return L->TryCall(reinterpret_cast<Lumen::Delegate>(func), ud);
}

LUA_API int lua_load(lua_State *L, lua_Reader reader, void *data,
                     const char *chunkName) {
    return L->Load(reinterpret_cast<Lumen::Reader>(reader), data, chunkName);
}

LUA_API int lua_dump(lua_State *L, lua_Writer writer, void *data) {
    return L->Dump(reinterpret_cast<Lumen::Writer>(writer), data);
}

// MARK: Coroutine

LUA_API int lua_resume(lua_State *L, int nArgs) {
    return L->Resume(nArgs);
}

LUA_API int lua_yield(lua_State *L, int nResults) {
    return L->Yield(nResults);
}

LUA_API int lua_status(lua_State *L) {
    return L->Status();
}


/*
** Garbage-collection function
*/

LUA_API int lua_gc(lua_State *L, int what, int data) {
    return L->GC(what, data);
}

/*
** miscellaneous functions
*/

LUA_API int lua_error(lua_State *L) {
    return L->Error();
}

LUA_API int lua_next(lua_State *L, int idx) {
    return L->Next(idx);
}

LUA_API void lua_concat(lua_State *L, int n) {
    L->Concat(n);
}

LUA_API void lua_len(lua_State *L, int idx) {
    L->LengthOf(idx);
}

LUA_API lua_Alloc lua_getallocf(lua_State *L, void **ud) {
    return reinterpret_cast<lua_Alloc>(L->GetAllocator(ud));
}

LUA_API void lua_setallocf(lua_State *L, lua_Alloc f, void *ud) {
    L->SetAllocator(reinterpret_cast<Lumen::Allocator>(f), ud);
}

LUA_API void *lua_newuserdata(lua_State *L, size_t size) {
    return L->NewUserdata(size);
}

// MARK: Debug API

LUA_API const char *lua_getlocal(lua_State *L, const lua_Debug *ar, int n) {
    return L->GetLocal(reinterpret_cast<const Lumen::DebugInfo *>(ar), n);
}

LUA_API const char *lua_setlocal(lua_State *L, const lua_Debug *ar, int n) {
    return L->SetLocal(reinterpret_cast<const Lumen::DebugInfo *>(ar), n);
}

LUA_API const char *lua_getupvalue(lua_State *L, int funcIndex, int n) {
    return L->GetUpValue(funcIndex, n);
}

LUA_API const char *lua_setupvalue(lua_State *L, int funcIndex, int n) {
    return L->SetUpValue(funcIndex, n);
}

LUA_API void *lua_upvalueid(lua_State *L, int fIdx, int n) {
    return L->GetUpValueId(fIdx, n);
}

LUA_API void lua_upvaluejoin(lua_State *L, int fIdx1, int n1,
                             int fIdx2, int n2) {
    L->JoinUpValue(fIdx1, n1, fIdx2, n2);
}

/*
** this function can be called asynchronous (e.g. during a signal)
*/
LUA_API int lua_sethook(lua_State *L, lua_Hook func, int mask, int count) {
    return L->SetHook(reinterpret_cast<Lumen::Hook>(func), mask, count);
}

LUA_API lua_Hook lua_gethook(lua_State *L) {
    return reinterpret_cast<lua_Hook>(L->GetHook());
}

LUA_API int lua_gethookmask(lua_State *L) {
    return L->GetHookMask();
}

LUA_API int lua_gethookcount(lua_State *L) {
    return L->GetHookCount();
}

LUA_API int lua_getstack(lua_State *L, int level, lua_Debug *ar) {
    return L->GetStack(level, reinterpret_cast<Lumen::DebugInfo *>(ar));
}

LUA_API int lua_getinfo(lua_State *L, const char *what, lua_Debug *ar) {
    return L->GetInfo(what, reinterpret_cast<Lumen::DebugInfo *>(ar));
}

LUA_API int lua_loadx(lua_State *L, lua_Reader reader, void *data,
                      const char *chunkName, const char *) {
    return lua_load(L, reader, data, chunkName);
}

LUA_API lua_Number lua_tonumberx(lua_State *L, int idx, int *isNum) {
    lua_Number n = lua_tonumber(L, idx);
    if (isNum) *isNum = (n != 0 || lua_isnumber(L, idx));
    return n;
}

LUA_API lua_Integer lua_tointegerx(lua_State *L, int idx, int *isNum) {
    lua_Integer n = lua_tointeger(L, idx);
    if (isNum) *isNum = (n != 0 || lua_isnumber(L, idx));
    return n;
}

LUA_API int lua_isyieldable(lua_State *L) {
    return L->CanYield();
}
