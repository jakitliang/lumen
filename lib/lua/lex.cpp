/*!
 * @brief Lexical Analyzer
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cctype>
#include <clocale>
#include <cstring>

#define LUA_CORE

#include "lua.h"

#include "lua/do.h"
#include "lua/lex.h"
#include "lua/object.h"
#include "lua/parser.h"
#include "lua/state.h"
#include "lua/string.h"
#include "lua/table.h"
#include "lua/zio.h"
#include "lua/code.h"


#define next(ls)             (ls->Current = LuaZIOGetCodePoint(ls->z))
#define currIsNewline(ls)    (ls->Current == '\n' || ls->Current == '\r')


/* ORDER RESERVED */
const char *const Lua::Token::Names[] = {
        "and", "break", "do", "else", "elseif",
        "end", "false", "for", "function", "if",
        "in", "local", "nil", "not", "or", "repeat",
        "return", "then", "true", "until", "while",
        "..", "...", "==", ">=", "<=", "~=",
        "<number>", "<name>", "<string>", "<eof>",
        nullptr
};

#define saveAndNext(ls) (save(ls, ls->Current), next(ls))

static void save(Lua::LexState *ls, int c) {
    Lua::ZBuffer *b = ls->buff;
    if (b->n + 1 > b->buffsize) {
        size_t newSize;
        if (b->buffsize >= Lua::MaxSize / 2)
            Lua::LexState::LexError(ls, "lexical element too long", 0);
        newSize = b->buffsize * 2;
        LuaZBufferResize(ls->L, b, newSize);
    }
    b->buffer[b->n++] = cast(char, c);
}


void Lua::LexState::Init(Lua::State *L) {
    int i;
    for (i = 0; i < Lua::Token::ReservedCount; i++) {
        Lua::String *ts = Lua::String::New(L, Lua::Token::Names[i]);
        LuaStringFix(ts);  /* reserved words are never collected */
        lua_assert(strlen(Lua::Token::Names[i]) + 1 <= Lua::LexState::TokenLength);
        ts->Reserved = cast_byte(i + 1);  /* reserved word */
    }
}


#define LUA_MAX_SRC          80


const char *Lua::LexState::Token2CString(Lua::LexState *ls, int token) {
    if (token < LUA_LEX_STATE_FIRST_RESERVED) {
        lua_assert(token == cast(unsigned char, token));
        return (iscntrl(token)) ? Lua::PushFString(ls->L, "char(%d)", token) :
               Lua::PushFString(ls->L, "%c", token);
    } else
        return Lua::Token::Names[token - LUA_LEX_STATE_FIRST_RESERVED];
}


static const char *txtToken(Lua::LexState *ls, int token) {
    switch (token) {
        case Lua::Token::SymbolName:
        case Lua::Token::SymbolString:
        case Lua::Token::SymbolNumber:
            save(ls, '\0');
            return LuaZBufferGet(ls->buff);
        default:
            return Lua::LexState::Token2CString(ls, token);
    }
}


void Lua::LexState::LexError(Lua::LexState *ls, const char *msg, int token) {
    char buff[LUA_MAX_SRC];
    Lua::ChunkId(buff, LuaStringCString(ls->Source), LUA_MAX_SRC);
    msg = Lua::PushFString(ls->L, "%s:%d: %s", buff, ls->LineNumber, msg);
    if (token)
        Lua::PushFString(ls->L, "%s near " LUA_QS, msg, txtToken(ls, token));
    Lua::Do::Throw(ls->L, LUA_ERRSYNTAX);
}


void Lua::LexState::SyntaxError(Lua::LexState *ls, const char *msg) {
    Lua::LexState::LexError(ls, msg, ls->CurToken.Kind);
}


Lua::String *Lua::LexState::NewString(Lua::LexState *ls, const char *str, size_t l) {
    Lua::State *L = ls->L;
    Lua::String *ts = Lua::String::New(L, str, l);
    Lua::Value *o = Lua::Table::SetString(L, ls->fs->Constants, ts);  /* entry for `str' */
    if (LuaTypeIsNil(o)) {
        LuaSetBoolValue(o, 1);  /* make sure `str` will not be collected */
        LuaGCCheckGC(L);
    }
    return ts;
}


