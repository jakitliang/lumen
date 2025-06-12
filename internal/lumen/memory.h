/*!
 * @brief Interface to Memory Manager
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_MEMORY_H
#define LUMEN_MEMORY_H

#include <cstddef>

#include "lumen/object.h"

#define LUA_MEM_ERR_MSG    "not enough memory"

/**
 * @param L Lumen::State
 * @param b memory block
 * @param on int old number of elements
 * @param n int new number of elements
 * @param e Lumen::UInteger sizeof(elementT)
 */
#define LumenMemoryReAllocBlock(L, b, on, n, e) \
    ((cast(Lumen::UInteger, (n)+1) <= Lumen::MaxSize/(e)) ?  /* +1 to avoid warnings */ \
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
 * @param v Lumen::Object
 * @param oldN int old number of elements
 * @param n int new number of elements
 * @param t T type of elements
 */
#define LumenMemoryReAllocVector(L, v, oldN, n, t) \
    ((v)=cast(t *, LumenMemoryReAllocBlock(L, v, oldN, n, sizeof(t))))


namespace Lumen::Memory {
    void *ReAlloc(Lumen::State *L, void *block, Lumen::UInteger oldSize, Lumen::UInteger newSize);

    void *TooBig(Lumen::State *L);

    void *GrowAux(Lumen::State *L, void *block, int *size,
                  Lumen::UInteger size_elem, int limit,
                  const char *errorMsg);

    const char *Find(const char *cStr1, Lumen::UInteger len1, const char *cStr2, Lumen::UInteger len2);

    void *Alloc(void *userData, void *ptr, Lumen::UInteger originSize, Lumen::UInteger newSize);
}

#endif //LUMEN_MEMORY_H

