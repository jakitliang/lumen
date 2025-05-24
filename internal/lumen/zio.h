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

#include "lua.h"

#include "lumen/mem.h"


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
        size_t n;
        size_t buffsize;

        static char *OpenSpace(Lumen::State *L, Lumen::ZBuffer *buff, size_t n);
    };

    struct ZIO {
        size_t n;            /* bytes still unread */
        const char *p;        /* current position in buffer */
        Lumen::Reader reader;
        void *data;            /* additional data */
        Lumen::State *L;            /* Lua state (for reader) */

        static void Init(Lumen::State *L, Lumen::ZIO *z, Lumen::Reader reader,
                         void *data);

        static size_t Read(Lumen::ZIO *z, void *b, size_t n);    /* read next n bytes */

        static int LookAhead(Lumen::ZIO *z);

        static int Fill(Lumen::ZIO *z);
    };
}

#endif
