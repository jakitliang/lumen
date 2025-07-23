/*!
 * @brief Opcodes for Lua virtual machine
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_OPCODES_H
#define LUMEN_OPCODES_H

#include "lumen/limits.h"


/*===========================================================================
  We assume that instructions are unsigned numbers.
  All instructions have an opcode in the first 6 bits.
  Instructions can have the following fields:
	`A` : 8 bits
	`B` : 9 bits
	`C` : 9 bits
	`Bx` : 18 bits (`B` and `C` together)
	`sBx` : signed Bx

  A signed argument is represented in excess K; that is, the number
  value is the unsigned value minus K. K is exactly the maximum value
  for that argument (so that -max is represented by 0, and +max is
  represented by 2*max), which is half the maximum for the corresponding
  unsigned argument.
===========================================================================*/


namespace Lumen::Code {
    /*
    ** size and position of opcode arguments.
    */

    inline constexpr Lumen::UInteger CFieldSize = 9;
    inline constexpr Lumen::UInteger BFieldSize = 9;
    inline constexpr Lumen::UInteger BxFieldSize = Lumen::Code::CFieldSize + Lumen::Code::BFieldSize;
    inline constexpr Lumen::UInteger AFieldSize = 8;
    inline constexpr Lumen::UInteger OpFieldSize = 6;

    inline constexpr Lumen::UInteger OpPos = 0;
    inline constexpr Lumen::UInteger APos = Lumen::Code::OpPos + Lumen::Code::OpFieldSize;
    inline constexpr Lumen::UInteger CPos = Lumen::Code::APos + Lumen::Code::AFieldSize;
    inline constexpr Lumen::UInteger BPos = Lumen::Code::CPos + Lumen::Code::CFieldSize;
    inline constexpr Lumen::UInteger BxPos = Lumen::Code::CPos;

    /*
    ** limits for opcode arguments.
    ** we use (signed) int to manipulate most arguments,
    ** so they must fit in LUA_BITS_INT-1 bits (-1 for sign)
    */

    inline constexpr Lumen::UInteger BxMaxArg = (1 << Lumen::Code::BxFieldSize) - 1;
    inline constexpr Lumen::UInteger sBxMaxArg = Lumen::Code::BxMaxArg >> 1; /* `sBx` is signed */

    inline constexpr Lumen::UInteger AMaxArg = (1 << Lumen::Code::AFieldSize) - 1;
    inline constexpr Lumen::UInteger BMaxArg = (1 << Lumen::Code::BFieldSize) - 1;
    inline constexpr Lumen::UInteger CMaxArg = (1 << Lumen::Code::CFieldSize) - 1;

    /* creates a mask with `n' 1 bits at position `p' */
    constexpr Lumen::Instruction MakeMask1(Lumen::UInteger n, Lumen::UInteger p) {
        return ((~((~(Lumen::Instruction) 0) << n)) << p);
    }

    /* creates a mask with `n' 0 bits at position `p' */
    constexpr Lumen::Instruction MakeMask0(Lumen::UInteger n, Lumen::UInteger p) {
        return (~MakeMask1(n, p));
    }

    inline constexpr Lumen::Instruction AMask1 = MakeMask1(Lumen::Code::AFieldSize, 0);
    inline constexpr Lumen::Instruction BMask1 = MakeMask1(Lumen::Code::BFieldSize, 0);
    inline constexpr Lumen::Instruction CMask1 = MakeMask1(Lumen::Code::CFieldSize, 0);
    inline constexpr Lumen::Instruction BxMask1 = MakeMask1(Lumen::Code::BxFieldSize, 0);
    inline constexpr Lumen::Instruction OpMask1 = MakeMask1(Lumen::Code::OpFieldSize, 0);

    inline constexpr Lumen::Instruction APosMask1 = MakeMask1(Lumen::Code::AFieldSize, Lumen::Code::APos);
    inline constexpr Lumen::Instruction BPosMask1 = MakeMask1(Lumen::Code::BFieldSize, Lumen::Code::BPos);
    inline constexpr Lumen::Instruction CPosMask1 = MakeMask1(Lumen::Code::CFieldSize, Lumen::Code::CPos);
    inline constexpr Lumen::Instruction BxPosMask1 = MakeMask1(Lumen::Code::BxFieldSize, Lumen::Code::BxPos);
    inline constexpr Lumen::Instruction OpPosMask1 = MakeMask1(Lumen::Code::OpFieldSize, Lumen::Code::OpPos);

