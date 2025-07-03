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
#include <cstdint>

#define LUA_CORE

#include "lumen/do.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/state.h"
#include "lumen/vm.h"


const Lumen::Object Lumen::NilValue = {Lumen::TypeNil, {nullptr}};

int Lumen::Log2(unsigned int x) {
    static const Lumen::Byte log2[256] = {
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
    return l + log2[x];
}

Lumen::Number Lumen::Arith(Lumen::ArithOp op, Lumen::Number v1, Lumen::Number v2) {
    switch (op) {
        case Lumen::ArithOpAdd:
            return LumenNumAdd(v1, v2);
        case Lumen::ArithOpSub:
            return LumenNumSub(v1, v2);
        case Lumen::ArithOpMul:
            return LumenNumMul(v1, v2);
        case Lumen::ArithOpDiv:
            return LumenNumDiv(v1, v2);
        case Lumen::ArithOpMod:
            return LumenNumMod(v1, v2);
        case Lumen::ArithOpPow:
            return LumenNumPow(v1, v2);
        case Lumen::ArithOpUnm:
            return LumenNumUnm(v1);
        default:
            LumenAssert(0);
            return 0;
    }
}

static void pushCString(Lumen::State *L, const char *str) {
    LumenSetStringValue2S(L, L->Top, Lumen::String::New(L, str));
    LumenIncrTop(L);
}

static void pushCString(Lumen::State *L, const char *str, Lumen::UInteger length) {
    LumenSetStringValue2S(L, L->Top, Lumen::String::New(L, str, length));
    LumenIncrTop(L);
}

static const char *formatPointer(const void *ptr, char *buffer) {
    auto value = reinterpret_cast<uintptr_t>(ptr);

    if (value == 0) {
        buffer[0] = 'N';
        buffer[1] = 'U';
        buffer[2] = 'L';
        buffer[3] = 'L';
        buffer[4] = '\0';
        return buffer;
    }

    // Prefix 0x
    char *p = buffer;
    *p++ = '0';
    *p++ = 'x';

    // 16bit + '\0'
    static constexpr char hex[] = "0123456789abcdef";
    char digits[2 * sizeof(void *)];
    int len = 0;

    while (value) {
        digits[len++] = hex[value & 0xF];
        value >>= 4;
    }

    // Reverse
    for (int i = len - 1; i >= 0; --i)
        *p++ = digits[i];
    *p = '\0';

    return buffer;
}

// this function handles only `%d`, `%c`, %f, %p, and `%s` formats
const char *Lumen::PushVFString(Lumen::State *L, const char *fmt, va_list argP) {
    int n = 1;
    pushCString(L, "");
    for (;;) {
        const char *e = strchr(fmt, '%');
        if (e == nullptr) break;
        LumenSetStringValue2S(L, L->Top, Lumen::String::New(L, fmt, e - fmt));
        LumenIncrTop(L);
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
                L->Top->SetNumber(cast_num(va_arg(argP, int)));
                LumenIncrTop(L);
                break;
            }
            case 'f': {
                L->Top->SetNumber(cast_num(va_arg(argP, Lumen::UACNumber)));
                LumenIncrTop(L);
                break;
            }
            case 'p': {
                char buff[4 * sizeof(void *) + 8]; /* should be enough space for a `%p` */
//                sprintf(buff, "%p", va_arg(argP, void *));
                formatPointer(va_arg(argP, void *), buff);
                pushCString(L, buff);
                break;
            }
            case 'U': {
                char buff[Lumen::UTF8BufferSize];
                int l = Lumen::Utf8Esc(buff, va_arg(argP, long));
                pushCString(L, buff + Lumen::UTF8BufferSize - l, l);
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
    Lumen::VM::Concat(L, n + 1, cast_int(L->Top - L->Base) - 1);
    L->Top -= n;
    return (L->Top - 1)->ToCString();
}


const char *Lumen::PushFString(Lumen::State *L, const char *fmt, ...) {
    const char *msg;
    va_list argP;
        va_start(argP, fmt);
    msg = Lumen::PushVFString(L, fmt, argP);
        va_end(argP);
    return msg;
}


void Lumen::ChunkId(char *out, const char *source, Lumen::UInteger buffLen) {
    if (*source == '=') {
        strncpy(out, source + 1, buffLen);  /* remove first char */
        out[buffLen - 1] = '\0';  /* ensures null termination */
    } else {  /* out = "source", or "...source" */
        if (*source == '@') {
            Lumen::UInteger l;
            source++;  /* skip the `@' */
            buffLen -= sizeof(" '...' ");
            l = Lumen::String::LengthOf(source);
            strcpy(out, "");
            if (l > buffLen) {
                source += (l - buffLen);  /* get last part of file name */
                strcat(out, "...");
            }
            strcat(out, source);
        } else {  /* out = [string "string"] */
            Lumen::UInteger len = strcspn(source, "\n\r");  /* stop at first newline */
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

const char *Lumen::Object::GetUpValueInfo(int n, Lumen::Object **val) {
    Lumen::Closure *f;
    if (!IsFunction()) return nullptr;
    f = GetClosure();
    if (f->AsC.IsC) {
        if (!(1 <= n && n <= f->AsC.NUpValues)) return nullptr;
        *val = &f->AsC.UpValues[n - 1];
        return "";
    } else {
        Lumen::Proto *p = f->AsLua.Func;
        if (!(1 <= n && n <= p->UpValuesCount)) return nullptr;
        *val = f->AsLua.UpValues[n - 1]->SelfValue;
        return p->UpValues[n - 1]->CString();
    }
}
