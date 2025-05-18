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

#include "lua.h"

#include "lua/limits.h"
#include "lua/mem.h"
#include "lua/state.h"
#include "lua/zio.h"


int Lua::ZIO::Fill(Lua::ZIO *z) {
    size_t size;
    Lua::State *L = z->L;
    const char *buff;
    LuaUnlock(L);
    buff = z->reader(L, z->data, &size);
    LuaLock(L);
    if (buff == nullptr || size == 0) return EOZ;
    z->n = size - 1;
    z->p = buff;
    return LuaChar2Int(*(z->p++));
}


int Lua::ZIO::LookAhead(Lua::ZIO *z) {
    if (z->n == 0) {
        if (Lua::ZIO::Fill(z) == EOZ)
            return EOZ;
        else {
            z->n++;  /* Lua::ZIO::Fill removed first byte; put back it */
            z->p--;
        }
    }
    return LuaChar2Int(*z->p);
}


void Lua::ZIO::Init(Lua::State *L, Lua::ZIO *z, Lua::Reader reader, void *data) {
    z->L = L;
    z->reader = reader;
    z->data = data;
    z->n = 0;
    z->p = nullptr;
}


/* --------------------------------------------------------------- read --- */
size_t Lua::ZIO::Read(Lua::ZIO *z, void *b, size_t n) {
    while (n) {
        size_t m;
        if (Lua::ZIO::LookAhead(z) == EOZ)
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
char *Lua::ZBuffer::OpenSpace(Lua::State *L, Lua::ZBuffer *buff, size_t n) {
    if (n > buff->buffsize) {
        if (n < Lua::MinBufferSize) n = Lua::MinBufferSize;
        LuaZBufferResize(L, buff, n);
    }
    return buff->buffer;
}


