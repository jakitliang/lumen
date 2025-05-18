/*!
 * @brief Garbage Collector
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstring>

#define LUA_CORE

#include "lua.h"

#include "lua/debug.h"
#include "lua/do.h"
#include "lua/gc.h"
#include "lua/mem.h"
#include "lua/object.h"
#include "lua/state.h"
#include "lua/string.h"
#include "lua/table.h"
#include "lua/tm.h"


#define LUA_GC_STEP_SIZE    1024u
#define LUA_GC_SWEEP_MAX    40
#define LUA_GC_SWEEP_COST    10
#define LUA_GC_FINALIZE_COST    100


#define maskMarks    cast_byte(~(LuaGCBitMask(Lua::GC::MarkBlackBit)|Lua::GC::MarkWhiteBits))

#define makeWhite(g, x) \
    ((x)->AsObject.Marked = cast_byte(((x)->AsObject.Marked & maskMarks) | LuaGCWhite(g)))

#define white2gray(x)    LuaGCReset2Bits((x)->AsObject.Marked, Lua::GC::MarkWhite0Bit, Lua::GC::MarkWhite1Bit)
#define black2gray(x)    LuaGCResetBit((x)->AsObject.Marked, Lua::GC::MarkBlackBit)

#define stringMark(s)    LuaGCReset2Bits((s)->Marked, Lua::GC::MarkWhite0Bit, Lua::GC::MarkWhite1Bit)


#define isFinalized(u)      LuaGCTestBit((u)->Marked, Lua::GC::MarkFinalizedBit)
#define markFinalized(u)    LuaGCLSetBit((u)->Marked, Lua::GC::MarkFinalizedBit)

namespace Lua::GC {
    static inline constexpr Lua::Byte WeakKey = LuaGCBitMask(Lua::GC::MarkKeyWeakBit);
    static inline constexpr Lua::Byte WeakValue = LuaGCBitMask(Lua::GC::MarkValueWeakBit);
}

#define markValue(g, o) \
LuaDo(                  \
    LuaCheckConsistency(o); \
    if (LuaIsCollectable(o) && LuaGCIsWhite(LuaGCValue(o))) \
        reallyMarkObject(g,LuaGCValue(o));                  \
)

#define markObject(g, t) \
LuaDo(                   \
    if (LuaGCIsWhite(LuaObject2GCObject(t))) \
        reallyMarkObject(g, LuaObject2GCObject(t)); \
)


#define setThreshold(g)  (g->GCThreshold = (g->Estimate/100) * g->GCPause)


static void removeEntry(Lua::Node *n) {
    lua_assert(LuaTypeIsNil(LuaTableGetValue(n)));
    if (LuaIsCollectable(LuaTableGetKey(n)))
        LuaSetType(LuaTableGetKey(n), LUA_TDEADKEY);  /* dead key; remove it */
}


static void reallyMarkObject(Lua::GlobalState *g, Lua::GCObject *o) {
    lua_assert(LuaGCIsWhite(o) && !LuaGCIsDead(g, o));
    white2gray(o);
    switch (o->AsObject.Type) {
        case LUA_TSTRING: {
            return;
        }
        case LUA_TUSERDATA: {
            Lua::Table *mt = LuaGCObject2Userdata(o)->Metatable;
            LuaGCGray2Black(o);  /* uData are never gray */
            if (mt) markObject(g, mt);
            markObject(g, LuaGCObject2Userdata(o)->Env);
            return;
        }
        case LUA_TUPVAL: {
            Lua::UpValue *uv = LuaGCObject2UpValue(o);
            markValue(g, uv->SelfValue);
            if (uv->SelfValue == &uv->Value)  /* closed? */
                LuaGCGray2Black(o);  /* open upValues are never black */
            return;
        }
        case LUA_TFUNCTION: {
            LuaGCObject2Closure(o)->AsC.GCList = g->GCGray;
            g->GCGray = o;
            break;
        }
        case LUA_TTABLE: {
            LuaGCObject2Table(o)->GCList = g->GCGray;
            g->GCGray = o;
            break;
        }
        case LUA_TTHREAD: {
            LuaGCObject2Thread(o)->GCList = g->GCGray;
            g->GCGray = o;
            break;
        }
        case LUA_TPROTO: {
            LuaGCObject2Proto(o)->GCList = g->GCGray;
            g->GCGray = o;
            break;
        }
        default:
            lua_assert(0);
    }
}


