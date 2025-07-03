/*!
 * @brief Code generator for Lua
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_CODE_H
#define LUMEN_CODE_H

#include "lumen/lex.h"
#include "lumen/object.h"
#include "lumen/opcodes.h"
#include "lumen/parser.h"


/*
** Marks the end of a patch list. It is an invalid value both as an absolute
** address, and as a list link (would link an element to itself).
*/
#define NO_JUMP (-1)

namespace Lumen {
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
        Lumen::Proto *Func;  /* current function header */
        Lumen::Table *Constants;  /* table to find (and reuse) elements in `k` */
        Lumen::FuncState *Prev;  /* enclosing function */
        Lumen::LexState *Lexer;  /* lexical state */
        Lumen::State *L;  /* copy of the Lua state */
        Lumen::BlockNode *Blocks;  /* chain of current blocks */
        int PC;  /* next position to code (equivalent to `n code`) */
        int LastPC;   /* `pc' of last `jump target' */
        int JumpPC;  /* list of pending jumps to `pc` */
        int FreeReg;  /* first free register */
        int ConstantsCount;  /* number of elements in `k` */
        int ProtoCount;  /* number of elements in `p` */
        short LocalVarsCount;  /* number of elements in `local vars` */
        Lumen::Byte ActiveVarsCount;  /* number of active local variables */
        Lumen::UpValueDesc UpValues[LUA_MAX_UP_VALUES];  /* up values */
        unsigned short ActiveVars[LUA_MAX_VARS];  /* declared-variable stack */

        static int CodeABx(Lumen::FuncState *fs, Lumen::OpCode o, int A, unsigned int Bx);

        static int CodeABC(Lumen::FuncState *fs, Lumen::OpCode o, int A, int B, int C);

        static void FixLine(Lumen::FuncState *fs, int line);

        static void Nil(Lumen::FuncState *fs, int from, int n);

        static void ReserveRegs(Lumen::FuncState *fs, int n);

        static void CheckStack(Lumen::FuncState *fs, int n);

        static int StringK(Lumen::FuncState *fs, Lumen::String *s);

        static int NumberK(Lumen::FuncState *fs, Lumen::Number r);

        static void DischargeVars(Lumen::FuncState *fs, Lumen::ExpDesc *e);

        static int Exp2AnyReg(Lumen::FuncState *fs, Lumen::ExpDesc *e);

        static void Exp2NextReg(Lumen::FuncState *fs, Lumen::ExpDesc *e);

        static void Exp2Val(Lumen::FuncState *fs, Lumen::ExpDesc *e);

        static int Exp2RK(Lumen::FuncState *fs, Lumen::ExpDesc *e);

        static void Self(Lumen::FuncState *fs, Lumen::ExpDesc *e, Lumen::ExpDesc *key);

        static void Indexed(Lumen::FuncState *fs, Lumen::ExpDesc *t, Lumen::ExpDesc *k);

        static void GoIfTrue(Lumen::FuncState *fs, Lumen::ExpDesc *e);

        static void StoreVar(Lumen::FuncState *fs, Lumen::ExpDesc *var, Lumen::ExpDesc *e);

        static void SetReturns(Lumen::FuncState *fs, Lumen::ExpDesc *e, int nResults);

        static void SetOneRet(Lumen::FuncState *fs, Lumen::ExpDesc *e);

        static int Jump(Lumen::FuncState *fs);

        static void Ret(Lumen::FuncState *fs, int first, int nRet);

        static void PatchList(Lumen::FuncState *fs, int list, int target);

        static void PatchToHere(Lumen::FuncState *fs, int list);

        static void Concat(Lumen::FuncState *fs, int *l1, int l2);

        static int GetLabel(Lumen::FuncState *fs);

        static void Prefix(Lumen::FuncState *fs, Lumen::UnOpr op, Lumen::ExpDesc *v);

        static void InFix(Lumen::FuncState *fs, Lumen::BinOpr op, Lumen::ExpDesc *v);

        static void PosFix(Lumen::FuncState *fs, Lumen::BinOpr op, Lumen::ExpDesc *v1, Lumen::ExpDesc *v2);

        static void SetList(Lumen::FuncState *fs, int base, int nElements, int toStore);
    };
}


#define LumenFuncStateGetCode(fs, e)    ((fs)->Func->Code[(e)->Info])

#define LumenFuncStateCodeAsBx(fs, o, A, sBx)    Lumen::FuncState::CodeABx(fs,o,A,(sBx)+Lumen::Code::sBxMaxArg)

#define LumenFuncStateSetMulRet(fs, e)    Lumen::FuncState::SetReturns(fs, e, Lumen::RetMul)

inline void Lumen::FuncState::FixLine(Lumen::FuncState *fs, int line) {
    fs->Func->LineInfo[fs->PC - 1] = line;
}

inline void Lumen::FuncState::ReserveRegs(Lumen::FuncState *fs, int n) {
    Lumen::FuncState::CheckStack(fs, n);
    fs->FreeReg += n;
}

inline void Lumen::FuncState::CheckStack(Lumen::FuncState *fs, int n) {
    int newStack = fs->FreeReg + n;
    if (newStack > fs->Func->MaxStackSize) {
        if (newStack >= Lumen::MaxStack)
            Lumen::LexState::SyntaxError(fs->Lexer, "function or expression too complex");
        fs->Func->MaxStackSize = cast_byte(newStack);
    }
}

inline void Lumen::FuncState::Indexed(Lumen::FuncState *fs, Lumen::ExpDesc *t, Lumen::ExpDesc *k) {
    t->Aux = Lumen::FuncState::Exp2RK(fs, k);
    t->k = Lumen::ExpDesc::KindIndexed;
}

inline void Lumen::FuncState::Ret(Lumen::FuncState *fs, int first, int nRet) {
    Lumen::FuncState::CodeABC(fs, Lumen::OpCodeReturn, first, nRet + 1, 0);
}

/*
** returns current `pc` and marks it as a jump target (to avoid wrong
** optimizations with consecutive instructions not in the same basic block).
*/
inline int Lumen::FuncState::GetLabel(Lumen::FuncState *fs) {
    fs->LastPC = fs->PC;
    return fs->PC;
}



#endif
