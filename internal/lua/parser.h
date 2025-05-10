/*!
 * @brief Lua Parser
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_PARSER_H
#define LUA_PARSER_H

#include "lua/limits.h"
#include "lua/object.h"
#include "lua/zio.h"


namespace Lua {
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

        Lua::ExpDesc::Kind k;
        union {
            struct {
                int Info, Aux;
            };
            Lua::Number NumberValue;
        };
        int t;  /* patch list of `exit when true' */
        int f;  /* patch list of `exit when false' */
    };

    struct UpValueDesc {
        Lua::Byte k; // Constant
        Lua::Byte Info;
    };

    /**
     * Execute a protected parser.
     */
    struct Parser {  /* data to `f_parser' */
        Lua::ZIO *z;
        Lua::ZBuffer buff;  /* buffer to be used by the scanner */
        const char *name;

        static Lua::Proto *Parse(Lua::State *L, Lua::ZIO *z, Lua::ZBuffer *buff, const char *name);
    };
}

#endif
