/*!
 * @brief Standard library for UTF-8 manipulation
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/6/18
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>

#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

#include "lumen.h"

#ifdef _WIN32

#include <Windows.h>

#endif

#define MAX_UNICODE    0x10FFFFu

#define MAX_UTF        0x7FFFFFFFu


#define MSGInvalid    "invalid UTF-8 code"

/*
** Integer type for decoded UTF-8 values; MAX_UTF needs 31 bits.
*/
#if (UINT_MAX >> 30) >= 1
typedef unsigned int UTFInt;
#else
typedef unsigned long UTFInt;
#endif


#define isCont(c)    (((c) & 0xC0) == 0x80)
#define isContP(p)    isCont(*(p))


/* from strlib */
/* translate a relative string position: negative means back from end */
static lua_Integer posRelated(lua_Integer pos, size_t len) {
    if (pos >= 0) return pos;
    else if (0u - (size_t) pos > len) return 0;
    else return (lua_Integer) len + pos + 1;
}


/*
** Decode one UTF-8 sequence, returning nullptr if byte sequence is
** invalid.  The array 'limits' stores the minimum value for each
** sequence length, to check for overlong representations. Its first
** entry forces an error for non-ascii bytes with no continuation
** bytes (count == 0).
*/
static const char *utf8_decode(const char *s, UTFInt *val, int strict) {
    static const UTFInt limits[] =
        {~(UTFInt) 0, 0x80, 0x800, 0x10000u, 0x200000u, 0x4000000u};
    unsigned int c = (unsigned char) s[0];
    UTFInt res = 0;  /* final result */
    if (c < 0x80)  /* ascii? */
        res = c;
    else {
        int count = 0;  /* to count number of continuation bytes */
        for (; c & 0x40; c <<= 1) {  /* while it needs continuation bytes... */
            unsigned int cc = (unsigned char) s[++count];  /* read next byte */
            if (!isCont(cc))  /* not a continuation byte? */
                return nullptr;  /* invalid byte sequence */
            res = (res << 6) | (cc & 0x3F);  /* add lower 6 bits from cont. byte */
        }
        res |= ((UTFInt) (c & 0x7F) << (count * 5));  /* add first byte */
        if (count > 5 || res > MAX_UTF || res < limits[count])
            return nullptr;  /* invalid byte sequence */
        s += count;  /* skip continuation bytes read */
    }
    if (strict) {
        /* check for invalid code points; too large or surrogates */
        if (res > MAX_UNICODE || (0xD800u <= res && res <= 0xDFFFu))
            return nullptr;
    }
    if (val) *val = res;
    return s + 1;  /* +1 to include first byte */
}


/*
** utf8len(s [, i [, j [, lax]]]) --> number of characters that
** start in the range [i,j], or nil + current position if 's' is not
** good formed in that interval
*/
static int utfLen(lua_State *L) {
    lua_Integer n = 0;  /* counter for the number of characters */
    size_t len;  /* string length in bytes */
    const char *s = luaL_checklstring(L, 1, &len);
    lua_Integer posI = posRelated(luaL_optinteger(L, 2, 1), len);
    lua_Integer posJ = posRelated(luaL_optinteger(L, 3, -1), len);
    int lax = lua_toboolean(L, 4);
    luaL_argcheck(L, 1 <= posI && --posI <= (lua_Integer) len, 2,
                  "initial position out of bounds");
    luaL_argcheck(L, --posJ < (lua_Integer) len, 3,
                  "final position out of bounds");
    while (posI <= posJ) {
        const char *s1 = utf8_decode(s + posI, nullptr, !lax);
        if (s1 == nullptr) {  /* conversion error? */
            lua_pushnil(L);  /* return fail ... */
            lua_pushinteger(L, posI + 1);  /* ... and current position */
            return 2;
        }
        posI = s1 - s;
        n++;
    }
    lua_pushinteger(L, n);
    return 1;
}


