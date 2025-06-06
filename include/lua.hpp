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

#include "lua.h"
#include "lualib.h"

namespace Lua {
    using Byte = unsigned char;
    using Number = LUA_NUMBER;
    using Integer = LUA_INTEGER;

    using Allocator = lua_Alloc;

    using Reader = lua_Reader;

    using Writer = lua_Writer;

    using Function = lua_CFunction;

    using BasicState = lua_State;

    struct State;

//    using Delegate = lua_CFunction;
    typedef int (*Delegate)(Lua::State *L);

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

    using Hook = lua_Hook;

    struct Interface {
        const char *Name;
        Delegate Invoke;
    };

    struct Registry {
        const char *Name;
        Function Invoke;
    };

    typedef LUA_ENUM(int, Ret) {
        RetOK = LUA_OK,
        RetYield = LUA_YIELD,
        RetErrRun = LUA_ERRRUN,
        RetErrSyntax = LUA_ERRSYNTAX,
        RetErrMem = LUA_ERRMEM,
        RetErr = LUA_ERRERR
    };

    enum {
        RetErrFile = LUA_ERRERR + 1
    };

    typedef LUA_ENUM(int, Index) {
        IndexRegistry = LUA_GLOBALSINDEX,
        IndexEnv = LUA_ENVIRONINDEX,
        IndexGlobal = LUA_GLOBALSINDEX
    };

    typedef LUA_ENUM(int, Ref) {
        RefNothing = -2,
        RefNil = -1
    };

    inline int IndexUpValue(Index i) {
        return LUA_GLOBALSINDEX - (i);
    }

    struct State {
        // MARK: state manipulation

        LPP_API static State *New(Allocator allocator, void *userdata);

        LPP_API State *NewThread();

        LPP_API Delegate AtPanic(Delegate pInvoke);

        // MARK: basic stack manipulation

        LPP_API int GetTop();

        LPP_API void SetTop(int idx);

        LPP_API void PushValue(int idx);

        LPP_API void Remove(int idx);

        LPP_API void Insert(int idx);

        LPP_API void Replace(int idx);

        LPP_API int CheckStack(int size);

        // MARK: access functions (stack -> C)

        LPP_API int IsNumber(int idx);

        LPP_API int IsString(int idx);

        LPP_API int IsDelegate(int idx);

        LPP_API int IsUserdata(int idx);

        LPP_API int Type(int idx);

        LPP_API const char *TypeName(int t);

        LPP_API int Equal(int idx1, int idx2);

        LPP_API int RawEqual(int idx1, int idx2);

        LPP_API int LessThan(int idx1, int idx2);

        LPP_API Number ToNumber(int idx);

        LPP_API Integer ToInteger(int idx);

        LPP_API bool ToBoolean(int idx);

        LPP_API const char *ToString(int idx, size_t *len);

        LPP_API size_t ObjectLength(int idx);

        LPP_API Delegate ToDelegate(int idx);

        LPP_API Function ToFunction(int idx);

        LPP_API void *ToUserdata(int idx);

        LPP_API State ToThread(int idx);

        LPP_API const void *ToPointer(int idx);

        // MARK: push functions (C -> stack)

        LPP_API void PushNil();

        LPP_API void PushNumber(Number n);

        LPP_API void PushInteger(Integer n);

        LPP_API void PushString(const char *s, size_t length);

        LPP_API void PushString(const char *s);

        LPP_API const char *PushVFString(const char *fmt, va_list argP);

        LPP_API const char *PushFString(const char *fmt, ...);

        LPP_API void PushDelegate(Delegate invoke, int n);

        LPP_API void PushFunction(Function invoke, int n);

        LPP_API void PushBoolean(int b);

        LPP_API void PushLightUserdata(void *p);

        LPP_API int PushThread();

        // MARK: get functions (LuaToState(this)ua -> stack)

        LPP_API void GetTable(int idx);

        LPP_API void GetField(int idx, const char *k);

        LPP_API void RawGet(int idx);

        LPP_API void RawGetAt(int idx, int n);

        LPP_API void CreateTable(int nArray, int nRec);

        LPP_API void *NewUserdata(size_t size);

        LPP_API int GetMetatable(int objIndex);

