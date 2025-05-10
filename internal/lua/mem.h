/*!
 * @brief Interface to Memory Manager
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_MEM_H
#define LUA_MEM_H


#include <cstddef>

#include "lua/limits.h"
#include "lua.h"

#define LUA_MEM_ERR_MSG    "not enough memory"


#define LuaMemoryReAllocBlock(L, b, on, n, e) \
    ((cast(size_t, (n)+1) <= Lua::MaxSize/(e)) ?  /* +1 to avoid warnings */ \
        Lua::Memory::ReAlloc(L, (b), (on)*(e), (n)*(e)) : \
        Lua::Memory::TooBig(L))

#define LuaMemoryFreeMemory(L, b, s)    Lua::Memory::ReAlloc(L, (b), (s), 0)
#define LuaMemoryFree(L, b)        Lua::Memory::ReAlloc(L, (b), sizeof(*(b)), 0)
#define LuaMemoryFreeArray(L, b, n, t)   LuaMemoryReAllocBlock(L, (b), n, 0, sizeof(t))

#define LuaMemoryAlloc(L, t)    Lua::Memory::ReAlloc(L, NULL, 0, (t))
#define LuaMemoryNew(L, t)        cast(t *, LuaMemoryAlloc(L, sizeof(t)))
#define LuaMemoryNewVector(L, n, t) \
        cast(t *, LuaMemoryReAllocBlock(L, NULL, 0, n, sizeof(t)))

#define LuaMemoryGrowVector(L, v, nelems, size, t, limit, e) \
    if ((nelems)+1 > (size))                                 \
        ((v)=cast(t *, Lua::Memory::GrowAux(L,v,&(size),sizeof(t),limit,e)))

#define LuaMemoryReAllocVector(L, v, oldn, n, t) \
    ((v)=cast(t *, LuaMemoryReAllocBlock(L, v, oldn, n, sizeof(t))))

namespace Lua::Memory {
    void *ReAlloc(Lua::State *L, void *block, size_t oldsize,
                       size_t size);

    void *TooBig(Lua::State *L);

    void *GrowAux(Lua::State *L, void *block, int *size,
                               size_t size_elem, int limit,
                               const char *errormsg);
}

#endif

