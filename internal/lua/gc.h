/*!
 * @brief Garbage Collector
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_GC_H
#define LUA_GC_H

#include "lua/object.h"

/*
** some useful bit tricks
*/
#define LuaGCResetBits(x, m)          ((x) &= cast(Lua::Byte, ~(m)))
#define LuaGCSetBits(x, m)            ((x) |= (m))
#define LuaGCTestBits(x, m)           ((x) & (m))
#define LuaGCBitMask(b)               (1 << (b))
#define LuaGCBit2Mask(b1, b2)         (LuaGCBitMask(b1) | LuaGCBitMask(b2))
#define LuaGCLSetBit(x, b)            LuaGCSetBits(x, LuaGCBitMask(b))
#define LuaGCResetBit(x, b)           LuaGCResetBits(x, LuaGCBitMask(b))
#define LuaGCTestBit(x, b)            LuaGCTestBits(x, LuaGCBitMask(b))
#define LuaGCSet2Bits(x, b1, b2)      LuaGCSetBits(x, (LuaGCBit2Mask(b1, b2)))
#define LuaGCReset2Bits(x, b1, b2)    LuaGCResetBits(x, (LuaGCBit2Mask(b1, b2)))
#define LuaGCTest2Bits(x, b1, b2)     LuaGCTestBits(x, (LuaGCBit2Mask(b1, b2)))

namespace Lua::GC {
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
        MarkWhiteBits = LuaGCBit2Mask(Lua::GC::MarkWhite0Bit, Lua::GC::MarkWhite1Bit)
    };

    size_t SeparateUserdata(Lua::State *L, int all);

    void CallGCTM(Lua::State *L);

    void FreeAll(Lua::State *L);

    void Step(Lua::State *L);

    void FullGC(Lua::State *L);

    void Link(Lua::State *L, Lua::GCObject *o, Lua::Byte Type);

    void LinkUpValue(Lua::State *L, Lua::UpValue *uv);

    void BarrierF(Lua::State *L, Lua::GCObject *o, Lua::GCObject *v);

    void BarrierBack(Lua::State *L, Lua::Table *t);
}

#define LuaGCIsWhite(x)        LuaGCTest2Bits((x)->AsObject.Marked, Lua::GC::MarkWhite0Bit, Lua::GC::MarkWhite1Bit)
#define LuaGCIsBlack(x)        LuaGCTestBit((x)->AsObject.Marked, Lua::GC::MarkBlackBit)
#define LuaGCIsGray(x)         (!LuaGCIsBlack(x) && !LuaGCIsWhite(x))
#define LuaGCOtherWhite(g)     (g->CurrentWhite ^ Lua::GC::MarkWhiteBits)
#define LuaGCIsDead(g, v)      ((v)->AsObject.Marked & LuaGCOtherWhite(g) & Lua::GC::MarkWhiteBits)
#define LuaGCChangeWhite(x)    ((x)->AsObject.Marked ^= Lua::GC::MarkWhiteBits)
#define LuaGCGray2Black(x)     LuaGCLSetBit((x)->AsObject.Marked, Lua::GC::MarkBlackBit)
#define LuaGCValIsWhite(x)     (LuaIsCollectable(x) && LuaGCIsWhite(LuaGCValue(x)))
#define LuaGCWhite(g)          cast(Lua::Byte, (g)->CurrentWhite & Lua::GC::MarkWhiteBits)


#define LuaGCCheckGC(L) \
LuaDo(                  \
    LuaCondHardStackTests(Lua::Do::ReAllocStack(L, L->StackCount - Lua::ExtraStack - 1)); \
    if (LuaGlobal(L)->TotalBytes >= LuaGlobal(L)->GCThreshold)                       \
        Lua::GC::Step(L);   \
)

#define LuaGCBarrier(L, p, v) \
LuaDo(                        \
    if (LuaGCValIsWhite(v) && LuaGCIsBlack(LuaObject2GCObject(p))) \
        Lua::GC::BarrierF(L, LuaObject2GCObject(p), LuaGCValue(v));  \
)

#define LuaGCBarrierTable(L, t, v) \
LuaDo(                             \
    if (LuaGCValIsWhite(v) && LuaGCIsBlack(LuaObject2GCObject(t))) \
        Lua::GC::BarrierBack(L, t); \
)

#define LuaGCObjectBarrier(L, p, o) \
LuaDo(                              \
    if (LuaGCIsWhite(LuaObject2GCObject(o)) && LuaGCIsBlack(LuaObject2GCObject(p))) \
        Lua::GC::BarrierF(L, LuaObject2GCObject(p), LuaObject2GCObject(o));           \
)

#define LuaGCObjectBarrierTable(L, t, o) \
LuaDo(                                   \
    if (LuaGCIsWhite(LuaObject2GCObject(o)) && LuaGCIsBlack(LuaObject2GCObject(t))) \
        Lua::GC::BarrierBack(L, t);       \
)

#endif
