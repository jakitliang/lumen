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

#include "lumen.h"

namespace Lumen {
    enum {
        TypeProto = 9,
        TypeUpValue = 10,
        TypeDeadKey = 11
    };

    /*
    ** Union of all collectable objects
    */
    struct GCObject;

    struct State;

    /**
     * Type for virtual-machine instructions
     * must be an unsigned with (at least) 4 bytes (see details in opcodes.h)
     */
    using Instruction = UInt32;

    inline constexpr Lumen::UInteger MaxSize = std::numeric_limits<Lumen::UInteger>::max() - 2;

    inline constexpr Lumen::UInteger MaxUMemory = std::numeric_limits<MemorySize>::max() - 2;

    inline constexpr Lumen::UInteger MaxInt = INT_MAX - 2;

    inline constexpr Lumen::UInteger MinStack = LUA_MIN_STACK;

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

    inline constexpr Lumen::UInteger UTF8BufferSize = 8;

    namespace MetaMethod {
        inline constexpr int NameN = NameCall + 1; // number of elements in the enum
    }
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

#define cast_byte(i)       cast(Lumen::Byte, (i))
#define cast_num(i)        cast(Lumen::Number, (i))
#define cast_int(i)        cast(int, (i))
#define cast_uint(i)       cast(unsigned int, (i))
#define cast_char(i)       cast(char, (i))

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
