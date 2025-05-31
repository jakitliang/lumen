/*!
 * @brief Lumen C++ API for Lua
 * @author Jakit
 * @date 2025/5/29
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#ifndef lua_hpp
#define lua_hpp

#include <climits>
#include <cstddef>
#include <limits>
#include <type_traits>

#define LUAI_STATE Lua::State
#define LUAI_DELEGATE Lua::Delegate
#define LUAI_READER Lua::Reader
#define LUAI_WRITER Lua::Writer
#define LUAI_ALLOCATOR Lua::Allocator
#define LUAI_DEBUGINFO Lua::DebugInfo
#define LUAI_HOOK Lua::Hook
#define LUAI_INTERFACE Lua::Interface

#include "luaconf.h"

namespace Lua {
    using Byte = unsigned char;
    using Number = LUA_NUMBER;
    using Integer = LUA_INTEGER;

    struct State;

    typedef int (*Delegate)(LUAI_STATE *L);

    typedef const char *(*Reader)(LUAI_STATE *L, void *ud, size_t *sz);

    typedef int (*Writer)(LUAI_STATE *L, const void *p, size_t sz, void *ud);

    typedef void *(*Allocator)(void *ud, void *ptr, size_t oldSize, size_t newSize);

    struct DebugInfo {
        int Event;
        const char *Name;    /* (n) */
        const char *NameSpace;    /* (n) `global', `local', `field', `method' */
        const char *Space;    /* (S) `Lua', `C', `main', `tail' */
        const char *Source;    /* (S) */
        int CurrentLine;    /* (l) */
        int NUpValues;        /* (u) number of upvalues */
        int LineDefined;    /* (S) */
        int LastLineDefined;    /* (S) */
        char SourceHint[LUA_IDSIZE]; /* (S) */
        int CurrentCI;  /* active function */
    };

    typedef void (*Hook)(LUAI_STATE *L, LUAI_DEBUGINFO_NAME *ar);

    struct Interface {
        const char *Name;
        Delegate Invoke;
    };
}

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

namespace Lua {
    typedef LUA_ENUM(int, Index) {
        IndexRegistry = LUA_GLOBALSINDEX,
        IndexEnv = LUA_ENVIRONINDEX,
        IndexGlobal = LUA_GLOBALSINDEX
    };

    inline int IndexUpValue(Index i) {
        return LUA_GLOBALSINDEX - (i);
    }

    struct State {
        // MARK: state manipulation

        static inline State *New(Allocator allocator, void *userdata) {
            return lua_newstate(allocator, userdata);
        }

        inline void Close() {
            lua_close(this);
        }

        inline State *NewThread() {
            return lua_newthread(this);
        }

        inline Delegate AtPanic(Delegate pInvoke) {
            return lua_atpanic(this, pInvoke);
        }

        // MARK: basic stack manipulation

        inline int GetTop() {
            return lua_gettop(this);
        }

        inline void SetTop(int idx) {
            lua_settop(this, idx);
        }

        inline void PushValue(int idx) {
            lua_pushvalue(this, idx);
        }

        inline void Remove(int idx) {
            lua_remove(this, idx);
        }

        inline void Insert(int idx) {
            lua_insert(this, idx);
        }

        inline void Replace(int idx) {
            lua_replace(this, idx);
        }

        inline int CheckStack(int size) {
            return lua_checkstack(this, size);
        }

        // MARK: access functions (stack -> C)

        inline int IsNumber(int idx) {
            return lua_isnumber(this, idx);
        }

        inline int IsString(int idx) {
            return lua_isstring(this, idx);
        }

        inline int IsCFunction(int idx) {
            return lua_iscfunction(this, idx);
        }

        inline int IsUserdata(int idx) {
            return lua_isuserdata(this, idx);
        }

        inline int Type(int idx) {
            return lua_type(this, idx);
        }

        inline const char *TypeName(int t) {
            return lua_typename(this, t);
        }

        inline int Equal(int idx1, int idx2) {
            return lua_equal(this, idx1, idx2);
        }

        inline int RawEqual(int idx1, int idx2) {
            return lua_rawequal(this, idx1, idx2);
        }

        inline int LessThan(int idx1, int idx2) {
            return lua_lessthan(this, idx1, idx2);
        }

        inline Number ToNumber(int idx) {
            return lua_tonumber(this, idx);
        }

        inline Integer ToInteger(int idx) {
            return lua_tointeger(this, idx);
        }

        inline bool ToBoolean(int idx) {
            return lua_toboolean(this, idx);
        }

        inline const char *ToString(int idx, size_t *len) {
            return lua_tolstring(this, idx, len);
        }