static void markTMUData(Lua::GlobalState *g) {
    Lua::GCObject *u = g->GCTMUData;
    if (u) {
        do {
            u = u->AsObject.GCNext;
            makeWhite(g, u);  /* may be marked, if left from previous GC */
            reallyMarkObject(g, u);
        } while (u != g->GCTMUData);
    }
}


/* move `dead` UData that need finalization to list `TMUData` */
size_t Lua::GC::SeparateUserdata(Lua::State *L, int all) {
    Lua::GlobalState *g = LuaGlobal(L);
    size_t deadMem = 0;
    Lua::GCObject **p = &g->MainThread->GCNext;
    Lua::GCObject *curr;
    while ((curr = *p) != nullptr) {
        if (!(LuaGCIsWhite(curr) || all) || isFinalized(LuaGCObject2Userdata(curr)))
            p = &curr->AsObject.GCNext;  /* don't bother with them */
        else if (LuaTMGetFast(L, LuaGCObject2Userdata(curr)->Metatable, Lua::TM::NameGC) == nullptr) {
            markFinalized(LuaGCObject2Userdata(curr));  /* don't need finalization */
            p = &curr->AsObject.GCNext;
        } else {  /* must call its gc method */
            deadMem += LuaUserdataSize(LuaGCObject2Userdata(curr));
            markFinalized(LuaGCObject2Userdata(curr));
            *p = curr->AsObject.GCNext;
            /* link `curr' at the end of `tmuData' list */
            if (g->GCTMUData == nullptr)  /* list is empty? */
                g->GCTMUData = curr->AsObject.GCNext = curr;  /* creates a circular list */
            else {
                curr->AsObject.GCNext = g->GCTMUData->AsObject.GCNext;
                g->GCTMUData->AsObject.GCNext = curr;
                g->GCTMUData = curr;
            }
        }
    }
    return deadMem;
}


static int traverseTable(Lua::GlobalState *g, Lua::Table *h) {
    int i;
    int weakKey = 0;
    int weakValue = 0;
    const Lua::Value *mode;
    if (h->Metatable) markObject(g, h->Metatable);
    mode = LuaTMGetGlobalFast(g, h->Metatable, Lua::TM::NameMode);
    if (mode && LuaTypeIsString(mode)) {  /* is there a weak mode? */
        weakKey = (strchr(LuaStringValue2CString(mode), 'k') != nullptr);
        weakValue = (strchr(LuaStringValue2CString(mode), 'v') != nullptr);
        if (weakKey || weakValue) {  /* is really weak? */
            h->Marked &= ~(Lua::GC::WeakKey | Lua::GC::WeakValue);  /* clear bits */
            h->Marked |= cast_byte((weakKey << Lua::GC::MarkKeyWeakBit) |
                                   (weakValue << Lua::GC::MarkValueWeakBit));
            h->GCList = g->GCWeak;  /* must be cleared after GC, ... */
            g->GCWeak = LuaObject2GCObject(h);  /* ... so put in the appropriate list */
        }
    }
    if (weakKey && weakValue) return 1;
    if (!weakValue) {
        i = h->ArrayCount;
        while (i--) markValue(g, &h->Array[i]);
    }
    i = LuaTableNodeCount(h);
    while (i--) {
        Lua::Node *n = LuaTableGetNode(h, i);
        lua_assert(LuaTypeOf(LuaTableGetKey(n)) != LUA_TDEADKEY || LuaTypeIsNil(LuaTableGetValue(n)));
        if (LuaTypeIsNil(LuaTableGetValue(n)))
            removeEntry(n);  /* remove empty entries */
        else {
            lua_assert(!LuaTypeIsNil(LuaTableGetKey(n)));
            if (!weakKey) markValue(g, LuaTableGetKey(n));
            if (!weakValue) markValue(g, LuaTableGetValue(n));
        }
    }
    return weakKey || weakValue;
}


/*
** All marks are conditional because a GC may happen while the
** prototype is still being created
*/
static void traverseProto(Lua::GlobalState *g, Lua::Proto *f) {
    int i;
    if (f->Source) stringMark(f->Source);
    for (i = 0; i < f->KCount; i++)  /* mark literals */
        markValue(g, &f->K[i]);
    for (i = 0; i < f->UpValuesCount; i++) {  /* mark upValue names */
        if (f->UpValues[i])
            stringMark(f->UpValues[i]);
    }
    for (i = 0; i < f->SubProtoCount; i++) {  /* mark nested proto */
        if (f->SubProto[i]) markObject(g, f->SubProto[i]);
    }
    for (i = 0; i < f->LocalVarsCount; i++) {  /* mark local-variable names */
        if (f->LocalVars[i].VarName)
            stringMark(f->LocalVars[i].VarName);
    }
}


