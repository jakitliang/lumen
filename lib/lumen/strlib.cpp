/*!
 * @brief Standard library for string operations and pattern-matching
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lumen/memory.h"


/* macro to `unsigned` a character */
#define uchar(c) static_cast<unsigned char>(c)

namespace Lua::Std::String {
    static int Byte(lua_State *L);

    static int Char(lua_State *L);

    static int Dump(lua_State *L);

    static int Find(lua_State *L);

    static int Format(lua_State *L);

    static int GFindNodeF(lua_State *L);

    static int GMatch(lua_State *L);

    static int GSub(lua_State *L);

    static int Length(lua_State *L);

    static int Lower(lua_State *L);

    static int Match(lua_State *L);

    static int Rep(lua_State *L);

    static int Reverse(lua_State *L);

    static int Sub(lua_State *L);

    static int Upper(lua_State *L);
}

static int Lua::Std::String::Length(lua_State *L) {
    size_t l;
    luaL_checklstring(L, 1, &l);
    lua_pushinteger(L, l);
    return 1;
}


static ptrdiff_t relStringPos(ptrdiff_t pos, size_t len) {
    /* relative string position: negative means back from end */
    if (pos < 0) pos += (ptrdiff_t) len + 1;
    return (pos >= 0) ? pos : 0;
}


static int Lua::Std::String::Sub(lua_State *L) {
    size_t l;
    const char *s = luaL_checklstring(L, 1, &l);
    ptrdiff_t start = relStringPos(luaL_checkinteger(L, 2), l);
    ptrdiff_t end = relStringPos(luaL_optinteger(L, 3, -1), l);
    if (start < 1) start = 1;
    if (end > (ptrdiff_t) l) end = (ptrdiff_t) l;
    if (start <= end)
        lua_pushlstring(L, s + start - 1, end - start + 1);
    else
        lua_pushliteral(L, "");
    return 1;
}


static int Lua::Std::String::Reverse(lua_State *L) {
    size_t l;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);
    luaL_buffinit(L, &b);
    while (l--) luaL_addchar(&b, s[l]);
    luaL_pushresult(&b);
    return 1;
}


static int Lua::Std::String::Lower(lua_State *L) {
    size_t l;
    size_t i;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);
    luaL_buffinit(L, &b);
    for (i = 0; i < l; i++)
        luaL_addchar(&b, tolower(uchar(s[i])));
    luaL_pushresult(&b);
    return 1;
}


static int Lua::Std::String::Upper(lua_State *L) {
    size_t l;
    size_t i;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);
    luaL_buffinit(L, &b);
    for (i = 0; i < l; i++)
        luaL_addchar(&b, toupper(uchar(s[i])));
    luaL_pushresult(&b);
    return 1;
}

static int Lua::Std::String::Rep(lua_State *L) {
    size_t l;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);
    int n = luaL_checkint(L, 2);
    luaL_buffinit(L, &b);
    while (n-- > 0)
        luaL_addlstring(&b, s, l);
    luaL_pushresult(&b);
    return 1;
}


static int Lua::Std::String::Byte(lua_State *L) {
    size_t l;
    const char *s = luaL_checklstring(L, 1, &l);
    ptrdiff_t posI = relStringPos(luaL_optinteger(L, 2, 1), l);
    ptrdiff_t posE = relStringPos(luaL_optinteger(L, 3, posI), l);
    int n, i;
    if (posI <= 0) posI = 1;
    if ((size_t) posE > l) posE = l; // NOLINT
    if (posI > posE) return 0;  /* empty interval; return no values */
    n = (int) (posE - posI + 1);
    if (posI + n <= posE)  /* overflow? */
        luaL_error(L, "string slice too long");
    luaL_checkstack(L, n, "string slice too long");
    for (i = 0; i < n; i++)
        lua_pushinteger(L, uchar(s[posI + i - 1]));
    return n;
}


static int Lua::Std::String::Char(lua_State *L) {
    int n = lua_gettop(L);  /* number of arguments */
    int i;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (i = 1; i <= n; i++) {
        int c = luaL_checkint(L, i);
        luaL_argcheck(L, uchar(c) == c, i, "invalid value");
        luaL_addchar(&b, uchar(c));
    }
    luaL_pushresult(&b);
    return 1;
}