        inline size_t ObjectLength(int idx) {
            return lua_objlen(this, idx);
        }

        inline Delegate ToCFunction(int idx) {
            return lua_tocfunction(this, idx);
        }

        inline void *ToUserdata(int idx) {
            return lua_touserdata(this, idx);
        }

        inline State *ToThread(int idx) {
            return lua_tothread(this, idx);
        }

        inline const void *ToPointer(int idx) {
            return lua_topointer(this, idx);
        }

        // MARK: push functions (C -> stack)

        inline void PushNil() {
            lua_pushnil(this);
        }

        inline void PushNumber(Number n) {
            lua_pushnumber(this, n);
        }

        inline void PushInteger(Integer n) {
            lua_pushinteger(this, n);
        }

        inline void PushString(const char *s, size_t length) {
            lua_pushlstring(this, s, length);
        }

        inline void PushString(const char *s) {
            lua_pushstring(this, s);
        }

        inline const char *PushVFString(const char *fmt,
                                        va_list argP) {
            return lua_pushvfstring(this, fmt, argP);
        }

        inline const char *PushFString(const char *fmt, ...) {
            va_list args;
                va_start(args, fmt);
            auto ret = lua_pushvfstring(this, fmt, args);
                va_end(args);
            return ret;
        }

        inline void PushCClosure(Delegate invoke, int n) {
            lua_pushcclosure(this, invoke, n);
        }

        inline void PushBoolean(int b) {
            lua_pushboolean(this, b);
        }

        inline void PushLightUserdata(void *p) {
            lua_pushlightuserdata(this, p);
        }

        inline int PushThread() {
            return lua_pushthread(this);
        }

        // MARK: get functions (Lua -> stack)

        inline void GetTable(int idx) {
            lua_gettable(this, idx);
        }

        inline void GetField(int idx, const char *k) {
            lua_getfield(this, idx, k);
        }

        inline void RawGet(int idx) {
            lua_rawget(this, idx);
        }

        inline void RawGetIndex(int idx, int n) {
            lua_rawgeti(this, idx, n);
        }

        inline void CreateTable(int nArray, int nRec) {
            lua_createtable(this, nArray, nRec);
        }

        inline void *NewUserdata(size_t size) {
            return lua_newuserdata(this, size);
        }

        inline int GetMetatable(int objIndex) {
            return lua_getmetatable(this, objIndex);
        }

        inline void GetFEnv(int idx) {
            lua_getfenv(this, idx);
        }

        // MARK: set functions (stack -> Lua)

        inline void SetTable(int idx) {
            lua_settable(this, idx);
        }

        inline void SetField(int idx, const char *k) {
            lua_setfield(this, idx, k);
        }

        inline void RawSet(int idx) {
            lua_rawset(this, idx);
        }

        inline void RawSetIndex(int idx, int n) {
            lua_rawseti(this, idx, n);
        }

        inline int SetMetatable(int objIndex) {
            return lua_setmetatable(this, objIndex);
        }

        inline int SetFEnv(int idx) {
            return lua_setfenv(this, idx);
        }

        // MARK: `load' and `call' functions (load and run Lua code)

        inline void Call(int nargs, int nResults) {
            lua_call(this, nargs, nResults);
        }

        inline int PCall(int nargs, int nResults, int errFunc) {
            return lua_pcall(this, nargs, nResults, errFunc);
        }

        inline int CPCall(Delegate invoke, void *userdata) {
            return lua_cpcall(this, invoke, userdata);
        }

        inline int Load(Reader reader, void *dt, const char *chunkName) {
            return lua_load(this, reader, dt, chunkName);
        }

        inline int Dump(Writer writer, void *data) {
            return lua_dump(this, writer, data);
        }

        // MARK: coroutine functions

        inline int Yield(int nResults) {
            return lua_yield(this, nResults);
        }

        inline int Resume(int nArgs) {
            return lua_resume(this, nArgs);
        }

        inline int Status() {
            return lua_status(this);
        }

        // MARK: garbage-collection function and options

        inline int GC(int what, int data) {
            return lua_gc(this, what, data);
        }

        // MARK: miscellaneous functions

        inline int Error() {
            return lua_error(this);
        }

        inline int Next(int idx) {
            return lua_next(this, idx);
        }

        inline void Concat(int n) {
            lua_concat(this, n);
        }

        inline Allocator GetAllocator(void **ud) {
            return lua_getallocf(this, ud);
        }

        inline void SetAllocator(Allocator f, void *ud) {
            lua_setallocf(this, f, ud);
        }

        inline void Pop(int n) {
            lua_settop(this, -(n) - 1);
        }

