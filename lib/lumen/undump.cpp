/*!
 * @brief Load precompiled Lua chunks
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstring>

#define LUA_CORE

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/memory.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/string.h"
#include "lumen/undump.h"
#include "lumen/zio.h"

struct LoadState {
    Lumen::State *L;
    Lumen::ZIO *Z;
    Lumen::ZBuffer *b;
    const char *name;
};

#ifdef LUAC_TRUST_BINARIES
#define IF(c,s)
#define error(S,s)
#else
#define IF(c, s)        if (c) error(S,s)

static void error(LoadState *S, const char *why) {
    Lumen::PushFString(S->L, "%s: %s in precompiled chunk", S->name, why);
    Lumen::Do::Throw(S->L, Lumen::RetErrSyntax);
}

#endif

#define LoadMem(S, b, n, size)       LoadBlock(S,b,(n)*(size))
#define LoadByte(S)                  (Lumen::Byte)LoadChar(S)
#define LoadVar(S, x)                LoadMem(S,&x,1,sizeof(x))
#define LoadVector(S, b, n, size)    LoadMem(S,b,n,size)

static void LoadBlock(LoadState *S, void *b, Lumen::UInteger size) {
    Lumen::UInteger r = Lumen::ZIO::Read(S->Z, b, size);
    IF (r != 0, "unexpected end");
}

static int LoadChar(LoadState *S) {
    char x;
    LoadVar(S, x);
    return x;
}

static int LoadInt(LoadState *S) {
    int x;
    LoadVar(S, x);
    IF (x < 0, "bad integer");
    return x;
}

static Lumen::Number LoadNumber(LoadState *S) {
    Lumen::Number x;
    LoadVar(S, x);
    return x;
}

static Lumen::String *LoadString(LoadState *S) {
    Lumen::UInteger size;
    LoadVar(S, size);
    if (size == 0)
        return nullptr;
    else {
        char *s = Lumen::ZBuffer::OpenSpace(S->L, S->b, size);
        LoadBlock(S, s, size);
        return Lumen::String::New(S->L, s, size - 1);        /* remove trailing '\0' */
    }
}

static void LoadCode(LoadState *S, Lumen::Proto *f) {
    int n = LoadInt(S);
    f->Code = LumenMemoryNewVector(S->L, n, Lumen::Instruction);
    f->CodeCount = n;
    LoadVector(S, f->Code, n, sizeof(Lumen::Instruction));
}

static Lumen::Proto *LoadFunction(LoadState *S, Lumen::String *p);

static void LoadConstants(LoadState *S, Lumen::Proto *f) {
    int i, n;
    n = LoadInt(S);
    f->K = LumenMemoryNewVector(S->L, n, Lumen::Object);
    f->KCount = n;
    for (i = 0; i < n; i++) (&f->K[i])->SetNil();
    for (i = 0; i < n; i++) {
        Lumen::Object *o = &f->K[i];
        int t = LoadChar(S);
        switch (t) {
            case Lumen::TypeNil:
                o->SetNil();
                break;
            case Lumen::TypeBool:
                o->SetBool(LoadChar(S) != 0);
                break;
            case Lumen::TypeNumber:
                o->SetNumber(LoadNumber(S));
                break;
            case Lumen::TypeString:
                LumenSetStringValue2N (S->L, o, LoadString(S));
                break;
            default:
                error(S, "bad constant");
                break;
        }
    }
    n = LoadInt(S);
    f->SubProto = LumenMemoryNewVector(S->L, n, Lumen::Proto*);
    f->SubProtoCount = n;
    for (i = 0; i < n; i++) f->SubProto[i] = nullptr;
    for (i = 0; i < n; i++) f->SubProto[i] = LoadFunction(S, f->Source);
}

static void LoadDebug(LoadState *S, Lumen::Proto *f) {
    int i, n;
    n = LoadInt(S);
    f->LineInfo = LumenMemoryNewVector(S->L, n, int);
    f->LineInfoCount = n;
    LoadVector(S, f->LineInfo, n, sizeof(int));
    n = LoadInt(S);
    f->LocalVars = LumenMemoryNewVector(S->L, n, Lumen::LocalVar);
    f->LocalVarsCount = n;
    for (i = 0; i < n; i++) f->LocalVars[i].VarName = nullptr;
    for (i = 0; i < n; i++) {
        f->LocalVars[i].VarName = LoadString(S);
        f->LocalVars[i].StartPC = LoadInt(S);
        f->LocalVars[i].EndPC = LoadInt(S);
    }
    n = LoadInt(S);
    f->UpValues = LumenMemoryNewVector(S->L, n, Lumen::String*);
    f->UpValuesCount = n;
    for (i = 0; i < n; i++) f->UpValues[i] = nullptr;
    for (i = 0; i < n; i++) f->UpValues[i] = LoadString(S);
}

static Lumen::Proto *LoadFunction(LoadState *S, Lumen::String *p) {
    Lumen::Proto *f;
    if (++S->L->NCCalls > LUA_MAX_C_CALLS) error(S, "code too deep");
    f = Lumen::Proto::New(S->L);
    LumenSetProtoValue2S(S->L, S->L->Top, f);
    LumenIncrTop(S->L);
    f->Source = LoadString(S);
    if (f->Source == nullptr) f->Source = p;
    f->LineDefined = LoadInt(S);
    f->LastLineDefined = LoadInt(S);
    f->NUpValues = LoadByte(S);
    f->NUmParams = LoadByte(S);
    f->IsVararg = LoadByte(S);
    f->MaxStackSize = LoadByte(S);
    LoadCode(S, f);
    LoadConstants(S, f);
    LoadDebug(S, f);
    IF (!Lumen::Debug::CheckCode(f), "bad code");
    S->L->Top--;
    S->L->NCCalls--;
    return f;
}

static void LoadHeader(LoadState *S) {
    char h[LUAC_HEADER_SIZE];
    char s[LUAC_HEADER_SIZE];
    Lumen::Dumper::Header(h);
    LoadBlock(S, s, LUAC_HEADER_SIZE);
    IF (memcmp(h, s, LUAC_HEADER_SIZE) != 0, "bad header");
}

/*
** load precompiled chunk
*/
Lumen::Proto *Lumen::Dumper::UnDump(Lumen::State *L, Lumen::ZIO *Z, Lumen::ZBuffer *buff, const char *name) {
    LoadState S;
    if (*name == '@' || *name == '=')
        S.name = name + 1;
    else if (*name == LUA_SIGNATURE[0])
        S.name = "binary string";
    else
        S.name = name;
    S.L = L;
    S.Z = Z;
    S.b = buff;
    LoadHeader(&S);
    return LoadFunction(&S, LumenStringNewLiteral(L, "=?"));
}

/*
* make header
*/
void Lumen::Dumper::Header(char *h) {
    int x = 1;
    memcpy(h, LUA_SIGNATURE, sizeof(LUA_SIGNATURE) - 1);
    h += sizeof(LUA_SIGNATURE) - 1;
    *h++ = (char) LUAC_VERSION;
    *h++ = (char) LUAC_FORMAT;
    *h++ = (char) *(char *) &x;                /* endianness */
    *h++ = (char) sizeof(int);
    *h++ = (char) sizeof(Lumen::UInteger);
    *h++ = (char) sizeof(Lumen::Instruction);
    *h++ = (char) sizeof(Lumen::Number);
    *h++ = (char) (((Lumen::Number) 0.5) == 0);        /* is Lumen::Number integral? */
}
