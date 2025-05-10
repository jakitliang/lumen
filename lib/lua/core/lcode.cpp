/*!
 * @brief Code generator for Lua
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstdlib>

#define lcode_c
#define LUA_CORE

#include "lua.h"

#include "lua/code.h"
#include "lua/debug.h"
#include "lua/do.h"
#include "lua/gc.h"
#include "lua/lex.h"
#include "lua/mem.h"
#include "lua/object.h"
#include "lua/opcodes.h"
#include "lua/parser.h"
#include "lua/table.h"


#define hasJumps(e)    ((e)->t != (e)->f)


static int isNumeric(Lua::ExpDesc *e) {
    return (e->k == Lua::ExpDesc::KindKNum && e->t == NO_JUMP && e->f == NO_JUMP);
}


void Lua::FuncState::Nil(Lua::FuncState *fs, int from, int n) {
    Lua::Instruction *previous;
    if (fs->PC > fs->LastPC) {  /* no jumps to current position? */
        if (fs->PC == 0) {  /* function start? */
            if (from >= fs->ActiveVarsCount)
                return;  /* positions are already clean */
        } else {
            previous = &fs->Func->Code[fs->PC - 1];
            if (LuaOpCodeGet(*previous) == Lua::OpCodeLoadNil) {
                int pFrom = LuaOpCodeGetArgA(*previous);
                int pto = LuaOpCodeGetArgB(*previous);
                if (pFrom <= from && from <= pto + 1) {  /* can connect both? */
                    if (from + n - 1 > pto)
                        LuaOpCodeSetArgB(*previous, from + n - 1);
                    return;
                }
            }
        }
    }
    Lua::FuncState::CodeABC(fs, Lua::OpCodeLoadNil, from, from + n - 1, 0);  /* else no optimization */
}


int Lua::FuncState::Jump(Lua::FuncState *fs) {
    int jpc = fs->JumpPC;  /* save list of jumps to here */
    int j;
    fs->JumpPC = NO_JUMP;
    j = LuaFuncStateCodeAsBx(fs, Lua::OpCodeJump, 0, NO_JUMP);
    Lua::FuncState::Concat(fs, &j, jpc);  /* keep them on hold */
    return j;
}


void Lua::FuncState::Ret(Lua::FuncState *fs, int first, int nRet) {
    Lua::FuncState::CodeABC(fs, Lua::OpCodeReturn, first, nRet + 1, 0);
}


static int condJump(Lua::FuncState *fs, Lua::OpCode op, int A, int B, int C) {
    Lua::FuncState::CodeABC(fs, op, A, B, C);
    return Lua::FuncState::Jump(fs);
}


static void fixJump(Lua::FuncState *fs, int pc, int dest) {
    Lua::Instruction *jmp = &fs->Func->Code[pc];
    int offset = dest - (pc + 1);
    lua_assert(dest != NO_JUMP);
    if (abs(offset) > LUA_CODE_MAX_ARG_sBx)
        Lua::LexState::SyntaxError(fs->Lexer, "control structure too long");
    LuaOpCodeSetArgsBx(*jmp, offset);
}


/*
** returns current `pc` and marks it as a jump target (to avoid wrong
** optimizations with consecutive instructions not in the same basic block).
*/
int Lua::FuncState::GetLabel(Lua::FuncState *fs) {
    fs->LastPC = fs->PC;
    return fs->PC;
}


static int getJump(Lua::FuncState *fs, int pc) {
    int offset = LuaOpCodeGetArgsBx(fs->Func->Code[pc]);
    if (offset == NO_JUMP)  /* point to itself represents end of list */
        return NO_JUMP;  /* end of list */
    else
        return (pc + 1) + offset;  /* turn offset into absolute position */
}


static Lua::Instruction *getJumpControl(Lua::FuncState *fs, int pc) {
    Lua::Instruction *pi = &fs->Func->Code[pc];
    if (pc >= 1 && LuaTestTMode(LuaOpCodeGet(*(pi - 1))))
        return pi - 1;
    else
        return pi;
}


/*
** check whether list has any jump that do not produce a value
** (or produce an inverted value)
*/
static int needValue(Lua::FuncState *fs, int list) {
    for (; list != NO_JUMP; list = getJump(fs, list)) {
        Lua::Instruction i = *getJumpControl(fs, list);
        if (LuaOpCodeGet(i) != Lua::OpCodeTestTest) return 1;
    }
    return 0;  /* not found */
}