static void inclineNumber(Lua::LexState *ls) {
    int old = ls->Current;
    lua_assert(currIsNewline(ls));
    next(ls);  /* skip `\n' or `\r' */
    if (currIsNewline(ls) && ls->Current != old)
        next(ls);  /* skip `\n\r' or `\r\n' */
    if (++ls->LineNumber >= Lua::MaxInt)
        Lua::LexState::SyntaxError(ls, "chunk has too many lines");
}


void Lua::LexState::SetInput(Lua::State *L, Lua::LexState *ls, Lua::ZIO *z, Lua::String *source) {
    ls->DecimalPoint = '.';
    ls->L = L;
    ls->Ahead.Kind = Lua::Token::SymbolEOS;  /* no look-ahead token */
    ls->z = z;
    ls->fs = nullptr;
    ls->LineNumber = 1;
    ls->LastLine = 1;
    ls->Source = source;
    LuaZBufferResize(ls->L, ls->buff, Lua::MinBufferSize);  /* initialize buffer */
    next(ls);  /* read first char */
}


/*
** =======================================================
** LEXICAL ANALYZER
** =======================================================
*/



static int checkNext(Lua::LexState *ls, const char *set) {
    if (!strchr(set, ls->Current))
        return 0;
    saveAndNext(ls);
    return 1;
}


static void bufferReplace(Lua::LexState *ls, char from, char to) {
    size_t n = LuaZBufferLength(ls->buff);
    char *p = LuaZBufferGet(ls->buff);
    while (n--)
        if (p[n] == from) p[n] = to;
}


static void tryDecimalPoint(Lua::LexState *ls, Lua::SemInfo *semInfo) {
    /* format error: try to update decimal point separator */
    struct lconv *cv = localeconv();
    char old = ls->DecimalPoint;
    ls->DecimalPoint = (cv ? cv->decimal_point[0] : '.');
    bufferReplace(ls, old, ls->DecimalPoint);  /* try updated decimal separator */
    if (!Lua::String2Decimal(LuaZBufferGet(ls->buff), &semInfo->r)) {
        /* format error with correct decimal point: no more options */
        bufferReplace(ls, ls->DecimalPoint, '.');  /* undo change (for error message) */
        Lua::LexState::LexError(ls, "malformed number", Lua::Token::SymbolNumber);
    }
}


/* LUA_NUMBER */
static void readNumeral(Lua::LexState *ls, Lua::SemInfo *semInfo) {
    lua_assert(isdigit(ls->Current));
    do {
        saveAndNext(ls);
    } while (isdigit(ls->Current) || ls->Current == '.');
    if (checkNext(ls, "Ee"))  /* `E'? */
        checkNext(ls, "+-");  /* optional exponent sign */
    while (isalnum(ls->Current) || ls->Current == '_')
        saveAndNext(ls);
    save(ls, '\0');
    bufferReplace(ls, '.', ls->DecimalPoint);  /* follow locale for decimal point */
    if (!Lua::String2Decimal(LuaZBufferGet(ls->buff), &semInfo->r))  /* format error? */
        tryDecimalPoint(ls, semInfo); /* try to update decimal point separator */
}


static int skipSep(Lua::LexState *ls) {
    int count = 0;
    int s = ls->Current;
    lua_assert(s == '[' || s == ']');
    saveAndNext(ls);
    while (ls->Current == '=') {
        saveAndNext(ls);
        count++;
    }
    return (ls->Current == s) ? count : (-count) - 1;
}