static int writer(lua_State *L, const void *b, size_t size, void *B) {
    (void) L;
    luaL_addlstring((luaL_Buffer *) B, (const char *) b, size);
    return 0;
}


static int Lua::Std::String::Dump(lua_State *L) {
    luaL_Buffer b;
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, 1);
    luaL_buffinit(L, &b);
    if (lua_dump(L, writer, &b) != 0)
        luaL_error(L, "unable to dump given function");
    luaL_pushresult(&b);
    return 1;
}


/*
** {======================================================
** MARK: PATTERN MATCHING
** =======================================================
*/


#define CAP_UNFINISHED    (-1)
#define CAP_POSITION    (-2)

struct MatchState {
    const char *SrcInit;  /* init of source string */
    const char *SrcEnd;  /* end (`\0') of source string */
    lua_State *L;
    int Level;  /* total number of captures (finished or unfinished) */
    struct {
        const char *Init;
        ptrdiff_t Length;
    } Capture[LUA_MAXCAPTURES];
};


#define L_ESC        '%'
#define SPECIALS    "^$*+?.([%-"


static int checkCapture(MatchState *ms, int l) {
    l -= '1';
    if (l < 0 || l >= ms->Level || ms->Capture[l].Length == CAP_UNFINISHED)
        return luaL_error(ms->L, "invalid capture index");
    return l;
}


static inline int captureToClose(MatchState *ms) {
    int level = ms->Level;
    for (level--; level >= 0; level--)
        if (ms->Capture[level].Length == CAP_UNFINISHED) return level;
    return luaL_error(ms->L, "invalid pattern capture");
}


static const char *classEnd(MatchState *ms, const char *p) {
    switch (*p++) {
        case L_ESC: {
            if (*p == '\0')
                luaL_error(ms->L, "malformed pattern (ends with " LUA_QL("%%") ")");
            return p + 1;
        }
        case '[': {
            if (*p == '^') p++;
            do {  /* look for a `]` */
                if (*p == '\0')
                    luaL_error(ms->L, "malformed pattern (missing " LUA_QL("]") ")");
                if (*(p++) == L_ESC && *p != '\0')
                    p++;  /* skip escapes (e.g. `%]`) */
            } while (*p != ']');
            return p + 1;
        }
        default: {
            return p;
        }
    }
}


static int matchClass(int c, int cl) {
    int res;
    switch (tolower(cl)) {
        case 'a' :
            res = isalpha(c);
            break;
        case 'c' :
            res = iscntrl(c);
            break;
        case 'd' :
            res = isdigit(c);
            break;
        case 'l' :
            res = islower(c);
            break;
        case 'p' :
            res = ispunct(c);
            break;
        case 's' :
            res = isspace(c);
            break;
        case 'u' :
            res = isupper(c);
            break;
        case 'w' :
            res = isalnum(c);
            break;
        case 'x' :
            res = isxdigit(c);
            break;
        case 'z' :
            res = (c == 0);
            break;
        default:
            return (cl == c);
    }
    return (islower(cl) ? res : !res); // NOLINT
}


static int matchBracketClass(int c, const char *p, const char *ec) {
    int sig = 1;
    if (*(p + 1) == '^') {
        sig = 0;
        p++;  /* skip the `^' */
    }
    while (++p < ec) {
        if (*p == L_ESC) {
            p++;
            if (matchClass(c, uchar(*p)))
                return sig;
        } else if ((*(p + 1) == '-') && (p + 2 < ec)) {
            p += 2;
            if (uchar(*(p - 2)) <= c && c <= uchar(*p))
                return sig;
        } else if (uchar(*p) == c) return sig;
    }
    return !sig;
}


static int singleMatch(int c, const char *p, const char *ep) {
    switch (*p) {
        case '.':
            return 1;  /* matches any char */
        case L_ESC:
            return matchClass(c, uchar(*(p + 1)));
        case '[':
            return matchBracketClass(c, p, ep - 1);
        default:
            return (uchar(*p) == c);
    }
}