    inline constexpr Lumen::Instruction AMask0 = MakeMask0(Lumen::Code::AFieldSize, 0);
    inline constexpr Lumen::Instruction BMask0 = MakeMask0(Lumen::Code::BFieldSize, 0);
    inline constexpr Lumen::Instruction CMask0 = MakeMask0(Lumen::Code::CFieldSize, 0);
    inline constexpr Lumen::Instruction BxMask0 = MakeMask0(Lumen::Code::BxFieldSize, 0);
    inline constexpr Lumen::Instruction OpMask0 = MakeMask0(Lumen::Code::OpFieldSize, 0);

    inline constexpr Lumen::Instruction APosMask0 = MakeMask0(Lumen::Code::AFieldSize, Lumen::Code::APos);
    inline constexpr Lumen::Instruction BPosMask0 = MakeMask0(Lumen::Code::BFieldSize, Lumen::Code::BPos);
    inline constexpr Lumen::Instruction CPosMask0 = MakeMask0(Lumen::Code::CFieldSize, Lumen::Code::CPos);
    inline constexpr Lumen::Instruction BxPosMask0 = MakeMask0(Lumen::Code::BxFieldSize, Lumen::Code::BxPos);
    inline constexpr Lumen::Instruction OpPosMask0 = MakeMask0(Lumen::Code::OpFieldSize, Lumen::Code::OpPos);
}

/*
** the following macros help to manipulate instructions
*/

#define LumenOpCodeGet(i)    (cast(Lumen::OpCode, ((i)>>Lumen::Code::OpPos) & Lumen::Code::OpMask1))
#define LumenOpCodeSet(i, o)    ((i) = (((i) & Lumen::Code::OpPosMask0) | \
        ((cast(Lumen::Instruction, o) << Lumen::Code::OpPos) & Lumen::Code::OpPosMask1)))

#define LumenOpCodeGetArgA(i)    (cast(int, ((i)>>Lumen::Code::APos) & Lumen::Code::AMask1))
#define LumenOpCodeSetArgA(i, u)    ((i) = (((i) & Lumen::Code::APosMask0) | \
        ((cast(Lumen::Instruction, u) << Lumen::Code::APos) & Lumen::Code::APosMask1)))

#define LumenOpCodeGetArgB(i)    (cast(int, ((i)>>Lumen::Code::BPos) & Lumen::Code::BMask1))
#define LumenOpCodeSetArgB(i, b)    ((i) = (((i) & Lumen::Code::BPosMask0) | \
        ((cast(Lumen::Instruction, b)<<Lumen::Code::BPos) & Lumen::Code::BPosMask1)))

#define LumenOpCodeGetArgC(i)    (cast(int, ((i)>>Lumen::Code::CPos) & Lumen::Code::CMask1))
#define LumenOpCodeSetArgC(i, b)    ((i) = (((i) & Lumen::Code::CPosMask0) | \
        ((cast(Lumen::Instruction, b)<<Lumen::Code::CPos) & Lumen::Code::CPosMask1)))

#define LumenOpCodeGetArgBx(i)    (cast(int, ((i)>>Lumen::Code::BxPos) & Lumen::Code::BxMask1))
#define LumenOpCodeSetArgBx(i, b)    ((i) = (((i) & Lumen::Code::BxPosMask0) | \
        ((cast(Lumen::Instruction, b)<<Lumen::Code::BxPos) & Lumen::Code::BxPosMask1)))

#define LumenOpCodeGetArgsBx(i)    (LumenOpCodeGetArgBx(i) - Lumen::Code::sBxMaxArg)
#define LumenOpCodeSetArgsBx(i, b)    LumenOpCodeSetArgBx((i), cast(unsigned int, (b) + Lumen::Code::sBxMaxArg))

