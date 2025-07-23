/*!
 * @brief Global State
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstddef>

#define LUA_CORE

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/gc.h"
#include "lumen/lex.h"
#include "lumen/memory.h"
#include "lumen/state.h"
#include "lumen/string.h"
#include "lumen/tm.h"
#include "lumen/common.inl"


#define sizeOfState(x)    (sizeof(x) + LUA_EXTRA_SPACE)
#define fromState(l)    (cast(Lumen::Byte *, (l)) - LUA_EXTRA_SPACE)
#define toState(l)   (cast(Lumen::State *, cast(Lumen::Byte *, l) + LUA_EXTRA_SPACE))


/*
** Main thread combines a thread state and the global state
*/
struct LG {
    Lumen::State l;
    Lumen::GlobalState g;
};


static void stackInit(Lumen::State *L1, Lumen::State *L) {
    /* initialize Lumen::CallInfo array */
    L1->BaseCI = LumenMemoryNewVector(L, Lumen::BasicCISize, Lumen::CallInfo);
    L1->CallInfo = L1->BaseCI;
    L1->BaseCICount = Lumen::BasicCISize;
    L1->EndCI = L1->BaseCI + L1->BaseCICount - 1;
    /* initialize stack array */
    L1->Stack = LumenMemoryNewVector(L, Lumen::BasicStackSize + Lumen::ExtraStack, Lumen::Object);
    L1->StackCount = Lumen::BasicStackSize + Lumen::ExtraStack;
    L1->Top = L1->Stack;
    L1->StackLast = L1->Stack + (L1->StackCount - Lumen::ExtraStack) - 1;
    /* initialize first ci */
    L1->CallInfo->Func = L1->Top;
    (L1->Top++)->SetNil();  /* `function' entry for this `ci' */
    L1->Base = L1->CallInfo->Base = L1->Top;
    L1->CallInfo->Top = L1->Top + Lumen::MinStack;
}


static void stackFree(Lumen::State *L, Lumen::State *L1) {
    LumenMemoryFreeArray(L, L1->BaseCI, L1->BaseCICount, Lumen::CallInfo);
    LumenMemoryFreeArray(L, L1->Stack, L1->StackCount, Lumen::Object);
}


/*
** open parts that may cause memory-allocation errors
*/
static void LuaStateOpenFile(Lumen::State *L, void *ud) {
    Lumen::GlobalState *g = LumenGlobalState(L);
    UNUSED(ud);
    stackInit(L, L);  /* init stack */
    LumenGlobalTable(L)->SetTable(L, Lumen::Table::New(L, 0, 2));  /* table of globals */
    LumenRegistryTable(L)->SetTable(L, Lumen::Table::New(L, 0, 2));  /* LumenRegistryTable */
    Lumen::String::Resize(L, Lumen::MinStringTableSize);  /* initial size of string table */
    Lumen::MetaMethod::Init(L);
    Lumen::LexState::Init(L);
    LumenStringFix(LumenStringNewLiteral(L, LUA_MEM_ERR_MSG));
    g->GCThreshold = 4 * g->TotalBytes;
}


static void LuaStatePreInit(Lumen::State *L, Lumen::GlobalState *g) {
    LumenGlobalState(L) = g;
    L->Stack = nullptr;
    L->StackCount = 0;
    L->ErrorJmp = nullptr;
    L->Hook = nullptr;
    L->HookMask = 0;
    L->BaseHookCount = 0;
    L->AllowHook = 1;
    LumenDebugResetHookCount(L);
    L->OpenedUpValue = nullptr;
    L->BaseCICount = 0;
    L->NCCalls = L->BaseCCalls = 0;
    L->Status = 0;
    L->BaseCI = L->CallInfo = nullptr;
    L->SavedPC = nullptr;
    L->ErrFunc = 0;
    LumenGlobalTable(L)->SetNil();
}


static void LuaStateClose(Lumen::State *L) {
    Lumen::GlobalState *g = LumenGlobalState(L);
    Lumen::UpValue::Close(L, L->Stack);  /* close all upvalues for this thread */
    Lumen::GC::FreeAll(L);  /* collect all objects */
    LumenAssert(g->GCRoot == LumenObject2GCObject(L));
    LumenAssert(g->StringMap.Count == 0);
    LumenMemoryFreeArray(L, LumenGlobalState(L)->StringMap.HashTable, LumenGlobalState(L)->StringMap.Capacity,
                         Lumen::String *);
    LumenZBufferFree(L, &g->Buff);
    stackFree(L, L);
    LumenAssert(g->TotalBytes == sizeof(LG));
    (*g->ReAllocator)(g->ReAllocatorUData, fromState(L), sizeOfState(LG), 0);
}

Lumen::Object *Lumen::State::ToObject(int idx) {
    if (idx > 0) {
        Lumen::Object *o = Base + (idx - 1);
        LumenApiCheck(this, idx <= CallInfo->Top - Base);
        if (o >= Top) return cast(Lumen::Object *, Lumen::NilObject);
        else return o;
    } else if (idx > Lumen::RegistryIndex) {
        LumenApiCheck(this, idx != 0 && -idx <= Top - Base);
        return Top + idx;
    } else
        switch (idx) {  /* pseudo-indices */
            case Lumen::RegistryIndex:
                return LumenRegistryTable(this);
            case Lumen::EnvIndex: {
                Lumen::Closure *func = GetCurrentFunction();
                Env.SetTable(this, func->AsC.Env);
                return &Env;
            }
            case Lumen::GlobalIndex:
                return LumenGlobalTable(this);
            default: {
                Lumen::Closure *func = GetCurrentFunction();
                idx = Lumen::GlobalIndex - idx;
                return (idx <= func->AsC.NUpValues)
                       ? &func->AsC.UpValues[idx - 1]
                       : cast(Lumen::Object *, Lumen::NilObject);
            }
        }
}