static const char *match(MatchState *ms, const char *s, const char *p);


static const char *matchBalance(MatchState *ms, const char *s,
                                const char *p) {
    if (*p == 0 || *(p + 1) == 0)
        luaL_error(ms->L, "unbalanced pattern");
    if (*s != *p) return nullptr;
    else {
        int b = *p; // NOLINT
        int e = *(p + 1); // NOLINT
        int cont = 1;
        while (++s < ms->SrcEnd) {
            if (*s == e) {
                if (--cont == 0) return s + 1;
            } else if (*s == b) cont++;
        }
    }
    return nullptr;  /* string ends out of balance */
}


static const char *maxExpand(MatchState *ms, const char *s,
                             const char *p, const char *ep) {
    ptrdiff_t i = 0;  /* counts maximum expand for item */
    while ((s + i) < ms->SrcEnd && singleMatch(uchar(*(s + i)), p, ep))
        i++;
    /* keeps trying to match with the maximum repetitions */
    while (i >= 0) {
        const char *res = match(ms, (s + i), ep + 1);
        if (res) return res;
        i--;  /* else didn't match; reduce 1 repetition to try again */
    }
    return nullptr;
}


static const char *minExpand(MatchState *ms, const char *s,
                             const char *p, const char *ep) {
    for (;;) {
        const char *res = match(ms, s, ep + 1);
        if (res != nullptr)
            return res;
        else if (s < ms->SrcEnd && singleMatch(uchar(*s), p, ep))
            s++;  /* try with one more repetition */
        else return nullptr;
    }
}


static const char *startCapture(MatchState *ms, const char *s,
                                const char *p, int what) {
    const char *res;
    int level = ms->Level;
    if (level >= LUA_MAXCAPTURES) luaL_error(ms->L, "too many captures");
    ms->Capture[level].Init = s;
    ms->Capture[level].Length = what;
    ms->Level = level + 1;
    if ((res = match(ms, s, p)) == nullptr)  /* match failed? */
        ms->Level--;  /* undo capture */
    return res;
}


static const char *endCapture(MatchState *ms, const char *s,
                              const char *p) {
    int l = captureToClose(ms);
    const char *res;
    ms->Capture[l].Length = s - ms->Capture[l].Init;  /* close capture */
    if ((res = match(ms, s, p)) == nullptr)  /* match failed? */
        ms->Capture[l].Length = CAP_UNFINISHED;  /* undo capture */
    return res;
}


static const char *matchCapture(MatchState *ms, const char *s, int l) {
    size_t len;
    l = checkCapture(ms, l);
    len = ms->Capture[l].Length;
    if ((size_t) (ms->SrcEnd - s) >= len &&
        memcmp(ms->Capture[l].Init, s, len) == 0)
        return s + len;
    else return nullptr;
}