        LPP_API void GetFEnv(int idx);

        // MARK: set functions (stack -> Lua)

        LPP_API void SetTable(int idx);

        LPP_API void SetField(int idx, const char *k);

        LPP_API void RawSet(int idx);

        LPP_API void RawSetAt(int idx, int n);

        LPP_API int SetMetatable(int objIndex);

        LPP_API int SetFEnv(int idx);

        // MARK: `load' and `call' functions (load and run Lua code)

        LPP_API void Call(int nargs, int nResults);

        LPP_API int TryCall(int nargs, int nResults, int errFunc);

        // Try C Call
        LPP_API int TryCall(Delegate invoke, void *userdata);

        // Try C Call
        LPP_API int TryCall(Function invoke, void *userdata);

        LPP_API int Load(Reader reader, void *data, const char *chunkName);

        LPP_API int Dump(Writer writer, void *data);

        // MARK: coroutine functions

        LPP_API int Yield(int nResults);

        LPP_API int Resume(int nArgs);

        LPP_API int Status();

        // MARK: garbage-collection function and options

        LPP_API int GC(int what, int data);

        // MARK: miscellaneous functions

        LPP_API int Error();

        LPP_API int Next(int idx);

        LPP_API void Concat(int n);

        LPP_API Allocator GetAllocator(void **ud);

        LPP_API void SetAllocator(Allocator f, void *ud);

        inline void Pop(int n) {
            SetTop(-(n) - 1);
        }

        inline void NewTable() {
            CreateTable(0, 0);
        }

        inline void Register(const char *name, Delegate invoke) {
            PushDelegate(invoke);
            SetGlobal(name);
        }

        inline void Register(const char *name, Function invoke) {
            PushFunction(invoke);
            SetGlobal(name);
        }

        inline void PushDelegate(Delegate invoke) {
            PushDelegate(invoke, 0);
        }

        inline void PushFunction(Function invoke) {
            PushFunction(invoke, 0);
        }

        inline size_t StringLength(int idx) {
            return ObjectLength(idx);
        }

        inline bool IsFunction(int idx) {
            return Type(idx) == LUA_TFUNCTION;
        }

        inline bool IsTable(int idx) {
            return Type(idx) == LUA_TTABLE;
        }

        inline bool IsLightUserdata(int idx) {
            return Type(idx) == LUA_TLIGHTUSERDATA;
        }

        inline bool IsNil(int idx) {
            return Type(idx) == LUA_TNIL;
        }

        inline bool IsBoolean(int idx) {
            return Type(idx) == LUA_TBOOLEAN;
        }

        inline bool IsThread(int idx) {
            return Type(idx) == LUA_TTHREAD;
        }

        inline bool IsNone(int idx) {
            return Type(idx) == LUA_TNONE;
        }

        inline bool IsNoneOrNil(int idx) {
            return Type(idx) <= 0;
        }

        template<size_t S>
        inline void PushLiteral(const char (&s)[S]) {
            PushString(s, S - 1);
        }

        inline void SetGlobal(const char *key) {
            SetField(LUA_GLOBALSINDEX, key);
        }

        inline void GetGlobal(const char *key) {
            GetField(LUA_GLOBALSINDEX, key);
        }

        const char *ToString(int idx) {
            return ToString(idx, nullptr);
        }

        // MARK: compatibility macros and functions

        inline void GetRegistry() {
            PushValue(LUA_REGISTRYINDEX);
        }

        inline int GetGCCount() {
            return GC(LUA_GCCOUNT, 0);
        }

        // MARK: debug

        LPP_API int GetStack(int level, DebugInfo *ar);

        LPP_API int GetInfo(const char *what, DebugInfo *ar);

        LPP_API const char *GetLocal(const DebugInfo *ar, int n);

        LPP_API const char *SetLocal(const DebugInfo *ar, int n);

        LPP_API const char *GetUpValue(int funcIndex, int n);

        LPP_API const char *SetUpValue(int funcIndex, int n);

        LPP_API int SetHook(Hook func, int mask, int count);

        LPP_API Hook GetHook();

        LPP_API int GetHookMask();

        LPP_API int GetHookCount();

        // MARK: Auxiliary basic APIs

