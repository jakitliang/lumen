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

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/gc.h"
#include "lumen/memory.h"
#include "lumen/object.h"
#include "lumen/state.h"
#include "lumen/string.h"
#include "lumen/table.h"
#include "lumen/tm.h"


#define LUA_GC_STEP_SIZE    1024u
#define LUA_GC_SWEEP_MAX    40
#define LUA_GC_SWEEP_COST    10
#define LUA_GC_FINALIZE_COST    100


#define maskMarks    cast_byte(~(LumenGCBitMask(Lumen::GC::MarkBlackBit)|Lumen::GC::MarkWhiteBits))

#define makeWhite(g, x) \
    ((x)->AsObject.Marked = cast_byte(((x)->AsObject.Marked & maskMarks) | LumenGCWhite(g)))

#define white2gray(x)    LumenGCReset2Bits((x)->AsObject.Marked, Lumen::GC::MarkWhite0Bit, Lumen::GC::MarkWhite1Bit)
#define black2gray(x)    LumenGCResetBit((x)->AsObject.Marked, Lumen::GC::MarkBlackBit)

#define stringMark(s)    LumenGCReset2Bits((s)->Marked, Lumen::GC::MarkWhite0Bit, Lumen::GC::MarkWhite1Bit)


#define isFinalized(u)      LumenGCTestBit((u)->Marked, Lumen::GC::MarkFinalizedBit)
#define markFinalized(u)    LumenGCLSetBit((u)->Marked, Lumen::GC::MarkFinalizedBit)

namespace Lumen::GC {
    static inline constexpr Lumen::Byte WeakKey = LumenGCBitMask(Lumen::GC::MarkKeyWeakBit);
    static inline constexpr Lumen::Byte WeakValue = LumenGCBitMask(Lumen::GC::MarkValueWeakBit);
}

#define markValue(g, o) \
LumenDo(                  \
    LumenCheckConsistency(o); \
    if (LumenIsCollectable(o) && LumenGCIsWhite(LumenGCValue(o))) \
        reallyMarkObject(g, LumenGCValue(o));                  \
)

#define markObject(g, t) \
LumenDo(                   \
    if (LumenGCIsWhite(LumenObject2GCObject(t))) \
        reallyMarkObject(g, LumenObject2GCObject(t)); \
)


#define setThreshold(g)  (g->GCThreshold = (g->Estimate/100) * g->GCPause)


static void removeEntry(Lumen::Node *n) {
    LumenAssert(LumenTypeIsNil(LumenTableGetValue(n)));
    if (LumenIsCollectable(LumenTableGetKey(n)))
        LumenSetType(LumenTableGetKey(n), Lumen::TypeDeadKey);  /* dead key; remove it */
}


static void reallyMarkObject(Lumen::GlobalState *g, Lumen::GCObject *o) {
    LumenAssert(LumenGCIsWhite(o) && !LumenGCIsDead(g, o));
    white2gray(o);
    switch (o->AsObject.Type) {
        case Lumen::TypeString: {
            return;
        }
        case Lumen::TypeUserdata: {
            Lumen::Table *mt = LumenGCObject2Userdata(o)->Metatable;
            LumenGCGray2Black(o);  /* uData are never gray */
            if (mt) markObject(g, mt);
            markObject(g, LumenGCObject2Userdata(o)->Env);
            return;
        }
        case Lumen::TypeUpValue: {
            Lumen::UpValue *uv = LumenGCObject2UpValue(o);
            markValue(g, uv->SelfValue);
            if (uv->SelfValue == &uv->Value)  /* closed? */
                LumenGCGray2Black(o);  /* open upValues are never black */
            return;
        }
        case Lumen::TypeFunction: {
            LumenGCObject2Closure(o)->AsC.GCList = g->GCGray;
            g->GCGray = o;
            break;
        }
        case Lumen::TypeTable: {
            LumenGCObject2Table(o)->GCList = g->GCGray;
            g->GCGray = o;
            break;
        }
        case Lumen::TypeThread: {
            LumenGCObject2Thread(o)->GCList = g->GCGray;
            g->GCGray = o;
            break;
        }
        case Lumen::TypeProto: {
            LumenGCObject2Proto(o)->GCList = g->GCGray;
            g->GCGray = o;
            break;
        }
        default:
            LumenAssert(0);
    }
}


