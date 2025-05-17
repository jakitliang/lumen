/*!
 * @brief Interface to Memory Manager
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstddef>

#define LUA_CORE

#include "lua.h"

#include "lua/debug.h"
#include "lua/do.h"
#include "lua/mem.h"
#include "lua/object.h"
#include "lua/state.h"



/*
** About the reAlloc function:
** void * Lua::Memory::ReAlloc(void *ud, void *ptr, size_t oldSize, size_t newSize);
**
** Lua ensures that (ptr == nullptr) iff (osize == 0).
**
** * frealloc(ud, nullptr, 0, x) creates a new block of size `x'
**
** * frealloc(ud, p, x, 0) frees the block `p'
** (in this specific case, frealloc must return nullptr).
** particularly, frealloc(ud, nullptr, 0, 0) does nothing
** (which is equivalent to free(nullptr) in ANSI C)
**
** frealloc returns nullptr if it cannot create or reallocate the area
** (any reallocation to an equal or smaller size cannot fail!)
*/



#define MIN_ARRAY_SIZE    4


void *Lua::Memory::GrowAux(Lua::State *L, void *block, int *size, size_t size_elems,
                           int limit, const char *errorMsg) {
    void *newBlock;
    int newSize;
    if (*size >= limit / 2) {  /* cannot double it? */
        if (*size >= limit)  /* cannot grow even a little? */
            Lua::Debug::RunError(L, errorMsg);
        newSize = limit;  /* still have at least one free place */
    } else {
        newSize = (*size) * 2;
        if (newSize < MIN_ARRAY_SIZE)
            newSize = MIN_ARRAY_SIZE;  /* minimum size */
    }
    newBlock = LuaMemoryReAllocBlock(L, block, *size, newSize, size_elems);
    *size = newSize;  /* update only when everything else is OK */
    return newBlock;
}


void *Lua::Memory::TooBig(Lua::State *L) {
    Lua::Debug::RunError(L, "memory allocation error: block too big");
    return nullptr;  /* to avoid warnings */
}


/*
** generic allocation routine.
*/
void *Lua::Memory::ReAlloc(Lua::State *L, void *block, size_t oldSize, size_t newSize) {
    Lua::GlobalState *g = LuaGlobal(L);
    lua_assert((oldSize == 0) == (block == nullptr));
    block = (*g->ReAllocator)(g->ReAllocatorUData, block, oldSize, newSize);
    if (block == nullptr && newSize > 0)
        Lua::Do::Throw(L, LUA_ERRMEM);
    lua_assert((newSize == 0) == (block == nullptr));
    g->TotalBytes = (g->TotalBytes - oldSize) + newSize;
    return block;
}

