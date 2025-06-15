/*!
 * @brief Buffered streams
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_ZIO_H
#define LUMEN_ZIO_H

#include "lumen/memory.h"

#define EOZ                        (-1)            /* end of stream */
#define LumenChar2Int(c)             cast(int, cast(Lumen::Byte, (c)))

#define LumenZBufferInit(L, buff)    ((buff)->buffer = NULL, (buff)->buffsize = 0)
#define LumenZBufferGet(buff)        ((buff)->buffer)
#define LumenZBufferSize(buff)       ((buff)->buffsize)
#define LumenZBufferLength(buff)     ((buff)->n)
#define LumenZBufferReset(buff)      ((buff)->n = 0)
#define LumenZBufferResize(L, buff, size) \
    (LumenMemoryReAllocVector(L, (buff)->buffer, (buff)->buffsize, size, char), \
    (buff)->buffsize = size)
#define LumenZBufferFree(L, buff)    LumenZBufferResize(L, buff, 0)

#define LumenZIOGetCodePoint(z)      (((z)->n--) > 0 ? LumenChar2Int(*(z)->p++) : Lumen::ZIO::Fill(z))

namespace Lumen {
    struct ZBuffer {
        char *buffer;
        Lumen::UInteger n;
        Lumen::UInteger buffsize;

        static char *OpenSpace(Lumen::State *L, Lumen::ZBuffer *buff, Lumen::UInteger n);
    };

    struct ZIO {
        bool eoz; /* true if reader has no more data */
        const char *p;        /* current position in buffer */
        Lumen::UInteger n;            /* bytes still unread */
        Lumen::Reader reader;
        void *data;            /* additional data */
        Lumen::State *L;            /* Lua state (for reader) */

        static void Init(Lumen::State *L, Lumen::ZIO *z, Lumen::Reader reader,
                         void *data);

        static Lumen::UInteger Read(Lumen::ZIO *z, void *b, Lumen::UInteger n);    /* read next n bytes */

        static int LookAhead(Lumen::ZIO *z);

        static int Fill(Lumen::ZIO *z);
    };
}

inline char *Lumen::ZBuffer::OpenSpace(Lumen::State *L, Lumen::ZBuffer *buff, Lumen::UInteger n) {
    if (n > buff->buffsize) {
        if (n < Lumen::MinBufferSize) n = Lumen::MinBufferSize;
        LumenZBufferResize(L, buff, n);
    }
    return buff->buffer;
}

inline void Lumen::ZIO::Init(Lumen::State *L, Lumen::ZIO *z, Lumen::Reader reader, void *data) {
    z->L = L;
    z->reader = reader;
    z->data = data;
    z->n = 0;
    z->p = nullptr;
    z->eoz = false;
}

inline int Lumen::ZIO::LookAhead(Lumen::ZIO *z) {
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

#endif
