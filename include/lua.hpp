/*!
 * @brief Lumen C++ FrontEnd for Lua
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
#include <cstdarg>
#include <type_traits>

#include "luaconf.h"

#ifndef LUA_SIGNATURE
#define LUA_SIGNATURE    "\033Lua"
#endif

struct LumenState;

namespace Lua {
    using Byte = unsigned char;
    using Number = LUA_NUMBER;
    using Integer = LUA_INTEGER;
    using UInteger = LUA_UINTEGER;

    using CState = LumenState;

    struct State;

    typedef void *(*Allocator)(void *ud, void *ptr, UInteger oldSize, UInteger newSize);

    typedef const char *(*Reader)(State *L, void *ud, UInteger *sz);

    typedef int (*Writer)(State *L, const void *p, UInteger sz, void *ud);

    typedef int (*Function)(CState *L);

    typedef int (*Delegate)(Lua::State *L);

    /*
    ** Hook Event codes
    */
    typedef LUA_ENUM(int, HookEvent) {
        HookCall = 0,
        HookRet = 1,
        HookLine = 2,
        HookCount = 3,
        HookTailRet = 4
    };

    /*
    ** Hook event masks
    */
    typedef LUA_ENUM(int, HookMask) {
        HookMaskCall = (1 << HookCall),
        HookMaskRet = (1 << HookRet),
        HookMaskLine = (1 << HookLine),
        HookMaskCount = (1 << HookCount)
    };

    struct DebugInfo {
        HookEvent Event;
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

    typedef void (*Hook)(State *L, DebugInfo *ar);

    /**
     * Interface for wrapping APIs to Lua
     */
    struct Interface {
        const char *Name;
        Delegate Invoke;
    };

    /**
     * Fallback support to old C API wrapper
     */
    struct Registry {
        const char *Name;
        Function Invoke;
    };

    typedef LUA_ENUM(int, Ret) {
        RetMul = -1,
        RetOK = 0,
        RetYield = 1,
        RetErrRun = 2,
        RetErrSyntax = 3,
        RetErrMem = 4,
        RetErr = 5,
        RetErrFile = RetErr + 1
    };

    typedef LUA_ENUM(int, Index) {
        RegistryIndex = -10000,
        EnvIndex = -10001,
        GlobalIndex = -10002
    };

    inline int UpValueIndex(Index i) {
        return GlobalIndex - (i);
    }

    typedef LUA_ENUM(int, Ref) {
        RefNothing = -2,
        RefNil = -1
    };

    /**
     * basic types
     */
    typedef LUA_ENUM(int, Type) {
        TypeNone = -1,
        TypeNil = 0,
        TypeBool = 1,
        TypeLightUserdata = 2,
        TypeNumber = 3,
        TypeString = 4,
        TypeTable = 5,
        TypeFunction = 6,
        TypeUserdata = 7,
        TypeThread = 8
    };

    typedef LUA_ENUM(int, GCAction) {
        GCStop = 0,
        GCRestart = 1,
        GCCollect = 2,
        GCCount = 3,
        GCCountB = 4,
        GCStep = 5,
        GCSetPause = 6,
        GCSetStepMul = 7
    };

    struct TypeInfo {
        Lua::Byte Type;
    };

    struct Object : TypeInfo {
    };

    struct String {
        struct Context;
    };

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

        LPP_API bool CheckStack(int size);

        // MARK: access functions (stack -> C)

        LPP_API bool IsNumber(int idx);

        LPP_API bool IsString(int idx);

        LPP_API bool IsDelegate(int idx);

        LPP_API bool IsUserdata(int idx);

        LPP_API Lua::Type Type(int idx);

        LPP_API const char *TypeName(int t) const;

        LPP_API bool Equal(int idx1, int idx2);

        LPP_API bool RawEqual(int idx1, int idx2);

        LPP_API bool LessThan(int idx1, int idx2);

        LPP_API Number ToNumber(int idx);

        LPP_API Integer ToInteger(int idx);

        LPP_API bool ToBoolean(int idx);

        LPP_API const char *ToString(int idx, UInteger *len);

        LPP_API UInteger ObjectLength(int idx);

        LPP_API Delegate ToDelegate(int idx);

        LPP_API Function ToFunction(int idx);

        LPP_API void *ToUserdata(int idx);

        LPP_API State *ToThread(int idx);

        LPP_API const void *ToPointer(int idx);

        // MARK: push functions (C -> stack)

        LPP_API void PushNil();

        LPP_API void PushNumber(Number n);

        LPP_API void PushInteger(Integer n);

        LPP_API void PushString(const char *s, UInteger length);

        LPP_API void PushString(const char *s);

        LPP_API const char *PushVFString(const char *fmt, va_list argP);

        LPP_API const char *PushFString(const char *fmt, ...);

        LPP_API void PushDelegate(Delegate invoke, int n);

        LPP_API void PushFunction(Function invoke, int n);

        LPP_API void PushBoolean(int b);

        LPP_API void PushLightUserdata(void *p);

        LPP_API int PushThread();

        // MARK: get functions (Lua -> stack)

        LPP_API void GetTable(int idx);

        LPP_API void GetField(int idx, const char *k);

        LPP_API void RawGet(int idx);

        LPP_API void RawGetAt(int idx, int n);

        LPP_API void CreateTable(int nArray, int nRec);

        LPP_API void *NewUserdata(UInteger size);

        LPP_API bool GetMetatable(int objIndex);

        LPP_API void GetFEnv(int idx);

        // MARK: set functions (stack -> Lua)

        LPP_API void SetTable(int idx);

        LPP_API void SetField(int idx, const char *k);

        LPP_API void RawSet(int idx);

        LPP_API void RawSetAt(int idx, int n);

        LPP_API bool SetMetatable(int objIndex);

        LPP_API bool SetFEnv(int idx);

        // MARK: `load' and `call' functions (load and run Lua code)

        LPP_API void Call(int nargs, int nResults);

        LPP_API Lua::Ret TryCall(int nargs, int nResults, int errFunc);

        // Try C Call
        LPP_API Lua::Ret TryCall(Delegate invoke, void *userdata);

        // Try C Call
        LPP_API Lua::Ret TryCall(Function invoke, void *userdata);

        LPP_API Lua::Ret Load(Reader reader, void *data, const char *chunkName);

        LPP_API Lua::Ret Dump(Writer writer, void *data);

        // MARK: coroutine functions

        LPP_API Lua::Ret Yield(int nResults);

        LPP_API Lua::Ret Resume(int nArgs);

        LPP_API Lua::Ret Status();

        // MARK: garbage-collection function and options

        LPP_API int GC(GCAction what, int data);

        // MARK: miscellaneous functions

        LPP_API Lua::Ret Error();

        LPP_API bool Next(int idx);

        LPP_API void Concat(int n);

        LPP_API Allocator GetAllocator(void **ud);

        LPP_API void SetAllocator(Allocator f, void *ud);

        inline void Pop(int n = 1) {
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

        inline UInteger StringLength(int idx) {
            return ObjectLength(idx);
        }

        inline bool IsFunction(int idx) {
            return Type(idx) == TypeFunction;
        }

        inline bool IsTable(int idx) {
            return Type(idx) == TypeTable;
        }

        inline bool IsLightUserdata(int idx) {
            return Type(idx) == TypeLightUserdata;
        }

        inline bool IsNil(int idx) {
            return Type(idx) == TypeNil;
        }

        inline bool IsBoolean(int idx) {
            return Type(idx) == TypeBool;
        }

        inline bool IsThread(int idx) {
            return Type(idx) == TypeThread;
        }

        inline bool IsNone(int idx) {
            return Type(idx) == TypeNone;
        }

        inline bool IsNoneOrNil(int idx) {
            return Type(idx) <= 0;
        }

        template<UInteger S>
        inline void PushLiteral(const char (&s)[S]) {
            PushString(s, S - 1);
        }

        inline void SetGlobal(const char *key) {
            SetField(GlobalIndex, key);
        }

        inline void GetGlobal(const char *key) {
            GetField(GlobalIndex, key);
        }

        const char *ToString(int idx) {
            return ToString(idx, nullptr);
        }

        LPP_API bool InstanceOf(int idxChild, int idxSuper);

        // MARK: compatibility fast call functions

        inline void GetRegistry() {
            PushValue(RegistryIndex);
        }

        inline int GetGCCount() {
            return GC(GCCount, 0);
        }

        // MARK: debug

        LPP_API bool GetStack(int level, DebugInfo *ar);

        LPP_API bool GetInfo(const char *what, DebugInfo *ar);

        LPP_API const char *GetLocal(const DebugInfo *ar, int n);

        LPP_API const char *SetLocal(const DebugInfo *ar, int n);

        LPP_API const char *GetUpValue(int funcIndex, int n);

        LPP_API const char *SetUpValue(int funcIndex, int n);

        LPP_API bool SetHook(Hook func, HookMask mask, int count);

        LPP_API Hook GetHook();

        LPP_API Lua::HookMask GetHookMask();

        LPP_API int GetHookCount();

        // MARK: Auxiliary basic APIs

        LPP_API void OpenLib(const char *name, const Interface *i, int nUpValue);

        inline void Register(const char *name, const Interface *i) {
            OpenLib(name, i, 0);
        }

        LPP_API bool GetMetaField(int obj, const char *e);

        LPP_API bool CallMeta(int obj, const char *e);

        LPP_API int TypeError(int nArg, const char *tName);

        LPP_API int ArgError(int nArg, const char *extraMsg);

        LPP_API const char *CheckString(int nArg, UInteger *length);

        LPP_API const char *OptString(int nArg, const char *def, UInteger *length);

        LPP_API Number CheckNumber(int nArg);

        LPP_API Number OptNumber(int nArg, Number def);

        LPP_API Integer CheckInteger(int nArg);

        LPP_API Integer OptInteger(int nArg, Integer def);

        LPP_API void CheckStack(int sz, const char *msg);

        LPP_API void CheckType(int nArg, int t);

        LPP_API void CheckAny(int nArg);

        LPP_API bool NewMetatable(const char *tName);

        LPP_API void *CheckUserdata(int ud, const char *tName);

        LPP_API void Where(int lvl);

        LPP_API int Error(const char *fmt, ...);

        LPP_API int CheckOption(int nArg, const char *def, const char *const lst[]);

        LPP_API Lua::Ref Ref(int t);

        LPP_API void Unref(int t, Lua::Ref ref);

//        void Unref(int ref);

        LPP_API Lua::Ret LoadFile(const char *filename);

        LPP_API Lua::Ret LoadBuffer(const char *buff, UInteger size, const char *name);

        LPP_API Lua::Ret LoadString(const char *s);

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

        template<int R = RetMul>
        inline int DoFile(const char *filename) {
            return LoadFile(filename) || TryCall(0, R, 0);
        }

        template<int R = RetMul>
        inline int DoString(const char *s) {
            return LoadString(s) || TryCall(0, R, 0);
        }

        inline void GetMetatable(const char *tName) {
            GetField(RegistryIndex, tName);
        }

        template<typename T>
        inline T Opt(T (Lua::State::*f)(int), int nArg, T def) {
            if (IsNoneOrNil(nArg)) {
                return def;
            }
            return (this->*f)(nArg);
        }

        // MARK: Library export

        LPP_API int OpenBase();

        LPP_API int OpenTable();

        LPP_API int OpenIO();

        LPP_API int OpenOS();

        LPP_API int OpenString();

        LPP_API int OpenMath();

        LPP_API int OpenDebug();

        LPP_API int OpenBit();

        LPP_API int OpenPackage();

        LPP_API void OpenLibs();
    };

    struct Buffer {
        LPP_API void Push(char c);

        LPP_API void Push(const char *cStr);

        LPP_API void Push(const void *cBuffer, UInteger size);

        LPP_API void Reverse();

        LPP_API void Reserve(UInteger size);

        LPP_API void Resize(UInteger size);

        LPP_API UInteger Length();

        LPP_API void Clear();

        LPP_API char *CString();

        LPP_API void *CBuffer();

        LPP_API  void AddValue(Lua::State *L);

        LPP_API static Buffer *Get();
    };

    inline State *Open() {
        return Lua::State::New();
    }

    LPP_API void Close(State *(&L));

    LPP_API void XMove(State *from, State *to, int n);

    /* hack */
    LPP_API void SetLevel(State *from, State *to);
}


#endif //lua_hpp
