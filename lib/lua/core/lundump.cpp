/*!
 * @brief Load precompiled Lua chunks
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstring>

#define lundump_c
#define LUA_CORE

#include "lua.h"

#include "lua/debug.h"
#include "lua/do.h"
#include "lua/mem.h"
#include "lua/object.h"
#include "lua/string.h"
#include "lua/undump.h"
#include "lua/zio.h"

struct LoadState {
    Lua::State *L;
    Lua::ZIO *Z;
    Lua::ZBuffer *b;
    const char *name;
};

#ifdef LUAC_TRUST_BINARIES
#define IF(c,s)
#define error(S,s)
#else
#define IF(c, s)        if (c) error(S,s)

static void error(LoadState *S, const char *why) {
    Lua::PushFString(S->L, "%s: %s in precompiled chunk", S->name, why);
    Lua::Do::Throw(S->L, LUA_ERRSYNTAX);
}

#endif

#define LoadMem(S, b, n, size)       LoadBlock(S,b,(n)*(size))
#define LoadByte(S)                  (Lua::Byte)LoadChar(S)
#define LoadVar(S, x)                LoadMem(S,&x,1,sizeof(x))
#define LoadVector(S, b, n, size)    LoadMem(S,b,n,size)

static void LoadBlock(LoadState *S, void *b, size_t size) {
    size_t r = Lua::ZIO::Read(S->Z, b, size);
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

static Lua::Number LoadNumber(LoadState *S) {
    Lua::Number x;
    LoadVar(S, x);
    return x;
}

static Lua::String *LoadString(LoadState *S) {
    size_t size;
    LoadVar(S, size);
    if (size == 0)
        return nullptr;
    else {
        char *s = Lua::ZBuffer::OpenSpace(S->L, S->b, size);
        LoadBlock(S, s, size);
        return Lua::String::New(S->L, s, size - 1);        /* remove trailing '\0' */
    }
}

static void LoadCode(LoadState *S, Lua::Proto *f) {
    int n = LoadInt(S);
    f->Code = LuaMemoryNewVector(S->L, n, Lua::Instruction);
    f->CodeCount = n;
    LoadVector(S, f->Code, n, sizeof(Lua::Instruction));
}

static Lua::Proto *LoadFunction(LoadState *S, Lua::String *p);

static void LoadConstants(LoadState *S, Lua::Proto *f) {
    int i, n;
    n = LoadInt(S);
    f->K = LuaMemoryNewVector(S->L, n, Lua::Value);
    f->KCount = n;
    for (i = 0; i < n; i++) LuaSetNilValue(&f->K[i]);
    for (i = 0; i < n; i++) {
        Lua::Value *o = &f->K[i];
        int t = LoadChar(S);
        switch (t) {
            case LUA_TNIL:
                LuaSetNilValue(o);
                break;
            case LUA_TBOOLEAN: LuaSetBoolValue(o, LoadChar(S) != 0);
                break;
            case LUA_TNUMBER: LuaSetNumberValue(o, LoadNumber(S));
                break;
            case LUA_TSTRING:
                LuaSetStringValue2N (S->L, o, LoadString(S));
                break;
            default:
                error(S, "bad constant");
                break;
        }
    }
    n = LoadInt(S);
    f->SubProto = LuaMemoryNewVector(S->L, n, Lua::Proto*);
    f->SubProtoCount = n;
    for (i = 0; i < n; i++) f->SubProto[i] = nullptr;
    for (i = 0; i < n; i++) f->SubProto[i] = LoadFunction(S, f->Source);
}

static void LoadDebug(LoadState *S, Lua::Proto *f) {
    int i, n;
    n = LoadInt(S);
    f->LineInfo = LuaMemoryNewVector(S->L, n, int);
    f->LineInfoCount = n;
    LoadVector(S, f->LineInfo, n, sizeof(int));
    n = LoadInt(S);
    f->LocalVars = LuaMemoryNewVector(S->L, n, Lua::LocalVar);
    f->LocalVarsCount = n;
    for (i = 0; i < n; i++) f->LocalVars[i].VarName = nullptr;
    for (i = 0; i < n; i++) {
        f->LocalVars[i].VarName = LoadString(S);
        f->LocalVars[i].StartPC = LoadInt(S);
        f->LocalVars[i].EndPC = LoadInt(S);
    }
    n = LoadInt(S);
    f->UpValues = LuaMemoryNewVector(S->L, n, Lua::String*);
    f->UpValuesCount = n;
    for (i = 0; i < n; i++) f->UpValues[i] = nullptr;
    for (i = 0; i < n; i++) f->UpValues[i] = LoadString(S);
}

static Lua::Proto *LoadFunction(LoadState *S, Lua::String *p) {
    Lua::Proto *f;
    if (++S->L->NCCalls > LUAI_MAXCCALLS) error(S, "code too deep");
    f = Lua::Proto::New(S->L);
    LuaSetProtoValue2S(S->L, S->L->Top, f);
    LuaIncrTop(S->L);
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
    IF (!Lua::Debug::CheckCode(f), "bad code");
    S->L->Top--;
    S->L->NCCalls--;
    return f;
}

static void LoadHeader(LoadState *S) {
    char h[LUAC_HEADER_SIZE];
    char s[LUAC_HEADER_SIZE];
    Lua::Dumper::Header(h);
    LoadBlock(S, s, LUAC_HEADER_SIZE);
    IF (memcmp(h, s, LUAC_HEADER_SIZE) != 0, "bad header");
}

/*
** load precompiled chunk
*/
Lua::Proto *Lua::Dumper::UnDump(Lua::State *L, Lua::ZIO *Z, Lua::ZBuffer *buff, const char *name) {
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
    return LoadFunction(&S, LuaStringNewLiteral(L, "=?"));
}

/*
* make header
*/
void Lua::Dumper::Header(char *h) {
    int x = 1;
    memcpy(h, LUA_SIGNATURE, sizeof(LUA_SIGNATURE) - 1);
    h += sizeof(LUA_SIGNATURE) - 1;
    *h++ = (char) LUAC_VERSION;
    *h++ = (char) LUAC_FORMAT;
    *h++ = (char) *(char *) &x;                /* endianness */
    *h++ = (char) sizeof(int);
    *h++ = (char) sizeof(size_t);
    *h++ = (char) sizeof(Lua::Instruction);
    *h++ = (char) sizeof(Lua::Number);
    *h++ = (char) (((Lua::Number) 0.5) == 0);        /* is Lua::Number integral? */
}
