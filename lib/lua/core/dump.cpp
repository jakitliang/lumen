/*!
 * @brief Save precompiled Lua chunks
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */

#include <cstddef>

#define LUA_CORE

#include "lua.h"

#include "lua/object.h"
#include "lua/state.h"
#include "lua/undump.h"

struct DumpState {
    Lua::State *L;
    Lua::Writer writer;
    void *data;
    int strip;
    int status;
};

#define DumpMem(b, n, size, D)    DumpBlock(b,(n)*(size),D)
#define DumpVar(x, D)        DumpMem(&x,1,sizeof(x),D)

static void DumpBlock(const void *b, size_t size, DumpState *D) {
    if (D->status == 0) {
        LuaUnlock(D->L);
        D->status = (*D->writer)(D->L, b, size, D->data);
        LuaLock(D->L);
    }
}

static void DumpChar(int y, DumpState *D) {
    char x = (char) y;
    DumpVar(x, D);
}

static void DumpInt(int x, DumpState *D) {
    DumpVar(x, D);
}

static void DumpNumber(Lua::Number x, DumpState *D) {
    DumpVar(x, D);
}

static void DumpVector(const void *b, int n, size_t size, DumpState *D) {
    DumpInt(n, D);
    DumpMem(b, n, size, D);
}

static void DumpString(const Lua::String *s, DumpState *D) {
    if (s == nullptr || LuaStringCString(s) == nullptr) {
        size_t size = 0;
        DumpVar(size, D);
    } else {
        size_t size = s->Length + 1;        /* include trailing '\0' */
        DumpVar(size, D);
        DumpBlock(LuaStringCString(s), size, D);
    }
}

#define DumpCode(f, D)     DumpVector(f->Code,f->CodeCount,sizeof(Lua::Instruction),D)

static void DumpFunction(const Lua::Proto *f, const Lua::String *p, DumpState *D);

static void DumpConstants(const Lua::Proto *f, DumpState *D) {
    int i, n = f->KCount;
    DumpInt(n, D);
    for (i = 0; i < n; i++) {
        const Lua::Value *o = &f->K[i];
        DumpChar(LuaTypeOf(o), D);
        switch (LuaTypeOf(o)) {
            case LUA_TNIL:
                break;
            case LUA_TBOOLEAN:
                DumpChar(LuaBoolValue(o), D);
                break;
            case LUA_TNUMBER:
                DumpNumber(LuaNumberValue(o), D);
                break;
            case LUA_TSTRING:
                DumpString(LuaStringValue(o), D);
                break;
            default:
                lua_assert(0);            /* cannot happen */
                break;
        }
    }
    n = f->SubProtoCount;
    DumpInt(n, D);
    for (i = 0; i < n; i++) DumpFunction(f->SubProto[i], f->Source, D);
}

static void DumpDebug(const Lua::Proto *f, DumpState *D) {
    int i, n;
    n = (D->strip) ? 0 : f->LineInfoCount;
    DumpVector(f->LineInfo, n, sizeof(int), D);
    n = (D->strip) ? 0 : f->LocalVarsCount;
    DumpInt(n, D);
    for (i = 0; i < n; i++) {
        DumpString(f->LocalVars[i].VarName, D);
        DumpInt(f->LocalVars[i].StartPC, D);
        DumpInt(f->LocalVars[i].EndPC, D);
    }
    n = (D->strip) ? 0 : f->UpValuesCount;
    DumpInt(n, D);
    for (i = 0; i < n; i++) DumpString(f->UpValues[i], D);
}

static void DumpFunction(const Lua::Proto *f, const Lua::String *p, DumpState *D) {
    DumpString((f->Source == p || D->strip) ? nullptr : f->Source, D);
    DumpInt(f->LineDefined, D);
    DumpInt(f->LastLineDefined, D);
    DumpChar(f->NUpValues, D);
    DumpChar(f->NUmParams, D);
    DumpChar(f->IsVararg, D);
    DumpChar(f->MaxStackSize, D);
    DumpCode(f, D);
    DumpConstants(f, D);
    DumpDebug(f, D);
}

static void DumpHeader(DumpState *D) {
    char h[LUAC_HEADER_SIZE];
    Lua::Dumper::Header(h);
    DumpBlock(h, LUAC_HEADER_SIZE, D);
}

/*
** dump Lua function as precompiled chunk
*/
int Lua::Dumper::Dump(Lua::State *L, const Lua::Proto *f, Lua::Writer w, void *data, int strip) {
    DumpState D;
    D.L = L;
    D.writer = w;
    D.data = data;
    D.strip = strip;
    D.status = 0;
    DumpHeader(&D);
    DumpFunction(f, nullptr, &D);
    return D.status;
}
