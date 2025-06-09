/*!
 * @brief Lexical Analyzer
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_LEX_H
#define LUMEN_LEX_H

#include "lumen/object.h"
#include "lumen/zio.h"
#include "lumen/string.h"


#define LUA_LEX_STATE_FIRST_RESERVED    257

namespace Lumen {
    struct FuncState;

    /* semantics information */
    union SemInfo {
        Lumen::Number r;
        Lumen::String *ts;
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

        Lumen::Token::Symbol Kind;
        Lumen::SemInfo SemInfo;

        /* array with token `names' */
        static const char *const Names[];

        /* number of reserved words */
        static inline constexpr int ReservedCount = Lumen::Token::SymbolWhile - LUA_LEX_STATE_FIRST_RESERVED + 1;
    };

    struct LexState {
        int Current;  /* current character (char_int, CodePoint) */
        int LineNumber;  /* input line counter */
        int LastLine;  /* line of last token `consumed` */
        Lumen::Token CurToken;  /* current token */
        Lumen::Token Ahead;  /* look ahead token */
        Lumen::FuncState *fs;  /* `Lumen::FuncState` is private to the parser */
        Lumen::State *L;
        Lumen::ZIO *z;  /* input stream */
        Lumen::ZBuffer *buff;  /* buffer for tokens */
        Lumen::String *Source;  /* current source name */
        char DecimalPoint;  /* locale decimal point */

        /* maximum length of a reserved word */
        static inline constexpr Lumen::UInteger TokenLength = sizeof("function") / sizeof(char);

        static void Init(Lumen::State *L);

        static void SetInput(Lumen::State *L, Lumen::LexState *ls, Lumen::ZIO *z,
                                            Lumen::String *source);

        static Lumen::String *NewString(Lumen::LexState *ls, const char *str, Lumen::UInteger l);

        static void Next(Lumen::LexState *ls);

        static void LookAhead(Lumen::LexState *ls);

        static void LexError(Lumen::LexState *ls, const char *msg, int token);

        static void SyntaxError(Lumen::LexState *ls, const char *s);

        static const char *Token2CString(Lumen::LexState *ls, int token);
    };
}

inline void Lumen::LexState::Init(Lumen::State *L) {
    int i;
    for (i = 0; i < Lumen::Token::ReservedCount; i++) {
        Lumen::String *ts = Lumen::String::New(L, Lumen::Token::Names[i]);
        LumenStringFix(ts);  /* reserved words are never collected */
        LumenAssert(Lumen::String::LengthOf(Lumen::Token::Names[i]) + 1 <= Lumen::LexState::TokenLength);
        ts->Reserved = cast_byte(i + 1);  /* reserved word */
    }
}

inline void Lumen::LexState::SyntaxError(Lumen::LexState *ls, const char *s) {
    Lumen::LexState::LexError(ls, s, ls->CurToken.Kind);
}

inline const char *Lumen::LexState::Token2CString(Lumen::LexState *ls, int token) {
    if (token < LUA_LEX_STATE_FIRST_RESERVED) {
        LumenAssert(token == cast(unsigned char, token));
        return (iscntrl(token)) ? Lumen::PushFString(ls->L, "char(%d)", token) :
               Lumen::PushFString(ls->L, "%c", token);
    } else
        return Lumen::Token::Names[token - LUA_LEX_STATE_FIRST_RESERVED];
}

#endif
