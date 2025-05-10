/*!
 * @brief Opcodes for Lua virtual machine
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_OPCODES_H
#define LUA_OPCODES_H

#include "lua/limits.h"


/*===========================================================================
  We assume that instructions are unsigned numbers.
  All instructions have an opcode in the first 6 bits.
  Instructions can have the following fields:
	`A' : 8 bits
	`B' : 9 bits
	`C' : 9 bits
	`Bx' : 18 bits (`B' and `C' together)
	`sBx' : signed Bx

  A signed argument is represented in excess K; that is, the number
  value is the unsigned value minus K. K is exactly the maximum value
  for that argument (so that -max is represented by 0, and +max is
  represented by 2*max), which is half the maximum for the corresponding
  unsigned argument.
===========================================================================*/

/*
** size and position of opcode arguments.
*/
#define LUA_CODE_SIZE_C        9
#define LUA_CODE_SIZE_B        9
#define LUA_CODE_SIZE_Bx        (LUA_CODE_SIZE_C + LUA_CODE_SIZE_B)
#define LUA_CODE_SIZE_A        8

#define LUA_CODE_SIZE_OP        6

#define LUA_CODE_POS_OP        0
#define LUA_CODE_POS_A        (LUA_CODE_POS_OP + LUA_CODE_SIZE_OP)
#define LUA_CODE_POS_C        (LUA_CODE_POS_A + LUA_CODE_SIZE_A)
#define LUA_CODE_POS_B        (LUA_CODE_POS_C + LUA_CODE_SIZE_C)
#define LUA_CODE_POS_Bx        LUA_CODE_POS_C


/*
** limits for opcode arguments.
** we use (signed) int to manipulate most arguments,
** so they must fit in LUAI_BITSINT-1 bits (-1 for sign)
*/
#if LUA_CODE_SIZE_Bx < LUAI_BITSINT - 1
#define LUA_CODE_MAX_ARG_Bx        ((1<<LUA_CODE_SIZE_Bx)-1)
#define LUA_CODE_MAX_ARG_sBx        (LUA_CODE_MAX_ARG_Bx>>1)         /* `sBx' is signed */
#else
#define LUA_CODE_MAX_ARG_Bx        Lua::MaxInt
#define LUA_CODE_MAX_ARG_sBx        Lua::MaxInt
#endif


#define LUA_CODE_MAX_ARG_A        ((1<<LUA_CODE_SIZE_A)-1)
#define LUA_CODE_MAX_ARG_B        ((1<<LUA_CODE_SIZE_B)-1)
#define LUA_CODE_MAX_ARG_C        ((1<<LUA_CODE_SIZE_C)-1)


/* creates a mask with `n' 1 bits at position `p' */
#define LUA_CODE_MASK1(n, p)    ((~((~(Lua::Instruction)0)<<n))<<p)

/* creates a mask with `n' 0 bits at position `p' */
#define LUA_CODE_MASK0(n, p)    (~LUA_CODE_MASK1(n,p))

/*
** the following macros help to manipulate instructions
*/

#define LuaOpCodeGet(i)    (cast(Lua::OpCode, ((i)>>LUA_CODE_POS_OP) & LUA_CODE_MASK1(LUA_CODE_SIZE_OP, 0)))
#define LuaOpCodeSet(i, o)    ((i) = (((i) & LUA_CODE_MASK0(LUA_CODE_SIZE_OP,LUA_CODE_POS_OP)) | \
        ((cast(Lua::Instruction, o)<<LUA_CODE_POS_OP) & LUA_CODE_MASK1(LUA_CODE_SIZE_OP,LUA_CODE_POS_OP))))

#define LuaOpCodeGetArgA(i)    (cast(int, ((i)>>LUA_CODE_POS_A) & LUA_CODE_MASK1(LUA_CODE_SIZE_A, 0)))
#define LuaOpCodeSetArgA(i, u)    ((i) = (((i) & LUA_CODE_MASK0(LUA_CODE_SIZE_A,LUA_CODE_POS_A)) | \
        ((cast(Lua::Instruction, u)<<LUA_CODE_POS_A) & LUA_CODE_MASK1(LUA_CODE_SIZE_A,LUA_CODE_POS_A))))

#define LuaOpCodeGetArgB(i)    (cast(int, ((i)>>LUA_CODE_POS_B) & LUA_CODE_MASK1(LUA_CODE_SIZE_B, 0)))
#define LuaOpCodeSetArgB(i, b)    ((i) = (((i) & LUA_CODE_MASK0(LUA_CODE_SIZE_B,LUA_CODE_POS_B)) | \
        ((cast(Lua::Instruction, b)<<LUA_CODE_POS_B) & LUA_CODE_MASK1(LUA_CODE_SIZE_B,LUA_CODE_POS_B))))

