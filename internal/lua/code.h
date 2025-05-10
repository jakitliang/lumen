/*!
 * @brief Code generator for Lua
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_CODE_H
#define LUA_CODE_H

#include "lua/lex.h"
#include "lua/object.h"
#include "lua/opcodes.h"
#include "lua/parser.h"


/*
** Marks the end of a patch list. It is an invalid value both as an absolute
** address, and as a list link (would link an element to itself).
*/
#define NO_JUMP (-1)

namespace Lua {
    /**
     * Lua Binary Operator\n
     * grep "ORDER OPR" if you change these enums
     */
    typedef int BinOpr;
    enum {
        BinOprAdd, BinOprSub, BinOprMul, BinOprDiv, BinOprMod, BinOprPow,
        BinOprConcat,
        BinOprNE, BinOprEQ,
        BinOprLT, BinOprLE, BinOprGT, BinOprGE,
        BinOprAND, BinOprOR,
        BinOprNo
    };

    typedef int UnOpr;
    enum {
        UnOprMinus, UnOprNot, UnOprLen, UnOprNo
    };

    /* state needed to generate code for a given function */
    struct FuncState {
        Lua::Proto *Func;  /* current function header */
        Lua::Table *Constants;  /* table to find (and reuse) elements in `k` */
        Lua::FuncState *Prev;  /* enclosing function */
        Lua::LexState *Lexer;  /* lexical state */
        Lua::State *L;  /* copy of the Lua state */
        Lua::BlockNode *Blocks;  /* chain of current blocks */
        int PC;  /* next position to code (equivalent to `n code`) */
        int LastPC;   /* `pc' of last `jump target' */
        int JumpPC;  /* list of pending jumps to `pc` */
        int FreeReg;  /* first free register */
        int ConstantsCount;  /* number of elements in `k` */
        int ProtoCount;  /* number of elements in `p` */
        short LocalVarsCount;  /* number of elements in `local vars` */
        Lua::Byte ActiveVarsCount;  /* number of active local variables */
        Lua::UpValueDesc UpValues[LUAI_MAXUPVALUES];  /* up values */
        unsigned short ActiveVars[LUAI_MAXVARS];  /* declared-variable stack */

        static int CodeABx(Lua::FuncState *fs, Lua::OpCode o, int A, unsigned int Bx);

        static int CodeABC(Lua::FuncState *fs, Lua::OpCode o, int A, int B, int C);

        static void FixLine(Lua::FuncState *fs, int line);

        static void Nil(Lua::FuncState *fs, int from, int n);

        static void ReserveRegs(Lua::FuncState *fs, int n);

        static void CheckStack(Lua::FuncState *fs, int n);

        static int StringK(Lua::FuncState *fs, Lua::String *s);

        static int NumberK(Lua::FuncState *fs, Lua::Number r);

        static void DischargeVars(Lua::FuncState *fs, Lua::ExpDesc *e);

        static int Exp2AnyReg(Lua::FuncState *fs, Lua::ExpDesc *e);

        static void Exp2NextReg(Lua::FuncState *fs, Lua::ExpDesc *e);

        static void Exp2Val(Lua::FuncState *fs, Lua::ExpDesc *e);

        static int Exp2RK(Lua::FuncState *fs, Lua::ExpDesc *e);

        static void Self(Lua::FuncState *fs, Lua::ExpDesc *e, Lua::ExpDesc *key);

        static void Indexed(Lua::FuncState *fs, Lua::ExpDesc *t, Lua::ExpDesc *k);

        static void GoIfTrue(Lua::FuncState *fs, Lua::ExpDesc *e);

        static void StoreVar(Lua::FuncState *fs, Lua::ExpDesc *var, Lua::ExpDesc *e);

        static void SetReturns(Lua::FuncState *fs, Lua::ExpDesc *e, int nResults);

        static void SetOneRet(Lua::FuncState *fs, Lua::ExpDesc *e);

        static int Jump(Lua::FuncState *fs);

        static void Ret(Lua::FuncState *fs, int first, int nRet);

        static void PatchList(Lua::FuncState *fs, int list, int target);

        static void PatchToHere(Lua::FuncState *fs, int list);

        static void Concat(Lua::FuncState *fs, int *l1, int l2);

        static int GetLabel(Lua::FuncState *fs);

        static void Prefix(Lua::FuncState *fs, Lua::UnOpr op, Lua::ExpDesc *v);

        static void InFix(Lua::FuncState *fs, Lua::BinOpr op, Lua::ExpDesc *v);

        static void PosFix(Lua::FuncState *fs, Lua::BinOpr op, Lua::ExpDesc *v1, Lua::ExpDesc *v2);

        static void SetList(Lua::FuncState *fs, int base, int nElements, int toStore);
    };
}


#define LuaFuncStateGetCode(fs, e)    ((fs)->Func->Code[(e)->Info])

#define LuaFuncStateCodeAsBx(fs, o, A, sBx)    Lua::FuncState::CodeABx(fs,o,A,(sBx)+LUA_CODE_MAX_ARG_sBx)

#define LuaFuncStateSetMulRet(fs, e)    Lua::FuncState::SetReturns(fs, e, LUA_MULTRET)


#endif
