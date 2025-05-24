/*!
 * @brief Lua Parser
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_PARSER_H
#define LUMEN_PARSER_H

#include "lumen/limits.h"
#include "lumen/object.h"
#include "lumen/zio.h"


namespace Lumen {
    struct LexState;

    struct BlockNode;  /* defined in lparser.c */

    /**
     * Expression descriptor
     */
    struct ExpDesc {
        typedef int Kind;
        enum {
            KindVoid,    /* no value */
            KindNil,
            KindTrue,
            KindFalse,
            KindK,        /* info = index of constant in `k */
            KindKNum,    /* nVal = numerical value */
            KindLocal,    /* info = local register */
            KindUpValue,       /* info = index of up value in `UpValues` */
            KindGlobal,    /* info = index of table; aux = index of global name in `k` */
            KindIndexed,    /* info = table register; aux = index register (or `k`) */
            KindJmp,        /* info = instruction pc */
            KindRelocatable,    /* info = instruction pc */
            KindNonRelocatable,    /* info = result register */
            KindCall,    /* info = instruction pc */
            KindVararg    /* info = instruction pc */
        };

        Lumen::ExpDesc::Kind k;
        union {
            struct {
                int Info, Aux;
            };
            Lumen::Number NumberValue;
        };
        int t;  /* patch list of `exit when true' */
        int f;  /* patch list of `exit when false' */
    };

    struct UpValueDesc {
        Lumen::Byte k; // Constant
        Lumen::Byte Info;
    };

    /**
     * Execute a protected parser.
     */
    struct Parser {  /* data to `f_parser' */
        Lumen::ZIO *z;
        Lumen::ZBuffer buff;  /* buffer to be used by the scanner */
        const char *name;

        static Lumen::Proto *Parse(Lumen::State *L, Lumen::ZIO *z, Lumen::ZBuffer *buff, const char *name);
    };
}

#endif
