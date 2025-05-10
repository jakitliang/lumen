/*!
 * @brief Global State
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstddef>

#define lstate_c
#define LUA_CORE

#include "lua.h"

#include "lua/debug.h"
#include "lua/do.h"
#include "lua/gc.h"
#include "lua/lex.h"
#include "lua/mem.h"
#include "lua/state.h"
#include "lua/string.h"
#include "lua/table.h"
#include "lua/tm.h"


#define state_size(x)    (sizeof(x) + LUAI_EXTRASPACE)
#define fromstate(l)    (cast(Lua::Byte *, (l)) - LUAI_EXTRASPACE)
#define tostate(l)   (cast(Lua::State *, cast(Lua::Byte *, l) + LUAI_EXTRASPACE))


/*
** Main thread combines a thread state and the global state
*/
typedef struct LG {
    Lua::State l;
    Lua::GlobalState g;
} LG;


static void stack_init(Lua::State *L1, Lua::State *L) {
    /* initialize Lua::CallInfo array */
    L1->BaseCI = LuaMemoryNewVector(L, Lua::BasicCISize, Lua::CallInfo);
    L1->CallInfo = L1->BaseCI;
    L1->BaseCICount = Lua::BasicCISize;
    L1->EndCI = L1->BaseCI + L1->BaseCICount - 1;
    /* initialize stack array */
    L1->Stack = LuaMemoryNewVector(L, Lua::BasicStackSize + Lua::ExtraStack, Lua::Value);
    L1->StackCount = Lua::BasicStackSize + Lua::ExtraStack;
    L1->Top = L1->Stack;
    L1->StackLast = L1->Stack + (L1->StackCount - Lua::ExtraStack) - 1;
    /* initialize first ci */
    L1->CallInfo->Func = L1->Top;
    LuaSetNilValue(L1->Top++);  /* `function' entry for this `ci' */
    L1->Base = L1->CallInfo->Base = L1->Top;
    L1->CallInfo->Top = L1->Top + Lua::MinStack;
}


static void freestack(Lua::State *L, Lua::State *L1) {
    LuaMemoryFreeArray(L, L1->BaseCI, L1->BaseCICount, Lua::CallInfo);
    LuaMemoryFreeArray(L, L1->Stack, L1->StackCount, Lua::Value);
}


/*
** open parts that may cause memory-allocation errors
*/
static void f_luaopen(Lua::State *L, void *ud) {
    Lua::GlobalState *g = LuaGlobal(L);
    UNUSED(ud);
    stack_init(L, L);  /* init stack */
    LuaSetTableValue(L, LuaGlobalTable(L), Lua::Table::New(L, 0, 2));  /* table of globals */
    LuaSetTableValue(L, LuaRegistry(L), Lua::Table::New(L, 0, 2));  /* LuaRegistry */
    Lua::String::Resize(L, Lua::MinStringTableSize);  /* initial size of string table */
    Lua::TM::Init(L);
    Lua::LexState::Init(L);
    LuaStringFix(LuaStringNewLiteral(L, LUA_MEM_ERR_MSG));
    g->GCThreshold = 4 * g->TotalBytes;
}


static void preinit_state(Lua::State *L, Lua::GlobalState *g) {
    LuaGlobal(L) = g;
    L->Stack = nullptr;
    L->StackCount = 0;
    L->ErrorJmp = nullptr;
    L->Hook = nullptr;
    L->HookMask = 0;
    L->BaseHookCount = 0;
    L->AllowHook = 1;
    LuaDebugResetHookCount(L);
    L->OpenedUpValue = nullptr;
    L->BaseCICount = 0;
    L->NCCalls = L->BaseCCalls = 0;
    L->Status = 0;
    L->BaseCI = L->CallInfo = nullptr;
    L->SavedPC = nullptr;
    L->ErrFunc = 0;
    LuaSetNilValue(LuaGlobalTable(L));
}


static void close_state(Lua::State *L) {
    Lua::GlobalState *g = LuaGlobal(L);
    Lua::UpValue::Close(L, L->Stack);  /* close all upvalues for this thread */
    Lua::GC::FreeAll(L);  /* collect all objects */
    lua_assert(g->GCRoot == LuaObject2GCObject(L));
    lua_assert(g->StringMap.Count == 0);
    LuaMemoryFreeArray(L, LuaGlobal(L)->StringMap.HashTable, LuaGlobal(L)->StringMap.Capacity, Lua::String *);
    LuaZBufferFree(L, &g->Buff);
    freestack(L, L);
    lua_assert(g->TotalBytes == sizeof(LG));
    (*g->ReAllocator)(g->ReAllocatorUData, fromstate(L), state_size(LG), 0);
}


