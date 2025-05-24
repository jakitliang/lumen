/*!
 * @brief Load precompiled Lua chunks
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_UNDUMP_H
#define LUMEN_UNDUMP_H

#include "lumen/object.h"
#include "lumen/zio.h"

namespace Lumen::Dumper {
    /* load one chunk; from lundump.c */
    Lumen::Proto *UnDump(Lumen::State *L, Lumen::ZIO *Z, Lumen::ZBuffer *buff, const char *name);

    /* make header; from lundump.c */
    void Header(char *h);

    /* dump one chunk; from ldump.c */
    int Dump(Lumen::State *L, const Lumen::Proto *f, Lumen::Writer w, void *data, int strip);

#ifdef lumenc_c
    /* print one chunk; from print.c */
    void Print(const Lumen::Proto *f, int full);
#endif
}

/* for header of binary files -- this is Lua 5.1 */
#define LUAC_VERSION        0x51

/* for header of binary files -- this is the official format */
#define LUAC_FORMAT        0

/* size of header of binary files */
#define LUAC_HEADER_SIZE        12

#endif
