/*!
 * @brief Buffer Helper
 * @author Jakit
 * @date 2025/6/12
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#define LUA_LIB

#include <string_view>
#include <algorithm>

#include "lua.hpp"

#define buffLen(B)    ((B)->p - (B)->buffer)
#define buffFree(B)    ((size_t)(LUAL_BUFFERSIZE - buffLen(B)))

#define LIMIT    (LUA_MIN_STACK / 2)

static int emptyBuffer(Lua::Buffer *B) {
    size_t l = buffLen(B);
    if (l == 0) return 0;  /* put nothing on stack */
    else {
        B->L->PushString(B->buffer, l);
        B->p = B->buffer;
        B->level++;
        return 1;
    }
}

static void adjustStack(Lua::Buffer *B) {
    if (B->level > 1) {
        Lua::State *L = B->L;
        int toGet = 1;  /* number of levels to concat */
        size_t toPLen = L->StringLength(-1);
        do {
            size_t l = L->StringLength(-(toGet + 1));
            if (B->level - toGet + 1 >= LIMIT || toPLen > l) {
                toPLen += l;
                toGet++;
            } else break;
        } while (toGet < B->level);
        L->Concat(toGet);
        B->level = B->level - toGet + 1;
    }
}

char *Lua::Buffer::Prepare() {
    if (emptyBuffer(this))
        adjustStack(this);
    return buffer;
}

void Lua::Buffer::AddString(const char *s) {
    AddString(s, std::string_view(s).length());
}

void Lua::Buffer::AddString(const char *s, size_t l) {
    while (l--)
        AddChar(*s++);
}

void Lua::Buffer::AddValue() {
    size_t vl;
    const char *s = L->ToString(-1, &vl);
    if (vl <= buffFree(this)) {  /* fit into buffer? */
        memcpy(p, s, vl);  /* put it there */
        p += vl;
        L->Pop(1);  /* remove from stack */
    } else {
        if (emptyBuffer(this))
            L->Insert(-2);  /* put buffer before new value */
        level++;  /* add new value into B stack */
        adjustStack(this);
    }
}

void Lua::Buffer::PushResult() {
    emptyBuffer(this);
    L->Concat(level);
    level = 1;
}
