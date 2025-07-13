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

#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/state.h"
#include "lumen/undump.h"

struct DumpState {
    Lumen::State *L;
    Lumen::Writer writer;
    void *data;
    int strip;
    int status;
};

#define DumpMem(b, n, size, D)    DumpBlock(b,(n)*(size),D)
#define DumpVar(x, D)        DumpMem(&x,1,sizeof(x),D)

static void DumpBlock(const void *b, Lumen::UInteger size, DumpState *D) {
    if (D->status == 0) {
        LumenUnlock(D->L);
        D->status = (*D->writer)(reinterpret_cast<Lumen::IState *>(D->L), b, size, D->data);
        LumenLock(D->L);
    }
}

static void DumpChar(int y, DumpState *D) {
    char x = (char) y;
    DumpVar(x, D);
}

static void DumpInt(int x, DumpState *D) {
    DumpVar(x, D);
}

static void DumpNumber(Lumen::Number x, DumpState *D) {
    DumpVar(x, D);
}

static void DumpVector(const void *b, int n, Lumen::UInteger size, DumpState *D) {
    DumpInt(n, D);
    DumpMem(b, n, size, D);
}

static void DumpString(const Lumen::String *s, DumpState *D) {
    if (s == nullptr || s->CString() == nullptr) {
        Lumen::UInteger size = 0;
        DumpVar(size, D);
    } else {
        Lumen::UInteger size = s->Length + 1;        /* include trailing '\0' */
        DumpVar(size, D);
        DumpBlock(s->CString(), size, D);
    }
}

#define DumpCode(f, D)     DumpVector(f->Code,f->CodeCount,sizeof(Lumen::Instruction),D)

static void DumpFunction(const Lumen::Proto *f, const Lumen::String *p, DumpState *D);

static void DumpConstants(const Lumen::Proto *f, DumpState *D) {
    int i, n = f->KCount;
    DumpInt(n, D);
    for (i = 0; i < n; i++) {
        const Lumen::Object *o = &f->K[i];
        DumpChar(o->Type, D);
        switch (o->Type) {
            case Lumen::TypeNil:
                break;
            case Lumen::TypeBool:
                DumpChar(o->GetBool(), D);
                break;
            case Lumen::TypeNumber:
                DumpNumber(o->GetNumber(), D);
                break;
            case Lumen::TypeString:
                DumpString(o->GetString(), D);
                break;
            default:
                LumenAssert(0);            /* cannot happen */
                break;
        }
    }
    n = f->SubProtoCount;
    DumpInt(n, D);
    for (i = 0; i < n; i++) DumpFunction(f->SubProto[i], f->Source, D);
}

static void DumpDebug(const Lumen::Proto *f, DumpState *D) {
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

static void DumpFunction(const Lumen::Proto *f, const Lumen::String *p, DumpState *D) {
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
    Lumen::Dumper::Header(h);
    DumpBlock(h, LUAC_HEADER_SIZE, D);
}

/*
** dump Lua function as precompiled chunk
*/
int Lumen::Dumper::Dump(Lumen::State *L, const Lumen::Proto *f, Lumen::Writer w, void *data, int strip) {
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