/*
** codepoint(s, [i, [j [, lax]]]) -> returns codepoints for all
** characters that start in the range [i,j]
*/
static int codepoint(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    lua_Integer posI = posRelated(luaL_optinteger(L, 2, 1), len);
    lua_Integer pose = posRelated(luaL_optinteger(L, 3, posI), len);
    int lax = lua_toboolean(L, 4);
    int n;
    const char *se;
    luaL_argcheck(L, posI >= 1, 2, "out of bounds");
    luaL_argcheck(L, pose <= (lua_Integer) len, 3, "out of bounds");
    if (posI > pose) return 0;  /* empty interval; return no values */
    if (pose - posI >= INT_MAX)  /* (lua_Integer -> int) overflow? */
        return luaL_error(L, "string slice too long");
    n = (int) (pose - posI) + 1;  /* upper bound for number of returns */
    luaL_checkstack(L, n, "string slice too long");
    n = 0;  /* count the number of returns */
    se = s + pose;  /* string end */
    for (s += posI - 1; s < se;) {
        UTFInt code;
        s = utf8_decode(s, &code, !lax);
        if (s == nullptr)
            return luaL_error(L, MSGInvalid);
        lua_pushinteger(L, code);
        n++;
    }
    return n;
}


static void pushUTFChar(lua_State *L, int arg) {
    auto code = (lua_Unsigned) luaL_checkinteger(L, arg);
    luaL_argcheck(L, code <= MAX_UTF, arg, "value out of range");
    lua_pushfstring(L, "%U", (long) code);
}


/*
** utfChar(n1, n2, ...)  -> char(n1)..char(n2)...
*/
static int utfChar(lua_State *L) {
    int n = lua_gettop(L);  /* number of arguments */
    if (n == 1)  /* optimize common case of single char */
        pushUTFChar(L, 1);
    else {
        int i;
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        for (i = 1; i <= n; i++) {
            pushUTFChar(L, i);
            luaL_addvalue(&b);
        }
        luaL_pushresult(&b);
    }
    return 1;
}


/*
** offset(s, n, [i])  -> index where n-th character counting from
**   position 'i' starts; 0 means character at 'i'.
*/
static int byteOffset(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    lua_Integer n = luaL_checkinteger(L, 2);
    lua_Integer posI = (n >= 0) ? 1 : len + 1;
    posI = posRelated(luaL_optinteger(L, 3, posI), len);
    luaL_argcheck(L, 1 <= posI && --posI <= (lua_Integer) len, 3,
                  "position out of bounds");
    if (n == 0) {
        /* find beginning of current byte sequence */
        while (posI > 0 && isContP(s + posI)) posI--;
    } else {
        if (isContP(s + posI))
            return luaL_error(L, "initial position is a continuation byte");
        if (n < 0) {
            while (n < 0 && posI > 0) {  /* move back */
                do {  /* find beginning of previous character */
                    posI--;
                } while (posI > 0 && isContP(s + posI));
                n++;
            }
        } else {
            n--;  /* do not move for 1st character */
            while (n > 0 && posI < (lua_Integer) len) {
                do {  /* find beginning of next character */
                    posI++;
                } while (isContP(s + posI));  /* (cannot pass final '\0') */
                n--;
            }
        }
    }
    if (n == 0)  /* did it find given character? */
        lua_pushinteger(L, posI + 1);
    else  /* no such character */
        lua_pushnil(L);
    return 1;
}


static int iter_aux(lua_State *L, int strict) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    auto n = (lua_Unsigned) lua_tointeger(L, 2);
    if (n < len) {
        while (isContP(s + n)) n++;  /* go to next character */
    }
    if (n >= len)  /* (also handles original 'n' being negative) */
        return 0;  /* no more codepoints */
    else {
        UTFInt code;
        const char *next = utf8_decode(s + n, &code, strict);
        if (next == nullptr || isContP(next))
            return luaL_error(L, MSGInvalid);
        lua_pushinteger(L, n + 1);
        lua_pushinteger(L, code);
        return 2;
    }
}


static int iterAuxStrict(lua_State *L) {
    return iter_aux(L, 1);
}

static int iterAuxLax(lua_State *L) {
    return iter_aux(L, 0);
}

static int iterCodes(lua_State *L) {
    int lax = lua_toboolean(L, 2);
    const char *s = luaL_checkstring(L, 1);
    luaL_argcheck(L, !isContP(s), 1, MSGInvalid);
    lua_pushcfunction(L, lax ? iterAuxLax : iterAuxStrict);
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 0);
    return 3;
}