Lumen::Table *Lumen::State::GetCurrentEnv() {
    if (CallInfo == BaseCI)  /* no enclosing function? */
        return LumenGlobalTable(this)->GetTable();  /* use global table as environment */
    else {
        Lumen::Closure *func = GetCurrentFunction();
        return func->AsC.Env;
    }
}

Lumen::State *Lumen::State::NewThread(Lumen::State *L) {
    Lumen::State *L1 = toState(LumenMemoryAlloc(L, sizeOfState(Lumen::State)));
    Lumen::GC::Link(L, LumenObject2GCObject(L1), Lumen::TypeThread);
    LuaStatePreInit(L1, LumenGlobalState(L));
    stackInit(L1, L);  /* init stack */
    LumenSetObject2N(L, LumenGlobalTable(L1), LumenGlobalTable(L));  /* share table of globals */
    L1->HookMask = L->HookMask;
    L1->BaseHookCount = L->BaseHookCount;
    L1->Hook = L->Hook;
    LumenDebugResetHookCount(L1);
    LumenAssert(LumenObject2GCObject(L1)->IsWhite());
    return L1;
}


void Lumen::State::FreeThread(Lumen::State *L, Lumen::State *L1) {
    Lumen::UpValue::Close(L1, L1->Stack);  /* close all upvalues for this thread */
    LumenAssert(L1->OpenedUpValue == nullptr);
    luai_userstatefree(L1);
    stackFree(L, L1);
    LumenMemoryFreeMemory(L, fromState(L1), sizeOfState(Lumen::State));
}


Lumen::State *Lumen::State::New(Lumen::Allocator allocator, void *userData) {
    int i;
    Lumen::State *L;
    Lumen::GlobalState *g;
    void *l = (*allocator)(userData, nullptr, 0, sizeOfState(LG));
    if (l == nullptr) return nullptr;
    L = toState(l);
    g = &((LG *) L)->g;
    L->GCNext = nullptr;
    L->Type = Lumen::TypeThread;
    g->CurrentWhite = Lumen::GC::Bit2Mask(Lumen::GC::MarkWhite0Bit, Lumen::GC::MarkFixedBit);
    L->Marked = g->GetWhite();
    Lumen::GC::Set2Bits(L->Marked, Lumen::GC::MarkFixedBit, Lumen::GC::MarkSFixedBit);
    LuaStatePreInit(L, g);
    g->ReAllocator = allocator;
    g->ReAllocatorUData = userData;
    g->MainThread = L;
    g->UpValueHead.Prev = &g->UpValueHead;
    g->UpValueHead.Next = &g->UpValueHead;
    g->GCThreshold = 0;  /* mark it as unfinished state */
    g->StringMap.Capacity = 0;
    g->StringMap.Count = 0;
    g->StringMap.HashTable = nullptr;
    LumenRegistryTable(L)->SetNil();
    LumenZBufferInit(L, &g->Buff);
    g->Panic = nullptr;
    g->GCState = Lumen::GC::StatePause;
    g->GCRoot = LumenObject2GCObject(L);
    g->GCStringMap = 0;
    g->GCSweep = &g->GCRoot;
    g->GCGray = nullptr;
    g->GCGrayAgain = nullptr;
    g->GCWeak = nullptr;
    g->GCTMUData = nullptr;
    g->TotalBytes = sizeof(LG);
    g->GCPause = LUA_GC_PAUSE;
    g->GCStepMul = LUA_GC_MUL;
    g->GCDept = 0;
    for (i = 0; i < Lumen::TypeCount; i++) g->Metatable[i] = nullptr;
    if (Lumen::Do::RawRunProtected(L, LuaStateOpenFile, nullptr) != 0) {
        /* memory allocation error: free partial state */
        LuaStateClose(L);
        L = nullptr;
    } else
        luai_userstateopen(L);
    return L;
}

static void LuaStateCallAllGcTM(Lumen::State *L, void *) {
    Lumen::GC::CallGCTM(L);  /* call GC metaMethods for all uData */
}

void Lumen::State::Close(Lumen::State *L) {
    L = LumenGlobalState(L)->MainThread;  /* only the main thread can be closed */
    LumenLock(L);
    Lumen::UpValue::Close(L, L->Stack);  /* close all upvalues for this thread */
    Lumen::GC::SeparateUserdata(L, 1);  /* separate uData that have GC metaMethods */
    L->ErrFunc = 0;  /* no error function during GC metaMethods */
    do {  /* repeat until no more errors */
        L->CallInfo = L->BaseCI;
        L->Base = L->Top = L->CallInfo->Base;
        L->NCCalls = L->BaseCCalls = 0;
    } while (Lumen::Do::RawRunProtected(L, LuaStateCallAllGcTM, nullptr) != 0);
    LumenAssert(LumenGlobalState(L)->GCTMUData == nullptr);
    luai_userstateclose(L);
    LuaStateClose(L);
}