static void readLongString(Lua::LexState *ls, Lua::SemInfo *semInfo, int sep) {
    int cont = 0;
    (void) (cont);  /* avoid warnings when `cont` is not used */
    saveAndNext(ls);  /* skip 2nd `[' */
    if (currIsNewline(ls))  /* string starts with a newline? */
        inclineNumber(ls);  /* skip it */
    for (;;) {
        switch (ls->Current) {
            case EOZ:
                Lua::LexState::LexError(ls, (semInfo) ? "unfinished long string" :
                                            "unfinished long comment", Lua::Token::SymbolEOS);
                break;  /* to avoid warnings */
#if defined(LUA_COMPAT_LSTR)
            case '[': {
                if (skipSep(ls) == sep) {
                    saveAndNext(ls);  /* skip 2nd `[' */
                    cont++;
#if LUA_COMPAT_LSTR == 1
                    if (sep == 0)
                        Lua::LexState::LexError(ls, "nesting of [[...]] is deprecated", '[');
#endif
                }
                break;
            }
#endif
            case ']': {
                if (skipSep(ls) == sep) {
                    saveAndNext(ls);  /* skip 2nd `]' */
#if defined(LUA_COMPAT_LSTR) && LUA_COMPAT_LSTR == 2
                    cont--;
                    if (sep == 0 && cont >= 0) break;
#endif
                    goto endloop;
                }
                break;
            }
            case '\n':
            case '\r': {
                save(ls, '\n');
                inclineNumber(ls);
                if (!semInfo) LuaZBufferReset(ls->buff);  /* avoid wasting space */
                break;
            }
            default: {
                if (semInfo) saveAndNext(ls);
                else
                    next(ls);
            }
        }
    }
    endloop:
    if (semInfo)
        semInfo->ts = Lua::LexState::NewString(ls, LuaZBufferGet(ls->buff) + (2 + sep),
                                     LuaZBufferLength(ls->buff) - 2 * (2 + sep));
}


static void readString(Lua::LexState *ls, int del, Lua::SemInfo *semInfo) {
    saveAndNext(ls);
    while (ls->Current != del) {
        switch (ls->Current) {
            case EOZ:
                Lua::LexState::LexError(ls, "unfinished string", Lua::Token::SymbolEOS);
                continue;  /* to avoid warnings */
            case '\n':
            case '\r':
                Lua::LexState::LexError(ls, "unfinished string", Lua::Token::SymbolString);
                continue;  /* to avoid warnings */
            case '\\': {
                int c;
                next(ls);  /* do not save the `\' */
                switch (ls->Current) {
                    case 'a':
                        c = '\a';
                        break;
                    case 'b':
                        c = '\b';
                        break;
                    case 'f':
                        c = '\f';
                        break;
                    case 'n':
                        c = '\n';
                        break;
                    case 'r':
                        c = '\r';
                        break;
                    case 't':
                        c = '\t';
                        break;
                    case 'v':
                        c = '\v';
                        break;
                    case '\n':  /* go through */
                    case '\r':
                        save(ls, '\n');
                        inclineNumber(ls);
                        continue;
                    case EOZ:
                        continue;  /* will raise an error next loop */
                    default: {
                        if (!isdigit(ls->Current))
                            saveAndNext(ls);  /* handles \\, \", \', and \? */
                        else {  /* \xxx */
                            int i = 0;
                            c = 0;
                            do {
                                c = 10 * c + (ls->Current - '0');
                                next(ls);
                            } while (++i < 3 && isdigit(ls->Current));
                            if (c > UCHAR_MAX)
                                Lua::LexState::LexError(ls, "escape sequence too large", Lua::Token::SymbolString);
                            save(ls, c);
                        }
                        continue;
                    }
                }
                save(ls, c);
                next(ls);
                continue;
            }
            default:
                saveAndNext(ls);
        }
    }
    saveAndNext(ls);  /* skip delimiter */
    semInfo->ts = Lua::LexState::NewString(ls, LuaZBufferGet(ls->buff) + 1,
                                 LuaZBufferLength(ls->buff) - 2);
}


