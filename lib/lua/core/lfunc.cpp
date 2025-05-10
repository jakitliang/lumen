/*!
 * @brief Auxiliary functions to manipulate prototypes and closures
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstddef>

#define lfunc_c
#define LUA_CORE

#include "lua.h"

#include "lua/gc.h"
#include "lua/mem.h"
#include "lua/object.h"
#include "lua/state.h"


Lua::Closure *Lua::CClosure::New(Lua::State *L, int nElements, Lua::Table *e) {
    Lua::Closure *c = cast(Lua::Closure *, LuaMemoryAlloc(L, LuaCClosureSize(nElements)));
    Lua::GC::Link(L, LuaObject2GCObject(c), LUA_TFUNCTION);
    c->AsC.IsC = 1;
    c->AsC.Env = e;
    c->AsC.NUpValues = cast_byte(nElements);
    return c;
}


Lua::Closure *Lua::LClosure::New(Lua::State *L, int nElements, Lua::Table *e) {
    Lua::Closure *c = cast(Lua::Closure *, LuaMemoryAlloc(L, LuaLClosureSize(nElements)));
    Lua::GC::Link(L, LuaObject2GCObject(c), LUA_TFUNCTION);
    c->AsLua.IsC = 0;
    c->AsLua.Env = e;
    c->AsLua.NUpValues = cast_byte(nElements);
    while (nElements--) c->AsLua.UpValues[nElements] = nullptr;
    return c;
}


Lua::UpValue *Lua::UpValue::New(Lua::State *L) {
    Lua::UpValue *uv = LuaMemoryNew(L, Lua::UpValue);
    Lua::GC::Link(L, LuaObject2GCObject(uv), LUA_TUPVAL);
    uv->SelfValue = &uv->Value;
    LuaSetNilValue(uv->SelfValue);
    return uv;
}


Lua::UpValue *Lua::UpValue::Find(Lua::State *L, Lua::StkId level) {
    Lua::GlobalState *g = LuaGlobal(L);
    Lua::GCObject **pp = &L->OpenedUpValue;
    Lua::UpValue *p;
    Lua::UpValue *uv;
    while (*pp != nullptr && (p = LuaNullGCObject2UpValue(*pp))->SelfValue >= level) {
        lua_assert(p->SelfValue != &p->Value);
        if (p->SelfValue == level) {  /* found a corresponding upvalue? */
            if (LuaGCIsDead(g, LuaObject2GCObject(p)))  /* is it dead? */
                LuaGCChangeWhite(LuaObject2GCObject(p));  /* ressurect it */
            return p;
        }
        pp = &p->GCNext;
    }
    uv = LuaMemoryNew(L, Lua::UpValue);  /* not found: create a new one */
    uv->Type = LUA_TUPVAL;
    uv->Marked = LuaGCWhite(g);
    uv->SelfValue = level;  /* current value lives in the stack */
    uv->GCNext = *pp;  /* chain it in the proper position */
    *pp = LuaObject2GCObject(uv);
    uv->Prev = &g->UpValueHead;  /* double link it in `uvhead' list */
    uv->Next = g->UpValueHead.Next;
    uv->Next->Prev = uv;
    g->UpValueHead.Next = uv;
    lua_assert(uv->Next->Prev == uv && uv->Prev->Next == uv);
    return uv;
}


static void unlinkUpValue(Lua::UpValue *uv) {
    lua_assert(uv->Next->Prev == uv && uv->Prev->Next == uv);
    uv->Next->Prev = uv->Prev;  /* remove from `uvhead' list */
    uv->Prev->Next = uv->Next;
}


void Lua::UpValue::Free(Lua::State *L, Lua::UpValue *uv) {
    if (uv->SelfValue != &uv->Value)  /* is it open? */
        unlinkUpValue(uv);  /* remove from open list */
    LuaMemoryFree(L, uv);  /* free upvalue */
}


void Lua::UpValue::Close(Lua::State *L, Lua::StkId level) {
    Lua::UpValue *uv;
    Lua::GlobalState *g = LuaGlobal(L);
    while (L->OpenedUpValue != nullptr && (uv = LuaNullGCObject2UpValue(L->OpenedUpValue))->SelfValue >= level) {
        Lua::GCObject *o = LuaObject2GCObject(uv);
        lua_assert(!LuaGCIsBlack(o) && uv->SelfValue != &uv->Value);
        L->OpenedUpValue = uv->GCNext;  /* remove from `open' list */
        if (LuaGCIsDead(g, o))
            Lua::UpValue::Free(L, uv);  /* free upvalue */
        else {
            unlinkUpValue(uv);
            LuaSetObject(L, &uv->Value, uv->SelfValue);
            uv->SelfValue = &uv->Value;  /* now current value lives here */
            Lua::GC::LinkUpValue(L, uv);  /* link upvalue into `gcroot' list */
        }
    }
}


Lua::Proto *Lua::Proto::New(Lua::State *L) {
    Lua::Proto *f = LuaMemoryNew(L, Lua::Proto);
    Lua::GC::Link(L, LuaObject2GCObject(f), LUA_TPROTO);
    f->K = nullptr;
    f->KCount = 0;
    f->SubProto = nullptr;
    f->SubProtoCount = 0;
    f->Code = nullptr;
    f->CodeCount = 0;
    f->LineInfoCount = 0;
    f->UpValuesCount = 0;
    f->NUpValues = 0;
    f->UpValues = nullptr;
    f->NUmParams = 0;
    f->IsVararg = 0;
    f->MaxStackSize = 0;
    f->LineInfo = nullptr;
    f->LocalVarsCount = 0;
    f->LocalVars = nullptr;
    f->LineDefined = 0;
    f->LastLineDefined = 0;
    f->Source = nullptr;
    return f;
}


void Lua::Proto::Free(Lua::State *L, Lua::Proto *f) {
    LuaMemoryFreeArray(L, f->Code, f->CodeCount, Lua::Instruction);
    LuaMemoryFreeArray(L, f->SubProto, f->SubProtoCount, Lua::Proto *);
    LuaMemoryFreeArray(L, f->K, f->KCount, Lua::Value);
    LuaMemoryFreeArray(L, f->LineInfo, f->LineInfoCount, int);
    LuaMemoryFreeArray(L, f->LocalVars, f->LocalVarsCount, struct Lua::LocalVar);
    LuaMemoryFreeArray(L, f->UpValues, f->UpValuesCount, Lua::String *);
    LuaMemoryFree(L, f);
}


void Lua::Closure::Free(Lua::State *L, Lua::Closure *c) {
    int size = (c->AsC.IsC) ? LuaCClosureSize(c->AsC.NUpValues) : LuaLClosureSize(c->AsLua.NUpValues);
    LuaMemoryFreeMemory(L, c, size);
}


/*
** Look for n-th local variable at line `line' in function `func'.
** Returns nullptr if not found.
*/
const char *Lua::Proto::GetLocalName(const Lua::Proto *f, int local_number, int pc) {
    int i;
    for (i = 0; i < f->LocalVarsCount && f->LocalVars[i].StartPC <= pc; i++) {
        if (pc < f->LocalVars[i].EndPC) {  /* is variable active? */
            local_number--;
            if (local_number == 0)
                return LuaStringCString(f->LocalVars[i].VarName);
        }
    }
    return nullptr;  /* not found */
}