#define LumenOpCodeCreateABC(o, a, b, c)    ((cast(Lumen::Instruction, o) << Lumen::Code::OpPos) \
            | (cast(Lumen::Instruction, a) << Lumen::Code::APos) \
            | (cast(Lumen::Instruction, b) << Lumen::Code::BPos) \
            | (cast(Lumen::Instruction, c) << Lumen::Code::CPos))

#define LumenOpCodeCreateABx(o, a, bc)    ((cast(Lumen::Instruction, o) << Lumen::Code::OpPos) \
            | (cast(Lumen::Instruction, a) << Lumen::Code::APos) \
            | (cast(Lumen::Instruction, bc) << Lumen::Code::BxPos))


/*
** Macros to operate RK indices
*/

namespace Lumen::Code {
    /* this bit 1 means constant (0 means register) */
    inline constexpr int BitRK = 1 << (BFieldSize - 1);

    inline constexpr int MaxIndexRK = BitRK - 1;
}

/* test whether value is a constant */
#define LumenOpCodeIsK(x)        ((x) & Lumen::Code::BitRK)

/* gets the index of the constant */
#define LumenOpCodeIndexK(r)    ((int)(r) & ~Lumen::Code::BitRK)

/* code a constant index as a RK value */
#define LumenOpCodeRKAsk(x)    ((x) | Lumen::Code::BitRK)


/*
** invalid register that fits in 8 bits
*/
#define NO_REG        Lumen::Code::AMaxArg


namespace Lumen {
    /* basic instruction format */
    typedef int OpMode;
    enum {
        OpModeIABC,
        OpModeIABx,
        OpModeIAsBx
    };

    /**
     * OpCode\n
     * R(x) - register\n
     * Kst(x) - constant (in constant table)\n
     * RK(x) == if LumenOpCodeIsK(x) then Kst(IndexK(x)) else R(x)\n
     * grep "ORDER OP" if you change these enums
     */
    typedef LUA_ENUM(unsigned int, OpCode) {
        /*----------------------------------------------------------------------
        name    args :=    description
        ------------------------------------------------------------------------*/
        OpCodeMove,/*	A B	R(A) := R(B)					*/
        OpCodeLoadK,/*	A Bx	R(A) := Kst(Bx)					*/
        OpCodeLoadBool,/*	A B C	R(A) := (Bool)B; if (C) pc++			*/
        OpCodeLoadNil,/*	A B	R(A) := ... := R(B) := nil			*/
        OpCodeGetUpVal,/*	A B	R(A) := UpValue[B]				*/

        OpCodeGetGlobal,/*	A Bx	R(A) := Gbl[Kst(Bx)]				*/
        OpCodeGetTable,/*	A B C	R(A) := R(B)[RK(C)]				*/

        OpCodeSetGlobal,/*	A Bx	Gbl[Kst(Bx)] := R(A)				*/
        OpCodeSetUpVal,/*	A B	UpValue[B] := R(A)				*/
        OpCodeSetTable,/*	A B C	R(A)[RK(B)] := RK(C)				*/

        OpCodeNewTable,/*	A B C	R(A) := {} (size = B,C)				*/

        OpCodeSelf,/*	A B C	R(A+1) := R(B); R(A) := R(B)[RK(C)]		*/

        OpCodeAdd,/*	A B C	R(A) := RK(B) + RK(C)				*/
        OpCodeSub,/*	A B C	R(A) := RK(B) - RK(C)				*/
        OpCodeMul,/*	A B C	R(A) := RK(B) * RK(C)				*/
        OpCodeDiv,/*	A B C	R(A) := RK(B) / RK(C)				*/
        OpCodeMod,/*	A B C	R(A) := RK(B) % RK(C)				*/
        OpCodePow,/*	A B C	R(A) := RK(B) ^ RK(C)				*/
        OpCodeUnm,/*	A B	R(A) := -R(B)					*/
        OpCodeNot,/*	A B	R(A) := not R(B)				*/
        OpCodeLen,/*	A B	R(A) := length of R(B)				*/

        OpCodeConcat,/*	A B C	R(A) := R(B).. ... ..R(C)			*/

        OpCodeJump,/*	sBx	pc+=sBx					*/

        OpCodeEQ,/*	A B C	if ((RK(B) == RK(C)) ~= A) then pc++		*/
        OpCodeLT,/*	A B C	if ((RK(B) <  RK(C)) ~= A) then pc++  		*/
        OpCodeLE,/*	A B C	if ((RK(B) <= RK(C)) ~= A) then pc++  		*/

        OpCodeTest,/*	A C	if not (R(A) <=> C) then pc++			*/
        OpCodeTestTest,/*	A B C	if (R(B) <=> C) then R(A) := R(B) else pc++	*/

        OpCodeCall,/*	A B C	R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1)) */
        OpCodeTailCall,/*	A B C	return R(A)(R(A+1), ... ,R(A+B-1))		*/
        OpCodeReturn,/*	A B	return R(A), ... ,R(A+B-2)	(see note)	*/

        OpCodeForLoop,/*	A sBx	R(A)+=R(A+2);
			if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) }*/
        OpCodeForPrep,/*	A sBx	R(A)-=R(A+2); pc+=sBx				*/

        OpCodeTForLoop,/*	A C	R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2));
                        if R(A+3) ~= nil then R(A+2)=R(A+3) else pc++	*/
        OpCodeSetList,/*	A B C	R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B	*/

        OpCodeClose,/*	A 	close all variables in the stack up to (>=) R(A)*/
        OpCodeClosure,/*	A Bx	R(A) := closure(KProto[Bx], R(A), ... ,R(A+n))	*/

        OpCodeVararg/*	A B	R(A), R(A+1), ..., R(A+B-1) = vararg		*/
    };