        inline void NewTable() {
            lua_createtable(this, 0, 0);
        }

        inline void Register(const char *name, Delegate invoke) {
            lua_pushcfunction(this, invoke);
            lua_setglobal(this, name);
        }

        inline void PushCFunction(Delegate invoke) {
            lua_pushcclosure(this, invoke, 0);
        }

        inline size_t StringLength(int idx) {
            return lua_objlen(this, idx);
        }

        inline bool IsFunction(int idx) {
            return lua_type(this, idx) == LUA_TFUNCTION;
        }

        inline bool IsTable(int idx) {
            return lua_type(this, idx) == LUA_TTABLE;
        }

        inline bool IsLightUserdata(int idx) {
            return lua_type(this, idx) == LUA_TLIGHTUSERDATA;
        }

        inline bool IsNil(int idx) {
            return lua_type(this, idx) == LUA_TNIL;
        }

        inline bool IsBoolean(int idx) {
            return lua_type(this, idx) == LUA_TBOOLEAN;
        }

        inline bool IsThread(int idx) {
            return lua_type(this, idx) == LUA_TTHREAD;
        }

        inline bool IsNone(int idx) {
            return lua_type(this, idx) == LUA_TNONE;
        }

        inline bool IsNoneOrNil(int idx) {
            return lua_type(this, idx) <= 0;
        }

        template<size_t S>
        inline void PushLiteral(const char (&s)[S]) {
            lua_pushlstring(this, s, (S / sizeof(char)) - 1);
        }

        inline void SetGlobal(const char *key) {
            lua_setfield(this, LUA_GLOBALSINDEX, key);
        }

        inline void GetGlobal(const char *key) {
            lua_getfield(this, LUA_GLOBALSINDEX, key);
        }

        inline const char *ToString(int idx) {
            return lua_tolstring(this, idx, nullptr);
        }

        // MARK: compatibility macros and functions

        inline void GetRegistry() {
            lua_pushvalue(this, LUA_REGISTRYINDEX);
        }

        inline int GetGCCount() {
            return lua_gc(this, LUA_GCCOUNT, 0);
        }

        // MARK: debug

        inline int GetStack(int level, DebugInfo *ar) {
            return lua_getstack(this, level, ar);
        }

        inline int GetInfo(const char *what, DebugInfo *ar) {
            return lua_getinfo(this, what, ar);
        }

        inline const char *GetLocal(const DebugInfo *ar, int n) {
            return lua_getlocal(this, ar, n);
        }

        inline const char *SetLocal(const DebugInfo *ar, int n) {
            return lua_setlocal(this, ar, n);
        }

        inline const char *GetUpValue(int funcIndex, int n) {
            return lua_getupvalue(this, funcIndex, n);
        }

        inline const char *SetUpValue(int funcIndex, int n) {
            return lua_setupvalue(this, funcIndex, n);
        }

        inline int SetHook(Hook func, int mask, int count) {
            return lua_sethook(this, func, mask, count);
        }

        inline Hook GetHook() {
            return lua_gethook(this);
        }

        inline int GetHookMask() {
            return lua_gethookmask(this);
        }

        inline int GetHookCount() {
            return lua_gethookcount(this);
        }

        // MARK: Auxiliary basic APIs

        inline void OpenLib(const char *name, const Interface *i, int nUpValue) {
            luaL_openlib(this, name, i, nUpValue);
        }

        inline void Register(const char *name, const Interface *i) {
            luaL_register(this, name, i);
        }

        inline int GetMetaField(int obj, const char *e) {
            return luaL_getmetafield(this, obj, e);
        }

        inline int CallMeta(int obj, const char *e) {
            return luaL_callmeta(this, obj, e);
        }

        inline int TypeError(int nArg, const char *tName) {
            return luaL_typerror(this, nArg, tName);
        }

        inline int ArgError(int nArg, const char *extraMsg) {
            return luaL_argerror(this, nArg, extraMsg);
        }

        inline const char *CheckString(int nArg, size_t *length) {
            return luaL_checklstring(this, nArg, length);
        }

        inline const char *OptString(int nArg, const char *def, size_t *length) {
            return luaL_optlstring(this, nArg, def, length);
        }

        inline Number CheckNumber(int nArg) {
            return luaL_checknumber(this, nArg);
        }

        inline Number OptNumber(int nArg, Number def) {
            return luaL_optnumber(this, nArg, def);
        }

        inline Integer CheckInteger(int nArg) {
            return luaL_checkinteger(this, nArg);
        }

        inline Integer OptInteger(int nArg, Integer def) {
            return luaL_optinteger(this, nArg, def);
        }