static int patchTestReg(Lua::FuncState *fs, int node, int reg) {
    Lua::Instruction *i = getJumpControl(fs, node);
    if (LuaOpCodeGet(*i) != Lua::OpCodeTestTest)
        return 0;  /* cannot patch other instructions */
    if (reg != NO_REG && reg != LuaOpCodeGetArgB(*i))
        LuaOpCodeSetArgA(*i, reg);
    else  /* no register to put value or register already has the value */
        *i = LuaOpCodeCreateABC(Lua::OpCodeTest, LuaOpCodeGetArgB(*i), 0, LuaOpCodeGetArgC(*i));

    return 1;
}


static void removeValues(Lua::FuncState *fs, int list) {
    for (; list != NO_JUMP; list = getJump(fs, list))
        patchTestReg(fs, list, NO_REG);
}


static void patchListAux(Lua::FuncState *fs, int list, int vTarget, int reg,
                         int dTarget) {
    while (list != NO_JUMP) {
        int next = getJump(fs, list);
        if (patchTestReg(fs, list, reg))
            fixJump(fs, list, vTarget);
        else
            fixJump(fs, list, dTarget);  /* jump to default target */
        list = next;
    }
}


static void dischargeJumpPC(Lua::FuncState *fs) {
    patchListAux(fs, fs->JumpPC, fs->PC, NO_REG, fs->PC);
    fs->JumpPC = NO_JUMP;
}


void Lua::FuncState::PatchList(Lua::FuncState *fs, int list, int target) {
    if (target == fs->PC)
        Lua::FuncState::PatchToHere(fs, list);
    else {
        lua_assert(target < fs->PC);
        patchListAux(fs, list, target, NO_REG, target);
    }
}


void Lua::FuncState::PatchToHere(Lua::FuncState *fs, int list) {
    Lua::FuncState::GetLabel(fs);
    Lua::FuncState::Concat(fs, &fs->JumpPC, list);
}


void Lua::FuncState::Concat(Lua::FuncState *fs, int *l1, int l2) {
    if (l2 == NO_JUMP) return;
    else if (*l1 == NO_JUMP)
        *l1 = l2;
    else {
        int list = *l1;
        int next;
        while ((next = getJump(fs, list)) != NO_JUMP)  /* find last element */
            list = next;
        fixJump(fs, list, l2);
    }
}


void Lua::FuncState::CheckStack(Lua::FuncState *fs, int n) {
    int newStack = fs->FreeReg + n;
    if (newStack > fs->Func->MaxStackSize) {
        if (newStack >= Lua::MaxStack)
            Lua::LexState::SyntaxError(fs->Lexer, "function or expression too complex");
        fs->Func->MaxStackSize = cast_byte(newStack);
    }
}


void Lua::FuncState::ReserveRegs(Lua::FuncState *fs, int n) {
    Lua::FuncState::CheckStack(fs, n);
    fs->FreeReg += n;
}


static void freeReg(Lua::FuncState *fs, int reg) {
    if (!LuaOpCodeIsK(reg) && reg >= fs->ActiveVarsCount) {
        fs->FreeReg--;
        lua_assert(reg == fs->FreeReg);
    }
}


static void freeExp(Lua::FuncState *fs, Lua::ExpDesc *e) {
    if (e->k == Lua::ExpDesc::KindNonRelocatable)
        freeReg(fs, e->Info);
}


static int addK(Lua::FuncState *fs, Lua::Value *k, Lua::Value *v) {
    Lua::State *L = fs->L;
    Lua::Value *idx = Lua::Table::Set(L, fs->Constants, k);
    Lua::Proto *f = fs->Func;
    int oldSize = f->KCount;
    if (LuaTypeIsNumber(idx)) {
        lua_assert(Lua::RawEqualObject(&fs->Func->K[cast_int(LuaNumberValue(idx))], v));
        return cast_int(LuaNumberValue(idx));
    } else {  /* constant not found; create a new entry */
        LuaSetNumberValue(idx, cast_num(fs->ConstantsCount));
        LuaMemoryGrowVector(L, f->K, fs->ConstantsCount, f->KCount, Lua::Value,
                            LUA_CODE_MAX_ARG_Bx, "constant table overflow");
        while (oldSize < f->KCount) LuaSetNilValue(&f->K[oldSize++]);
        LuaSetObject(L, &f->K[fs->ConstantsCount], v);
        LuaGCBarrier(L, f, v);
        return fs->ConstantsCount++;
    }
}