static void traverseClosure(Lua::GlobalState *g, Lua::Closure *cl) {
    markObject(g, cl->AsC.Env);
    if (cl->AsC.IsC) {
        int i;
        for (i = 0; i < cl->AsC.NUpValues; i++)  /* mark its upValues */
            markValue(g, &cl->AsC.UpValues[i]);
    } else {
        int i;
        lua_assert(cl->AsLua.NUpValues == cl->AsLua.Func->NUpValues);
        markObject(g, cl->AsLua.Func);
        for (i = 0; i < cl->AsLua.NUpValues; i++)  /* mark its upValues */
            markObject(g, cl->AsLua.UpValues[i]);
    }
}


static void checkStackSizes(Lua::State *L, Lua::StkId max) {
    int ci_used = cast_int(L->CallInfo - L->BaseCI);  /* number of `ci' in use */
    int s_used = cast_int(max - L->Stack);  /* part of stack in use */
    if (L->BaseCICount > LUAI_MAXCALLS)  /* handling overflow? */
        return;  /* do not touch the stacks */
    if (4 * ci_used < L->BaseCICount && 2 * Lua::BasicCISize < L->BaseCICount)
        Lua::Do::ReAllocCI(L, L->BaseCICount / 2);  /* still big enough... */
    LuaCondHardStackTests(Lua::Do::ReAllocCI(L, ci_used + 1));
    if (4 * s_used < L->StackCount &&
        2 * (Lua::BasicStackSize + Lua::ExtraStack) < L->StackCount)
        Lua::Do::ReAllocStack(L, L->StackCount / 2);  /* still big enough... */
    LuaCondHardStackTests(Lua::Do::ReAllocStack(L, s_used));
}


static void traverseStack(Lua::GlobalState *g, Lua::State *l) {
    Lua::StkId o, lim;
    Lua::CallInfo *ci;
    markValue(g, LuaGlobalTable(l));
    lim = l->Top;
    for (ci = l->BaseCI; ci <= l->CallInfo; ci++) {
        lua_assert(ci->Top <= l->StackLast);
        if (lim < ci->Top) lim = ci->Top;
    }
    for (o = l->Stack; o < l->Top; o++) markValue(g, o);
    for (; o <= lim; o++)
        LuaSetNilValue(o);
    checkStackSizes(l, lim);
}


/*
** traverse one gray object, turning it to black.
** Returns `quantity` traversed.
*/
static Lua::MemoryDelta propagateMark(Lua::GlobalState *g) {
    Lua::GCObject *o = g->GCGray;
    lua_assert(LuaGCIsGray(o));
    LuaGCGray2Black(o);
    switch (o->AsObject.Type) {
        case LUA_TTABLE: {
            Lua::Table *h = LuaGCObject2Table(o);
            g->GCGray = h->GCList;
            if (traverseTable(g, h))  /* table is weak? */
                black2gray(o);  /* keep it gray */
            return sizeof(Lua::Table) + sizeof(Lua::Value) * h->ArrayCount +
                   sizeof(Lua::Node) * LuaTableNodeCount(h);
        }
        case LUA_TFUNCTION: {
            Lua::Closure *cl = LuaGCObject2Closure(o);
            g->GCGray = cl->AsC.GCList;
            traverseClosure(g, cl);
            return (cl->AsC.IsC) ? LuaCClosureSize(cl->AsC.NUpValues) :
                   LuaLClosureSize(cl->AsLua.NUpValues);
        }
        case LUA_TTHREAD: {
            Lua::State *th = LuaGCObject2Thread(o);
            g->GCGray = th->GCList;
            th->GCList = g->GCGrayAgain;
            g->GCGrayAgain = o;
            black2gray(o);
            traverseStack(g, th);
            return sizeof(Lua::State) + sizeof(Lua::Value) * th->StackCount +
                   sizeof(Lua::CallInfo) * th->BaseCICount;
        }
        case LUA_TPROTO: {
            Lua::Proto *p = LuaGCObject2Proto(o);
            g->GCGray = p->GCList;
            traverseProto(g, p);
            return sizeof(Lua::Proto) + sizeof(Lua::Instruction) * p->CodeCount +
                   sizeof(Lua::Proto *) * p->SubProtoCount +
                   sizeof(Lua::Value) * p->KCount +
                   sizeof(int) * p->LineInfoCount +
                   sizeof(Lua::LocalVar) * p->LocalVarsCount +
                   sizeof(Lua::String *) * p->UpValuesCount;
        }
        default:
            lua_assert(0);
            return 0;
    }
}


