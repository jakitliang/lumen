/*!
 * @brief Garbage Collector
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_GC_H
#define LUMEN_GC_H

#include "lumen/object.h"

/*
** some useful bit tricks
*/
#define LumenGCResetBits(x, m)          ((x) &= cast(Lumen::Byte, ~(m)))
#define LumenGCSetBits(x, m)            ((x) |= (m))
#define LumenGCTestBits(x, m)           ((x) & (m))
#define LumenGCBitMask(b)               (1 << (b))
#define LumenGCBit2Mask(b1, b2)         (LumenGCBitMask(b1) | LumenGCBitMask(b2))
#define LumenGCLSetBit(x, b)            LumenGCSetBits(x, LumenGCBitMask(b))
#define LumenGCResetBit(x, b)           LumenGCResetBits(x, LumenGCBitMask(b))
#define LumenGCTestBit(x, b)            LumenGCTestBits(x, LumenGCBitMask(b))
#define LumenGCSet2Bits(x, b1, b2)      LumenGCSetBits(x, (LumenGCBit2Mask(b1, b2)))
#define LumenGCReset2Bits(x, b1, b2)    LumenGCResetBits(x, (LumenGCBit2Mask(b1, b2)))
#define LumenGCTest2Bits(x, b1, b2)     LumenGCTestBits(x, (LumenGCBit2Mask(b1, b2)))

namespace Lumen::GC {
    /**
     * Possible states of the Garbage Collector
     */
    typedef int State;
    enum {
        StatePause = 0,
        StatePropagate = 1,
        StateSweepString = 2,
        StateSweep = 3,
        StateFinalize = 4
    };

    /**
     * ORDER Mark
     */
    enum {
        MarkWhite0Bit = 0,
        MarkWhite1Bit = 1,
        MarkBlackBit = 2,
        MarkFinalizedBit = 3,
        MarkKeyWeakBit = 3,
        MarkValueWeakBit = 4,
        MarkFixedBit = 5,
        MarkSFixedBit = 6,
        MarkWhiteBits = LumenGCBit2Mask(Lumen::GC::MarkWhite0Bit, Lumen::GC::MarkWhite1Bit)
    };

    Lumen::UInteger SeparateUserdata(Lumen::State *L, int all);

    void CallGCTM(Lumen::State *L);

    void FreeAll(Lumen::State *L);

    void Step(Lumen::State *L);

    void FullGC(Lumen::State *L);

    void Link(Lumen::State *L, Lumen::GCObject *o, Lumen::Byte Type);

    void LinkUpValue(Lumen::State *L, Lumen::UpValue *uv);

    void BarrierF(Lumen::State *L, Lumen::GCObject *o, Lumen::GCObject *v);

    void BarrierBack(Lumen::State *L, Lumen::Table *t);
}

#define LumenGCIsWhite(x)        \
    LumenGCTest2Bits((x)->AsObject.Marked, Lumen::GC::MarkWhite0Bit, Lumen::GC::MarkWhite1Bit)
#define LumenGCIsBlack(x)        LumenGCTestBit((x)->AsObject.Marked, Lumen::GC::MarkBlackBit)
#define LumenGCIsGray(x)         (!LumenGCIsBlack(x) && !LumenGCIsWhite(x))
#define LumenGCOtherWhite(g)     (g->CurrentWhite ^ Lumen::GC::MarkWhiteBits)
#define LumenGCIsDead(g, v)      ((v)->AsObject.Marked & LumenGCOtherWhite(g) & Lumen::GC::MarkWhiteBits)
#define LumenGCChangeWhite(x)    ((x)->AsObject.Marked ^= Lumen::GC::MarkWhiteBits)
#define LumenGCGray2Black(x)     LumenGCLSetBit((x)->AsObject.Marked, Lumen::GC::MarkBlackBit)
#define LumenGCValIsWhite(x)     (LumenIsCollectable(x) && LumenGCIsWhite(LumenGCValue(x)))
#define LumenGCWhite(g)          cast(Lumen::Byte, (g)->CurrentWhite & Lumen::GC::MarkWhiteBits)


#define LumenGCCheckGC(L) \
LumenDo(                  \
    LumenCondHardStackTests(Lumen::Do::ReAllocStack(L, L->StackCount - Lumen::ExtraStack - 1)); \
    if (LumenGlobalState(L)->TotalBytes >= LumenGlobalState(L)->GCThreshold)                       \
        Lumen::GC::Step(L);   \
)

#define LumenGCBarrier(L, p, v) \
LumenDo(                        \
    if (LumenGCValIsWhite(v) && LumenGCIsBlack(LumenObject2GCObject(p))) \
        Lumen::GC::BarrierF(L, LumenObject2GCObject(p), LumenGCValue(v));  \
)

#define LumenGCBarrierTable(L, t, v) \
LumenDo(                             \
    if (LumenGCValIsWhite(v) && LumenGCIsBlack(LumenObject2GCObject(t))) \
        Lumen::GC::BarrierBack(L, t); \
)

#define LumenGCObjectBarrier(L, p, o) \
LumenDo(                              \
    if (LumenGCIsWhite(LumenObject2GCObject(o)) && LumenGCIsBlack(LumenObject2GCObject(p))) \
        Lumen::GC::BarrierF(L, LumenObject2GCObject(p), LumenObject2GCObject(o));           \
)

#define LumenGCObjectBarrierTable(L, t, o) \
LumenDo(                                   \
    if (LumenGCIsWhite(LumenObject2GCObject(o)) && LumenGCIsBlack(LumenObject2GCObject(t))) \
        Lumen::GC::BarrierBack(L, t);       \
)

#endif
