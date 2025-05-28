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

#define LUA_STATE Lumen::State

#include "luaconf.h"

namespace Lumen {
    using Byte = unsigned char;
    using Int32 = LUAI_INT32;
    using UInt32 = LUAI_UINT32;

    using Number = LUA_NUMBER;
    using MemorySize = LUAI_UMEM;
    using MemoryDelta = LUAI_MEM;

    using UACNumber = LUAI_UACNUMBER; // Result of a `usual argument conversion' over lua_Number

    struct State;

    struct BasicObject {
        Lumen::Byte Type;
    };

    /*
    ** Union of all collectable objects
    */
    union GCObject;

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
#ifdef lua_assert
#define LumenCheckExp(c,e)     (lua_assert(c), (e))
#define LumenApiCheck(l,e)     lua_assert(e)
#elif defined(LUA_CORE_DEBUG)
#define lua_assert(exp)      assert(exp)
#define LumenCheckExp(c,e)     (lua_assert(c), (e))
#define LumenApiCheck(l,e)     lua_assert(e)
#else
#define lua_assert(c)        ((void)0)
#define LumenCheckExp(c, e)    (e)
#define LumenApiCheck          luai_apicheck
#endif


#ifndef UNUSED
#define UNUSED(x)    ((void)(x))    /* to avoid warnings */
#endif


#ifndef cast
#define cast(t, exp)    ((t)(exp))
#endif

#define cast_byte(i)    cast(Lumen::Byte, (i))
#define cast_num(i)     cast(lua_Number, (i))
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
