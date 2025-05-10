/*!
 * @brief Lexical Analyzer
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_LEX_H
#define LUA_LEX_H

#include "lua/object.h"
#include "lua/zio.h"


#define LUA_LEX_STATE_FIRST_RESERVED    257

namespace Lua {
    struct FuncState;

    /* semantics information */
    union SemInfo {
        Lua::Number r;
        Lua::String *ts;
    };

    struct Token {
        /**
         * Token Kind\n
         * WARNING: if you change the order of this enumeration,
         * grep "ORDER RESERVED"
         */
        typedef int Symbol;
        enum {
            /* terminal symbols denoted by reserved words */
            SymbolAnd = LUA_LEX_STATE_FIRST_RESERVED,
            SymbolBreak,
            SymbolDo,
            SymbolElse,
            SymbolElseIf,
            SymbolEnd,
            SymbolFalse,
            SymbolFor,
            SymbolFunction,
            SymbolIf,
            SymbolIn,
            SymbolLocal,
            SymbolNil,
            SymbolNot,
            SymbolOr,
            SymbolRepeat,
            SymbolReturn,
            SymbolThen,
            SymbolTrue,
            SymbolUntil,
            SymbolWhile,
            /* other terminal symbols */
            SymbolConcat,
            SymbolDots,
            SymbolEQ,
            SymbolGE,
            SymbolLE,
            SymbolNE,
            SymbolNumber,
            SymbolName,
            SymbolString,
            SymbolEOS
        };

        Lua::Token::Symbol Kind;
        Lua::SemInfo SemInfo;

        /* array with token `names' */
        static const char *const Names[];

        /* number of reserved words */
        static inline constexpr int ReservedCount = Lua::Token::SymbolWhile - LUA_LEX_STATE_FIRST_RESERVED + 1;
    };

    struct LexState {
        int Current;  /* current character (char_int, CodePoint) */
        int LineNumber;  /* input line counter */
        int LastLine;  /* line of last token `consumed` */
        Lua::Token CurToken;  /* current token */
        Lua::Token Ahead;  /* look ahead token */
        Lua::FuncState *fs;  /* `Lua::FuncState` is private to the parser */
        Lua::State *L;
        Lua::ZIO *z;  /* input stream */
        Lua::ZBuffer *buff;  /* buffer for tokens */
        Lua::String *Source;  /* current source name */
        char DecimalPoint;  /* locale decimal point */

        /* maximum length of a reserved word */
        static inline constexpr size_t TokenLength = sizeof("function") / sizeof(char);

        static void Init(Lua::State *L);

        static void SetInput(Lua::State *L, Lua::LexState *ls, Lua::ZIO *z,
                                            Lua::String *source);

        static Lua::String *NewString(Lua::LexState *ls, const char *str, size_t l);

        static void Next(Lua::LexState *ls);

        static void LookAhead(Lua::LexState *ls);

        static void LexError(Lua::LexState *ls, const char *msg, int token);

        static void SyntaxError(Lua::LexState *ls, const char *s);

        static const char *Token2CString(Lua::LexState *ls, int token);
    };
}

#endif