static size_t propagateAll(Lua::GlobalState *g) {
    size_t m = 0;
    while (g->GCGray) m += propagateMark(g);
    return m;
}


/*
** The next function tells whether a key or value can be cleared from
** a weak table. Non-collectable objects are never removed from weak
** tables. Strings behave as `values', so are never removed too. for
** other objects: if really collected, cannot keep them; for userdata
** being finalized, keep them in keys, but not in values
*/
static int isCleared(const Lua::Value *o, int isKey) {
    if (!LuaIsCollectable(o)) return 0;
    if (LuaTypeIsString(o)) {
        stringMark(LuaStringValue(o));  /* strings are `values', so are never weak */
        return 0;
    }
    return LuaGCIsWhite(LuaGCValue(o)) ||
           (LuaTypeIsUData(o) && (!isKey && isFinalized(LuaUDataValue(o))));
}


/*
** clear collected entries from weakTables
*/
static void clearTable(Lua::GCObject *l) {
    while (l) {
        Lua::Table *h = LuaGCObject2Table(l);
        int i = h->ArrayCount;
        lua_assert(LuaGCTestBit(h->Marked, Lua::GC::MarkValueWeakBit) ||
                   LuaGCTestBit(h->Marked, Lua::GC::MarkKeyWeakBit));
        if (LuaGCTestBit(h->Marked, Lua::GC::MarkValueWeakBit)) {
            while (i--) {
                Lua::Value *o = &h->Array[i];
                if (isCleared(o, 0))  /* value was collected? */
                    LuaSetNilValue(o);  /* remove value */
            }
        }
        i = LuaTableNodeCount(h);
        while (i--) {
            Lua::Node *n = LuaTableGetNode(h, i);
            if (!LuaTypeIsNil(LuaTableGetValue(n)) &&  /* non-empty entry? */
                (isCleared(LuaTableKey2KeyValue(n), 1) || isCleared(LuaTableGetValue(n), 0))) {
                LuaSetNilValue(LuaTableGetValue(n));  /* remove value ... */
                removeEntry(n);  /* remove entry from table */
            }
        }
        l = h->GCList;
    }
}


static void freeObject(Lua::State *L, Lua::GCObject *o) {
    switch (o->AsObject.Type) {
        case LUA_TPROTO:
            Lua::Proto::Free(L, LuaGCObject2Proto(o));
            break;
        case LUA_TFUNCTION:
            Lua::Closure::Free(L, LuaGCObject2Closure(o));
            break;
        case LUA_TUPVAL:
            Lua::UpValue::Free(L, LuaGCObject2UpValue(o));
            break;
        case LUA_TTABLE:
            Lua::Table::Free(L, LuaGCObject2Table(o));
            break;
        case LUA_TTHREAD: {
            lua_assert(LuaGCObject2Thread(o) != L && LuaGCObject2Thread(o) != LuaGlobal(L)->MainThread);
            Lua::State::FreeThread(L, LuaGCObject2Thread(o));
            break;
        }
        case LUA_TSTRING: {
            LuaGlobal(L)->StringMap.Count--;
            LuaMemoryFreeMemory(L, o, LuaStringSize(LuaGCObject2String(o)));
            break;
        }
        case LUA_TUSERDATA: {
            LuaMemoryFreeMemory(L, o, LuaUserdataSize(LuaGCObject2Userdata(o)));
            break;
        }
        default:
            lua_assert(0);
    }
}


#define sweepWholeList(L, p)    sweepList(L,p,Lua::MaxUMemory)