int Lua::FuncState::StringK(Lua::FuncState *fs, Lua::String *s) {
    Lua::Value o;
    LuaSetStringValue(fs->L, &o, s);
    return addK(fs, &o, &o);
}


int Lua::FuncState::NumberK(Lua::FuncState *fs, Lua::Number r) {
    Lua::Value o;
    LuaSetNumberValue(&o, r);
    return addK(fs, &o, &o);
}


static int boolK(Lua::FuncState *fs, int b) {
    Lua::Value o;
    LuaSetBoolValue(&o, b);
    return addK(fs, &o, &o);
}


static int nilK(Lua::FuncState *fs) {
    Lua::Value k, v;
    LuaSetNilValue(&v);
    /* cannot use nil as key; instead use table itself to represent nil */
    LuaSetTableValue(fs->L, &k, fs->Constants);
    return addK(fs, &k, &v);
}


void Lua::FuncState::SetReturns(Lua::FuncState *fs, Lua::ExpDesc *e, int nResults) {
    if (e->k == Lua::ExpDesc::KindCall) {  /* expression is an open function call? */
        LuaOpCodeSetArgC(LuaFuncStateGetCode(fs, e), nResults + 1);
    } else if (e->k == Lua::ExpDesc::KindVararg) {
        LuaOpCodeSetArgB(LuaFuncStateGetCode(fs, e), nResults + 1);
        LuaOpCodeSetArgA(LuaFuncStateGetCode(fs, e), fs->FreeReg);
        Lua::FuncState::ReserveRegs(fs, 1);
    }
}


void Lua::FuncState::SetOneRet(Lua::FuncState *fs, Lua::ExpDesc *e) {
    if (e->k == Lua::ExpDesc::KindCall) {  /* expression is an open function call? */
        e->k = Lua::ExpDesc::KindNonRelocatable;
        e->Info = LuaOpCodeGetArgA(LuaFuncStateGetCode(fs, e));
    } else if (e->k == Lua::ExpDesc::KindVararg) {
        LuaOpCodeSetArgB(LuaFuncStateGetCode(fs, e), 2);
        e->k = Lua::ExpDesc::KindRelocatable;  /* can relocate its simple result */
    }
}


void Lua::FuncState::DischargeVars(Lua::FuncState *fs, Lua::ExpDesc *e) {
    switch (e->k) {
        case Lua::ExpDesc::KindLocal: {
            e->k = Lua::ExpDesc::KindNonRelocatable;
            break;
        }
        case Lua::ExpDesc::KindUpValue: {
            e->Info = Lua::FuncState::CodeABC(fs, Lua::OpCodeGetUpVal, 0, e->Info, 0);
            e->k = Lua::ExpDesc::KindRelocatable;
            break;
        }
        case Lua::ExpDesc::KindGlobal: {
            e->Info = Lua::FuncState::CodeABx(fs, Lua::OpCodeGetGlobal, 0, e->Info);
            e->k = Lua::ExpDesc::KindRelocatable;
            break;
        }
        case Lua::ExpDesc::KindIndexed: {
            freeReg(fs, e->Aux);
            freeReg(fs, e->Info);
            e->Info = Lua::FuncState::CodeABC(fs, Lua::OpCodeGetTable, 0, e->Info, e->Aux);
            e->k = Lua::ExpDesc::KindRelocatable;
            break;
        }
        case Lua::ExpDesc::KindVararg:
        case Lua::ExpDesc::KindCall: {
            Lua::FuncState::SetOneRet(fs, e);
            break;
        }
        default:
            break;  /* there is one value available (somewhere) */
    }
}


static int codeLabel(Lua::FuncState *fs, int A, int b, int jump) {
    Lua::FuncState::GetLabel(fs);  /* those instructions may be jump targets */
    return Lua::FuncState::CodeABC(fs, Lua::OpCodeLoadBool, A, b, jump);
}


