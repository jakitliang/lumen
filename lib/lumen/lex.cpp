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

#include "lumen/do.h"
#include "lumen/lex.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/parser.h"
#include "lumen/state.h"
#include "lumen/zio.h"
#include "lumen/code.h"

#define next(ls)             (ls->Current = LumenZIOGetCodePoint(ls->z))
#define currIsNewline(ls)    (ls->Current == '\n' || ls->Current == '\r')


/* ORDER RESERVED */
const char *const Lumen::Token::Names[] = {
    "and", "break", "do", "else", "elseif",
    "end", "false", "for", "function", "if",
    "in", "local", "nil", "not", "or", "repeat",
    "return", "then", "true", "until", "while",
    "..", "...", "==", ">=", "<=", "~=",
    "<number>", "<name>", "<string>", "<eof>",
    nullptr
};

#define saveAndNext(ls) (save(ls, ls->Current), next(ls))

static inline void save(Lumen::LexState *ls, int c) {
    Lumen::ZBuffer *b = ls->buff;
    if (b->n + 1 > b->buffsize) {
        Lumen::UInteger newSize;
        if (b->buffsize >= Lumen::MaxSize / 2)
            Lumen::LexState::LexError(ls, "lexical element too long", 0);
        newSize = b->buffsize * 2;
        LumenZBufferResize(ls->L, b, newSize);
    }
    b->buffer[b->n++] = cast(char, c);
}

#define LUA_MAX_SRC          80

static inline const char *txtToken(Lumen::LexState *ls, int token) {
    switch (token) {
        case Lumen::Token::SymbolName:
        case Lumen::Token::SymbolString:
        case Lumen::Token::SymbolNumber:
            save(ls, '\0');
            return LumenZBufferGet(ls->buff);
        default:
            return Lumen::LexState::Token2CString(ls, token);
    }
}


void Lumen::LexState::LexError(Lumen::LexState *ls, const char *msg, int token) {
    char buff[LUA_MAX_SRC];
    Lumen::ChunkId(buff, ls->Source->CString(), LUA_MAX_SRC);
    msg = Lumen::PushFString(ls->L, "%s:%d: %s", buff, ls->LineNumber, msg);
    if (token)
        Lumen::PushFString(ls->L, "%s near " LUA_QS, msg, txtToken(ls, token));
    Lumen::Do::Throw(ls->L, Lumen::RetErrSyntax);
}

Lumen::String *Lumen::LexState::NewString(Lumen::LexState *ls, const char *str, Lumen::UInteger l) {
    Lumen::State *L = ls->L;
    Lumen::String *ts = Lumen::String::New(L, str, l);
    Lumen::Object *o = Lumen::Table::SetString(L, ls->fs->Constants, ts);  /* entry for `str' */
    if (o->IsNil()) {
        o->SetBool(1);  /* make sure `str` will not be collected */
        L->CheckGC();
    }
    return ts;
}


static inline void inclineNumber(Lumen::LexState *ls) {
    int old = ls->Current;
    LumenAssert(currIsNewline(ls));
    next(ls);  /* skip `\n' or `\r' */
    if (currIsNewline(ls) && ls->Current != old)
        next(ls);  /* skip `\n\r' or `\r\n' */
    if (++ls->LineNumber >= Lumen::MaxInt)
        Lumen::LexState::SyntaxError(ls, "chunk has too many lines");
}


void Lumen::LexState::SetInput(Lumen::State *L, Lumen::LexState *ls, Lumen::ZIO *z, Lumen::String *source) {
    ls->DecimalPoint = '.';
    ls->L = L;
    ls->Ahead.Kind = Lumen::Token::SymbolEOS;  /* no look-ahead token */
    ls->z = z;
    ls->fs = nullptr;
    ls->LineNumber = 1;
    ls->LastLine = 1;
    ls->Source = source;
    LumenZBufferResize(ls->L, ls->buff, Lumen::MinBufferSize);  /* initialize buffer */
    next(ls);  /* read first char */
}


/*
** =======================================================
** LEXICAL ANALYZER
** =======================================================
*/



