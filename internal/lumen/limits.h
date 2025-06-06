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

    using MemorySize = LUAI_UMEM;
    using MemoryDelta = LUAI_MEM;

    using UACNumber = LUAI_UACNUMBER; // Result of a `usual argument conversion' over lua_Number

    using State = LumenState;

    struct BasicObject {
        Lumen::Byte Type;
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

    struct Value : BasicObject {
        Variant value;
    };

    typedef Lumen::Value *StkId;  /* index to stack elements */

    /**
     * Type for virtual-machine instructions
     * must be an unsigned with (at least) 4 bytes (see details in lopcodes.h)
     */
    using Instruction = UInt32;

    typedef int (*Delegate)(Lumen::State *L);

    typedef const char *(*Reader)(Lumen::State *L, void *ud, size_t *sz);

    typedef int (*Writer)(Lumen::State *L, const void *p, size_t sz, void *ud);

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

    typedef void (*Hook)(Lumen::State *L, Lumen::DebugInfo *ar);

    inline constexpr size_t MaxSize = std::numeric_limits<size_t>::max() - 2;

    inline constexpr size_t MaxUMemory = std::numeric_limits<MemorySize>::max() - 2;

    inline constexpr size_t MaxInt = INT_MAX - 2;

    inline constexpr size_t MinStack = LUA_MINSTACK;

    /* maximum stack for a Lua function */
    inline constexpr size_t MaxStack = 250;

    /* minimum size for the string table (must be power of 2) */
#ifndef MINSTRTABSIZE
    inline constexpr size_t MinStringTableSize = 32;
#else
    inline constexpr size_t MinStringTableSize = MINSTRTABSIZE;
#endif

    /* minimum size for string buffer */
#ifndef LUA_MINBUFFER
    inline constexpr size_t MinBufferSize = 32;
#else
    inline constexpr size_t MinBufferSize = LUA_MINBUFFER;
#endif

    inline constexpr size_t BitsInt = sizeof(int);
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
#ifdef lua_assert
#define LumenAssert(e)         lua_assert(e)
#else
#define LumenAssert(e)         assert(e)
#endif
#define LumenCheckExp(c, e)    (LumenAssert(c), (e))
#define LumenApiCheck(L, e)    luai_apicheck(L, e)
#else
#define LumenAssert(e)         ((void)0)
#define LumenCheckExp(c, e)    (e)
#define LumenApiCheck(L, o)    luai_apicheck(L, o)
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
