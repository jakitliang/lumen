/*!
 * @brief Interface to Memory Manager
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstddef>
#include <cstdlib>
#include <unordered_map>

#define LUA_CORE

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/memory.h"
#include "lumen/object.h"
#include "lumen/state.h"



/*
** About the reAlloc function:
** void * Lumen::Memory::ReAlloc(void *ud, void *ptr, Lumen::UInteger oldSize, Lumen::UInteger newSize);
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


void *Lumen::Memory::GrowAux(Lumen::State *L, void *block, int *size, Lumen::UInteger size_elems,
                             int limit, const char *errorMsg) {
    void *newBlock;
    int newSize;
    if (*size >= limit / 2) {  /* cannot double it? */
        if (*size >= limit)  /* cannot grow even a little? */
            Lumen::Debug::RunError(L, errorMsg);
        newSize = limit;  /* still have at least one free place */
    } else {
        newSize = (*size) * 2;
        if (newSize < MIN_ARRAY_SIZE)
            newSize = MIN_ARRAY_SIZE;  /* minimum size */
    }
    newBlock = LumenMemoryReAllocBlock(L, block, *size, newSize, size_elems);
    *size = newSize;  /* update only when everything else is OK */
    return newBlock;
}


void *Lumen::Memory::TooBig(Lumen::State *L) {
    Lumen::Debug::RunError(L, "memory allocation error: block too big");
    return nullptr;  /* to avoid warnings */
}


/*
** generic allocation routine.
*/
void *Lumen::Memory::ReAlloc(Lumen::State *L, void *block, Lumen::UInteger oldSize, Lumen::UInteger newSize) {
    Lumen::GlobalState *g = LumenGlobalState(L);
    LumenAssert((oldSize == 0) == (block == nullptr));
    block = (*g->ReAllocator)(g->ReAllocatorUData, block, oldSize, newSize);
    if (block == nullptr && newSize > 0)
        Lumen::Do::Throw(L, Lumen::RetErrMem);
    LumenAssert((newSize == 0) == (block == nullptr));
    g->TotalBytes = (g->TotalBytes - oldSize) + newSize;
    return block;
}

const char *Lumen::Memory::Find(const char *cStr1, Lumen::UInteger len1, const char *cStr2, Lumen::UInteger len2) {
    if (len2 == 0) return cStr1;  /* empty strings are everywhere */
    else if (len2 > len1) return nullptr;  /* avoids a negative `len1` */
    else {
        const char *found;  /* to search for a `*s2' inside `cStr1` */
        len2--;  /* 1st char will be checked by `memchr` */
        len1 = len1 - len2;  /* `s2` cannot be found after that */
        while (len1 > 0 && (found = (const char *) memchr(cStr1, *cStr2, len1)) != nullptr) {
            found++;   /* 1st char is already checked */
            if (memcmp(found, cStr2 + 1, len2) == 0)
                return found - 1;
            else {  /* correct `len1` and `cStr1` to try again */
                len1 -= found - cStr1;
                cStr1 = found;
            }
        }
        return nullptr;  /* not found */
    }
}

void *Lumen::Memory::Alloc(void *userData, void *ptr, Lumen::UInteger originSize, Lumen::UInteger newSize) {
    if (newSize == 0) {
        free(ptr);
        return nullptr;
    }

    return realloc(ptr, newSize);
}