static int LLex(Lua::LexState *ls, Lua::SemInfo *semInfo) {
    LuaZBufferReset(ls->buff);
    for (;;) {
        switch (ls->Current) {
            case '\n':
            case '\r': {
                inclineNumber(ls);
                continue;
            }
            case '-': {
                next(ls);
                if (ls->Current != '-') return '-';
                /* else is a comment */
                next(ls);
                if (ls->Current == '[') {
                    int sep = skipSep(ls);
                    LuaZBufferReset(ls->buff);  /* `skip_sep` may dirty the buffer */
                    if (sep >= 0) {
                        readLongString(ls, nullptr, sep);  /* long comment */
                        LuaZBufferReset(ls->buff);
                        continue;
                    }
                }
                /* else short comment */
                while (!currIsNewline(ls) && ls->Current != EOZ)
                    next(ls);
                continue;
            }
            case '[': {
                int sep = skipSep(ls);
                if (sep >= 0) {
                    readLongString(ls, semInfo, sep);
                    return Lua::Token::SymbolString;
                } else if (sep == -1) return '[';
                else Lua::LexState::LexError(ls, "invalid long string delimiter", Lua::Token::SymbolString);
            }
            case '=': {
                next(ls);
                if (ls->Current != '=') return '=';
                else {
                    next(ls);
                    return Lua::Token::SymbolEQ;
                }
            }
            case '<': {
                next(ls);
                if (ls->Current != '=') return '<';
                else {
                    next(ls);
                    return Lua::Token::SymbolLE;
                }
            }
            case '>': {
                next(ls);
                if (ls->Current != '=') return '>';
                else {
                    next(ls);
                    return Lua::Token::SymbolGE;
                }
            }
            case '~': {
                next(ls);
                if (ls->Current != '=') return '~';
                else {
                    next(ls);
                    return Lua::Token::SymbolNE;
                }
            }
            case '"':
            case '\'': {
                readString(ls, ls->Current, semInfo);
                return Lua::Token::SymbolString;
            }
            case '.': {
                saveAndNext(ls);
                if (checkNext(ls, ".")) {
                    if (checkNext(ls, "."))
                        return Lua::Token::SymbolDots;   /* ... */
                    else return Lua::Token::SymbolConcat;   /* .. */
                } else if (!isdigit(ls->Current)) return '.';
                else {
                    readNumeral(ls, semInfo);
                    return Lua::Token::SymbolNumber;
                }
            }
            case EOZ: {
                return Lua::Token::SymbolEOS;
            }
            default: {
                if (isspace(ls->Current)) {
                    lua_assert(!currIsNewline(ls));
                    next(ls);
                    continue;
                } else if (isdigit(ls->Current)) {
                    readNumeral(ls, semInfo);
                    return Lua::Token::SymbolNumber;
                } else if (isalpha(ls->Current) || ls->Current == '_') {
                    /* identifier or reserved word */
                    Lua::String *ts;
                    do {
                        saveAndNext(ls);
                    } while (isalnum(ls->Current) || ls->Current == '_');
                    ts = Lua::LexState::NewString(ls, LuaZBufferGet(ls->buff),
                                        LuaZBufferLength(ls->buff));
                    if (ts->Reserved > 0)  /* reserved word? */
                        return ts->Reserved - 1 + LUA_LEX_STATE_FIRST_RESERVED;
                    else {
                        semInfo->ts = ts;
                        return Lua::Token::SymbolName;
                    }
                } else {
                    int c = ls->Current;
                    next(ls);
                    return c;  /* single-char tokens (+ - / ...) */
                }
            }
        }
    }
}


void Lua::LexState::Next(Lua::LexState *ls) {
    ls->LastLine = ls->LineNumber;
    if (ls->Ahead.Kind != Lua::Token::SymbolEOS) {  /* is there a look-ahead token? */
        ls->CurToken = ls->Ahead;  /* use this one */
        ls->Ahead.Kind = Lua::Token::SymbolEOS;  /* and discharge it */
    } else
        ls->CurToken.Kind = LLex(ls, &ls->CurToken.SemInfo);  /* read next token */
}


void Lua::LexState::LookAhead(Lua::LexState *ls) {
    lua_assert(ls->Ahead.Kind == Lua::Token::SymbolEOS);
    ls->Ahead.Kind = LLex(ls, &ls->Ahead.SemInfo);
}