static void discharge2reg(Lua::FuncState *fs, Lua::ExpDesc *e, int reg) {
    Lua::FuncState::DischargeVars(fs, e);
    switch (e->k) {
        case Lua::ExpDesc::KindNil: {
            Lua::FuncState::Nil(fs, reg, 1);
            break;
        }
        case Lua::ExpDesc::KindFalse:
        case Lua::ExpDesc::KindTrue: {
            Lua::FuncState::CodeABC(fs, Lua::OpCodeLoadBool, reg, e->k == Lua::ExpDesc::KindTrue, 0);
            break;
        }
        case Lua::ExpDesc::KindK: {
            Lua::FuncState::CodeABx(fs, Lua::OpCodeLoadK, reg, e->Info);
            break;
        }
        case Lua::ExpDesc::KindKNum: {
            Lua::FuncState::CodeABx(fs, Lua::OpCodeLoadK, reg, Lua::FuncState::NumberK(fs, e->NumberValue));
            break;
        }
        case Lua::ExpDesc::KindRelocatable: {
            Lua::Instruction *pc = &LuaFuncStateGetCode(fs, e);
            LuaOpCodeSetArgA(*pc, reg);
            break;
        }
        case Lua::ExpDesc::KindNonRelocatable: {
            if (reg != e->Info)
                Lua::FuncState::CodeABC(fs, Lua::OpCodeMove, reg, e->Info, 0);
            break;
        }
        default: {
            lua_assert(e->k == Lua::ExpDesc::KindVoid || e->k == Lua::ExpDesc::KindJmp);
            return;  /* nothing to do... */
        }
    }
    e->Info = reg;
    e->k = Lua::ExpDesc::KindNonRelocatable;
}


static void discharge2AnyReg(Lua::FuncState *fs, Lua::ExpDesc *e) {
    if (e->k != Lua::ExpDesc::KindNonRelocatable) {
        Lua::FuncState::ReserveRegs(fs, 1);
        discharge2reg(fs, e, fs->FreeReg - 1);
    }
}


static void exp2reg(Lua::FuncState *fs, Lua::ExpDesc *e, int reg) {
    discharge2reg(fs, e, reg);
    if (e->k == Lua::ExpDesc::KindJmp)
        Lua::FuncState::Concat(fs, &e->t, e->Info);  /* put this jump in `t` list */
    if (hasJumps(e)) {
        int final;  /* position after whole expression */
        int p_f = NO_JUMP;  /* position of an eventual LOAD false */
        int p_t = NO_JUMP;  /* position of an eventual LOAD true */
        if (needValue(fs, e->t) || needValue(fs, e->f)) {
            int fj = (e->k == Lua::ExpDesc::KindJmp) ? NO_JUMP : Lua::FuncState::Jump(fs);
            p_f = codeLabel(fs, reg, 0, 1);
            p_t = codeLabel(fs, reg, 1, 0);
            Lua::FuncState::PatchToHere(fs, fj);
        }
        final = Lua::FuncState::GetLabel(fs);
        patchListAux(fs, e->f, final, reg, p_f);
        patchListAux(fs, e->t, final, reg, p_t);
    }
    e->f = e->t = NO_JUMP;
    e->Info = reg;
    e->k = Lua::ExpDesc::KindNonRelocatable;
}


void Lua::FuncState::Exp2NextReg(Lua::FuncState *fs, Lua::ExpDesc *e) {
    Lua::FuncState::DischargeVars(fs, e);
    freeExp(fs, e);
    Lua::FuncState::ReserveRegs(fs, 1);
    exp2reg(fs, e, fs->FreeReg - 1);
}


int Lua::FuncState::Exp2AnyReg(Lua::FuncState *fs, Lua::ExpDesc *e) {
    Lua::FuncState::DischargeVars(fs, e);
    if (e->k == Lua::ExpDesc::KindNonRelocatable) {
        if (!hasJumps(e)) return e->Info;  /* exp is already in a register */
        if (e->Info >= fs->ActiveVarsCount) {  /* reg. is not a local? */
            exp2reg(fs, e, e->Info);  /* put value on it */
            return e->Info;
        }
    }
    Lua::FuncState::Exp2NextReg(fs, e);  /* default */
    return e->Info;
}


void Lua::FuncState::Exp2Val(Lua::FuncState *fs, Lua::ExpDesc *e) {
    if (hasJumps(e))
        Lua::FuncState::Exp2AnyReg(fs, e);
    else
        Lua::FuncState::DischargeVars(fs, e);
}