static Lua::GCObject **sweepList(Lua::State *L, Lua::GCObject **p, Lua::MemorySize count) {
    Lua::GCObject *curr;
    Lua::GlobalState *g = LuaGlobal(L);
    int deadMask = LuaGCOtherWhite(g);
    while ((curr = *p) != nullptr && count-- > 0) {
        if (curr->AsObject.Type == LUA_TTHREAD)  /* sweep open upvalues of each thread */
            sweepWholeList(L, &LuaGCObject2Thread(curr)->OpenedUpValue);
        if ((curr->AsObject.Marked ^ Lua::GC::MarkWhiteBits) & deadMask) {  /* not dead? */
            lua_assert(!LuaGCIsDead(g, curr) || LuaGCTestBit(curr->AsObject.Marked, Lua::GC::MarkFixedBit));
            makeWhite(g, curr);  /* make it white (for next cycle) */
            p = &curr->AsObject.GCNext;
        } else {  /* must erase `curr' */
            lua_assert(LuaGCIsDead(g, curr) || deadMask == LuaGCBitMask(Lua::GC::MarkSFixedBit));
            *p = curr->AsObject.GCNext;
            if (curr == g->GCRoot)  /* is the first element of the list? */
                g->GCRoot = curr->AsObject.GCNext;  /* adjust first */
            freeObject(L, curr);
        }
    }
    return p;
}


static void checkSizes(Lua::State *L) {
    Lua::GlobalState *g = LuaGlobal(L);
    /* check size of string hash */
    if (g->StringMap.Count < cast(Lua::UInt32, g->StringMap.Capacity / 4) &&
        g->StringMap.Capacity > Lua::MinStringTableSize * 2)
        Lua::String::Resize(L, g->StringMap.Capacity / 2);  /* table is too big */
    /* check size of buffer */
    if (LuaZBufferSize(&g->Buff) > Lua::MinBufferSize * 2) {  /* buffer too big? */
        size_t newSize = LuaZBufferSize(&g->Buff) / 2;
        LuaZBufferResize(L, &g->Buff, newSize);
    }
}


static void doGCMetatable(Lua::State *L) {
    Lua::GlobalState *g = LuaGlobal(L);
    Lua::GCObject *o = g->GCTMUData->AsObject.GCNext;  /* get first element */
    Lua::Userdata *uData = LuaGCObject2Userdata(o);
    const Lua::Value *tm;
    /* remove uData from `tmUData' */
    if (o == g->GCTMUData)  /* last element? */
        g->GCTMUData = nullptr;
    else
        g->GCTMUData->AsObject.GCNext = uData->GCNext;
    uData->GCNext = g->MainThread->GCNext;  /* return it to `root' list */
    g->MainThread->GCNext = o;
    makeWhite(g, o);
    tm = LuaTMGetFast(L, uData->Metatable, Lua::TM::NameGC);
    if (tm != nullptr) {
        Lua::Byte oldAH = L->AllowHook;
        Lua::MemorySize oldTh = g->GCThreshold;
        L->AllowHook = 0;  /* stop debug hooks during GC tag method */
        g->GCThreshold = 2 * g->TotalBytes;  /* avoid GC steps */
        LuaSetObject2S(L, L->Top, tm);
        LuaSetUDataValue(L, L->Top + 1, uData);
        L->Top += 2;
        Lua::Do::Call(L, L->Top - 2, 0);
        L->AllowHook = oldAH;  /* restore hooks */
        g->GCThreshold = oldTh;  /* restore threshold */
    }
}


/*
** Call all GC tag methods
*/
void Lua::GC::CallGCTM(Lua::State *L) {
    while (LuaGlobal(L)->GCTMUData)
        doGCMetatable(L);
}


void Lua::GC::FreeAll(Lua::State *L) {
    Lua::GlobalState *g = LuaGlobal(L);
    int i;
    g->CurrentWhite = Lua::GC::MarkWhiteBits | LuaGCBitMask(Lua::GC::MarkSFixedBit); // mask to collect all elements
    sweepWholeList(L, &g->GCRoot);
    for (i = 0; i < g->StringMap.Capacity; i++)  /* free all string lists */
        sweepWholeList(L, &g->StringMap.HashTable[i]);
}


static void markMetatable(Lua::GlobalState *g) {
    int i;
    for (i = 0; i < LUA_NUM_TAGS; i++)
        if (g->Metatable[i]) markObject(g, g->Metatable[i]);
}


/* mark root set */
static void markRoot(Lua::State *L) {
    Lua::GlobalState *g = LuaGlobal(L);
    g->GCGray = nullptr;
    g->GCGrayAgain = nullptr;
    g->GCWeak = nullptr;
    markObject(g, g->MainThread);
    /* make global table be traversed before main stack */
    markValue(g, LuaGlobalTable(g->MainThread));
    markValue(g, LuaRegistry(L));
    markMetatable(g);
    g->GCState = Lua::GC::StatePropagate;
}