static void markTMUData(Lumen::GlobalState *g) {
    Lumen::GCObject *u = g->GCTMUData;
    if (u) {
        do {
            u = u->AsObject.GCNext;
            makeWhite(g, u);  /* may be marked, if left from previous GC */
            reallyMarkObject(g, u);
        } while (u != g->GCTMUData);
    }
}


/* move `dead` UData that need finalization to list `TMUData` */
Lumen::UInteger Lumen::GC::SeparateUserdata(Lumen::State *L, int all) {
    Lumen::GlobalState *g = LumenGlobal(L);
    Lumen::UInteger deadMem = 0;
    Lumen::GCObject **p = &g->MainThread->GCNext;
    Lumen::GCObject *curr;
    while ((curr = *p) != nullptr) {
        if (!(LumenGCIsWhite(curr) || all) || isFinalized(LumenGCObject2Userdata(curr)))
            p = &curr->AsObject.GCNext;  /* don't bother with them */
        else if (LumenTMGetFast(L, LumenGCObject2Userdata(curr)->Metatable, Lumen::TM::NameGC) == nullptr) {
            markFinalized(LumenGCObject2Userdata(curr));  /* don't need finalization */
            p = &curr->AsObject.GCNext;
        } else {  /* must call its gc method */
            deadMem += LumenUserdataSize(LumenGCObject2Userdata(curr));
            markFinalized(LumenGCObject2Userdata(curr));
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


static int traverseTable(Lumen::GlobalState *g, Lumen::Table *h) {
    int i;
    int weakKey = 0;
    int weakValue = 0;
    const Lumen::Object *mode;
    if (h->Metatable) markObject(g, h->Metatable);
    mode = LumenTMGetGlobalFast(g, h->Metatable, Lumen::TM::NameMode);
    if (mode && LumenTypeIsString(mode)) {  /* is there a weak mode? */
        weakKey = (strchr(LumenStringValue2CString(mode), 'k') != nullptr);
        weakValue = (strchr(LumenStringValue2CString(mode), 'v') != nullptr);
        if (weakKey || weakValue) {  /* is really weak? */
            h->Marked &= ~(Lumen::GC::WeakKey | Lumen::GC::WeakValue);  /* clear bits */
            h->Marked |= cast_byte((weakKey << Lumen::GC::MarkKeyWeakBit) |
                                   (weakValue << Lumen::GC::MarkValueWeakBit));
            h->GCList = g->GCWeak;  /* must be cleared after GC, ... */
            g->GCWeak = LumenObject2GCObject(h);  /* ... so put in the appropriate list */
        }
    }
    if (weakKey && weakValue) return 1;
    if (!weakValue) {
        i = h->ArrayCount;
        while (i--) markValue(g, &h->Array[i]);
    }
    i = LumenTableNodeCount(h);
    while (i--) {
        Lumen::Node *n = LumenTableGetNode(h, i);
        LumenAssert(LumenTypeOf(LumenTableGetKey(n)) != Lumen::TypeDeadKey || LumenTypeIsNil(LumenTableGetValue(n)));
        if (LumenTypeIsNil(LumenTableGetValue(n)))
            removeEntry(n);  /* remove empty entries */
        else {
            LumenAssert(!LumenTypeIsNil(LumenTableGetKey(n)));
            if (!weakKey) markValue(g, LumenTableGetKey(n));
            if (!weakValue) markValue(g, LumenTableGetValue(n));
        }
    }
    return weakKey || weakValue;
}


/*
** All marks are conditional because a GC may happen while the
** prototype is still being created
*/
static void traverseProto(Lumen::GlobalState *g, Lumen::Proto *f) {
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


static void traverseClosure(Lumen::GlobalState *g, Lumen::Closure *cl) {
    markObject(g, cl->AsC.Env);
    if (cl->AsC.IsC) {
        int i;
        for (i = 0; i < cl->AsC.NUpValues; i++)  /* mark its upValues */
            markValue(g, &cl->AsC.UpValues[i]);
    } else {
        int i;
        LumenAssert(cl->AsLua.NUpValues == cl->AsLua.Func->NUpValues);
        markObject(g, cl->AsLua.Func);
        for (i = 0; i < cl->AsLua.NUpValues; i++)  /* mark its upValues */
            markObject(g, cl->AsLua.UpValues[i]);
    }
}


static void checkStackSizes(Lumen::State *L, Lumen::Value max) {
    int ci_used = cast_int(L->CallInfo - L->BaseCI);  /* number of `ci' in use */
    int s_used = cast_int(max - L->Stack);  /* part of stack in use */
    if (L->BaseCICount > LUAI_MAXCALLS)  /* handling overflow? */
        return;  /* do not touch the stacks */
    if (4 * ci_used < L->BaseCICount && 2 * Lumen::BasicCISize < L->BaseCICount)
        Lumen::Do::ReAllocCI(L, L->BaseCICount / 2);  /* still big enough... */
    LumenCondHardStackTests(Lumen::Do::ReAllocCI(L, ci_used + 1));
    if (4 * s_used < L->StackCount &&
        2 * (Lumen::BasicStackSize + Lumen::ExtraStack) < L->StackCount)
        Lumen::Do::ReAllocStack(L, L->StackCount / 2);  /* still big enough... */
    LumenCondHardStackTests(Lumen::Do::ReAllocStack(L, s_used));
}


static void traverseStack(Lumen::GlobalState *g, Lumen::State *l) {
    Lumen::Value o, lim;
    Lumen::CallInfo *ci;
    markValue(g, LumenGlobalTable(l));
    lim = l->Top;
    for (ci = l->BaseCI; ci <= l->CallInfo; ci++) {
        LumenAssert(ci->Top <= l->StackLast);
        if (lim < ci->Top) lim = ci->Top;
    }
    for (o = l->Stack; o < l->Top; o++) markValue(g, o);
    for (; o <= lim; o++)
        LumenSetNilValue(o);
    checkStackSizes(l, lim);
}


/*
** traverse one gray object, turning it to black.
** Returns `quantity` traversed.
*/
static Lumen::MemoryDelta propagateMark(Lumen::GlobalState *g) {
    Lumen::GCObject *o = g->GCGray;
    LumenAssert(LumenGCIsGray(o));
    LumenGCGray2Black(o);
    switch (o->AsObject.Type) {
        case Lumen::TypeTable: {
            Lumen::Table *h = LumenGCObject2Table(o);
            g->GCGray = h->GCList;
            if (traverseTable(g, h))  /* table is weak? */
                black2gray(o);  /* keep it gray */
            return sizeof(Lumen::Table) + sizeof(Lumen::Object) * h->ArrayCount +
                   sizeof(Lumen::Node) * LumenTableNodeCount(h);
        }
        case Lumen::TypeFunction: {
            Lumen::Closure *cl = LumenGCObject2Closure(o);
            g->GCGray = cl->AsC.GCList;
            traverseClosure(g, cl);
            return (cl->AsC.IsC) ? LumenCClosureSize(cl->AsC.NUpValues) :
                   LumenLClosureSize(cl->AsLua.NUpValues);
        }
        case Lumen::TypeThread: {
            Lumen::State *th = LumenGCObject2Thread(o);
            g->GCGray = th->GCList;
            th->GCList = g->GCGrayAgain;
            g->GCGrayAgain = o;
            black2gray(o);
            traverseStack(g, th);
            return sizeof(Lumen::State) + sizeof(Lumen::Object) * th->StackCount +
                   sizeof(Lumen::CallInfo) * th->BaseCICount;
        }
        case Lumen::TypeProto: {
            Lumen::Proto *p = LumenGCObject2Proto(o);
            g->GCGray = p->GCList;
            traverseProto(g, p);
            return sizeof(Lumen::Proto) + sizeof(Lumen::Instruction) * p->CodeCount +
                   sizeof(Lumen::Proto *) * p->SubProtoCount +
                   sizeof(Lumen::Object) * p->KCount +
                   sizeof(int) * p->LineInfoCount +
                   sizeof(Lumen::LocalVar) * p->LocalVarsCount +
                   sizeof(Lumen::String *) * p->UpValuesCount;
        }
        default:
            LumenAssert(0);
            return 0;
    }
}


static Lumen::UInteger propagateAll(Lumen::GlobalState *g) {
    Lumen::UInteger m = 0;
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
static int isCleared(const Lumen::Object *o, int isKey) {
    if (!LumenIsCollectable(o)) return 0;
    if (LumenTypeIsString(o)) {
        stringMark(LumenStringValue(o));  /* strings are `values', so are never weak */
        return 0;
    }
    return LumenGCIsWhite(LumenGCValue(o)) ||
           (LumenTypeIsUData(o) && (!isKey && isFinalized(LumenUDataValue(o))));
}


/*
** clear collected entries from weakTables
*/
static void clearTable(Lumen::GCObject *l) {
    while (l) {
        Lumen::Table *h = LumenGCObject2Table(l);
        int i = h->ArrayCount;
        LumenAssert(LumenGCTestBit(h->Marked, Lumen::GC::MarkValueWeakBit) ||
                   LumenGCTestBit(h->Marked, Lumen::GC::MarkKeyWeakBit));
        if (LumenGCTestBit(h->Marked, Lumen::GC::MarkValueWeakBit)) {
            while (i--) {
                Lumen::Object *o = &h->Array[i];
                if (isCleared(o, 0))  /* value was collected? */
                    LumenSetNilValue(o);  /* remove value */
            }
        }
        i = LumenTableNodeCount(h);
        while (i--) {
            Lumen::Node *n = LumenTableGetNode(h, i);
            if (!LumenTypeIsNil(LumenTableGetValue(n)) &&  /* non-empty entry? */
                (isCleared(LumenTableKey2KeyValue(n), 1) || isCleared(LumenTableGetValue(n), 0))) {
                LumenSetNilValue(LumenTableGetValue(n));  /* remove value ... */
                removeEntry(n);  /* remove entry from table */
            }
        }
        l = h->GCList;
    }
}


static void freeObject(Lumen::State *L, Lumen::GCObject *o) {
    switch (o->AsObject.Type) {
        case Lumen::TypeProto:
            Lumen::Proto::Free(L, LumenGCObject2Proto(o));
            break;
        case Lumen::TypeFunction:
            Lumen::Closure::Free(L, LumenGCObject2Closure(o));
            break;
        case Lumen::TypeUpValue:
            Lumen::UpValue::Free(L, LumenGCObject2UpValue(o));
            break;
        case Lumen::TypeTable:
            Lumen::Table::Free(L, LumenGCObject2Table(o));
            break;
        case Lumen::TypeThread: {
            LumenAssert(LumenGCObject2Thread(o) != L && LumenGCObject2Thread(o) != LumenGlobal(L)->MainThread);
            Lumen::State::FreeThread(L, LumenGCObject2Thread(o));
            break;
        }
        case Lumen::TypeString: {
            LumenGlobal(L)->StringMap.Count--;
            LumenMemoryFreeMemory(L, o, LumenStringSize(LumenGCObject2String(o)));
            break;
        }
        case Lumen::TypeUserdata: {
            LumenMemoryFreeMemory(L, o, LumenUserdataSize(LumenGCObject2Userdata(o)));
            break;
        }
        default:
            LumenAssert(0);
    }
}


#define sweepWholeList(L, p)    sweepList(L,p,Lumen::MaxUMemory)


static Lumen::GCObject **sweepList(Lumen::State *L, Lumen::GCObject **p, Lumen::MemorySize count) {
    Lumen::GCObject *curr;
    Lumen::GlobalState *g = LumenGlobal(L);
    int deadMask = LumenGCOtherWhite(g);
    while ((curr = *p) != nullptr && count-- > 0) {
        if (curr->AsObject.Type == Lumen::TypeThread)  /* sweep open upvalues of each thread */
            sweepWholeList(L, &LumenGCObject2Thread(curr)->OpenedUpValue);
        if ((curr->AsObject.Marked ^ Lumen::GC::MarkWhiteBits) & deadMask) {  /* not dead? */
            LumenAssert(!LumenGCIsDead(g, curr) || LumenGCTestBit(curr->AsObject.Marked, Lumen::GC::MarkFixedBit));
            makeWhite(g, curr);  /* make it white (for next cycle) */
            p = &curr->AsObject.GCNext;
        } else {  /* must erase `curr' */
            LumenAssert(LumenGCIsDead(g, curr) || deadMask == LumenGCBitMask(Lumen::GC::MarkSFixedBit));
            *p = curr->AsObject.GCNext;
            if (curr == g->GCRoot)  /* is the first element of the list? */
                g->GCRoot = curr->AsObject.GCNext;  /* adjust first */
            freeObject(L, curr);
        }
    }
    return p;
}


static void checkSizes(Lumen::State *L) {
    Lumen::GlobalState *g = LumenGlobal(L);
    /* check size of string hash */
    if (g->StringMap.Count < cast(Lumen::UInt32, g->StringMap.Capacity / 4) &&
        g->StringMap.Capacity > Lumen::MinStringTableSize * 2)
        Lumen::String::Resize(L, g->StringMap.Capacity / 2);  /* table is too big */
    /* check size of buffer */
    if (LumenZBufferSize(&g->Buff) > Lumen::MinBufferSize * 2) {  /* buffer too big? */
        Lumen::UInteger newSize = LumenZBufferSize(&g->Buff) / 2;
        LumenZBufferResize(L, &g->Buff, newSize);
    }
}


static void doGCMetatable(Lumen::State *L) {
    Lumen::GlobalState *g = LumenGlobal(L);
    Lumen::GCObject *o = g->GCTMUData->AsObject.GCNext;  /* get first element */
    Lumen::Userdata *uData = LumenGCObject2Userdata(o);
    const Lumen::Object *tm;
    /* remove uData from `tmUData' */
    if (o == g->GCTMUData)  /* last element? */
        g->GCTMUData = nullptr;
    else
        g->GCTMUData->AsObject.GCNext = uData->GCNext;
    uData->GCNext = g->MainThread->GCNext;  /* return it to `root' list */
    g->MainThread->GCNext = o;
    makeWhite(g, o);
    tm = LumenTMGetFast(L, uData->Metatable, Lumen::TM::NameGC);
    if (tm != nullptr) {
        Lumen::Byte oldAH = L->AllowHook;
        Lumen::MemorySize oldTh = g->GCThreshold;
        L->AllowHook = 0;  /* stop debug hooks during GC tag method */
        g->GCThreshold = 2 * g->TotalBytes;  /* avoid GC steps */
        LumenSetObject2S(L, L->Top, tm);
        LumenSetUDataValue(L, L->Top + 1, uData);
        L->Top += 2;
        Lumen::Do::Call(L, L->Top - 2, 0);
        L->AllowHook = oldAH;  /* restore hooks */
        g->GCThreshold = oldTh;  /* restore threshold */
    }
}


/*
** Call all GC tag methods
*/
void Lumen::GC::CallGCTM(Lumen::State *L) {
    while (LumenGlobal(L)->GCTMUData)
        doGCMetatable(L);
}


void Lumen::GC::FreeAll(Lumen::State *L) {
    Lumen::GlobalState *g = LumenGlobal(L);
    int i;
    g->CurrentWhite = Lumen::GC::MarkWhiteBits | LumenGCBitMask(Lumen::GC::MarkSFixedBit); // mask to collect all elements
    sweepWholeList(L, &g->GCRoot);
    for (i = 0; i < g->StringMap.Capacity; i++)  /* free all string lists */
        sweepWholeList(L, &g->StringMap.HashTable[i]);
}


static void markMetatable(Lumen::GlobalState *g) {
    int i;
    for (i = 0; i < Lumen::TypeCount; i++)
        if (g->Metatable[i]) markObject(g, g->Metatable[i]);
}


/* mark root set */
static void markRoot(Lumen::State *L) {
    Lumen::GlobalState *g = LumenGlobal(L);
    g->GCGray = nullptr;
    g->GCGrayAgain = nullptr;
    g->GCWeak = nullptr;
    markObject(g, g->MainThread);
    /* make global table be traversed before main stack */
    markValue(g, LumenGlobalTable(g->MainThread));
    markValue(g, LumenRegistry(L));
    markMetatable(g);
    g->GCState = Lumen::GC::StatePropagate;
}


static void remarkUpValues(Lumen::GlobalState *g) {
    Lumen::UpValue *uv;
    for (uv = g->UpValueHead.Next; uv != &g->UpValueHead; uv = uv->Next) {
        LumenAssert(uv->Next->Prev == uv && uv->Prev->Next == uv);
        if (LumenGCIsGray(LumenObject2GCObject(uv))) markValue(g, uv->SelfValue);
    }
}


static void atomic(Lumen::State *L) {
    Lumen::GlobalState *g = LumenGlobal(L);
    Lumen::UInteger uDataSize;  /* total size of userdata to be finalized */
    /* remark occasional upValues of (maybe) dead threads */
    remarkUpValues(g);
    /* traverse objects caught by write barrier and by 'remarkUpValues' */
    propagateAll(g);
    /* remark weak tables */
    g->GCGray = g->GCWeak;
    g->GCWeak = nullptr;
    LumenAssert(!LumenGCIsWhite(LumenObject2GCObject(g->MainThread)));
    markObject(g, L);  /* mark running thread */
    markMetatable(g);  /* mark basic metatables (again) */
    propagateAll(g);
    /* remark gray again */
    g->GCGray = g->GCGrayAgain;
    g->GCGrayAgain = nullptr;
    propagateAll(g);
    uDataSize = Lumen::GC::SeparateUserdata(L, 0);  /* separate userdata to be finalized */
    markTMUData(g);  /* mark `preserved' userdata */
    uDataSize += propagateAll(g);  /* remark, to propagate `preserveness` */
    clearTable(g->GCWeak);  /* remove collected objects from weak tables */
    /* flip current white */
    g->CurrentWhite = cast_byte(LumenGCOtherWhite(g));
    g->GCStringMap = 0;
    g->GCSweep = &g->GCRoot;
    g->GCState = Lumen::GC::StateSweepString;
    g->Estimate = g->TotalBytes - uDataSize;  /* first estimate */
}


static Lumen::MemoryDelta singleStep(Lumen::State *L) {
    Lumen::GlobalState *g = LumenGlobal(L);
    /*lua_checkmemory(L);*/
    switch (g->GCState) {
        case Lumen::GC::StatePause: {
            markRoot(L);  /* start a new collection */
            return 0;
        }
        case Lumen::GC::StatePropagate: {
            if (g->GCGray)
                return propagateMark(g);
            else {  /* no more `gray' objects */
                atomic(L);  /* finish mark phase */
                return 0;
            }
        }
        case Lumen::GC::StateSweepString: {
            Lumen::MemorySize old = g->TotalBytes;
            sweepWholeList(L, &g->StringMap.HashTable[g->GCStringMap++]);
            if (g->GCStringMap >= g->StringMap.Capacity)  /* nothing more to sweep? */
                g->GCState = Lumen::GC::StateSweep;  /* end sweep-string phase */
            LumenAssert(old >= g->TotalBytes);
            g->Estimate -= old - g->TotalBytes;
            return LUA_GC_SWEEP_COST;
        }
        case Lumen::GC::StateSweep: {
            Lumen::MemorySize old = g->TotalBytes;
            g->GCSweep = sweepList(L, g->GCSweep, LUA_GC_SWEEP_MAX);
            if (*g->GCSweep == nullptr) {  /* nothing more to sweep? */
                checkSizes(L);
                g->GCState = Lumen::GC::StateFinalize;  /* end sweep phase */
            }
            LumenAssert(old >= g->TotalBytes);
            g->Estimate -= old - g->TotalBytes;
            return LUA_GC_SWEEP_MAX * LUA_GC_SWEEP_COST;
        }
        case Lumen::GC::StateFinalize: {
            if (g->GCTMUData) {
                doGCMetatable(L);
                if (g->Estimate > LUA_GC_FINALIZE_COST)
                    g->Estimate -= LUA_GC_FINALIZE_COST;
                return LUA_GC_FINALIZE_COST;
            } else {
                g->GCState = Lumen::GC::StatePause;  /* end collection */
                g->GCDept = 0;
                return 0;
            }
        }
        default:
            LumenAssert(0);
            return 0;
    }
}


void Lumen::GC::Step(Lumen::State *L) {
    Lumen::GlobalState *g = LumenGlobal(L);
    Lumen::MemoryDelta lim = (LUA_GC_STEP_SIZE / 100) * g->GCStepMul;
    if (lim == 0)
        lim = (Lumen::MaxUMemory - 1) / 2;  /* no limit */
    g->GCDept += g->TotalBytes - g->GCThreshold;
    do {
        lim -= singleStep(L);
        if (g->GCState == Lumen::GC::StatePause)
            break;
    } while (lim > 0);
    if (g->GCState != Lumen::GC::StatePause) {
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


void Lumen::GC::FullGC(Lumen::State *L) {
    Lumen::GlobalState *g = LumenGlobal(L);
    if (g->GCState <= Lumen::GC::StatePropagate) {
        /* reset sweep marks to sweep all elements (returning them to white) */
        g->GCStringMap = 0;
        g->GCSweep = &g->GCRoot;
        /* reset other collector lists */
        g->GCGray = nullptr;
        g->GCGrayAgain = nullptr;
        g->GCWeak = nullptr;
        g->GCState = Lumen::GC::StateSweepString;
    }
    LumenAssert(g->GCState != Lumen::GC::StatePause && g->GCState != Lumen::GC::StatePropagate);
    /* finish any pending sweep phase */
    while (g->GCState != Lumen::GC::StateFinalize) {
        LumenAssert(g->GCState == Lumen::GC::StateSweepString || g->GCState == Lumen::GC::StateSweep);
        singleStep(L);
    }
    markRoot(L);
    while (g->GCState != Lumen::GC::StatePause) {
        singleStep(L);
    }
    setThreshold(g);
}


void Lumen::GC::BarrierF(Lumen::State *L, Lumen::GCObject *o, Lumen::GCObject *v) {
    Lumen::GlobalState *g = LumenGlobal(L);
    LumenAssert(LumenGCIsBlack(o) && LumenGCIsWhite(v) && !LumenGCIsDead(g, v) && !LumenGCIsDead(g, o));
    LumenAssert(g->GCState != Lumen::GC::StateFinalize && g->GCState != Lumen::GC::StatePause);
    LumenAssert(LumenTypeOf(&o->AsObject) != Lumen::TypeTable);
    /* must keep invariant? */
    if (g->GCState == Lumen::GC::StatePropagate)
        reallyMarkObject(g, v);  /* restore invariant */
    else  /* don't mind */
        makeWhite(g, o);  /* mark as white just to avoid other barriers */
}


void Lumen::GC::BarrierBack(Lumen::State *L, Lumen::Table *t) {
    Lumen::GlobalState *g = LumenGlobal(L);
    Lumen::GCObject *o = LumenObject2GCObject(t);
    LumenAssert(LumenGCIsBlack(o) && !LumenGCIsDead(g, o));
    LumenAssert(g->GCState != Lumen::GC::StateFinalize && g->GCState != Lumen::GC::StatePause);
    black2gray(o);  /* make table gray (again) */
    t->GCList = g->GCGrayAgain;
    g->GCGrayAgain = o;
}


void Lumen::GC::Link(Lumen::State *L, Lumen::GCObject *o, Lumen::Byte Type) {
    Lumen::GlobalState *g = LumenGlobal(L);
    o->AsObject.GCNext = g->GCRoot;
    g->GCRoot = o;
    o->AsObject.Marked = LumenGCWhite(g);
    o->AsObject.Type = Type;
}


void Lumen::GC::LinkUpValue(Lumen::State *L, Lumen::UpValue *uv) {
    Lumen::GlobalState *g = LumenGlobal(L);
    Lumen::GCObject *o = LumenObject2GCObject(uv);
    o->AsObject.GCNext = g->GCRoot;  /* link upValue into `GCRoot` list */
    g->GCRoot = o;
    if (LumenGCIsGray(o)) {
        if (g->GCState == Lumen::GC::StatePropagate) {
            LumenGCGray2Black(o);  /* closed upValues need barrier */
            LumenGCBarrier(L, uv, uv->SelfValue);
        } else {  /* sweep phase: sweep it (turning it into white) */
            makeWhite(g, o);
            LumenAssert(g->GCState != Lumen::GC::StateFinalize && g->GCState != Lumen::GC::StatePause);
        }
    }
}