int Lua::FuncState::Exp2RK(Lua::FuncState *fs, Lua::ExpDesc *e) {
    Lua::FuncState::Exp2Val(fs, e);
    switch (e->k) {
        case Lua::ExpDesc::KindKNum:
        case Lua::ExpDesc::KindTrue:
        case Lua::ExpDesc::KindFalse:
        case Lua::ExpDesc::KindNil: {
            if (fs->ConstantsCount <= LuaOpCodeMaxIndexRK) {  /* constant fit in RK operand? */
                e->Info = (e->k == Lua::ExpDesc::KindNil) ? nilK(fs) :
                          (e->k == Lua::ExpDesc::KindKNum) ? Lua::FuncState::NumberK(fs, e->NumberValue) :
                          boolK(fs, (e->k == Lua::ExpDesc::KindTrue));
                e->k = Lua::ExpDesc::KindK;
                return LuaOpCodeRKAsk(e->Info);
            } else break;
        }
        case Lua::ExpDesc::KindK: {
            if (e->Info <= LuaOpCodeMaxIndexRK)  /* constant fit in argC? */
                return LuaOpCodeRKAsk(e->Info);
            else break;
        }
        default:
            break;
    }
    /* not a constant in the right range: put it in a register */
    return Lua::FuncState::Exp2AnyReg(fs, e);
}


void Lua::FuncState::StoreVar(Lua::FuncState *fs, Lua::ExpDesc *var, Lua::ExpDesc *ex) {
    switch (var->k) {
        case Lua::ExpDesc::KindLocal: {
            freeExp(fs, ex);
            exp2reg(fs, ex, var->Info);
            return;
        }
        case Lua::ExpDesc::KindUpValue: {
            int e = Lua::FuncState::Exp2AnyReg(fs, ex);
            Lua::FuncState::CodeABC(fs, Lua::OpCodeSetUpVal, e, var->Info, 0);
            break;
        }
        case Lua::ExpDesc::KindGlobal: {
            int e = Lua::FuncState::Exp2AnyReg(fs, ex);
            Lua::FuncState::CodeABx(fs, Lua::OpCodeSetGlobal, e, var->Info);
            break;
        }
        case Lua::ExpDesc::KindIndexed: {
            int e = Lua::FuncState::Exp2RK(fs, ex);
            Lua::FuncState::CodeABC(fs, Lua::OpCodeSetTable, var->Info, var->Aux, e);
            break;
        }
        default: {
            lua_assert(0);  /* invalid var kind to store */
            break;
        }
    }
    freeExp(fs, ex);
}


void Lua::FuncState::Self(Lua::FuncState *fs, Lua::ExpDesc *e, Lua::ExpDesc *key) {
    int func;
    Lua::FuncState::Exp2AnyReg(fs, e);
    freeExp(fs, e);
    func = fs->FreeReg;
    Lua::FuncState::ReserveRegs(fs, 2);
    Lua::FuncState::CodeABC(fs, Lua::OpCodeSelf, func, e->Info, Lua::FuncState::Exp2RK(fs, key));
    freeExp(fs, key);
    e->Info = func;
    e->k = Lua::ExpDesc::KindNonRelocatable;
}


static void invertJump(Lua::FuncState *fs, Lua::ExpDesc *e) {
    Lua::Instruction *pc = getJumpControl(fs, e->Info);
    lua_assert(LuaTestTMode(LuaOpCodeGet(*pc)) && LuaOpCodeGet(*pc) != Lua::OpCodeTestTest &&
               LuaOpCodeGet(*pc) != Lua::OpCodeTest);
    LuaOpCodeSetArgA(*pc, !(LuaOpCodeGetArgA(*pc)));
}


static int jumpOnCond(Lua::FuncState *fs, Lua::ExpDesc *e, int cond) {
    if (e->k == Lua::ExpDesc::KindRelocatable) {
        Lua::Instruction ie = LuaFuncStateGetCode(fs, e);
        if (LuaOpCodeGet(ie) == Lua::OpCodeNot) {
            fs->PC--;  /* remove previous Lua::OpCodeNot */
            return condJump(fs, Lua::OpCodeTest, LuaOpCodeGetArgB(ie), 0, !cond);
        }
        /* else go through */
    }
    discharge2AnyReg(fs, e);
    freeExp(fs, e);
    return condJump(fs, Lua::OpCodeTestTest, NO_REG, e->Info, cond);
}


void Lua::FuncState::GoIfTrue(Lua::FuncState *fs, Lua::ExpDesc *e) {
    int pc;  /* pc of last jump */
    Lua::FuncState::DischargeVars(fs, e);
    switch (e->k) {
        case Lua::ExpDesc::KindK:
        case Lua::ExpDesc::KindKNum:
        case Lua::ExpDesc::KindTrue: {
            pc = NO_JUMP;  /* always true; do nothing */
            break;
        }
        case Lua::ExpDesc::KindJmp: {
            invertJump(fs, e);
            pc = e->Info;
            break;
        }
        default: {
            pc = jumpOnCond(fs, e, 0);
            break;
        }
    }
    Lua::FuncState::Concat(fs, &e->f, pc);  /* insert last jump in `f` list */
    Lua::FuncState::PatchToHere(fs, e->t);
    e->t = NO_JUMP;
}


