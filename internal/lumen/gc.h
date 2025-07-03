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

    Lumen::UInteger SeparateUserdata(Lumen::State *L, int all);

    void CallGCTM(Lumen::State *L);

    void FreeAll(Lumen::State *L);

    void Step(Lumen::State *L);

    void FullGC(Lumen::State *L);

    void Link(Lumen::State *L, Lumen::GCObject *o, Lumen::Byte Type);

    void LinkUpValue(Lumen::State *L, Lumen::UpValue *uv);

    void BarrierF(Lumen::State *L, Lumen::GCObject *o, Lumen::GCObject *v);

    void BarrierBack(Lumen::State *L, Lumen::Table *t);

    inline void ResetBits(Lumen::Byte &x, Lumen::Byte mask) {
        x &= static_cast<Lumen::Byte>(~mask);
    }

    inline void SetBits(Lumen::Byte &x, Lumen::Byte mask) {
        x |= mask;
    }

    inline bool TestBits(Lumen::Byte x, Lumen::Byte mask) {
        return (x & mask) != 0;
    }

    inline Lumen::Byte BitMask(int bit) {
        return static_cast<Lumen::Byte>(1 << bit);
    }

    inline Lumen::Byte Bit2Mask(int bit1, int bit2) {
        return BitMask(bit1) | BitMask(bit2);
    }

    inline constexpr Lumen::Byte StaticBitMask(int bit) {
        return static_cast<Lumen::Byte>(1 << bit);
    }

    inline constexpr Lumen::Byte StaticBit2Mask(int bit1, int bit2) {
        return StaticBitMask(bit1) | StaticBitMask(bit2);
    }

    inline void LSetBit(Lumen::Byte &x, int bit) {
        SetBits(x, BitMask(bit));
    }

    inline void ResetBit(Lumen::Byte &x, int bit) {
        ResetBits(x, BitMask(bit));
    }

    inline bool TestBit(Lumen::Byte x, int bit) {
        return TestBits(x, BitMask(bit));
    }

    inline void Set2Bits(Lumen::Byte &x, int bit1, int bit2) {
        SetBits(x, Bit2Mask(bit1, bit2));
    }

    inline void Reset2Bits(Lumen::Byte &x, int bit1, int bit2) {
        ResetBits(x, Bit2Mask(bit1, bit2));
    }

    inline bool Test2Bits(Lumen::Byte x, int bit1, int bit2) {
        return TestBits(x, Bit2Mask(bit1, bit2));
    }

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
        MarkWhiteBits = StaticBit2Mask(Lumen::GC::MarkWhite0Bit, Lumen::GC::MarkWhite1Bit)
    };
}

#endif