static const char *match(MatchState *ms, const char *s, const char *p) {
    init: /* using goto to optimize tail recursion */
    switch (*p) {
        case '(': {  /* start capture */
            if (*(p + 1) == ')')  /* position capture? */
                return startCapture(ms, s, p + 2, CAP_POSITION);
            else
                return startCapture(ms, s, p + 1, CAP_UNFINISHED);
        }
        case ')': {  /* end capture */
            return endCapture(ms, s, p + 1);
        }
        case L_ESC: {
            switch (*(p + 1)) {
                case 'b': {  /* balanced string? */
                    s = matchBalance(ms, s, p + 2);
                    if (s == nullptr) return nullptr;
                    p += 4;
                    goto init;  /* else return match(ms, s, p+4); */
                }
                case 'f': {  /* frontier? */
                    const char *ep;
                    char previous;
                    p += 2;
                    if (*p != '[')
                        luaL_error(ms->L, "missing " LUA_QL("[") " after "
                                          LUA_QL("%%f") " in pattern");
                    ep = classEnd(ms, p);  /* points to what is next */
                    previous = (s == ms->SrcInit) ? '\0' : *(s - 1);
                    if (matchBracketClass(uchar(previous), p, ep - 1) ||
                        !matchBracketClass(uchar(*s), p, ep - 1))
                        return nullptr;
                    p = ep;
                    goto init;  /* else return match(ms, s, ep); */
                }
                default: {
                    if (isdigit(uchar(*(p + 1)))) {  /* capture results (%0-%9)? */
                        s = matchCapture(ms, s, uchar(*(p + 1)));
                        if (s == nullptr) return nullptr;
                        p += 2;
                        goto init;  /* else return match(ms, s, p+2) */
                    }
                    goto DEFAULT_CASE;  /* case default */
                }
            }
        }
        case '\0': {  /* end of pattern */
            return s;  /* match succeeded */
        }
        case '$': {
            if (*(p + 1) == '\0')  /* is the `$' the last char in pattern? */
                return (s == ms->SrcEnd) ? s : nullptr;  /* check end of string */
            else goto DEFAULT_CASE;
        }
        default:
        DEFAULT_CASE:
        {  /* it is a pattern item */
            const char *ep = classEnd(ms, p);  /* points to what is next */
            int m = s < ms->SrcEnd && singleMatch(uchar(*s), p, ep);
            switch (*ep) {
                case '?': {  /* optional */
                    const char *res;
                    if (m && ((res = match(ms, s + 1, ep + 1)) != nullptr))
                        return res;
                    p = ep + 1;
                    goto init;  /* else return match(ms, s, ep+1); */
                }
                case '*': {  /* 0 or more repetitions */
                    return maxExpand(ms, s, p, ep);
                }
                case '+': {  /* 1 or more repetitions */
                    return (m ? maxExpand(ms, s + 1, p, ep) : nullptr);
                }
                case '-': {  /* 0 or more repetitions (minimum) */
                    return minExpand(ms, s, p, ep);
                }
                default: {
                    if (!m) return nullptr;
                    s++;
                    p = ep;
                    goto init;  /* else return match(ms, s+1, ep); */
                }
            }
        }
    }
}


static void pushOneCapture(MatchState *ms, int i, const char *s,
                           const char *e) {
    if (i >= ms->Level) {
        if (i == 0)  /* ms->level == 0, too */
            lua_pushlstring(ms->L, s, e - s);  /* add whole match */
        else
            luaL_error(ms->L, "invalid capture index");
    } else {
        ptrdiff_t l = ms->Capture[i].Length;
        if (l == CAP_UNFINISHED) luaL_error(ms->L, "unfinished capture");
        if (l == CAP_POSITION)
            lua_pushinteger(ms->L, ms->Capture[i].Init - ms->SrcInit + 1);
        else
            lua_pushlstring(ms->L, ms->Capture[i].Init, l);
    }
}


static int pushCaptures(MatchState *ms, const char *s, const char *e) {
    int i;
    int nLevels = (ms->Level == 0 && s) ? 1 : ms->Level;
    luaL_checkstack(ms->L, nLevels, "too many captures");
    for (i = 0; i < nLevels; i++)
        pushOneCapture(ms, i, s, e);
    return nLevels;  /* number of strings pushed */
}


static int strFindAux(lua_State *L, int find) {
    size_t l1, l2;
    const char *s = luaL_checklstring(L, 1, &l1);
    const char *p = luaL_checklstring(L, 2, &l2);
    ptrdiff_t init = relStringPos(luaL_optinteger(L, 3, 1), l1) - 1;
    if (init < 0) init = 0;
    else if ((size_t) (init) > l1) init = (ptrdiff_t) l1;
    if (find && (lua_toboolean(L, 4) ||  /* explicit request? */
                 strpbrk(p, SPECIALS) == nullptr)) {  /* or no special characters? */
        /* do a plain search */
        const char *s2 = Lumen::Memory::Find(s + init, l1 - init, p, l2);
        if (s2) {
            lua_pushinteger(L, s2 - s + 1);
            lua_pushinteger(L, s2 - s + l2);
            return 2;
        }
    } else {
        int anchor = (*p == '^') ? (p++, 1) : 0;
        const char *s1 = s + init;
        MatchState ms; // NOLINT
        ms.L = L;
        ms.SrcInit = s;
        ms.SrcEnd = s + l1;
        do {
            const char *res;
            ms.Level = 0;
            if ((res = match(&ms, s1, p)) != nullptr) {
                if (find) {
                    lua_pushinteger(L, s1 - s + 1);  /* start */
                    lua_pushinteger(L, res - s);   /* end */
                    return pushCaptures(&ms, nullptr, 0) + 2;
                } else
                    return pushCaptures(&ms, s1, res);
            }
        } while (s1++ < ms.SrcEnd && !anchor);
    }
    lua_pushnil(L);  /* not found */
    return 1;
}