        LPP_API void OpenLib(const char *name, const Interface *i, int nUpValue);

        inline void Register(const char *name, const Interface *i) {
            OpenLib(name, i, 0);
        }

        LPP_API int GetMetaField(int obj, const char *e);

        LPP_API int CallMeta(int obj, const char *e);

        LPP_API int TypeError(int nArg, const char *tName);

        LPP_API int ArgError(int nArg, const char *extraMsg);

        LPP_API const char *CheckString(int nArg, size_t *length);

        LPP_API const char *OptString(int nArg, const char *def, size_t *length);

        LPP_API Number CheckNumber(int nArg);

        LPP_API Number OptNumber(int nArg, Number def);

        LPP_API Integer CheckInteger(int nArg);

        LPP_API Integer OptInteger(int nArg, Integer def);

        LPP_API void CheckStack(int sz, const char *msg);

        LPP_API void CheckType(int nArg, int t);

        LPP_API void CheckAny(int nArg);

        LPP_API int NewMetatable(const char *tName);

        LPP_API void *CheckUserdata(int ud, const char *tName);

        LPP_API void Where(int lvl);

        LPP_API int Error(const char *fmt, ...);

        LPP_API int CheckOption(int nArg, const char *def, const char *const lst[]);

        LPP_API int Ref(int t);

        LPP_API void Unref(int t, int ref);

//        void Unref(int ref);

        LPP_API int LoadFile(const char *filename);

        LPP_API int LoadBuffer(const char *buff, size_t size, const char *name);

        LPP_API int LoadString(const char *s);

        LPP_API static State *New();

        LPP_API const char *GSub(const char *s, const char *p, const char *r);

        LPP_API const char *FindTable(int idx, const char *name, int hintSize);

        // MARK: Auxiliary miscellaneous functions

        inline void ArgCheck(int cond, int numArg, const char *extraMsg) {
            (cond) || ArgError(numArg, extraMsg);
        }

        inline const char *CheckString(int arg) {
            return CheckString(arg, nullptr);
        }

        inline const char *OptString(int arg, const char *d) {
            return OptString(arg, d, nullptr);
        }

        inline int CheckInt(int arg) {
            return static_cast<int>(CheckInteger(arg));
        }

        inline int OptInt(int arg, int d) {
            return static_cast<int>(OptInteger(arg, d));
        }

        inline long CheckLong(int arg) {
            return static_cast<long>(CheckInteger(arg));
        }

        inline long OptLong(int arg, long d) {
            return static_cast<long>(OptInteger(arg, d));
        }

        inline const char *CheckTypeName(int idx) {
            return TypeName(Type(idx));
        }

        template<int R = LUA_MULTRET>
        inline int DoFile(const char *filename) {
            return LoadFile(filename) || TryCall(0, R, 0);
        }

        template<int R = LUA_MULTRET>
        inline int DoString(const char *s) {
            return LoadString(s) || TryCall(0, R, 0);
        }

        inline void GetMetatable(const char *tName) {
            GetField(LUA_REGISTRYINDEX, tName);
        }

        template<typename T>
        inline T Opt(T (Lua::State::*f)(int), int nArg, T def) {
            if (IsNoneOrNil(nArg)) {
                return def;
            }
            return (this->*f)(nArg);
        }

        // MARK: Library export

        inline int OpenBase() {
            return luaopen_base(L);
        }

        inline int OpenTable() {
            return luaopen_table(L);
        }

        inline int OpenIO() {
            return luaopen_io(L);
        }

        inline int OpenOS() {
            return luaopen_os(L);
        }

        inline int OpenString() {
            return luaopen_string(L);
        }

        inline int OpenMath() {
            return luaopen_math(L);
        }

        inline int OpenDebug() {
            return luaopen_debug(L);
        }

        inline int OpenBit() {
            return luaopen_bit(L);
        }

        inline int OpenPackage() {
            return luaopen_package(L);
        }

        LPP_API void OpenLibs();

        BasicState *L;
    };

    LPP_API State *Open();

    LPP_API void Close(State *(&L));

    LPP_API void XMove(State *from, State *to, int n);

    /* hack */
    LPP_API void SetLevel(State *from, State *to);
}


#endif //lua_hpp