static void LuaStateGoIfFalse(Lua::FuncState *fs, Lua::ExpDesc *e) {
    int pc;  /* pc of last jump */
    Lua::FuncState::DischargeVars(fs, e);
    switch (e->k) {
        case Lua::ExpDesc::KindNil:
        case Lua::ExpDesc::KindFalse: {
            pc = NO_JUMP;  /* always false; do nothing */
            break;
        }
        case Lua::ExpDesc::KindJmp: {
            pc = e->Info;
            break;
        }
        default: {
            pc = jumpOnCond(fs, e, 1);
            break;
        }
    }
    Lua::FuncState::Concat(fs, &e->t, pc);  /* insert last jump in `t` list */
    Lua::FuncState::PatchToHere(fs, e->f);
    e->f = NO_JUMP;
}


static void codeNot(Lua::FuncState *fs, Lua::ExpDesc *e) {
    Lua::FuncState::DischargeVars(fs, e);
    switch (e->k) {
        case Lua::ExpDesc::KindNil:
        case Lua::ExpDesc::KindFalse: {
            e->k = Lua::ExpDesc::KindTrue;
            break;
        }
        case Lua::ExpDesc::KindK:
        case Lua::ExpDesc::KindKNum:
        case Lua::ExpDesc::KindTrue: {
            e->k = Lua::ExpDesc::KindFalse;
            break;
        }
        case Lua::ExpDesc::KindJmp: {
            invertJump(fs, e);
            break;
        }
        case Lua::ExpDesc::KindRelocatable:
        case Lua::ExpDesc::KindNonRelocatable: {
            discharge2AnyReg(fs, e);
            freeExp(fs, e);
            e->Info = Lua::FuncState::CodeABC(fs, Lua::OpCodeNot, 0, e->Info, 0);
            e->k = Lua::ExpDesc::KindRelocatable;
            break;
        }
        default: {
            lua_assert(0);  /* cannot happen */
            break;
        }
    }
    /* interchange true and false lists */
    {
        int temp = e->f;
        e->f = e->t;
        e->t = temp;
    }
    removeValues(fs, e->f);
    removeValues(fs, e->t);
}


void Lua::FuncState::Indexed(Lua::FuncState *fs, Lua::ExpDesc *t, Lua::ExpDesc *k) {
    t->Aux = Lua::FuncState::Exp2RK(fs, k);
    t->k = Lua::ExpDesc::KindIndexed;
}


static int constFolding(Lua::OpCode op, Lua::ExpDesc *e1, Lua::ExpDesc *e2) {
    Lua::Number v1, v2, r;
    if (!isNumeric(e1) || !isNumeric(e2)) return 0;
    v1 = e1->NumberValue;
    v2 = e2->NumberValue;
    switch (op) {
        case Lua::OpCodeAdd:
            r = luai_numadd(v1, v2);
            break;
        case Lua::OpCodeSub:
            r = luai_numsub(v1, v2);
            break;
        case Lua::OpCodeMul:
            r = luai_nummul(v1, v2);
            break;
        case Lua::OpCodeDiv:
            if (v2 == 0) return 0;  /* do not attempt to divide by 0 */
            r = luai_numdiv(v1, v2);
            break;
        case Lua::OpCodeMod:
            if (v2 == 0) return 0;  /* do not attempt to divide by 0 */
            r = luai_nummod(v1, v2);
            break;
        case Lua::OpCodePow:
            r = luai_numpow(v1, v2);
            break;
        case Lua::OpCodeUnm:
            r = luai_numunm(v1);
            break;
        case Lua::OpCodeLen:
            return 0;  /* no constant folding for 'len' */
        default:
            lua_assert(0);
            r = 0;
            break;
    }
    if (luai_numisnan(r)) return 0;  /* do not attempt to produce NaN */
    e1->NumberValue = r;
    return 1;
}