static void remarkUpValues(Lua::GlobalState *g) {
    Lua::UpValue *uv;
    for (uv = g->UpValueHead.Next; uv != &g->UpValueHead; uv = uv->Next) {
        lua_assert(uv->Next->Prev == uv && uv->Prev->Next == uv);
        if (LuaGCIsGray(LuaObject2GCObject(uv))) markValue(g, uv->SelfValue);
    }
}


static void atomic(Lua::State *L) {
    Lua::GlobalState *g = LuaGlobal(L);
    size_t uDataSize;  /* total size of userdata to be finalized */
    /* remark occasional upValues of (maybe) dead threads */
    remarkUpValues(g);
    /* traverse objects caught by write barrier and by 'remarkUpValues' */
    propagateAll(g);
    /* remark weak tables */
    g->GCGray = g->GCWeak;
    g->GCWeak = nullptr;
    lua_assert(!LuaGCIsWhite(LuaObject2GCObject(g->MainThread)));
    markObject(g, L);  /* mark running thread */
    markMetatable(g);  /* mark basic metatables (again) */
    propagateAll(g);
    /* remark gray again */
    g->GCGray = g->GCGrayAgain;
    g->GCGrayAgain = nullptr;
    propagateAll(g);
    uDataSize = Lua::GC::SeparateUserdata(L, 0);  /* separate userdata to be finalized */
    markTMUData(g);  /* mark `preserved' userdata */
    uDataSize += propagateAll(g);  /* remark, to propagate `preserveness` */
    clearTable(g->GCWeak);  /* remove collected objects from weak tables */
    /* flip current white */
    g->CurrentWhite = cast_byte(LuaGCOtherWhite(g));
    g->GCStringMap = 0;
    g->GCSweep = &g->GCRoot;
    g->GCState = Lua::GC::StateSweepString;
    g->Estimate = g->TotalBytes - uDataSize;  /* first estimate */
}


static Lua::MemoryDelta singleStep(Lua::State *L) {
    Lua::GlobalState *g = LuaGlobal(L);
    /*lua_checkmemory(L);*/
    switch (g->GCState) {
        case Lua::GC::StatePause: {
            markRoot(L);  /* start a new collection */
            return 0;
        }
        case Lua::GC::StatePropagate: {
            if (g->GCGray)
                return propagateMark(g);
            else {  /* no more `gray' objects */
                atomic(L);  /* finish mark phase */
                return 0;
            }
        }
        case Lua::GC::StateSweepString: {
            Lua::MemorySize old = g->TotalBytes;
            sweepWholeList(L, &g->StringMap.HashTable[g->GCStringMap++]);
            if (g->GCStringMap >= g->StringMap.Capacity)  /* nothing more to sweep? */
                g->GCState = Lua::GC::StateSweep;  /* end sweep-string phase */
            lua_assert(old >= g->TotalBytes);
            g->Estimate -= old - g->TotalBytes;
            return LUA_GC_SWEEP_COST;
        }
        case Lua::GC::StateSweep: {
            Lua::MemorySize old = g->TotalBytes;
            g->GCSweep = sweepList(L, g->GCSweep, LUA_GC_SWEEP_MAX);
            if (*g->GCSweep == nullptr) {  /* nothing more to sweep? */
                checkSizes(L);
                g->GCState = Lua::GC::StateFinalize;  /* end sweep phase */
            }
            lua_assert(old >= g->TotalBytes);
            g->Estimate -= old - g->TotalBytes;
            return LUA_GC_SWEEP_MAX * LUA_GC_SWEEP_COST;
        }
        case Lua::GC::StateFinalize: {
            if (g->GCTMUData) {
                doGCMetatable(L);
                if (g->Estimate > LUA_GC_FINALIZE_COST)
                    g->Estimate -= LUA_GC_FINALIZE_COST;
                return LUA_GC_FINALIZE_COST;
            } else {
                g->GCState = Lua::GC::StatePause;  /* end collection */
                g->GCDept = 0;
                return 0;
            }
        }
        default:
            lua_assert(0);
            return 0;
    }
}