#define LuaOpCodeGetArgC(i)    (cast(int, ((i)>>LUA_CODE_POS_C) & LUA_CODE_MASK1(LUA_CODE_SIZE_C, 0)))
#define LuaOpCodeSetArgC(i, b)    ((i) = (((i) & LUA_CODE_MASK0(LUA_CODE_SIZE_C,LUA_CODE_POS_C)) | \
        ((cast(Lua::Instruction, b)<<LUA_CODE_POS_C) & LUA_CODE_MASK1(LUA_CODE_SIZE_C,LUA_CODE_POS_C))))

#define LuaOpCodeGetArgBx(i)    (cast(int, ((i)>>LUA_CODE_POS_Bx) & LUA_CODE_MASK1(LUA_CODE_SIZE_Bx, 0)))
#define LuaOpCodeSetArgBx(i, b)    ((i) = (((i) & LUA_CODE_MASK0(LUA_CODE_SIZE_Bx,LUA_CODE_POS_Bx)) | \
        ((cast(Lua::Instruction, b)<<LUA_CODE_POS_Bx) & LUA_CODE_MASK1(LUA_CODE_SIZE_Bx,LUA_CODE_POS_Bx))))

#define LuaOpCodeGetArgsBx(i)    (LuaOpCodeGetArgBx(i) - LUA_CODE_MAX_ARG_sBx)
#define LuaOpCodeSetArgsBx(i, b)    LuaOpCodeSetArgBx((i), cast(unsigned int, (b) + LUA_CODE_MAX_ARG_sBx))

#define LuaOpCodeCreateABC(o, a, b, c)    ((cast(Lua::Instruction, o)<<LUA_CODE_POS_OP) \
            | (cast(Lua::Instruction, a)<<LUA_CODE_POS_A) \
            | (cast(Lua::Instruction, b)<<LUA_CODE_POS_B) \
            | (cast(Lua::Instruction, c)<<LUA_CODE_POS_C))

#define LuaOpCodeCreateABx(o, a, bc)    ((cast(Lua::Instruction, o)<<LUA_CODE_POS_OP) \
            | (cast(Lua::Instruction, a)<<LUA_CODE_POS_A) \
            | (cast(Lua::Instruction, bc)<<LUA_CODE_POS_Bx))


/*
** Macros to operate RK indices
*/

/* this bit 1 means constant (0 means register) */
#define LuaOpCodeBitRK        (1 << (LUA_CODE_SIZE_B - 1))

/* test whether value is a constant */
#define LuaOpCodeIsK(x)        ((x) & LuaOpCodeBitRK)

/* gets the index of the constant */
#define LuaOpCodeIndexK(r)    ((int)(r) & ~LuaOpCodeBitRK)

#define LuaOpCodeMaxIndexRK    (LuaOpCodeBitRK - 1)

/* code a constant index as a RK value */
#define LuaOpCodeRKAsk(x)    ((x) | LuaOpCodeBitRK)


/*
** invalid register that fits in 8 bits
*/
#define NO_REG        LUA_CODE_MAX_ARG_A


namespace Lua {
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
     * RK(x) == if LuaOpCodeIsK(x) then Kst(IndexK(x)) else R(x)\n
     * grep "ORDER OP" if you change these enums
     */
    typedef int OpCode;
    enum {
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

    inline constexpr int OpCodeCount = Lua::OpCodeVararg + 1;
}


/*===========================================================================
  Notes:
  (*) In Lua::OpCodeCall, if (B == 0) then B = top. C is the number of returns - 1,
      and can be 0: Lua::OpCodeCall then sets `top' to last_result+1, so
      next open instruction (Lua::OpCodeCall, Lua::OpCodeReturn, Lua::OpCodeSetList) may use `top'.

  (*) In Lua::OpCodeVararg, if (B == 0) then use actual number of varargs and
      set top (like in Lua::OpCodeCall with C == 0).

  (*) In Lua::OpCodeReturn, if (B == 0) then return up to `top'

  (*) In Lua::OpCodeSetList, if (B == 0) then B = `top';
      if (C == 0) then next `instruction' is real C

  (*) For comparisons, A specifies what condition the test should accept
      (true or false).

  (*) All `skips' (pc++) assume that next instruction is a jump
===========================================================================*/

namespace Lua {
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

    LUAI_DATA const Lua::Byte OpModes[Lua::OpCodeCount];

    /**
     * OpCode Names
     */
    LUAI_DATA const char *const OpNames[Lua::OpCodeCount + 1];
}

#define LuaGetOpMode(m)    (cast(Lua::OpMode, Lua::OpModes[m] & 3))
#define LuaGetBMode(m)     (cast(Lua::OpArg, (Lua::OpModes[m] >> 4) & 3))
#define LuaGetCMode(m)     (cast(Lua::OpArg, (Lua::OpModes[m] >> 2) & 3))
#define LuaTestAMode(m)    (Lua::OpModes[m] & (1 << 6))
#define LuaTestTMode(m)    (Lua::OpModes[m] & (1 << 7))

/* number of list items to accumulate before a SETLIST instruction */
#define LUA_FIELDS_PER_FLUSH    50

#endif
