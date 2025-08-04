/*!
 * @brief Lumen - A modernized reinvention of Lua
 * @author Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/7/7
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */

#ifndef LUMEN_H
#define LUMEN_H

#include <cstdio>
#include <climits>
#include <cstddef>
#include <cstdarg>
#include <type_traits>

#include "luaconf.h"

namespace Lumen {
    // MARK: Number types

    using Byte = unsigned char;
    using Int32 = LUA_INT32;
    using UInt32 = LUA_UINT32;

    using Number = LUA_NUMBER;
    using Integer = LUA_INTEGER;
    using UInteger = LUA_UINTEGER;

    using MemorySize = LUA_UMEM;
    using MemoryDelta = LUA_MEM;

    using UACNumber = LUA_UAC_NUMBER; // Result of a `usual argument conversion` over lua_Number

    typedef LUA_ENUM(int, Index) {
        RegistryIndex = -10000,
        EnvIndex = -10001,
        GlobalIndex = -10002
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

    /**
     * GC Actions
     */
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

    /**
     * Comparison and arithmetic functions
     */
    typedef LUA_ENUM(int, ArithOp) {
        ArithOpAdd = 0,    /* ORDER TM */
        ArithOpSub = 1,
        ArithOpMul = 2,
        ArithOpDiv = 3,
        ArithOpMod = 4,
        ArithOpPow = 5,
        ArithOpUnm = 6
    };

    typedef LUA_ENUM(int, CompareOp) {
        CompareOpEQ = 0,
        CompareOpLT = 1,
        CompareOpLE = 2
    };

    namespace MetaMethod {
        /**
         * Names of meta method
         */
        typedef LUA_ENUM(int, Name) {
            NameIndex,
            NameNewIndex,
            NameGC,
            NameMode,
            NameEQ,  /* last tag method with `fast' access */
            NameAdd,
            NameSub,
            NameMul,
            NameDiv,
            NameMod,
            NamePow,
            NameUnm,
            NameLen,
            NameLT,
            NameLE,
            NameConcat,
            NameCall,
        };


    }

    constexpr const char *RegKeyLoaded = "_LOADED";
    constexpr const char *RegKeyPreload = "_PRELOAD";
    constexpr const char *RegKeyGlobals = "_G";

    typedef struct lua_State CState;

    struct IState;

    typedef int (*Delegate)(IState *L);

    typedef void *(*Allocator)(void *ud, void *ptr, UInteger oldSize, UInteger newSize);

    typedef const char *(*Reader)(IState *L, void *ud, UInteger *sz);

    typedef int (*Writer)(IState *L, const void *p, UInteger sz, void *ud);

    typedef int (*Function)(CState *L);

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

    typedef void (*Hook)(IState *L, DebugInfo *ar);

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
    typedef struct luaL_Reg Registry;

    inline int UpValueIndex(Index i) {
        return GlobalIndex - (i);
    }

    struct TypeInfo {
        Lumen::Type Type;

        inline bool IsNil() const { return Type == Lumen::TypeNil; } // NOLINT

        inline bool IsNumber() const { return Type == Lumen::TypeNumber; } // NOLINT

        inline bool IsString() const { return Type == Lumen::TypeString; } // NOLINT

        inline bool IsTable() const { return Type == Lumen::TypeTable; } // NOLINT

        inline bool IsFunction() const { return Type == Lumen::TypeFunction; } // NOLINT

        inline bool IsBoolean() const { return Type == Lumen::TypeBool; } // NOLINT

        inline bool IsUData() const { return Type == Lumen::TypeUserdata; } // NOLINT

        inline bool IsThread() const { return Type == Lumen::TypeThread; } // NOLINT

        inline bool IsLUData() const { return Type == Lumen::TypeLightUserdata; } // NOLINT
    };

    // Utils

    struct Hash {
        static UInt32 EncodeUInt32(const Byte *key, Lumen::UInteger len, UInt32 seed);
    };

    // MARK: Lumen Native Interface

    struct IObject;

    struct IState {
        // MARK: state manipulation

        LPP_API static IState *New(Allocator allocator, void *userdata);

        LPP_API IState *NewThread();

        LPP_API Delegate AtPanic(Delegate pInvoke);

        LPP_API const Lumen::Number *Version();

        // MARK: basic stack manipulation

        LPP_API int AbsIndex(int idx);

        LPP_API int GetTop();

        LPP_API void SetTop(int idx);

        LPP_API void PushValue(int idx);

        LPP_API void Remove(int idx);

        LPP_API void Insert(int idx);

        LPP_API void Replace(int idx);

        LPP_API void Copy(int fromIdx, int toIdx);

        LPP_API void Rotate(int idx, int n);

        LPP_API bool CheckStack(int size);

        // MARK: access functions (stack -> C)

        LPP_API bool IsNumber(int idx);

        LPP_API bool IsString(int idx);

        LPP_API bool IsDelegate(int idx);

        LPP_API bool IsUserdata(int idx);

        LPP_API Lumen::Type TypeId(int idx);

        LPP_API const char *TypeOf(int tp); // NOLINT

        inline const char *TypeName(int idx) {
            return TypeOf(TypeId(idx));
        }

        LPP_API void Arith(ArithOp op);

        LPP_API int Compare(int idx1, int idx2, ArithOp op);

        LPP_API bool Equal(int idx1, int idx2);

        LPP_API bool RawEqual(int idx1, int idx2);

        LPP_API bool LessThan(int idx1, int idx2);

        LPP_API Number ToNumber(int idx);

        LPP_API Integer ToInteger(int idx);

        LPP_API bool ToBoolean(int idx);

        LPP_API const char *ToString(int idx, UInteger *len);

        LPP_API UInteger ObjectLength(int idx);

        LPP_API Delegate ToDelegate(int idx);

        inline Function ToFunction(int idx) {
            return reinterpret_cast<Function>(ToDelegate(idx));
        }

        LPP_API void *ToUserdata(int idx);

        LPP_API IState *ToThread(int idx);

        LPP_API const void *ToPointer(int idx);

        // MARK: push functions (C -> stack)

        LPP_API void PushNil();

        LPP_API void PushNumber(Number n);

        LPP_API void PushInteger(Integer n);

        LPP_API const char *PushString(const char *s, UInteger length);

        LPP_API const char *PushString(const char *s);

        LPP_API const char *PushVFString(const char *fmt, va_list argP);

        LPP_API const char *PushFString(const char *fmt, ...);

        LPP_API void PushDelegate(Delegate invoke, int n = 0);

        inline void PushFunction(Function invoke, int n = 0) {
            PushDelegate(reinterpret_cast<Delegate>(invoke), n);
        }

        LPP_API void PushBoolean(int b);

        LPP_API void PushLightUserdata(void *p);

        LPP_API int PushThread();

        void PushObject(const Lumen::IObject *o);

        // MARK: get functions (Lua -> stack)

        LPP_API Lumen::Type GetTable(int idx);

        LPP_API Lumen::Type GetField(int idx, const char *k);

        LPP_API Lumen::Type RawGet(int idx);

        LPP_API Lumen::Type RawGetAt(int idx, int n);

        LPP_API Lumen::Type RawGetPtr(int idx, const void *p);

        LPP_API void CreateTable(int nArray, int nRec);

        LPP_API void *NewUserdata(UInteger size);

        LPP_API bool GetMetatable(int objIndex);

        LPP_API void GetFEnv(int idx);

        // MARK: set functions (stack -> Lua)

        LPP_API void SetTable(int idx);

        LPP_API void SetField(int idx, const char *k);

        LPP_API void RawSet(int idx);

        LPP_API void RawSetAt(int idx, int n);

        LPP_API void RawSetPtr(int idx, const void *p);

        LPP_API bool SetMetatable(int objIndex);

        LPP_API bool SetFEnv(int idx);

        // MARK: `load' and `call' functions (load and run Lua code)

        LPP_API void Call(int nargs, int nResults);

        LPP_API Lumen::Ret TryCall(int nargs, int nResults, int errFunc);

        // Try C Call
        LPP_API Lumen::Ret TryCall(Delegate invoke, void *userdata);

        // Try C Call
        inline Lumen::Ret TryCall(Function invoke, void *userdata) {
            return TryCall(reinterpret_cast<Delegate>(invoke), userdata);
        }

        LPP_API Lumen::Ret Load(Reader reader, void *data, const char *chunkName);

        LPP_API Lumen::Ret Dump(Writer writer, void *data);

        // MARK: coroutine functions

        LPP_API Lumen::Ret Yield(int nResults);

        LPP_API Lumen::Ret Resume(int nArgs);

        LPP_API Lumen::Ret Status();

        LPP_API bool CanYield();

        // MARK: garbage-collection function and options

        LPP_API int GC(GCAction what, int data);

        // MARK: miscellaneous functions

        LPP_API Lumen::Ret Error();

        LPP_API bool Next(int idx);

        LPP_API void Concat(int n);

        LPP_API void LengthOf(int idx);

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

        inline UInteger StringLength(int idx) {
            return ObjectLength(idx);
        }

        inline bool IsFunction(int idx) {
            return TypeId(idx) == TypeFunction;
        }

        inline bool IsTable(int idx) {
            return TypeId(idx) == TypeTable;
        }

        inline bool IsLightUserdata(int idx) {
            return TypeId(idx) == TypeLightUserdata;
        }

        inline bool IsNil(int idx) {
            return TypeId(idx) == TypeNil;
        }

        inline bool IsBoolean(int idx) {
            return TypeId(idx) == TypeBool;
        }

        inline bool IsThread(int idx) {
            return TypeId(idx) == TypeThread;
        }

        inline bool IsNone(int idx) {
            return TypeId(idx) == TypeNone;
        }

        inline bool IsNoneOrNil(int idx) {
            return TypeId(idx) <= 0;
        }

        template<UInteger S>
        inline void PushLiteral(const char (&s)[S]) {
            PushString(s, S - 1);
        }

        inline void SetGlobal(const char *key) {
            SetField(GlobalIndex, key);
        }

        inline Lumen::Type GetGlobal(const char *key) {
            return GetField(GlobalIndex, key);
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

        LPP_API void *GetUpValueId(int fIdx, int n);

        LPP_API void JoinUpValue(int fIdx1, int n1, int fIdx2, int n2);

        LPP_API bool SetHook(Hook func, HookMask mask, int count);

        LPP_API Hook GetHook();

        LPP_API Lumen::HookMask GetHookMask();

        LPP_API int GetHookCount();

        // MARK: Auxiliary basic APIs

        inline int GetN(int idx) {
            return static_cast<int>(ObjectLength(idx));
        }

        inline void SetN(int, UInteger) {} // NOLINT

        LPP_API void OpenLib(const char *name, const Interface *i, int nUpValue);

        inline void OpenLib(const char *name, const Registry *i, int nUpValue) {
            OpenLib(name, reinterpret_cast<const Interface *>(i), nUpValue);
        }

        LPP_API void Register(const Interface *i, int nUpValue);

        inline void Register(const Registry *i, int nUpValue) {
            Register(reinterpret_cast<const Interface *>(i), nUpValue);
        }

        inline void Register(const char *name, const Interface *i) {
            OpenLib(name, i, 0);
        }

        inline void Register(const char *name, const Registry *i) {
            OpenLib(name, i, 0);
        }

        inline void RegisterAt(int idx, const char *name) {
            GetField(Lumen::RegistryIndex, Lumen::RegKeyLoaded);
            PushValue(idx);
            SetField(-2, name);
            Pop();
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

        LPP_API void CheckType(int nArg, Lumen::Type t);

        LPP_API void CheckAny(int nArg);

        LPP_API bool NewMetatable(const char *tName);

        LPP_API void *TestUserdata(int ud, const char *tName);

        LPP_API void *TestUserdataInstance(int ud, const char *tName);

        template<typename T, bool R = true>
        inline T *TestInstance(int ud, const char *tName) {
            if constexpr (R) {
                return static_cast<T *>(TestUserdataInstance(ud, tName));
            } else {
                return static_cast<T *>(TestUserdata(ud, tName));
            }
        }

        LPP_API void *CheckUserdata(int ud, const char *tName);

        LPP_API void *CheckUserdataInstance(int ud, const char *tName);

        template<typename T, bool R = true>
        inline T *CheckInstance(int ud, const char *tName) {
            if constexpr (R) {
                return static_cast<T *>(CheckUserdataInstance(ud, tName));
            } else {
                return static_cast<T *>(CheckUserdata(ud, tName));
            }
        }

        LPP_API void Where(int lvl);

        LPP_API int Error(const char *fmt, ...);

        LPP_API int CheckOption(int nArg, const char *def, const char *const lst[]);

        LPP_API Lumen::Ref Ref(int t);

        LPP_API void Unref(int t, Lumen::Ref ref);

        LPP_API Lumen::Ret LoadFile(const char *filename);

        LPP_API Lumen::Ret LoadBuffer(const char *buff, UInteger size, const char *name);

        LPP_API Lumen::Ret LoadString(const char *s);

        LPP_API static IState *New();

        LPP_API const char *GSub(const char *s, const char *p, const char *r);

        LPP_API const char *FindTable(int idx, const char *name, int hintSize);

        LPP_API bool FindOrCreateTable(int idx, const char *name);

        LPP_API void Require(const char *modName, Lumen::Delegate loader, bool exported = false);

        inline void Require(const char *modName, Lumen::Function loader, bool exported = false) {
            Require(modName, reinterpret_cast<Lumen::Delegate>(loader), exported);
        }

        inline bool IsRequired(const char *modName) {
            GetField(Lumen::RegistryIndex, Lumen::RegKeyLoaded);
            GetField(-1, modName);
            auto ret = ToBoolean(-1);
            Pop(2);
            return ret;
        }

        LPP_API void PrintStack();

        // MARK: Auxiliary miscellaneous functions

        inline void ArgCheck(bool cond, int numArg, const char *extraMsg) {
            cond || ArgError(numArg, extraMsg);
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
            return TypeName(TypeId(idx));
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
        inline T Opt(T (Lumen::IState::*f)(int), int nArg, T def) {
            if (IsNoneOrNil(nArg)) {
                return def;
            }
            return (this->*f)(nArg);
        }

        // MARK: Library export

        LPP_API void OpenLibs();
    };

    struct Buffer {
        char *p;      /* current position in buffer */
        int level;    /* number of strings in the stack (level) */
        IState *L;
        char buffer[LUAL_BUFFERSIZE];

        inline void Init(Lumen::IState *l) {
            L = l;
            p = buffer;
            level = 0;
        }

        LPP_API char *Prepare();

        LPP_API void AddString(const char *s, size_t l);

        LPP_API void AddString(const char *s);

        LPP_API void AddValue();

        LPP_API void PushResult();

        inline void AddChar(char c) {
            if (p >= (buffer + LUAL_BUFFERSIZE)) {
                Prepare();
            }
            *p++ = c;
        }

        inline void AddChar(int d) {
            AddChar((char) d);
        }

        inline void PutChar(const char c) {
            AddChar(c);
        }

        inline void AddSize(int size) {
            p += size;
        }
    };

    struct IObject {
        LPP_API bool IsNil() const; // NOLINT

        LPP_API bool IsNumber() const; // NOLINT

        LPP_API bool IsBoolean() const; // NOLINT

        LPP_API bool IsString() const; // NOLINT

        LPP_API bool IsTable() const;// NOLINT

        LPP_API bool IsDelegate() const; // NOLINT

        LPP_API bool IsUData() const; // NOLINT

        LPP_API bool IsLUData() const; // NOLINT

        LPP_API Number ToNumber() const; // NOLINT

        LPP_API bool ToBoolean() const; // NOLINT

        LPP_API const Number *ToNumberRef() const; // NOLINT

        LPP_API struct IString *ToString();

        LPP_API struct ITable *ToTable();

        LPP_API Delegate ToDelegate();

        LPP_API struct IUserdata *ToUserdata();

        LPP_API void *ToLightUserdata();

        LPP_API static IObject *Get(IState *L, int idx);
    };

    struct IBase;

    struct ICoroutine;

    struct IPackage;

    struct IIO;

    struct IOS;

    struct IString {
        struct Context;

        LPP_API const char *CString();

        LPP_API UInteger Length();

        LPP_API static IString *Get(IState *L, int idx);

        LPP_API static IString *New(IState *L, const char *cStr);

        LPP_API static IString *New(IState *L, const char *cStr, UInteger length);
    };

    struct ITable {
        LPP_API IObject *operator[](int n);

        LPP_API IObject *operator[](const IObject *object);

        LPP_API IObject *operator[](IString *str);

        LPP_API void Insert(IState *L, int n, const IObject *val);

        LPP_API void Insert(IState *L, IString *key, const IObject *val);

        LPP_API void Insert(IState *L, const IObject *key, const IObject *val);

        LPP_API void Insert(IState *L, IString *key, Delegate val);

        inline void Insert(IState *L, const char *key, const IObject *val) {
            Insert(L, Lumen::IString::New(L, key), val);
        }

        inline void Insert(IState *L, const char *key, UInteger length, const IObject *val) {
            Insert(L, Lumen::IString::New(L, key, length), val);
        }

        inline void Insert(IState *L, const char *key, Delegate val) {
            Insert(L, Lumen::IString::New(L, key), val);
        }

        inline void Insert(IState *L, const char *key, UInteger length, Delegate val) {
            Insert(L, Lumen::IString::New(L, key, length), val);
        }

        LPP_API ITable *GetMetatable();

        LPP_API static ITable *Get(IState *L, int idx);

        LPP_API static ITable *New(IState *L);
    };

    struct IMath;

    struct IUTF8;

    struct IBit;

    struct IDebug;

    template<typename T>
    LPP_API int Open(IState *L);

    LPP_API IState *Open();

    LPP_API void Close(IState *&L);

    LPP_API void XMove(IState *from, IState *to, int n);

    /* hack */
    LPP_API void SetLevel(IState *from, IState *to);
}

#endif //LUMEN_H