    inline constexpr int OpCodeCount = Lumen::OpCodeVararg + 1;
}


/*===========================================================================
  Notes:
  (*) In Lumen::OpCodeCall, if (B == 0) then B = top. C is the number of returns - 1,
      and can be 0: Lumen::OpCodeCall then sets `top' to last_result+1, so
      next open instruction (Lumen::OpCodeCall, Lumen::OpCodeReturn, Lumen::OpCodeSetList) may use `top'.

  (*) In Lumen::OpCodeVararg, if (B == 0) then use actual number of varargs and
      set top (like in Lumen::OpCodeCall with C == 0).

  (*) In Lumen::OpCodeReturn, if (B == 0) then return up to `top'

  (*) In Lumen::OpCodeSetList, if (B == 0) then B = `top';
      if (C == 0) then next `instruction' is real C

  (*) For comparisons, A specifies what condition the test should accept
      (true or false).

  (*) All `skips' (pc++) assume that next instruction is a jump
===========================================================================*/

namespace Lumen {
    /**
     * OpArg\n
     * masks for instruction properties. The format is:\n
     * bits 0-1: op mode\n
     * bits 2-3: C arg mode\n
     * bits 4-5: B arg mode\n
     * bit 6: instruction set register A\n
     * bit 7: operator is a test
     */
    typedef int OpArg;
    enum {
        OpArgN,  /* argument is not used */
        OpArgU,  /* argument is used */
        OpArgR,  /* argument is a register or a jump offset */
        OpArgK   /* argument is a constant or register/constant */
    };

    LUAI_DATA const Lumen::Byte OpModes[Lumen::OpCodeCount];

    /**
     * OpCode Names
     */
    LUAI_DATA const char *const OpNames[Lumen::OpCodeCount + 1];
}

#define LumenGetOpMode(m)    (cast(Lumen::OpMode, Lumen::OpModes[m] & 3))
#define LumenGetBMode(m)     (cast(Lumen::OpArg, (Lumen::OpModes[m] >> 4) & 3))
#define LumenGetCMode(m)     (cast(Lumen::OpArg, (Lumen::OpModes[m] >> 2) & 3))
#define LumenTestAMode(m)    (Lumen::OpModes[m] & (1 << 6))
#define LumenTestTMode(m)    (Lumen::OpModes[m] & (1 << 7))

/* number of list items to accumulate before a SETLIST instruction */
#define LUA_FIELDS_PER_FLUSH    50

#endif