void Lua::GC::Step(Lua::State *L) {
    Lua::GlobalState *g = LuaGlobal(L);
    Lua::MemoryDelta lim = (LUA_GC_STEP_SIZE / 100) * g->GCStepMul;
    if (lim == 0)
        lim = (Lua::MaxUMemory - 1) / 2;  /* no limit */
    g->GCDept += g->TotalBytes - g->GCThreshold;
    do {
        lim -= singleStep(L);
        if (g->GCState == Lua::GC::StatePause)
            break;
    } while (lim > 0);
    if (g->GCState != Lua::GC::StatePause) {
        if (g->GCDept < LUA_GC_STEP_SIZE)
            g->GCThreshold = g->TotalBytes + LUA_GC_STEP_SIZE;  /* - lim/g->GCStepMul;*/
        else {
            g->GCDept -= LUA_GC_STEP_SIZE;
            g->GCThreshold = g->TotalBytes;
        }
    } else {
        setThreshold(g);
    }
}


void Lua::GC::FullGC(Lua::State *L) {
    Lua::GlobalState *g = LuaGlobal(L);
    if (g->GCState <= Lua::GC::StatePropagate) {
        /* reset sweep marks to sweep all elements (returning them to white) */
        g->GCStringMap = 0;
        g->GCSweep = &g->GCRoot;
        /* reset other collector lists */
        g->GCGray = nullptr;
        g->GCGrayAgain = nullptr;
        g->GCWeak = nullptr;
        g->GCState = Lua::GC::StateSweepString;
    }
    lua_assert(g->GCState != Lua::GC::StatePause && g->GCState != Lua::GC::StatePropagate);
    /* finish any pending sweep phase */
    while (g->GCState != Lua::GC::StateFinalize) {
        lua_assert(g->GCState == Lua::GC::StateSweepString || g->GCState == Lua::GC::StateSweep);
        singleStep(L);
    }
    markRoot(L);
    while (g->GCState != Lua::GC::StatePause) {
        singleStep(L);
    }
    setThreshold(g);
}


void Lua::GC::BarrierF(Lua::State *L, Lua::GCObject *o, Lua::GCObject *v) {
    Lua::GlobalState *g = LuaGlobal(L);
    lua_assert(LuaGCIsBlack(o) && LuaGCIsWhite(v) && !LuaGCIsDead(g, v) && !LuaGCIsDead(g, o));
    lua_assert(g->GCState != Lua::GC::StateFinalize && g->GCState != Lua::GC::StatePause);
    lua_assert(LuaTypeOf(&o->AsObject) != LUA_TTABLE);
    /* must keep invariant? */
    if (g->GCState == Lua::GC::StatePropagate)
        reallyMarkObject(g, v);  /* restore invariant */
    else  /* don't mind */
        makeWhite(g, o);  /* mark as white just to avoid other barriers */
}


void Lua::GC::BarrierBack(Lua::State *L, Lua::Table *t) {
    Lua::GlobalState *g = LuaGlobal(L);
    Lua::GCObject *o = LuaObject2GCObject(t);
    lua_assert(LuaGCIsBlack(o) && !LuaGCIsDead(g, o));
    lua_assert(g->GCState != Lua::GC::StateFinalize && g->GCState != Lua::GC::StatePause);
    black2gray(o);  /* make table gray (again) */
    t->GCList = g->GCGrayAgain;
    g->GCGrayAgain = o;
}


void Lua::GC::Link(Lua::State *L, Lua::GCObject *o, Lua::Byte Type) {
    Lua::GlobalState *g = LuaGlobal(L);
    o->AsObject.GCNext = g->GCRoot;
    g->GCRoot = o;
    o->AsObject.Marked = LuaGCWhite(g);
    o->AsObject.Type = Type;
}


void Lua::GC::LinkUpValue(Lua::State *L, Lua::UpValue *uv) {
    Lua::GlobalState *g = LuaGlobal(L);
    Lua::GCObject *o = LuaObject2GCObject(uv);
    o->AsObject.GCNext = g->GCRoot;  /* link upValue into `GCRoot` list */
    g->GCRoot = o;
    if (LuaGCIsGray(o)) {
        if (g->GCState == Lua::GC::StatePropagate) {
            LuaGCGray2Black(o);  /* closed upValues need barrier */
            LuaGCBarrier(L, uv, uv->SelfValue);
        } else {  /* sweep phase: sweep it (turning it into white) */
            makeWhite(g, o);
            lua_assert(g->GCState != Lua::GC::StateFinalize && g->GCState != Lua::GC::StatePause);
        }
    }
}

