/*!
 * @brief Some generic functions over Lua objects
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define lobject_c
#define LUA_CORE

#include "lua.h"

#include "lua/do.h"
#include "lua/mem.h"
#include "lua/object.h"
#include "lua/state.h"
#include "lua/string.h"
#include "lua/vm.h"


const Lua::Value Lua::NilValue = {LUA_TNIL, {nullptr}};


/**
 * converts an integer to a "floating point byte", represented as
 * (eeeeexxx), where the real value is (1xxx) * 2^(eeeee - 1) if
 * eeeee != 0 and (xxx) otherwise.
 */
int Lua::Int2FB(unsigned int x) {
    int e = 0;  /* exponent */
    while (x >= 16) {
        x = (x + 1) >> 1;
        e++;
    }
    if (x < 8) return static_cast<int>(x);
    else return ((e + 1) << 3) | (static_cast<int>(x) - 8);
}


/* converts back */
int Lua::FB2Int(int x) {
    int e = (x >> 3) & 31;
    if (e == 0) return x;
    else return ((x & 7) + 8) << (e - 1);
}


int Lua::Log2(unsigned int x) {
    static const Lua::Byte log_2[256] = {
            0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
    };
    int l = -1;
    while (x >= 256) {
        l += 8;
        x >>= 8;
    }
    return l + log_2[x];

}


int Lua::RawEqualObject(const Lua::Value *t1, const Lua::Value *t2) {
    if (LuaTypeOf(t1) != LuaTypeOf(t2)) return 0;
    else
        switch (LuaTypeOf(t1)) {
            case LUA_TNIL:
                return 1;
            case LUA_TNUMBER:
                return luai_numeq(LuaNumberValue(t1), LuaNumberValue(t2));
            case LUA_TBOOLEAN:
                return LuaBoolValue(t1) == LuaBoolValue(t2);  /* boolean true must be 1 !! */
            case LUA_TLIGHTUSERDATA:
                return LuaLUDataValue(t1) == LuaLUDataValue(t2);
            default:
                lua_assert(LuaIsCollectable(t1));
                return LuaGCValue(t1) == LuaGCValue(t2);
        }
}


int Lua::String2Decimal(const char *s, Lua::Number *result) {
    char *endPtr;
    *result = lua_str2number(s, &endPtr);
    if (endPtr == s) return 0;  /* conversion failed */
    if (*endPtr == 'x' || *endPtr == 'X')  /* maybe an hexadecimal constant? */
        *result = cast_num(strtoul(s, &endPtr, 16));
    if (*endPtr == '\0') return 1;  /* most common case */
    while (isspace(cast(unsigned char, *endPtr))) endPtr++;
    if (*endPtr != '\0') return 0;  /* invalid trailing characters? */
    return 1;
}


static void pushCString(Lua::State *L, const char *str) {
    LuaSetStringValue2S(L, L->Top, Lua::String::New(L, str));
    LuaIncrTop(L);
}


// this function handles only `%d`, `%c`, %f, %p, and `%s` formats
const char *Lua::PushVFString(Lua::State *L, const char *fmt, va_list argP) {
    int n = 1;
    pushCString(L, "");
    for (;;) {
        const char *e = strchr(fmt, '%');
        if (e == nullptr) break;
        LuaSetStringValue2S(L, L->Top, Lua::String::New(L, fmt, e - fmt));
        LuaIncrTop(L);
        switch (*(e + 1)) {
            case 's': {
                const char *s = va_arg(argP, char *);
                if (s == nullptr) s = "(null)";
                pushCString(L, s);
                break;
            }
            case 'c': {
                char buff[2];
                buff[0] = cast(char, va_arg(argP, int));
                buff[1] = '\0';
                pushCString(L, buff);
                break;
            }
            case 'd': {
                LuaSetNumberValue(L->Top, cast_num(va_arg(argP, int)));
                LuaIncrTop(L);
                break;
            }
            case 'f': {
                LuaSetNumberValue(L->Top, cast_num(va_arg(argP, Lua::UACNumber)));
                LuaIncrTop(L);
                break;
            }
            case 'p': {
                char buff[4 * sizeof(void *) + 8]; /* should be enough space for a `%p` */
                sprintf(buff, "%p", va_arg(argP, void *));
                pushCString(L, buff);
                break;
            }
            case '%': {
                pushCString(L, "%");
                break;
            }
            default: {
                char buff[3];
                buff[0] = '%';
                buff[1] = *(e + 1);
                buff[2] = '\0';
                pushCString(L, buff);
                break;
            }
        }
        n += 2;
        fmt = e + 2;
    }
    pushCString(L, fmt);
    Lua::VM::Concat(L, n + 1, cast_int(L->Top - L->Base) - 1);
    L->Top -= n;
    return LuaStringValue2CString(L->Top - 1);
}


const char *Lua::PushFString(Lua::State *L, const char *fmt, ...) {
    const char *msg;
    va_list argP;
            va_start(argP, fmt);
    msg = Lua::PushVFString(L, fmt, argP);
            va_end(argP);
    return msg;
}


void Lua::ChunkId(char *out, const char *source, size_t buffLen) {
    if (*source == '=') {
        strncpy(out, source + 1, buffLen);  /* remove first char */
        out[buffLen - 1] = '\0';  /* ensures null termination */
    } else {  /* out = "source", or "...source" */
        if (*source == '@') {
            size_t l;
            source++;  /* skip the `@' */
            buffLen -= sizeof(" '...' ");
            l = strlen(source);
            strcpy(out, "");
            if (l > buffLen) {
                source += (l - buffLen);  /* get last part of file name */
                strcat(out, "...");
            }
            strcat(out, source);
        } else {  /* out = [string "string"] */
            size_t len = strcspn(source, "\n\r");  /* stop at first newline */
            buffLen -= sizeof(" [string \"...\"] ");
            if (len > buffLen) len = buffLen;
            strcpy(out, "[string \"");
            if (source[len] != '\0') {  /* must truncate? */
                strncat(out, source, len);
                strcat(out, "...");
            } else
                strcat(out, source);
            strcat(out, "\"]");
        }
    }
}
