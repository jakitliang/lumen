/*!
 * @brief Auxiliary functions to manipulate prototypes and closures
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstddef>

#define LUA_CORE

#include "lumen/gc.h"
#include "lumen/memory.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/state.h"


Lumen::Closure *Lumen::CClosure::New(Lumen::State *L, int nElements, Lumen::Table *e) {
    Lumen::Closure *c = cast(Lumen::Closure *, LumenMemoryAlloc(L, Lumen::CClosure::SizeOf(nElements)));
    Lumen::GC::Link(L, LumenObject2GCObject(c), Lumen::TypeFunction);
    c->AsC.IsC = Lumen::BasicClosure::KindC;
    c->AsC.Env = e;
    c->AsC.NUpValues = cast_byte(nElements);
    return c;
}


Lumen::Closure *Lumen::LClosure::New(Lumen::State *L, int nElements, Lumen::Table *e) {
    Lumen::Closure *c = cast(Lumen::Closure *, LumenMemoryAlloc(L, Lumen::LClosure::SizeOf(nElements)));
    Lumen::GC::Link(L, LumenObject2GCObject(c), Lumen::TypeFunction);
    c->AsLua.IsC = Lumen::BasicClosure::KindLua;
    c->AsLua.Env = e;
    c->AsLua.NUpValues = cast_byte(nElements);
    while (nElements--) c->AsLua.UpValues[nElements] = nullptr;
    return c;
}


Lumen::UpValue *Lumen::UpValue::New(Lumen::State *L) {
    Lumen::UpValue *uv = LumenMemoryNew(L, Lumen::UpValue);
    Lumen::GC::Link(L, LumenObject2GCObject(uv), Lumen::TypeUpValue);
    uv->SelfValue = &uv->Value;
    uv->SelfValue->SetNil();
    return uv;
}


Lumen::UpValue *Lumen::UpValue::Find(Lumen::State *L, Lumen::Value level) {
    Lumen::GlobalState *g = LumenGlobalState(L);
    Lumen::GCObject **pp = &L->OpenedUpValue;
    Lumen::UpValue *p;
    Lumen::UpValue *uv;
    while (*pp != nullptr && (p = Lumen::GCObject::ToNullableUpValue(*pp))->SelfValue >= level) {
        LumenAssert(p->SelfValue != &p->Value);
        if (p->SelfValue == level) {  /* found a corresponding upvalue? */
            if (g->IsDead(LumenObject2GCObject(p)))  /* is it dead? */
                LumenObject2GCObject(p)->ChangeWhite();  /* ressurect it */
            return p;
        }
        pp = &p->GCNext;
    }
    uv = LumenMemoryNew(L, Lumen::UpValue);  /* not found: create a new one */
    uv->Type = Lumen::TypeUpValue;
    uv->Marked = g->GetWhite();
    uv->SelfValue = level;  /* current value lives in the stack */
    uv->GCNext = *pp;  /* chain it in the proper position */
    *pp = LumenObject2GCObject(uv);
    uv->Prev = &g->UpValueHead;  /* double link it in `uvhead' list */
    uv->Next = g->UpValueHead.Next;
    uv->Next->Prev = uv;
    g->UpValueHead.Next = uv;
    LumenAssert(uv->Next->Prev == uv && uv->Prev->Next == uv);
    return uv;
}


static inline void unlinkUpValue(Lumen::UpValue *uv) {
    LumenAssert(uv->Next->Prev == uv && uv->Prev->Next == uv);
    uv->Next->Prev = uv->Prev;  /* remove from `uvhead' list */
    uv->Prev->Next = uv->Next;
}


void Lumen::UpValue::Free(Lumen::State *L, Lumen::UpValue *uv) {
    if (uv->SelfValue != &uv->Value)  /* is it open? */
        unlinkUpValue(uv);  /* remove from open list */
    LumenMemoryFree(L, uv);  /* free upvalue */
}


void Lumen::UpValue::Close(Lumen::State *L, Lumen::Value level) {
    Lumen::UpValue *uv;
    Lumen::GlobalState *g = LumenGlobalState(L);
    while (L->OpenedUpValue != nullptr
           && (uv = Lumen::GCObject::ToNullableUpValue(L->OpenedUpValue))->SelfValue >= level) {
        Lumen::GCObject *o = LumenObject2GCObject(uv);
        LumenAssert(!o->IsBlack() && uv->SelfValue != &uv->Value);
        L->OpenedUpValue = uv->GCNext;  /* remove from `open' list */
        if (g->IsDead(o))
            Lumen::UpValue::Free(L, uv);  /* free upvalue */
        else {
            unlinkUpValue(uv);
            (&uv->Value)->SetObject(L, uv->SelfValue);
            uv->SelfValue = &uv->Value;  /* now current value lives here */
            Lumen::GC::LinkUpValue(L, uv);  /* link upvalue into `gcroot' list */
        }
    }
}


Lumen::Proto *Lumen::Proto::New(Lumen::State *L) {
    Lumen::Proto *f = LumenMemoryNew(L, Lumen::Proto);
    Lumen::GC::Link(L, LumenObject2GCObject(f), Lumen::TypeProto);
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


void Lumen::Proto::Free(Lumen::State *L, Lumen::Proto *f) {
    LumenMemoryFreeArray(L, f->Code, f->CodeCount, Lumen::Instruction);
    LumenMemoryFreeArray(L, f->SubProto, f->SubProtoCount, Lumen::Proto *);
    LumenMemoryFreeArray(L, f->K, f->KCount, Lumen::Object);
    LumenMemoryFreeArray(L, f->LineInfo, f->LineInfoCount, int);
    LumenMemoryFreeArray(L, f->LocalVars, f->LocalVarsCount, struct Lumen::LocalVar);
    LumenMemoryFreeArray(L, f->UpValues, f->UpValuesCount, Lumen::String *);
    LumenMemoryFree(L, f);
}


void Lumen::Closure::Free(Lumen::State *L, Lumen::Closure *c) {
    int size = c->Size();
    LumenMemoryFreeMemory(L, c, size);
}


/*
** Look for n-th local variable at line `line' in function `func'.
** Returns nullptr if not found.
*/
const char *Lumen::Proto::GetLocalName(const Lumen::Proto *f, int local_number, int pc) {
    int i;
    for (i = 0; i < f->LocalVarsCount && f->LocalVars[i].StartPC <= pc; i++) {
        if (pc < f->LocalVars[i].EndPC) {  /* is variable active? */
            local_number--;
            if (local_number == 0)
                return f->LocalVars[i].VarName->CString();
        }
    }
    return nullptr;  /* not found */
}