static int Lua::Std::String::Find(lua_State *L) {
    return strFindAux(L, 1);
}


static int Lua::Std::String::Match(lua_State *L) {
    return strFindAux(L, 0);
}


static int GMatchAux(lua_State *L) {
    MatchState ms; // NOLINT
    size_t ls;
    const char *s = lua_tolstring(L, lua_upvalueindex(1), &ls);
    const char *p = lua_tostring(L, lua_upvalueindex(2));
    const char *src;
    ms.L = L;
    ms.SrcInit = s;
    ms.SrcEnd = s + ls;
    for (src = s + (size_t) lua_tointeger(L, lua_upvalueindex(3));
         src <= ms.SrcEnd;
         src++) {
        const char *e;
        ms.Level = 0;
        if ((e = match(&ms, src, p)) != nullptr) {
            lua_Integer newStart = e - s;
            if (e == src) newStart++;  /* empty match? go at least one position */
            lua_pushinteger(L, newStart);
            lua_replace(L, lua_upvalueindex(3));
            return pushCaptures(&ms, src, e);
        }
    }
    return 0;  /* not found */
}


static int Lua::Std::String::GMatch(lua_State *L) {
    luaL_checkstring(L, 1);
    luaL_checkstring(L, 2);
    lua_settop(L, 2);
    lua_pushinteger(L, 0);
    lua_pushcclosure(L, GMatchAux, 3);
    return 1;
}


static int Lua::Std::String::GFindNodeF(lua_State *L) {
    return luaL_error(L, LUA_QL("string.gfind") " was renamed to "
                         LUA_QL("string.gmatch"));
}


static void addCString(MatchState *ms, luaL_Buffer *b, const char *s,
                       const char *e) {
    size_t l, i;
    const char *news = lua_tolstring(ms->L, 3, &l);
    for (i = 0; i < l; i++) {
        if (news[i] != L_ESC)
            luaL_addchar(b, news[i]);
        else {
            i++;  /* skip ESC */
            if (!isdigit(uchar(news[i])))
                luaL_addchar(b, news[i]);
            else if (news[i] == '0')
                luaL_addlstring(b, s, e - s);
            else {
                pushOneCapture(ms, news[i] - '1', s, e);
                luaL_addvalue(b);  /* add capture to accumulated result */
            }
        }
    }
}


static void addValue(MatchState *ms, luaL_Buffer *b, const char *s,
                     const char *e) {
    lua_State *L = ms->L;
    switch (lua_type(L, 3)) {
        case LUA_TNUMBER:
        case LUA_TSTRING: {
            addCString(ms, b, s, e);
            return;
        }
        case LUA_TFUNCTION: {
            int n;
            lua_pushvalue(L, 3);
            n = pushCaptures(ms, s, e);
            lua_call(L, n, 1);
            break;
        }
        case LUA_TTABLE: {
            pushOneCapture(ms, 0, s, e);
            lua_gettable(L, 3);
            break;
        }
    }
    if (!lua_toboolean(L, -1)) {  /* nil or false? */
        lua_pop(L, 1);
        lua_pushlstring(L, s, e - s);  /* keep original text */
    } else if (!lua_isstring(L, -1))
        luaL_error(L, "invalid replacement value (a %s)", luaL_typename(L, -1));
    luaL_addvalue(b);  /* add result to accumulator */
}


