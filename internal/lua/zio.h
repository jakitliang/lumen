/*!
 * @brief Buffered streams
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_ZIO_H
#define LUA_ZIO_H

#include "lua.h"

#include "lua/mem.h"


#define EOZ                        (-1)            /* end of stream */
#define LuaChar2Int(c)             cast(int, cast(Lua::Byte, (c)))

#define LuaZBufferInit(L, buff)    ((buff)->buffer = NULL, (buff)->buffsize = 0)
#define LuaZBufferGet(buff)        ((buff)->buffer)
#define LuaZBufferSize(buff)       ((buff)->buffsize)
#define LuaZBufferLength(buff)     ((buff)->n)
#define LuaZBufferReset(buff)      ((buff)->n = 0)
#define LuaZBufferResize(L, buff, size) \
    (LuaMemoryReAllocVector(L, (buff)->buffer, (buff)->buffsize, size, char), \
    (buff)->buffsize = size)
#define LuaZBufferFree(L, buff)    LuaZBufferResize(L, buff, 0)

#define LuaZIOGetCodePoint(z)      (((z)->n--) > 0 ? LuaChar2Int(*(z)->p++) : Lua::ZIO::Fill(z))

namespace Lua {
    struct ZBuffer {
        char *buffer;
        size_t n;
        size_t buffsize;

        static char *OpenSpace(Lua::State *L, Lua::ZBuffer *buff, size_t n);
    };

    struct ZIO {
        size_t n;            /* bytes still unread */
        const char *p;        /* current position in buffer */
        Lua::Reader reader;
        void *data;            /* additional data */
        Lua::State *L;            /* Lua state (for reader) */

        static void Init(Lua::State *L, Lua::ZIO *z, Lua::Reader reader,
                         void *data);

        static size_t Read(Lua::ZIO *z, void *b, size_t n);    /* read next n bytes */

        static int LookAhead(Lua::ZIO *z);

        static int Fill(Lua::ZIO *z);
    };
}

#endif
