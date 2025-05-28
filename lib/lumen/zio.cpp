/*!
 * @brief A generic input stream interface
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstring>

#define LUA_CORE

#include "lumen/limits.h"
#include "lumen/mem.h"
#include "lumen/state.h"
#include "lumen/zio.h"

int Lumen::ZIO::Fill(Lumen::ZIO *z) {
    size_t size;
    Lumen::State *L = z->L;
    const char *buff;
    LumenUnlock(L);
    buff = z->reader(L, z->data, &size);
    LumenLock(L);
    if (buff == nullptr || size == 0) return EOZ;
    z->n = size - 1;
    z->p = buff;
    return LumenChar2Int(*(z->p++));
}


int Lumen::ZIO::LookAhead(Lumen::ZIO *z) {
    if (z->n == 0) {
        if (Lumen::ZIO::Fill(z) == EOZ)
            return EOZ;
        else {
            z->n++;  /* Lumen::ZIO::Fill removed first byte; put back it */
            z->p--;
        }
    }
    return LumenChar2Int(*z->p);
}


void Lumen::ZIO::Init(Lumen::State *L, Lumen::ZIO *z, Lumen::Reader reader, void *data) {
    z->L = L;
    z->reader = reader;
    z->data = data;
    z->n = 0;
    z->p = nullptr;
}


/* --------------------------------------------------------------- read --- */
size_t Lumen::ZIO::Read(Lumen::ZIO *z, void *b, size_t n) {
    while (n) {
        size_t m;
        if (Lumen::ZIO::LookAhead(z) == EOZ)
            return n;  /* return number of missing bytes */
        m = (n <= z->n) ? n : z->n;  /* min. between n and z->n */
        memcpy(b, z->p, m);
        z->n -= m;
        z->p += m;
        b = (char *) b + m;
        n -= m;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
char *Lumen::ZBuffer::OpenSpace(Lumen::State *L, Lumen::ZBuffer *buff, size_t n) {
    if (n > buff->buffsize) {
        if (n < Lumen::MinBufferSize) n = Lumen::MinBufferSize;
        LumenZBufferResize(L, buff, n);
    }
    return buff->buffer;
}


