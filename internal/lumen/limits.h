/*!
 * @brief Limits, basic types, and some other `installation-dependent` definitions
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_LIMITS_H
#define LUMEN_LIMITS_H

#include <climits>
#include <cstddef>
#include <limits>

#if defined(LUA_CORE_DEBUG)
#include <cassert>
#endif

struct LumenState;

#define LUAI_DELEGATE Lumen::Delegate
#define LUAI_READER Lumen::Reader
#define LUAI_WRITER Lumen::Writer
#define LUAI_ALLOCATOR Lumen::Allocator
#define LUAI_DEBUGINFO Lumen::DebugInfo
#define LUAI_HOOK Lumen::Hook

#include "luaconf.h"

namespace Lumen {
    using Byte = unsigned char;
    using Int32 = LUAI_INT32;
    using UInt32 = LUAI_UINT32;

    using Number = LUA_NUMBER;
    using Integer = LUA_INTEGER;
    using UInteger = LUA_UINTEGER;

    using MemorySize = LUAI_UMEM;
    using MemoryDelta = LUAI_MEM;

    using UACNumber = LUAI_UACNUMBER; // Result of a `usual argument conversion' over lua_Number

    /*
    ** pseudo-indices
    */
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

    /**
     * basic types
     */
    typedef LUA_ENUM(int, Type) {
        TypeNil = 0,
        TypeBool = 1,
        TypeLightUserdata = 2,
        TypeNumber = 3,
        TypeString = 4,
        TypeTable = 5,
        TypeFunction = 6,
        TypeUserdata = 7,
        TypeThread = 8,
        TypeProto = 9,
        TypeUpValue = 10,
        TypeDeadKey = 11
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

    using State = LumenState;

    struct TypeInfo {
        Lumen::Type Type;
    };

    /*
    ** Union of all collectable objects
    */
    struct GCObject;

    /**
     * Union of all Lua values
     */
    union Variant {
        Lumen::GCObject *gc;
        void *p;
        Lumen::Number n;
        int b;
    };

    struct Object : TypeInfo {
        Variant value;

        const char *GetUpValueInfo(int n, Lumen::Object **val);
    };

    /**
     * Value is a pointer to Object
     * and index to stack elements
     */
    typedef Lumen::Object *Value;

    /**
     * Type for virtual-machine instructions
     * must be an unsigned with (at least) 4 bytes (see details in opcodes.h)
     */
    using Instruction = UInt32;

    typedef int (*Delegate)(Lumen::State *L);

    typedef const char *(*Reader)(Lumen::State *L, void *ud, Lumen::UInteger *sz);

    typedef int (*Writer)(Lumen::State *L, const void *p, Lumen::UInteger sz, void *ud);

    typedef void *(*Allocator)(void *ud, void *ptr, Lumen::UInteger oldSize, Lumen::UInteger newSize);

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

    typedef void (*Hook)(Lumen::State *L, Lumen::DebugInfo *ar);

    inline constexpr Lumen::UInteger MaxSize = std::numeric_limits<Lumen::UInteger>::max() - 2;

    inline constexpr Lumen::UInteger MaxUMemory = std::numeric_limits<MemorySize>::max() - 2;

    inline constexpr Lumen::UInteger MaxInt = INT_MAX - 2;

    inline constexpr Lumen::UInteger MinStack = LUA_MINSTACK;

    /* maximum stack for a Lua function */
    inline constexpr Lumen::UInteger MaxStack = 250;

    /* minimum size for the string table (must be power of 2) */
#ifndef MINSTRTABSIZE
    inline constexpr Lumen::UInteger MinStringTableSize = 32;
#else
    inline constexpr Lumen::UInteger MinStringTableSize = MINSTRTABSIZE;
#endif

    /* minimum size for string buffer */
#ifndef LUA_MINBUFFER
    inline constexpr Lumen::UInteger MinBufferSize = 32;
#else
    inline constexpr Lumen::UInteger MinBufferSize = LUA_MINBUFFER;
#endif

    inline constexpr Lumen::UInteger BitsInt = sizeof(int);

    /* tags for values visible from Lua */
    inline constexpr Lumen::UInteger LastType = Lumen::TypeThread;

    inline constexpr Lumen::UInteger TypeCount = LastType + 1;
}

#define LumenDo(block) do { block } while(0)

/*
** conversion of pointer to integer
** this is for hashing only; there is no problem if the integer
** cannot hold the whole pointer value
*/
#define LumenIntPoint(p)  ((unsigned int)(Lumen::MemorySize)(p))

/* internal assertions for in-house debugging */
#if defined(LUA_CORE_DEBUG)
#define LumenAssert(e)         assert(e)
#define LumenCheckExp(c, e)    (LumenAssert(c), (e))
#else
#define LumenAssert(e)         ((void) 0)
#define LumenCheckExp(c, e)    (e)
#endif


#ifndef UNUSED
#define UNUSED(x)    ((void)(x))    /* to avoid warnings */
#endif


#ifndef cast
#define cast(t, exp)    ((t)(exp))
#endif

#define cast_byte(i)    cast(Lumen::Byte, (i))
#define cast_num(i)     cast(Lumen::Number, (i))
#define cast_int(i)     cast(int, (i))


#ifndef lua_lock
#define LumenLock(L)    ((void) 0)
#else
#define LumenLock(L)    lua_lock
#endif

#ifndef lua_unlock
#define LumenUnlock(L)    ((void) 0)
#else
#define LumenUnlock(L)    lua_unlock
#endif

#ifndef luai_threadyield
#define LumenThreadYield(L)    LumenDo(LumenUnlock(L); LumenLock(L);)
#else
#define LumenThreadYield       luai_threadyield
#endif

/*
** macro to control inclusion of some hard tests on stack reallocation
*/
#ifndef HARDSTACKTESTS
#define LumenCondHardStackTests(x)    ((void)0)
#else
#define LumenCondHardStackTests(x)    x
#endif

#endif