/*
** sub(s, i [, j]) --> string.sub(s, i, j)
** Extracts a substring of a UTF-8 string based on character positions.
*/
static int utfSub(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    lua_Integer posI = posRelated(luaL_optinteger(L, 2, 1), len);
    lua_Integer pose = posRelated(luaL_optinteger(L, 3, -1), len);
    int lax = lua_toboolean(L, 4);

    luaL_Buffer b;
    luaL_buffinit(L, &b);

    lua_Integer current_byte_pos = 0;
    lua_Integer current_char_count = 0;

    lua_Integer start_byte_offset = -1;
    lua_Integer end_byte_offset = -1;

    const char *p = s;

    while (current_char_count < posI - 1 && ((size_t) (current_byte_pos)) < len) {
        const char *next_p = utf8_decode(p, nullptr, !lax);
        if (next_p == nullptr) {
            luaL_error(L, MSGInvalid);
        }
        current_byte_pos = next_p - s;
        p = next_p;
        current_char_count++;
    }
    start_byte_offset = current_byte_pos;

    p = s + start_byte_offset;
    current_char_count = posI - 1;

    while (current_char_count < pose && ((size_t) (current_byte_pos)) < len) {
        const char *next_p = utf8_decode(p, nullptr, !lax);
        if (next_p == nullptr) {
            luaL_error(L, MSGInvalid);
        }

        size_t char_byte_len = next_p - p;

        luaL_addlstring(&b, p, char_byte_len);

        current_byte_pos = next_p - s;
        p = next_p;
        current_char_count++;
    }

    end_byte_offset = current_byte_pos;
    luaL_pushresult(&b);
    return 1;
}

const char *Utf8ToLocale(const char *utf8Str, int wLen, int &len) {
#ifdef _WIN32
    thread_local std::string localeStr;
    if (wLen == 0) {
        wLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str, wLen, nullptr, 0);
    }
    if (wLen == 0) return "";

    std::wstring wStr(wLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, &wStr[0], wLen);

    len = WideCharToMultiByte(CP_ACP, 0, wStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len == 0) return "";

    localeStr = std::string(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, wStr.c_str(), -1, &localeStr[0], len, nullptr, nullptr);
    len--;
    return localeStr.c_str();
#else
    return utf8Str;
#endif
}

int UTF8ToLocale(lua_State *L) {
    size_t utf8StrLen;
    int localStrLen = 0;
    auto utf8Str = luaL_checklstring(L, 1, &utf8StrLen);
    auto localStr = Utf8ToLocale(utf8Str, (int) utf8StrLen, localStrLen);
    lua_pushlstring(L, localStr, localStrLen);
    return 1;
}

/* pattern to match a single UTF-8 character */
#define UTF8PATT    "[\0-\x7F\xC2-\xFD][\x80-\xBF]*"


static const luaL_Reg funcs[] = {
    {"offset",      byteOffset},
    {"codepoint",   codepoint},
    {"char",        utfChar},
    {"len",         utfLen},
    {"codes",       iterCodes},
    {"sub",         utfSub},
    {"toLocal",     UTF8ToLocale},
    /* placeholders */
    {"charpattern", nullptr},
    {nullptr,       nullptr}
};

template<>
LPP_API int Lumen::Open<Lumen::IUTF8>(Lumen::IState *L) {
#if LUA_VERSION_NUM < 502
    L->Register(LUA_UTF8LIBNAME, funcs);
#else
    L->NewLib(funcs);
#endif
    L->PushString(UTF8PATT, sizeof(UTF8PATT) / sizeof(char) - 1);
    L->SetField(-2, "charpattern");
    return 1;
}

LUALIB_API int luaopen_utf8(lua_State *L) {
#if LUA_VERSION_NUM < 502
    luaL_register(L, LUA_UTF8LIBNAME, funcs);
#else
    luaL_newlib(L, funcs);
#endif
    lua_pushlstring(L, UTF8PATT, sizeof(UTF8PATT) / sizeof(char) - 1);
    lua_setfield(L, -2, "charpattern");
    return 1;
}