static inline int checkNext(Lumen::LexState *ls, const char *set) {
    if (!strchr(set, ls->Current))
        return 0;
    saveAndNext(ls);
    return 1;
}


static inline void bufferReplace(Lumen::LexState *ls, char from, char to) {
    Lumen::UInteger n = LumenZBufferLength(ls->buff);
    char *p = LumenZBufferGet(ls->buff);
    while (n--)
        if (p[n] == from) p[n] = to;
}


static void tryDecimalPoint(Lumen::LexState *ls, Lumen::SemInfo *semInfo) {
    /* format error: try to update decimal point separator */
    struct lconv *cv = localeconv();
    char old = ls->DecimalPoint;
    ls->DecimalPoint = (cv ? cv->decimal_point[0] : '.');
    bufferReplace(ls, old, ls->DecimalPoint);  /* try updated decimal separator */
    if (!Lumen::String2Decimal(LumenZBufferGet(ls->buff), &semInfo->r)) {
        /* format error with correct decimal point: no more options */
        bufferReplace(ls, ls->DecimalPoint, '.');  /* undo change (for error message) */
        Lumen::LexState::LexError(ls, "malformed number", Lumen::Token::SymbolNumber);
    }
}


/* LUA_NUMBER */
static void readNumeral(Lumen::LexState *ls, Lumen::SemInfo *semInfo) {
    LumenAssert(isdigit(ls->Current));
    do {
        saveAndNext(ls);
    } while (isdigit(ls->Current) || ls->Current == '.');
    if (checkNext(ls, "Ee"))  /* `E'? */
        checkNext(ls, "+-");  /* optional exponent sign */
    while (isalnum(ls->Current) || ls->Current == '_')
        saveAndNext(ls);
    save(ls, '\0');
    bufferReplace(ls, '.', ls->DecimalPoint);  /* follow locale for decimal point */
    if (!Lumen::String2Decimal(LumenZBufferGet(ls->buff), &semInfo->r))  /* format error? */
        tryDecimalPoint(ls, semInfo); /* try to update decimal point separator */
}


static inline int skipSep(Lumen::LexState *ls) {
    int count = 0;
    int s = ls->Current;
    LumenAssert(s == '[' || s == ']');
    saveAndNext(ls);
    while (ls->Current == '=') {
        saveAndNext(ls);
        count++;
    }
    return (ls->Current == s) ? count : (-count) - 1;
}