static int Lua::Std::String::GSub(lua_State *L) {
    size_t srcLength;
    const char *src = luaL_checklstring(L, 1, &srcLength);
    const char *p = luaL_checkstring(L, 2);
    int tr = lua_type(L, 3);
    int max_s = luaL_optint(L, 4, srcLength + 1);
    int anchor = (*p == '^') ? (p++, 1) : 0;
    int n = 0;
    MatchState ms; // NOLINT
    luaL_Buffer b;
    luaL_argcheck(L, tr == LUA_TNUMBER || tr == LUA_TSTRING ||
                     tr == LUA_TFUNCTION || tr == LUA_TTABLE, 3,
                  "string/function/table expected");
    luaL_buffinit(L, &b);
    ms.L = L;
    ms.SrcInit = src;
    ms.SrcEnd = src + srcLength;
    while (n < max_s) {
        const char *e;
        ms.Level = 0;
        e = match(&ms, src, p);
        if (e) {
            n++;
            addValue(&ms, &b, src, e);
        }
        if (e && e > src) /* not empty match? */
            src = e;  /* skip it */
        else if (src < ms.SrcEnd)
            luaL_addchar(&b, *src++);
        else break;
        if (anchor) break;
    }
    luaL_addlstring(&b, src, ms.SrcEnd - src);
    luaL_pushresult(&b);
    lua_pushinteger(L, n);  /* number of substitutions */
    return 2;
}

/* }====================================================== */


/* maximum size of each formatted item (> len(format('%99.99f', -1e308))) */
#define MAX_ITEM    512
/* valid flags in a format specification */
#define FLAGS    "-+ #0"
/*
** maximum size of each format specification (such as '%-099.99d')
** (+10 accounts for %99.99x plus margin of error)
*/
#define MAX_FORMAT    (sizeof(FLAGS) + sizeof(LUA_INTFRMLEN) + 10)


static void addQuoted(lua_State *L, luaL_Buffer *b, int arg) {
    size_t l;
    const char *s = luaL_checklstring(L, arg, &l);
    luaL_addchar(b, '"');
    while (l--) {
        switch (*s) {
            case '"':
            case '\\':
            case '\n': {
                luaL_addchar(b, '\\');
                luaL_addchar(b, *s);
                break;
            }
            case '\r': {
                luaL_addlstring(b, "\\r", 2);
                break;
            }
            case '\0': {
                luaL_addlstring(b, "\\000", 4);
                break;
            }
            default: {
                luaL_addchar(b, *s);
                break;
            }
        }
        s++;
    }
    luaL_addchar(b, '"');
}

static const char *scanFormat(lua_State *L, const char *strFormat, char *form) {
    const char *p = strFormat;
    while (*p != '\0' && strchr(FLAGS, *p) != nullptr) p++;  /* skip flags */
    if ((size_t) (p - strFormat) >= sizeof(FLAGS))
        luaL_error(L, "invalid format (repeated flags)");
    if (isdigit(uchar(*p))) p++;  /* skip width */
    if (isdigit(uchar(*p))) p++;  /* (2 digits at most) */
    if (*p == '.') {
        p++;
        if (isdigit(uchar(*p))) p++;  /* skip precision */
        if (isdigit(uchar(*p))) p++;  /* (2 digits at most) */
    }
    if (isdigit(uchar(*p)))
        luaL_error(L, "invalid format (width or precision too long)");
    *(form++) = '%';
    strncpy(form, strFormat, p - strFormat + 1);
    form += p - strFormat + 1;
    *form = '\0';
    return p;
}


static void addIntLength(char *form) {
    size_t l = strlen(form);
    char spec = form[l - 1];
    strcpy(form + l - 1, LUA_INTFRMLEN);
    form[l + sizeof(LUA_INTFRMLEN) - 2] = spec;
    form[l + sizeof(LUA_INTFRMLEN) - 1] = '\0';
}