        inline void CheckStack(int sz, const char *msg) {
            luaL_checkstack(this, sz, msg);
        }

        inline void CheckType(int nArg, int t) {
            luaL_checktype(this, nArg, t);
        }

        inline void CheckAny(int nArg) {
            luaL_checkany(this, nArg);
        }

        inline int NewMetatable(const char *tName) {
            return luaL_newmetatable(this, tName);
        }

        inline void *CheckUserdata(int ud, const char *tName) {
            return luaL_checkudata(this, ud, tName);
        }

        inline void Where(int lvl) {
            luaL_where(this, lvl);
        }

        inline int Error(const char *fmt, ...) {
            va_list args;
                va_start(args, fmt);
            auto ret = luaL_error(this, fmt, args);
                va_end(args);
            return ret;
        }

        inline int CheckOption(int nArg, const char *def, const char *const lst[]) {
            return luaL_checkoption(this, nArg, def, lst);
        }

        inline int Ref(int t) {
            return luaL_ref(this, t);
        }

        inline int Ref() {
            return luaL_ref(this, LUA_REGISTRYINDEX);
        }

        inline void Unref(int t, int ref) {
            luaL_unref(this, t, ref);
        }

        inline void Unref(int ref) {
            luaL_unref(this, LUA_REGISTRYINDEX, ref);
        }

        inline int LoadFile(const char *filename) {
            return luaL_loadfile(this, filename);
        }

        inline int LoadBuffer(const char *buff, size_t size, const char *name) {
            return luaL_loadbuffer(this, buff, size, name);
        }

        inline int LoadString(const char *s) {
            return luaL_loadstring(this, s);
        }

        static inline State *New() {
            return luaL_newstate();
        }

        inline const char *GSub(const char *s, const char *p, const char *r) {
            return luaL_gsub(this, s, p, r);
        }

        inline const char *FindTable(int idx, const char *name, int hintSize) {
            return luaL_findtable(this, idx, name, hintSize);
        }

        // MARK: Auxiliary miscellaneous functions

        inline void ArgCheck(int cond, int numArg, const char *extraMsg) {
            cond || luaL_argerror(this, numArg, extraMsg);
        }

        inline const char *CheckString(int arg) {
            return luaL_checklstring(this, arg, nullptr);
        }

        inline const char *OptString(int arg, const char *d) {
            return luaL_optlstring(this, arg, d, nullptr);
        }

        inline int CheckInt(int arg) {
            return static_cast<int>(luaL_checkinteger(this, arg));
        }

        inline int OptInt(int arg, int d) {
            return static_cast<int>(luaL_optinteger(this, arg, d));
        }

        inline long CheckLong(int arg) {
            return static_cast<long>(luaL_checkinteger(this, arg));
        }

        inline long OptLong(int arg, long d) {
            return static_cast<long>(luaL_optinteger(this, arg, d));
        }

        inline const char *CheckTypeName(int idx) {
            return lua_typename(this, lua_type(this, idx));
        }

        template<int R = LUA_MULTRET>
        inline int DoFile(const char *filename) {
            return luaL_loadfile(this, filename) || lua_pcall(this, 0, R, 0);
        }

        template<int R = LUA_MULTRET>
        inline int DoString(const char *s) {
            return luaL_loadstring(this, s) || lua_pcall(this, 0, R, 0);
        }

        inline void GetMetatable(const char *tName) {
            lua_getfield(this, LUA_REGISTRYINDEX, tName);
        }

        template<typename T, typename D>
        inline T Opt(D f, int arg, T d) {
            return lua_isnoneornil(this, arg) ? d : D(this, arg);
        }

        // MARK: Library export

        inline int OpenBase() {
            return luaopen_base(this);
        }

        inline int OpenTable() {
            return luaopen_table(this);
        }

        inline int OpenIO() {
            return luaopen_io(this);
        }

        inline int OpenOS() {
            return luaopen_os(this);
        }

        inline int OpenString() {
            return luaopen_string(this);
        }

        inline int OpenMath() {
            return luaopen_math(this);
        }

        inline int OpenDebug() {
            return luaopen_debug(this);
        }

        inline int OpenBit() {
            return luaopen_bit(this);
        }

        inline int OpenPackage() {
            return luaopen_package(this);
        }

        inline void OpenLibs() {
            luaL_openlibs(this);
        }
    };

    inline State *Open() {
        return luaL_newstate();
    }

    inline void XMove(State *from, State *to, int n) {
        lua_xmove(from, to, n);
    }

    /* hack */
    inline void SetLevel(State *from, State *to) {
        lua_setlevel(from, to);
    }
}


#endif //lua_hpp