static void readLongString(Lumen::LexState *ls, Lumen::SemInfo *semInfo, int sep) {
    int cont = 0;
    (void) (cont);  /* avoid warnings when `cont` is not used */
    saveAndNext(ls);  /* skip 2nd `[' */
    if (currIsNewline(ls))  /* string starts with a newline? */
        inclineNumber(ls);  /* skip it */
    for (;;) {
        switch (ls->Current) {
            case EOZ:
                Lumen::LexState::LexError(ls, (semInfo) ? "unfinished long string" :
                                              "unfinished long comment", Lumen::Token::SymbolEOS);
                break;  /* to avoid warnings */
#if defined(LUA_COMPAT_LSTR)
            case '[': {
                if (skipSep(ls) == sep) {
                    saveAndNext(ls);  /* skip 2nd `[' */
                    cont++;
#if LUA_COMPAT_LSTR == 1
                    if (sep == 0)
                        Lumen::LexState::LexError(ls, "nesting of [[...]] is deprecated", '[');
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
                if (!semInfo) LumenZBufferReset(ls->buff);  /* avoid wasting space */
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
        semInfo->ts = Lumen::LexState::NewString(ls, LumenZBufferGet(ls->buff) + (2 + sep),
                                                 LumenZBufferLength(ls->buff) - 2 * (2 + sep));
}


static void readString(Lumen::LexState *ls, int del, Lumen::SemInfo *semInfo) {
    saveAndNext(ls);
    while (ls->Current != del) {
        switch (ls->Current) {
            case EOZ:
                Lumen::LexState::LexError(ls, "unfinished string", Lumen::Token::SymbolEOS);
                continue;  /* to avoid warnings */
            case '\n':
            case '\r':
                Lumen::LexState::LexError(ls, "unfinished string", Lumen::Token::SymbolString);
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
                                Lumen::LexState::LexError(ls, "escape sequence too large", Lumen::Token::SymbolString);
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
    semInfo->ts = Lumen::LexState::NewString(ls, LumenZBufferGet(ls->buff) + 1,
                                             LumenZBufferLength(ls->buff) - 2);
}


static int LLex(Lumen::LexState *ls, Lumen::SemInfo *semInfo) {
    LumenZBufferReset(ls->buff);
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
                    LumenZBufferReset(ls->buff);  /* `skip_sep` may dirty the buffer */
                    if (sep >= 0) {
                        readLongString(ls, nullptr, sep);  /* long comment */
                        LumenZBufferReset(ls->buff);
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
                    return Lumen::Token::SymbolString;
                } else if (sep == -1) return '[';
                else Lumen::LexState::LexError(ls, "invalid long string delimiter", Lumen::Token::SymbolString);
            }
            case '=': {
                next(ls);
                if (ls->Current != '=') return '=';
                else {
                    next(ls);
                    return Lumen::Token::SymbolEQ;
                }
            }
            case '<': {
                next(ls);
                if (ls->Current != '=') return '<';
                else {
                    next(ls);
                    return Lumen::Token::SymbolLE;
                }
            }
            case '>': {
                next(ls);
                if (ls->Current != '=') return '>';
                else {
                    next(ls);
                    return Lumen::Token::SymbolGE;
                }
            }
            case '~': {
                next(ls);
                if (ls->Current != '=') return '~';
                else {
                    next(ls);
                    return Lumen::Token::SymbolNE;
                }
            }
            case '"':
            case '\'': {
                readString(ls, ls->Current, semInfo);
                return Lumen::Token::SymbolString;
            }
            case '.': {
                saveAndNext(ls);
                if (checkNext(ls, ".")) {
                    if (checkNext(ls, "."))
                        return Lumen::Token::SymbolDots;   /* ... */
                    else return Lumen::Token::SymbolConcat;   /* .. */
                } else if (!isdigit(ls->Current)) return '.';
                else {
                    readNumeral(ls, semInfo);
                    return Lumen::Token::SymbolNumber;
                }
            }
            case EOZ: {
                return Lumen::Token::SymbolEOS;
            }
            default: {
                if (isspace(ls->Current)) {
                    LumenAssert(!currIsNewline(ls));
                    next(ls);
                    continue;
                } else if (isdigit(ls->Current)) {
                    readNumeral(ls, semInfo);
                    return Lumen::Token::SymbolNumber;
                } else if (isalpha(ls->Current) || ls->Current == '_') {
                    /* identifier or reserved word */
                    Lumen::String *ts;
                    do {
                        saveAndNext(ls);
                    } while (isalnum(ls->Current) || ls->Current == '_');
                    ts = Lumen::LexState::NewString(ls, LumenZBufferGet(ls->buff),
                                                    LumenZBufferLength(ls->buff));
                    if (ts->Reserved > 0)  /* reserved word? */
                        return ts->Reserved - 1 + LUA_LEX_STATE_FIRST_RESERVED;
                    else {
                        semInfo->ts = ts;
                        return Lumen::Token::SymbolName;
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


void Lumen::LexState::Next(Lumen::LexState *ls) {
    ls->LastLine = ls->LineNumber;
    if (ls->Ahead.Kind != Lumen::Token::SymbolEOS) {  /* is there a look-ahead token? */
        ls->CurToken = ls->Ahead;  /* use this one */
        ls->Ahead.Kind = Lumen::Token::SymbolEOS;  /* and discharge it */
    } else
        ls->CurToken.Kind = LLex(ls, &ls->CurToken.SemInfo);  /* read next token */
}


void Lumen::LexState::LookAhead(Lumen::LexState *ls) {
    LumenAssert(ls->Ahead.Kind == Lumen::Token::SymbolEOS);
    ls->Ahead.Kind = LLex(ls, &ls->Ahead.SemInfo);
}