static int Lua::Std::String::Format(lua_State *L) {
    int top = lua_gettop(L);
    int arg = 1;
    size_t sfl;
    const char *strFormat = luaL_checklstring(L, arg, &sfl);
    const char *strFormatEnd = strFormat + sfl;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    while (strFormat < strFormatEnd) {
        if (*strFormat != L_ESC)
            luaL_addchar(&b, *strFormat++);
        else if (*++strFormat == L_ESC)
            luaL_addchar(&b, *strFormat++);  /* %% */
        else { /* format item */
            char form[MAX_FORMAT];  /* to store the format (`%...') */
            char buff[MAX_ITEM];  /* to store the formatted item */
            if (++arg > top)
                luaL_argerror(L, arg, "no value");
            strFormat = scanFormat(L, strFormat, form);
            switch (*strFormat++) {
                case 'c': {
                    sprintf(buff, form, (int) luaL_checknumber(L, arg));
                    break;
                }
                case 'd':
                case 'i': {
                    addIntLength(form);
                    sprintf(buff, form, (LUA_INTFRM_T) luaL_checknumber(L, arg));
                    break;
                }
                case 'o':
                case 'u':
                case 'x':
                case 'X': {
                    addIntLength(form);
                    sprintf(buff, form, (unsigned LUA_INTFRM_T) luaL_checknumber(L, arg));
                    break;
                }
                case 'e':
                case 'E':
                case 'f':
                case 'g':
                case 'G': {
                    sprintf(buff, form, (double) luaL_checknumber(L, arg));
                    break;
                }
                case 'q': {
                    addQuoted(L, &b, arg);
                    continue;  /* skip the 'addSize' at the end */
                }
                case 's': {
                    size_t l;
                    const char *s = luaL_checklstring(L, arg, &l);
                    if (!strchr(form, '.') && l >= 100) {
                        /* no precision and string is too long to be formatted;
                           keep original string */
                        lua_pushvalue(L, arg);
                        luaL_addvalue(&b);
                        continue;  /* skip the `addSize` at the end */
                    } else {
                        sprintf(buff, form, s);
                        break;
                    }
                }
                default: {  /* also treat cases `pnLlh' */
                    return luaL_error(L, "invalid option " LUA_QL("%%%c") " to "
                                         LUA_QL("format"), *(strFormat - 1));
                }
            }
            luaL_addlstring(&b, buff, strlen(buff));
        }
    }
    luaL_pushresult(&b);
    return 1;
}


static const luaL_Reg strlib[] = {
        {"byte",    Lua::Std::String::Byte},
        {"char",    Lua::Std::String::Char},
        {"dump",    Lua::Std::String::Dump},
        {"find",    Lua::Std::String::Find},
        {"format",  Lua::Std::String::Format},
        {"gfind",   Lua::Std::String::GFindNodeF},
        {"gmatch",  Lua::Std::String::GMatch},
        {"gsub",    Lua::Std::String::GSub},
        {"len",     Lua::Std::String::Length},
        {"lower",   Lua::Std::String::Lower},
        {"match",   Lua::Std::String::Match},
        {"rep",     Lua::Std::String::Rep},
        {"reverse", Lua::Std::String::Reverse},
        {"sub",     Lua::Std::String::Sub},
        {"upper",   Lua::Std::String::Upper},
        {nullptr,   nullptr}
};


static void createMetatable(lua_State *L) {
    lua_createtable(L, 0, 1);  /* create metatable for strings */
    lua_pushliteral(L, "");  /* dummy string */
    lua_pushvalue(L, -2);
    lua_setmetatable(L, -2);  /* set string metatable */
    lua_pop(L, 1);  /* pop dummy string */
    lua_pushvalue(L, -2);  /* string library... */
    lua_setfield(L, -2, "__index");  /* ...is the __index metamethod */
    lua_pushvalue(L, -1);
    lua_setglobal(L, "String");
    lua_pop(L, 1);  /* pop metatable */
}


/*
** Open string library
*/
LUALIB_API int luaopen_string(lua_State *L) {
    luaL_register(L, LUA_STRLIBNAME, strlib);
#if defined(LUA_COMPAT_GFIND)
    lua_getfield(L, -1, "gmatch");
    lua_setfield(L, -2, "gfind");
#endif
    createMetatable(L);
    return 1;
}