static void codeArith(Lua::FuncState *fs, Lua::OpCode op, Lua::ExpDesc *e1, Lua::ExpDesc *e2) {
    if (constFolding(op, e1, e2))
        return;
    else {
        int o2 = (op != Lua::OpCodeUnm && op != Lua::OpCodeLen) ? Lua::FuncState::Exp2RK(fs, e2) : 0;
        int o1 = Lua::FuncState::Exp2RK(fs, e1);
        if (o1 > o2) {
            freeExp(fs, e1);
            freeExp(fs, e2);
        } else {
            freeExp(fs, e2);
            freeExp(fs, e1);
        }
        e1->Info = Lua::FuncState::CodeABC(fs, op, 0, o1, o2);
        e1->k = Lua::ExpDesc::KindRelocatable;
    }
}


static void codeComp(Lua::FuncState *fs, Lua::OpCode op, int cond, Lua::ExpDesc *e1,
                     Lua::ExpDesc *e2) {
    int o1 = Lua::FuncState::Exp2RK(fs, e1);
    int o2 = Lua::FuncState::Exp2RK(fs, e2);
    freeExp(fs, e2);
    freeExp(fs, e1);
    if (cond == 0 && op != Lua::OpCodeEQ) {
        int temp;  /* exchange args to replace by `<' or `<=' */
        temp = o1;
        o1 = o2;
        o2 = temp;  /* o1 <==> o2 */
        cond = 1;
    }
    e1->Info = condJump(fs, op, cond, o1, o2);
    e1->k = Lua::ExpDesc::KindJmp;
}


void Lua::FuncState::Prefix(Lua::FuncState *fs, Lua::UnOpr op, Lua::ExpDesc *e) {
    Lua::ExpDesc e2;
    e2.t = e2.f = NO_JUMP;
    e2.k = Lua::ExpDesc::KindKNum;
    e2.NumberValue = 0;
    switch (op) {
        case Lua::UnOprMinus: {
            if (!isNumeric(e))
                Lua::FuncState::Exp2AnyReg(fs, e);  /* cannot operate on non-numeric constants */
            codeArith(fs, Lua::OpCodeUnm, e, &e2);
            break;
        }
        case Lua::UnOprNot:
            codeNot(fs, e);
            break;
        case Lua::UnOprLen: {
            Lua::FuncState::Exp2AnyReg(fs, e);  /* cannot operate on constants */
            codeArith(fs, Lua::OpCodeLen, e, &e2);
            break;
        }
        default:
            lua_assert(0);
    }
}


void Lua::FuncState::InFix(Lua::FuncState *fs, Lua::BinOpr op, Lua::ExpDesc *v) {
    switch (op) {
        case Lua::BinOprAND: {
            Lua::FuncState::GoIfTrue(fs, v);
            break;
        }
        case Lua::BinOprOR: {
            LuaStateGoIfFalse(fs, v);
            break;
        }
        case Lua::BinOprConcat: {
            Lua::FuncState::Exp2NextReg(fs, v);  /* operand must be on the `stack` */
            break;
        }
        case Lua::BinOprAdd:
        case Lua::BinOprSub:
        case Lua::BinOprMul:
        case Lua::BinOprDiv:
        case Lua::BinOprMod:
        case Lua::BinOprPow: {
            if (!isNumeric(v)) Lua::FuncState::Exp2RK(fs, v);
            break;
        }
        default: {
            Lua::FuncState::Exp2RK(fs, v);
            break;
        }
    }
}


