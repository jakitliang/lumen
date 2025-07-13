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
#include "lumen/memory.h"
#include "lumen/state.h"
#include "lumen/zio.h"

int Lumen::ZIO::Fill(Lumen::ZIO *z) {
    Lumen::UInteger size;
    Lumen::State *L = z->L;
    const char *buff;
    if (z->eoz) return EOZ;
    LumenUnlock(L);
    buff = z->reader(reinterpret_cast<Lumen::IState *>(L), z->data, &size);
    LumenLock(L);
    if (buff == nullptr || size == 0) {
        z->eoz = true; /* avoid calling reader function next time */
        return EOZ;
    }
    z->n = size - 1;
    z->p = buff;
    return LumenChar2Int(*(z->p++));
}

Lumen::UInteger Lumen::ZIO::Read(Lumen::ZIO *z, void *b, Lumen::UInteger n) {
    while (n) {
        Lumen::UInteger m;
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