Lua::State *Lua::State::NewThread(Lua::State *L) {
    Lua::State *L1 = tostate(LuaMemoryAlloc(L, state_size(Lua::State)));
    Lua::GC::Link(L, LuaObject2GCObject(L1), LUA_TTHREAD);
    preinit_state(L1, LuaGlobal(L));
    stack_init(L1, L);  /* init stack */
    LuaSetObject2N(L, LuaGlobalTable(L1), LuaGlobalTable(L));  /* share table of globals */
    L1->HookMask = L->HookMask;
    L1->BaseHookCount = L->BaseHookCount;
    L1->Hook = L->Hook;
    LuaDebugResetHookCount(L1);
    lua_assert(LuaGCIsWhite(LuaObject2GCObject(L1)));
    return L1;
}


void Lua::State::FreeThread(Lua::State *L, Lua::State *L1) {
    Lua::UpValue::Close(L1, L1->Stack);  /* close all upvalues for this thread */
    lua_assert(L1->OpenedUpValue == nullptr);
    luai_userstatefree(L1);
    freestack(L, L1);
    LuaMemoryFreeMemory(L, fromstate(L1), state_size(Lua::State));
}


LUA_API Lua::State *lua_newstate(lua_Alloc f, void *ud) {
    int i;
    Lua::State *L;
    Lua::GlobalState *g;
    void *l = (*f)(ud, nullptr, 0, state_size(LG));
    if (l == nullptr) return nullptr;
    L = tostate(l);
    g = &((LG *) L)->g;
    L->GCNext = nullptr;
    L->Type = LUA_TTHREAD;
    g->CurrentWhite = LuaGCBit2Mask(Lua::GC::MarkWhite0Bit, Lua::GC::MarkFixedBit);
    L->Marked = LuaGCWhite(g);
    LuaGCSet2Bits(L->Marked, Lua::GC::MarkFixedBit, Lua::GC::MarkSFixedBit);
    preinit_state(L, g);
    g->ReAllocator = f;
    g->ReAllocatorUData = ud;
    g->MainThread = L;
    g->UpValueHead.Prev = &g->UpValueHead;
    g->UpValueHead.Next = &g->UpValueHead;
    g->GCThreshold = 0;  /* mark it as unfinished state */
    g->StringMap.Capacity = 0;
    g->StringMap.Count = 0;
    g->StringMap.HashTable = nullptr;
    LuaSetNilValue(LuaRegistry(L));
    LuaZBufferInit(L, &g->Buff);
    g->Panic = nullptr;
    g->GCState = Lua::GC::StatePause;
    g->GCRoot = LuaObject2GCObject(L);
    g->GCStringMap = 0;
    g->GCSweep = &g->GCRoot;
    g->GCGray = nullptr;
    g->GCGrayAgain = nullptr;
    g->GCWeak = nullptr;
    g->GCTMUData = nullptr;
    g->TotalBytes = sizeof(LG);
    g->GCPause = LUAI_GCPAUSE;
    g->GCStepMul = LUAI_GCMUL;
    g->GCDept = 0;
    for (i = 0; i < LUA_NUM_TAGS; i++) g->Metatable[i] = nullptr;
    if (Lua::Do::RawRunProtected(L, f_luaopen, nullptr) != 0) {
        /* memory allocation error: free partial state */
        close_state(L);
        L = nullptr;
    } else
        luai_userstateopen(L);
    return L;
}


static void callallgcTM(Lua::State *L, void *ud) {
    UNUSED(ud);
    Lua::GC::CallGCTM(L);  /* call GC metamethods for all udata */
}


LUA_API void lua_close(lua_State *L) {
    L = LuaGlobal(L)->MainThread;  /* only the main thread can be closed */
    LuaLock(L);
    Lua::UpValue::Close(L, L->Stack);  /* close all upvalues for this thread */
    Lua::GC::SeparateUserdata(L, 1);  /* separate udata that have GC metamethods */
    L->ErrFunc = 0;  /* no error function during GC metamethods */
    do {  /* repeat until no more errors */
        L->CallInfo = L->BaseCI;
        L->Base = L->Top = L->CallInfo->Base;
        L->NCCalls = L->BaseCCalls = 0;
    } while (Lua::Do::RawRunProtected(L, callallgcTM, nullptr) != 0);
    lua_assert(LuaGlobal(L)->GCTMUData == nullptr);
    luai_userstateclose(L);
    close_state(L);
}

