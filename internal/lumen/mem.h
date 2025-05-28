/*!
 * @brief Interface to Memory Manager
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_MEM_H
#define LUMEN_MEM_H

#include <cstddef>

#include "lumen/object.h"

#define LUA_MEM_ERR_MSG    "not enough memory"

/**
 * @param L Lumen::State
 * @param b memory block
 * @param on int old number of elements
 * @param n int new number of elements
 * @param e size_t sizeof(elementT)
 */
#define LumenMemoryReAllocBlock(L, b, on, n, e) \
    ((cast(size_t, (n)+1) <= Lumen::MaxSize/(e)) ?  /* +1 to avoid warnings */ \
        Lumen::Memory::ReAlloc(L, (b), (on)*(e), (n)*(e)) : \
        Lumen::Memory::TooBig(L))

#define LumenMemoryFreeMemory(L, b, s)      Lumen::Memory::ReAlloc(L, (b), (s), 0)
#define LumenMemoryFree(L, b)               Lumen::Memory::ReAlloc(L, (b), sizeof(*(b)), 0)
#define LumenMemoryFreeArray(L, b, n, t)    LumenMemoryReAllocBlock(L, (b), n, 0, sizeof(t))

#define LumenMemoryAlloc(L, t)           Lumen::Memory::ReAlloc(L, NULL, 0, (t))
#define LumenMemoryNew(L, t)             cast(t *, LumenMemoryAlloc(L, sizeof(t)))
#define LumenMemoryNewVector(L, n, t)    cast(t *, LumenMemoryReAllocBlock(L, NULL, 0, n, sizeof(t)))

#define LumenMemoryGrowVector(L, v, nelems, size, t, limit, e) \
    if ((nelems)+1 > (size))                                 \
        ((v)=cast(t *, Lumen::Memory::GrowAux(L,v,&(size),sizeof(t),limit,e)))

/**
 * @param L Lumen::State
 * @param v Lumen::Value
 * @param oldN int old number of elements
 * @param n int new number of elements
 * @param t T type of elements
 */
#define LumenMemoryReAllocVector(L, v, oldN, n, t) \
    ((v)=cast(t *, LumenMemoryReAllocBlock(L, v, oldN, n, sizeof(t))))


namespace Lumen::Memory {
    void *ReAlloc(Lumen::State *L, void *block, size_t oldSize, size_t newSize);

    void *TooBig(Lumen::State *L);

    void *GrowAux(Lumen::State *L, void *block, int *size,
                  size_t size_elem, int limit,
                  const char *errorMsg);
}

#endif