void Lua::FuncState::PosFix(Lua::FuncState *fs, Lua::BinOpr op, Lua::ExpDesc *e1, Lua::ExpDesc *e2) {
    switch (op) {
        case Lua::BinOprAND: {
            lua_assert(e1->t == NO_JUMP);  /* list must be closed */
            Lua::FuncState::DischargeVars(fs, e2);
            Lua::FuncState::Concat(fs, &e2->f, e1->f);
            *e1 = *e2;
            break;
        }
        case Lua::BinOprOR: {
            lua_assert(e1->f == NO_JUMP);  /* list must be closed */
            Lua::FuncState::DischargeVars(fs, e2);
            Lua::FuncState::Concat(fs, &e2->t, e1->t);
            *e1 = *e2;
            break;
        }
        case Lua::BinOprConcat: {
            Lua::FuncState::Exp2Val(fs, e2);
            if (e2->k == Lua::ExpDesc::KindRelocatable && LuaOpCodeGet(LuaFuncStateGetCode(fs, e2)) == Lua::OpCodeConcat) {
                lua_assert(e1->Info == LuaOpCodeGetArgB(LuaFuncStateGetCode(fs, e2)) - 1);
                freeExp(fs, e1);
                LuaOpCodeSetArgB(LuaFuncStateGetCode(fs, e2), e1->Info);
                e1->k = Lua::ExpDesc::KindRelocatable;
                e1->Info = e2->Info;
            } else {
                Lua::FuncState::Exp2NextReg(fs, e2);  /* operand must be on the 'stack' */
                codeArith(fs, Lua::OpCodeConcat, e1, e2);
            }
            break;
        }
        case Lua::BinOprAdd:
            codeArith(fs, Lua::OpCodeAdd, e1, e2);
            break;
        case Lua::BinOprSub:
            codeArith(fs, Lua::OpCodeSub, e1, e2);
            break;
        case Lua::BinOprMul:
            codeArith(fs, Lua::OpCodeMul, e1, e2);
            break;
        case Lua::BinOprDiv:
            codeArith(fs, Lua::OpCodeDiv, e1, e2);
            break;
        case Lua::BinOprMod:
            codeArith(fs, Lua::OpCodeMod, e1, e2);
            break;
        case Lua::BinOprPow:
            codeArith(fs, Lua::OpCodePow, e1, e2);
            break;
        case Lua::BinOprEQ:
            codeComp(fs, Lua::OpCodeEQ, 1, e1, e2);
            break;
        case Lua::BinOprNE:
            codeComp(fs, Lua::OpCodeEQ, 0, e1, e2);
            break;
        case Lua::BinOprLT:
            codeComp(fs, Lua::OpCodeLT, 1, e1, e2);
            break;
        case Lua::BinOprLE:
            codeComp(fs, Lua::OpCodeLE, 1, e1, e2);
            break;
        case Lua::BinOprGT:
            codeComp(fs, Lua::OpCodeLT, 0, e1, e2);
            break;
        case Lua::BinOprGE:
            codeComp(fs, Lua::OpCodeLE, 0, e1, e2);
            break;
        default:
            lua_assert(0);
    }
}


void Lua::FuncState::FixLine(Lua::FuncState *fs, int line) {
    fs->Func->LineInfo[fs->PC - 1] = line;
}


static int LuaFuncStateCode(Lua::FuncState *fs, Lua::Instruction i, int line) {
    Lua::Proto *f = fs->Func;
    dischargeJumpPC(fs);  /* `pc` will change */
    /* put new instruction in code array */
    LuaMemoryGrowVector(fs->L, f->Code, fs->PC, f->CodeCount, Lua::Instruction,
                        Lua::MaxInt, "code size overflow");
    f->Code[fs->PC] = i;
    /* save corresponding line information */
    LuaMemoryGrowVector(fs->L, f->LineInfo, fs->PC, f->LineInfoCount, int,
                        Lua::MaxInt, "code size overflow");
    f->LineInfo[fs->PC] = line;
    return fs->PC++;
}


int Lua::FuncState::CodeABC(Lua::FuncState *fs, Lua::OpCode o, int a, int b, int c) {
    lua_assert(LuaGetOpMode(o) == Lua::OpModeIABC);
    lua_assert(LuaGetBMode(o) != Lua::OpArgN || b == 0);
    lua_assert(LuaGetCMode(o) != Lua::OpArgN || c == 0);
    return LuaFuncStateCode(fs, LuaOpCodeCreateABC(o, a, b, c), fs->Lexer->LastLine);
}


int Lua::FuncState::CodeABx(Lua::FuncState *fs, Lua::OpCode o, int a, unsigned int bc) {
    lua_assert(LuaGetOpMode(o) == Lua::OpModeIABx || LuaGetOpMode(o) == Lua::OpModeIAsBx);
    lua_assert(LuaGetCMode(o) == Lua::OpArgN);
    return LuaFuncStateCode(fs, LuaOpCodeCreateABx(o, a, bc), fs->Lexer->LastLine);
}


void Lua::FuncState::SetList(Lua::FuncState *fs, int base, int nElements, int toStore) {
    int c = (nElements - 1) / LUA_FIELDS_PER_FLUSH + 1;
    int b = (toStore == LUA_MULTRET) ? 0 : toStore;
    lua_assert(toStore != 0);
    if (c <= LUA_CODE_MAX_ARG_C)
        Lua::FuncState::CodeABC(fs, Lua::OpCodeSetList, base, b, c);
    else {
        Lua::FuncState::CodeABC(fs, Lua::OpCodeSetList, base, b, 0);
        LuaFuncStateCode(fs, cast(Lua::Instruction, c), fs->Lexer->LastLine);
    }
    fs->FreeReg = base + 1;  /* free registers with list values */
}

