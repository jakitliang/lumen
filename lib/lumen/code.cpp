/*!
 * @brief Code generator for Lua
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstdlib>

#define LUA_CORE

#include "lumen/code.h"
#include "lumen/debug.h"
#include "lumen/lex.h"
#include "lumen/memory.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/opcodes.h"
#include "lumen/parser.h"

#define hasJumps(e)    ((e)->t != (e)->f)

static inline bool isNumeric(Lumen::ExpDesc *e) {
    return (e->k == Lumen::ExpDesc::KindKNum && e->t == NO_JUMP && e->f == NO_JUMP);
}

void Lumen::FuncState::Nil(Lumen::FuncState *fs, int from, int n) {
    Lumen::Instruction *previous;
    if (fs->PC > fs->LastPC) {  /* no jumps to current position? */
        if (fs->PC == 0) {  /* function start? */
            if (from >= fs->ActiveVarsCount)
                return;  /* positions are already clean */
        } else {
            previous = &fs->Func->Code[fs->PC - 1];
            if (LumenOpCodeGet(*previous) == Lumen::OpCodeLoadNil) {
                int pFrom = LumenOpCodeGetArgA(*previous);
                int pto = LumenOpCodeGetArgB(*previous);
                if (pFrom <= from && from <= pto + 1) {  /* can connect both? */
                    if (from + n - 1 > pto)
                        LumenOpCodeSetArgB(*previous, from + n - 1);
                    return;
                }
            }
        }
    }
    Lumen::FuncState::CodeABC(fs, Lumen::OpCodeLoadNil, from, from + n - 1, 0);  /* else no optimization */
}


int Lumen::FuncState::Jump(Lumen::FuncState *fs) {
    int jpc = fs->JumpPC;  /* save list of jumps to here */
    int j;
    fs->JumpPC = NO_JUMP;
    j = LumenFuncStateCodeAsBx(fs, Lumen::OpCodeJump, 0, NO_JUMP);
    Lumen::FuncState::Concat(fs, &j, jpc);  /* keep them on hold */
    return j;
}

static inline int condJump(Lumen::FuncState *fs, Lumen::OpCode op, int A, int B, int C) {
    Lumen::FuncState::CodeABC(fs, op, A, B, C);
    return Lumen::FuncState::Jump(fs);
}

static inline void fixJump(Lumen::FuncState *fs, int pc, int dest) {
    Lumen::Instruction *jmp = &fs->Func->Code[pc];
    int offset = dest - (pc + 1);
    LumenAssert(dest != NO_JUMP);
    if (abs(offset) > Lumen::Code::sBxMaxArg)
        Lumen::LexState::SyntaxError(fs->Lexer, "control structure too long");
    LumenOpCodeSetArgsBx(*jmp, offset);
}

static inline int getJump(Lumen::FuncState *fs, int pc) {
    int offset = LumenOpCodeGetArgsBx(fs->Func->Code[pc]);
    if (offset == NO_JUMP)  /* point to itself represents end of list */
        return NO_JUMP;  /* end of list */
    else
        return (pc + 1) + offset;  /* turn offset into absolute position */
}


static inline Lumen::Instruction *getJumpControl(Lumen::FuncState *fs, int pc) {
    Lumen::Instruction *pi = &fs->Func->Code[pc];
    if (pc >= 1 && LumenTestTMode(LumenOpCodeGet(*(pi - 1))))
        return pi - 1;
    else
        return pi;
}


/*
** check whether list has any jump that do not produce a value
** (or produce an inverted value)
*/
static inline int needValue(Lumen::FuncState *fs, int list) {
    for (; list != NO_JUMP; list = getJump(fs, list)) {
        Lumen::Instruction i = *getJumpControl(fs, list);
        if (LumenOpCodeGet(i) != Lumen::OpCodeTestTest) return 1;
    }
    return 0;  /* not found */
}


static inline int patchTestReg(Lumen::FuncState *fs, int node, int reg) {
    Lumen::Instruction *i = getJumpControl(fs, node);
    if (LumenOpCodeGet(*i) != Lumen::OpCodeTestTest)
        return 0;  /* cannot patch other instructions */
    if (reg != NO_REG && reg != LumenOpCodeGetArgB(*i))
        LumenOpCodeSetArgA(*i, reg);
    else  /* no register to put value or register already has the value */
        *i = LumenOpCodeCreateABC(Lumen::OpCodeTest, LumenOpCodeGetArgB(*i), 0, LumenOpCodeGetArgC(*i));

    return 1;
}


static inline void removeValues(Lumen::FuncState *fs, int list) {
    for (; list != NO_JUMP; list = getJump(fs, list))
        patchTestReg(fs, list, NO_REG);
}


static inline void patchListAux(Lumen::FuncState *fs, int list, int vTarget, int reg,
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


static inline void dischargeJumpPC(Lumen::FuncState *fs) {
    patchListAux(fs, fs->JumpPC, fs->PC, NO_REG, fs->PC);
    fs->JumpPC = NO_JUMP;
}


void Lumen::FuncState::PatchList(Lumen::FuncState *fs, int list, int target) {
    if (target == fs->PC)
        Lumen::FuncState::PatchToHere(fs, list);
    else {
        LumenAssert(target < fs->PC);
        patchListAux(fs, list, target, NO_REG, target);
    }
}


void Lumen::FuncState::PatchToHere(Lumen::FuncState *fs, int list) {
    Lumen::FuncState::GetLabel(fs);
    Lumen::FuncState::Concat(fs, &fs->JumpPC, list);
}


void Lumen::FuncState::Concat(Lumen::FuncState *fs, int *l1, int l2) {
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

static inline void freeReg(Lumen::FuncState *fs, int reg) {
    if (!LumenOpCodeIsK(reg) && reg >= fs->ActiveVarsCount) {
        fs->FreeReg--;
        LumenAssert(reg == fs->FreeReg);
    }
}


static inline void freeExp(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    if (e->k == Lumen::ExpDesc::KindNonRelocatable)
        freeReg(fs, e->Info);
}


static int addK(Lumen::FuncState *fs, Lumen::Object *k, Lumen::Object *v) {
    Lumen::State *L = fs->L;
    Lumen::Object *idx = Lumen::Table::Set(L, fs->Constants, k);
    Lumen::Proto *f = fs->Func;
    int oldSize = f->KCount;
    if (idx->IsNumber()) {
        LumenAssert(Lumen::RawEqualObject(&fs->Func->K[cast_int(idx->GetNumber())], v));
        return cast_int(idx->GetNumber());
    } else {  /* constant not found; create a new entry */
        idx->SetNumber(cast_num(fs->ConstantsCount));
        LumenMemoryGrowVector(L, f->K, fs->ConstantsCount, f->KCount, Lumen::Object,
                              Lumen::Code::BxMaxArg, "constant table overflow");
        while (oldSize < f->KCount) (&f->K[oldSize++])->SetNil();
        (&f->K[fs->ConstantsCount])->SetObject(L, v);
        L->Barrier(f, v);
        return fs->ConstantsCount++;
    }
}


int Lumen::FuncState::StringK(Lumen::FuncState *fs, Lumen::String *s) {
    Lumen::Object o; // NOLINT
    o.SetString(fs->L, s);
    return addK(fs, &o, &o);
}


int Lumen::FuncState::NumberK(Lumen::FuncState *fs, Lumen::Number r) {
    Lumen::Object o; // NOLINT
    o.SetNumber(r);
    return addK(fs, &o, &o);
}


static inline int boolK(Lumen::FuncState *fs, int b) {
    Lumen::Object o; // NOLINT
    o.SetBool(b);
    return addK(fs, &o, &o);
}


static inline int nilK(Lumen::FuncState *fs) {
    Lumen::Object k, v; // NOLINT
    v.SetNil();
    /* cannot use nil as key; instead use table itself to represent nil */
    k.SetTable(fs->L, fs->Constants);
    return addK(fs, &k, &v);
}


void Lumen::FuncState::SetReturns(Lumen::FuncState *fs, Lumen::ExpDesc *e, int nResults) {
    if (e->k == Lumen::ExpDesc::KindCall) {  /* expression is an open function call? */
        LumenOpCodeSetArgC(LumenFuncStateGetCode(fs, e), nResults + 1);
    } else if (e->k == Lumen::ExpDesc::KindVararg) {
        LumenOpCodeSetArgB(LumenFuncStateGetCode(fs, e), nResults + 1);
        LumenOpCodeSetArgA(LumenFuncStateGetCode(fs, e), fs->FreeReg);
        Lumen::FuncState::ReserveRegs(fs, 1);
    }
}


void Lumen::FuncState::SetOneRet(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    if (e->k == Lumen::ExpDesc::KindCall) {  /* expression is an open function call? */
        e->k = Lumen::ExpDesc::KindNonRelocatable;
        e->Info = LumenOpCodeGetArgA(LumenFuncStateGetCode(fs, e));
    } else if (e->k == Lumen::ExpDesc::KindVararg) {
        LumenOpCodeSetArgB(LumenFuncStateGetCode(fs, e), 2);
        e->k = Lumen::ExpDesc::KindRelocatable;  /* can relocate its simple result */
    }
}


void Lumen::FuncState::DischargeVars(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    switch (e->k) {
        case Lumen::ExpDesc::KindLocal: {
            e->k = Lumen::ExpDesc::KindNonRelocatable;
            break;
        }
        case Lumen::ExpDesc::KindUpValue: {
            e->Info = Lumen::FuncState::CodeABC(fs, Lumen::OpCodeGetUpVal, 0, e->Info, 0);
            e->k = Lumen::ExpDesc::KindRelocatable;
            break;
        }
        case Lumen::ExpDesc::KindGlobal: {
            e->Info = Lumen::FuncState::CodeABx(fs, Lumen::OpCodeGetGlobal, 0, e->Info);
            e->k = Lumen::ExpDesc::KindRelocatable;
            break;
        }
        case Lumen::ExpDesc::KindIndexed: {
            freeReg(fs, e->Aux);
            freeReg(fs, e->Info);
            e->Info = Lumen::FuncState::CodeABC(fs, Lumen::OpCodeGetTable, 0, e->Info, e->Aux);
            e->k = Lumen::ExpDesc::KindRelocatable;
            break;
        }
        case Lumen::ExpDesc::KindVararg:
        case Lumen::ExpDesc::KindCall: {
            Lumen::FuncState::SetOneRet(fs, e);
            break;
        }
        default:
            break;  /* there is one value available (somewhere) */
    }
}


static inline int codeLabel(Lumen::FuncState *fs, int A, int b, int jump) {
    Lumen::FuncState::GetLabel(fs);  /* those instructions may be jump targets */
    return Lumen::FuncState::CodeABC(fs, Lumen::OpCodeLoadBool, A, b, jump);
}


static void discharge2reg(Lumen::FuncState *fs, Lumen::ExpDesc *e, int reg) {
    Lumen::FuncState::DischargeVars(fs, e);
    switch (e->k) {
        case Lumen::ExpDesc::KindNil: {
            Lumen::FuncState::Nil(fs, reg, 1);
            break;
        }
        case Lumen::ExpDesc::KindFalse:
        case Lumen::ExpDesc::KindTrue: {
            Lumen::FuncState::CodeABC(fs, Lumen::OpCodeLoadBool, reg, e->k == Lumen::ExpDesc::KindTrue, 0);
            break;
        }
        case Lumen::ExpDesc::KindK: {
            Lumen::FuncState::CodeABx(fs, Lumen::OpCodeLoadK, reg, e->Info);
            break;
        }
        case Lumen::ExpDesc::KindKNum: {
            Lumen::FuncState::CodeABx(fs, Lumen::OpCodeLoadK, reg, Lumen::FuncState::NumberK(fs, e->NumberValue));
            break;
        }
        case Lumen::ExpDesc::KindRelocatable: {
            Lumen::Instruction *pc = &LumenFuncStateGetCode(fs, e);
            LumenOpCodeSetArgA(*pc, reg);
            break;
        }
        case Lumen::ExpDesc::KindNonRelocatable: {
            if (reg != e->Info)
                Lumen::FuncState::CodeABC(fs, Lumen::OpCodeMove, reg, e->Info, 0);
            break;
        }
        default: {
            LumenAssert(e->k == Lumen::ExpDesc::KindVoid || e->k == Lumen::ExpDesc::KindJmp);
            return;  /* nothing to do... */
        }
    }
    e->Info = reg;
    e->k = Lumen::ExpDesc::KindNonRelocatable;
}


static inline void discharge2AnyReg(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    if (e->k != Lumen::ExpDesc::KindNonRelocatable) {
        Lumen::FuncState::ReserveRegs(fs, 1);
        discharge2reg(fs, e, fs->FreeReg - 1);
    }
}


static void exp2reg(Lumen::FuncState *fs, Lumen::ExpDesc *e, int reg) {
    discharge2reg(fs, e, reg);
    if (e->k == Lumen::ExpDesc::KindJmp)
        Lumen::FuncState::Concat(fs, &e->t, e->Info);  /* put this jump in `t` list */
    if (hasJumps(e)) {
        int final;  /* position after whole expression */
        int p_f = NO_JUMP;  /* position of an eventual LOAD false */
        int p_t = NO_JUMP;  /* position of an eventual LOAD true */
        if (needValue(fs, e->t) || needValue(fs, e->f)) {
            int fj = (e->k == Lumen::ExpDesc::KindJmp) ? NO_JUMP : Lumen::FuncState::Jump(fs);
            p_f = codeLabel(fs, reg, 0, 1);
            p_t = codeLabel(fs, reg, 1, 0);
            Lumen::FuncState::PatchToHere(fs, fj);
        }
        final = Lumen::FuncState::GetLabel(fs);
        patchListAux(fs, e->f, final, reg, p_f);
        patchListAux(fs, e->t, final, reg, p_t);
    }
    e->f = e->t = NO_JUMP;
    e->Info = reg;
    e->k = Lumen::ExpDesc::KindNonRelocatable;
}


void Lumen::FuncState::Exp2NextReg(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    Lumen::FuncState::DischargeVars(fs, e);
    freeExp(fs, e);
    Lumen::FuncState::ReserveRegs(fs, 1);
    exp2reg(fs, e, fs->FreeReg - 1);
}


int Lumen::FuncState::Exp2AnyReg(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    Lumen::FuncState::DischargeVars(fs, e);
    if (e->k == Lumen::ExpDesc::KindNonRelocatable) {
        if (!hasJumps(e)) return e->Info;  /* exp is already in a register */
        if (e->Info >= fs->ActiveVarsCount) {  /* reg. is not a local? */
            exp2reg(fs, e, e->Info);  /* put value on it */
            return e->Info;
        }
    }
    Lumen::FuncState::Exp2NextReg(fs, e);  /* default */
    return e->Info;
}


void Lumen::FuncState::Exp2Val(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    if (hasJumps(e))
        Lumen::FuncState::Exp2AnyReg(fs, e);
    else
        Lumen::FuncState::DischargeVars(fs, e);
}


int Lumen::FuncState::Exp2RK(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    Lumen::FuncState::Exp2Val(fs, e);
    switch (e->k) {
        case Lumen::ExpDesc::KindKNum:
        case Lumen::ExpDesc::KindTrue:
        case Lumen::ExpDesc::KindFalse:
        case Lumen::ExpDesc::KindNil: {
            if (fs->ConstantsCount <= Lumen::Code::MaxIndexRK) {  /* constant fit in RK operand? */
                e->Info = (e->k == Lumen::ExpDesc::KindNil) ? nilK(fs) :
                          (e->k == Lumen::ExpDesc::KindKNum) ? Lumen::FuncState::NumberK(fs, e->NumberValue) :
                          boolK(fs, (e->k == Lumen::ExpDesc::KindTrue));
                e->k = Lumen::ExpDesc::KindK;
                return LumenOpCodeRKAsk(e->Info);
            } else break;
        }
        case Lumen::ExpDesc::KindK: {
            if (e->Info <= Lumen::Code::MaxIndexRK)  /* constant fit in argC? */
                return LumenOpCodeRKAsk(e->Info);
            else break;
        }
        default:
            break;
    }
    /* not a constant in the right range: put it in a register */
    return Lumen::FuncState::Exp2AnyReg(fs, e);
}


void Lumen::FuncState::StoreVar(Lumen::FuncState *fs, Lumen::ExpDesc *var, Lumen::ExpDesc *ex) {
    switch (var->k) {
        case Lumen::ExpDesc::KindLocal: {
            freeExp(fs, ex);
            exp2reg(fs, ex, var->Info);
            return;
        }
        case Lumen::ExpDesc::KindUpValue: {
            int e = Lumen::FuncState::Exp2AnyReg(fs, ex);
            Lumen::FuncState::CodeABC(fs, Lumen::OpCodeSetUpVal, e, var->Info, 0);
            break;
        }
        case Lumen::ExpDesc::KindGlobal: {
            int e = Lumen::FuncState::Exp2AnyReg(fs, ex);
            Lumen::FuncState::CodeABx(fs, Lumen::OpCodeSetGlobal, e, var->Info);
            break;
        }
        case Lumen::ExpDesc::KindIndexed: {
            int e = Lumen::FuncState::Exp2RK(fs, ex);
            Lumen::FuncState::CodeABC(fs, Lumen::OpCodeSetTable, var->Info, var->Aux, e);
            break;
        }
        default: {
            LumenAssert(0);  /* invalid var kind to store */
            break;
        }
    }
    freeExp(fs, ex);
}


void Lumen::FuncState::Self(Lumen::FuncState *fs, Lumen::ExpDesc *e, Lumen::ExpDesc *key) {
    int func;
    Lumen::FuncState::Exp2AnyReg(fs, e);
    freeExp(fs, e);
    func = fs->FreeReg;
    Lumen::FuncState::ReserveRegs(fs, 2);
    Lumen::FuncState::CodeABC(fs, Lumen::OpCodeSelf, func, e->Info, Lumen::FuncState::Exp2RK(fs, key));
    freeExp(fs, key);
    e->Info = func;
    e->k = Lumen::ExpDesc::KindNonRelocatable;
}


static inline void invertJump(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    Lumen::Instruction *pc = getJumpControl(fs, e->Info);
    LumenAssert(LumenTestTMode(LumenOpCodeGet(*pc)) && LumenOpCodeGet(*pc) != Lumen::OpCodeTestTest &&
                LumenOpCodeGet(*pc) != Lumen::OpCodeTest);
    LumenOpCodeSetArgA(*pc, !(LumenOpCodeGetArgA(*pc)));
}


static int jumpOnCond(Lumen::FuncState *fs, Lumen::ExpDesc *e, int cond) {
    if (e->k == Lumen::ExpDesc::KindRelocatable) {
        Lumen::Instruction ie = LumenFuncStateGetCode(fs, e);
        if (LumenOpCodeGet(ie) == Lumen::OpCodeNot) {
            fs->PC--;  /* remove previous Lumen::OpCodeNot */
            return condJump(fs, Lumen::OpCodeTest, LumenOpCodeGetArgB(ie), 0, !cond);
        }
        /* else go through */
    }
    discharge2AnyReg(fs, e);
    freeExp(fs, e);
    return condJump(fs, Lumen::OpCodeTestTest, NO_REG, e->Info, cond);
}


void Lumen::FuncState::GoIfTrue(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    int pc;  /* pc of last jump */
    Lumen::FuncState::DischargeVars(fs, e);
    switch (e->k) {
        case Lumen::ExpDesc::KindK:
        case Lumen::ExpDesc::KindKNum:
        case Lumen::ExpDesc::KindTrue: {
            pc = NO_JUMP;  /* always true; do nothing */
            break;
        }
        case Lumen::ExpDesc::KindJmp: {
            invertJump(fs, e);
            pc = e->Info;
            break;
        }
        default: {
            pc = jumpOnCond(fs, e, 0);
            break;
        }
    }
    Lumen::FuncState::Concat(fs, &e->f, pc);  /* insert last jump in `f` list */
    Lumen::FuncState::PatchToHere(fs, e->t);
    e->t = NO_JUMP;
}


static void LuaStateGoIfFalse(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    int pc;  /* pc of last jump */
    Lumen::FuncState::DischargeVars(fs, e);
    switch (e->k) {
        case Lumen::ExpDesc::KindNil:
        case Lumen::ExpDesc::KindFalse: {
            pc = NO_JUMP;  /* always false; do nothing */
            break;
        }
        case Lumen::ExpDesc::KindJmp: {
            pc = e->Info;
            break;
        }
        default: {
            pc = jumpOnCond(fs, e, 1);
            break;
        }
    }
    Lumen::FuncState::Concat(fs, &e->t, pc);  /* insert last jump in `t` list */
    Lumen::FuncState::PatchToHere(fs, e->f);
    e->f = NO_JUMP;
}


static void codeNot(Lumen::FuncState *fs, Lumen::ExpDesc *e) {
    Lumen::FuncState::DischargeVars(fs, e);
    switch (e->k) {
        case Lumen::ExpDesc::KindNil:
        case Lumen::ExpDesc::KindFalse: {
            e->k = Lumen::ExpDesc::KindTrue;
            break;
        }
        case Lumen::ExpDesc::KindK:
        case Lumen::ExpDesc::KindKNum:
        case Lumen::ExpDesc::KindTrue: {
            e->k = Lumen::ExpDesc::KindFalse;
            break;
        }
        case Lumen::ExpDesc::KindJmp: {
            invertJump(fs, e);
            break;
        }
        case Lumen::ExpDesc::KindRelocatable:
        case Lumen::ExpDesc::KindNonRelocatable: {
            discharge2AnyReg(fs, e);
            freeExp(fs, e);
            e->Info = Lumen::FuncState::CodeABC(fs, Lumen::OpCodeNot, 0, e->Info, 0);
            e->k = Lumen::ExpDesc::KindRelocatable;
            break;
        }
        default: {
            LumenAssert(0);  /* cannot happen */
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

static int constFolding(Lumen::OpCode op, Lumen::ExpDesc *e1, Lumen::ExpDesc *e2) {
    Lumen::Number v1, v2, r;
    if (!isNumeric(e1) || !isNumeric(e2)) return 0;
    v1 = e1->NumberValue;
    v2 = e2->NumberValue;
    switch (op) {
        case Lumen::OpCodeAdd:
            r = LumenNumAdd(v1, v2);
            break;
        case Lumen::OpCodeSub:
            r = LumenNumSub(v1, v2);
            break;
        case Lumen::OpCodeMul:
            r = LumenNumMul(v1, v2);
            break;
        case Lumen::OpCodeDiv:
            if (v2 == 0) return 0;  /* do not attempt to divide by 0 */
            r = LumenNumDiv(v1, v2);
            break;
        case Lumen::OpCodeMod:
            if (v2 == 0) return 0;  /* do not attempt to divide by 0 */
            r = LumenNumMod(v1, v2);
            break;
        case Lumen::OpCodePow:
            r = LumenNumPow(v1, v2);
            break;
        case Lumen::OpCodeUnm:
            r = LumenNumUnm(v1);
            break;
        case Lumen::OpCodeLen:
            return 0;  /* no constant folding for 'len' */
        default:
            LumenAssert(0);
            r = 0;
            break;
    }
    if (LumenNumIsNAN(r)) return 0;  /* do not attempt to produce NaN */
    e1->NumberValue = r;
    return 1;
}


static void codeArith(Lumen::FuncState *fs, Lumen::OpCode op, Lumen::ExpDesc *e1, Lumen::ExpDesc *e2) {
    if (constFolding(op, e1, e2))
        return;
    else {
        int o2 = (op != Lumen::OpCodeUnm && op != Lumen::OpCodeLen) ? Lumen::FuncState::Exp2RK(fs, e2) : 0;
        int o1 = Lumen::FuncState::Exp2RK(fs, e1);
        if (o1 > o2) {
            freeExp(fs, e1);
            freeExp(fs, e2);
        } else {
            freeExp(fs, e2);
            freeExp(fs, e1);
        }
        e1->Info = Lumen::FuncState::CodeABC(fs, op, 0, o1, o2);
        e1->k = Lumen::ExpDesc::KindRelocatable;
    }
}


static void codeComp(Lumen::FuncState *fs, Lumen::OpCode op, int cond, Lumen::ExpDesc *e1,
                     Lumen::ExpDesc *e2) {
    int o1 = Lumen::FuncState::Exp2RK(fs, e1);
    int o2 = Lumen::FuncState::Exp2RK(fs, e2);
    freeExp(fs, e2);
    freeExp(fs, e1);
    if (cond == 0 && op != Lumen::OpCodeEQ) {
        int temp;  /* exchange args to replace by `<' or `<=' */
        temp = o1;
        o1 = o2;
        o2 = temp;  /* o1 <==> o2 */
        cond = 1;
    }
    e1->Info = condJump(fs, op, cond, o1, o2);
    e1->k = Lumen::ExpDesc::KindJmp;
}


void Lumen::FuncState::Prefix(Lumen::FuncState *fs, Lumen::UnOpr op, Lumen::ExpDesc *e) {
    Lumen::ExpDesc e2; // NOLINT
    e2.t = e2.f = NO_JUMP;
    e2.k = Lumen::ExpDesc::KindKNum;
    e2.NumberValue = 0;
    switch (op) {
        case Lumen::UnOprMinus: {
            if (!isNumeric(e))
                Lumen::FuncState::Exp2AnyReg(fs, e);  /* cannot operate on non-numeric constants */
            codeArith(fs, Lumen::OpCodeUnm, e, &e2);
            break;
        }
        case Lumen::UnOprNot:
            codeNot(fs, e);
            break;
        case Lumen::UnOprLen: {
            Lumen::FuncState::Exp2AnyReg(fs, e);  /* cannot operate on constants */
            codeArith(fs, Lumen::OpCodeLen, e, &e2);
            break;
        }
        default:
            LumenAssert(0);
    }
}


void Lumen::FuncState::InFix(Lumen::FuncState *fs, Lumen::BinOpr op, Lumen::ExpDesc *v) {
    switch (op) {
        case Lumen::BinOprAND: {
            Lumen::FuncState::GoIfTrue(fs, v);
            break;
        }
        case Lumen::BinOprOR: {
            LuaStateGoIfFalse(fs, v);
            break;
        }
        case Lumen::BinOprConcat: {
            Lumen::FuncState::Exp2NextReg(fs, v);  /* operand must be on the `stack` */
            break;
        }
        case Lumen::BinOprAdd:
        case Lumen::BinOprSub:
        case Lumen::BinOprMul:
        case Lumen::BinOprDiv:
        case Lumen::BinOprMod:
        case Lumen::BinOprPow: {
            if (!isNumeric(v)) Lumen::FuncState::Exp2RK(fs, v);
            break;
        }
        default: {
            Lumen::FuncState::Exp2RK(fs, v);
            break;
        }
    }
}


void Lumen::FuncState::PosFix(Lumen::FuncState *fs, Lumen::BinOpr op, Lumen::ExpDesc *e1, Lumen::ExpDesc *e2) {
    switch (op) {
        case Lumen::BinOprAND: {
            LumenAssert(e1->t == NO_JUMP);  /* list must be closed */
            Lumen::FuncState::DischargeVars(fs, e2);
            Lumen::FuncState::Concat(fs, &e2->f, e1->f);
            *e1 = *e2;
            break;
        }
        case Lumen::BinOprOR: {
            LumenAssert(e1->f == NO_JUMP);  /* list must be closed */
            Lumen::FuncState::DischargeVars(fs, e2);
            Lumen::FuncState::Concat(fs, &e2->t, e1->t);
            *e1 = *e2;
            break;
        }
        case Lumen::BinOprConcat: {
            Lumen::FuncState::Exp2Val(fs, e2);
            if (e2->k == Lumen::ExpDesc::KindRelocatable &&
                LumenOpCodeGet(LumenFuncStateGetCode(fs, e2)) == Lumen::OpCodeConcat) {
                LumenAssert(e1->Info == LumenOpCodeGetArgB(LumenFuncStateGetCode(fs, e2)) - 1);
                freeExp(fs, e1);
                LumenOpCodeSetArgB(LumenFuncStateGetCode(fs, e2), e1->Info);
                e1->k = Lumen::ExpDesc::KindRelocatable;
                e1->Info = e2->Info;
            } else {
                Lumen::FuncState::Exp2NextReg(fs, e2);  /* operand must be on the 'stack' */
                codeArith(fs, Lumen::OpCodeConcat, e1, e2);
            }
            break;
        }
        case Lumen::BinOprAdd:
            codeArith(fs, Lumen::OpCodeAdd, e1, e2);
            break;
        case Lumen::BinOprSub:
            codeArith(fs, Lumen::OpCodeSub, e1, e2);
            break;
        case Lumen::BinOprMul:
            codeArith(fs, Lumen::OpCodeMul, e1, e2);
            break;
        case Lumen::BinOprDiv:
            codeArith(fs, Lumen::OpCodeDiv, e1, e2);
            break;
        case Lumen::BinOprMod:
            codeArith(fs, Lumen::OpCodeMod, e1, e2);
            break;
        case Lumen::BinOprPow:
            codeArith(fs, Lumen::OpCodePow, e1, e2);
            break;
        case Lumen::BinOprEQ:
            codeComp(fs, Lumen::OpCodeEQ, 1, e1, e2);
            break;
        case Lumen::BinOprNE:
            codeComp(fs, Lumen::OpCodeEQ, 0, e1, e2);
            break;
        case Lumen::BinOprLT:
            codeComp(fs, Lumen::OpCodeLT, 1, e1, e2);
            break;
        case Lumen::BinOprLE:
            codeComp(fs, Lumen::OpCodeLE, 1, e1, e2);
            break;
        case Lumen::BinOprGT:
            codeComp(fs, Lumen::OpCodeLT, 0, e1, e2);
            break;
        case Lumen::BinOprGE:
            codeComp(fs, Lumen::OpCodeLE, 0, e1, e2);
            break;
        default:
            LumenAssert(0);
    }
}

static int LuaFuncStateCode(Lumen::FuncState *fs, Lumen::Instruction i, int line) {
    Lumen::Proto *f = fs->Func;
    dischargeJumpPC(fs);  /* `pc` will change */
    /* put new instruction in code array */
    LumenMemoryGrowVector(fs->L, f->Code, fs->PC, f->CodeCount, Lumen::Instruction,
                          Lumen::MaxInt, "code size overflow");
    f->Code[fs->PC] = i;
    /* save corresponding line information */
    LumenMemoryGrowVector(fs->L, f->LineInfo, fs->PC, f->LineInfoCount, int,
                          Lumen::MaxInt, "code size overflow");
    f->LineInfo[fs->PC] = line;
    return fs->PC++;
}


int Lumen::FuncState::CodeABC(Lumen::FuncState *fs, Lumen::OpCode o, int a, int b, int c) {
    LumenAssert(LumenGetOpMode(o) == Lumen::OpModeIABC);
    LumenAssert(LumenGetBMode(o) != Lumen::OpArgN || b == 0);
    LumenAssert(LumenGetCMode(o) != Lumen::OpArgN || c == 0);
    return LuaFuncStateCode(fs, LumenOpCodeCreateABC(o, a, b, c), fs->Lexer->LastLine);
}


int Lumen::FuncState::CodeABx(Lumen::FuncState *fs, Lumen::OpCode o, int a, unsigned int bc) {
    LumenAssert(LumenGetOpMode(o) == Lumen::OpModeIABx || LumenGetOpMode(o) == Lumen::OpModeIAsBx);
    LumenAssert(LumenGetCMode(o) == Lumen::OpArgN);
    return LuaFuncStateCode(fs, LumenOpCodeCreateABx(o, a, bc), fs->Lexer->LastLine);
}


void Lumen::FuncState::SetList(Lumen::FuncState *fs, int base, int nElements, int toStore) {
    int c = (nElements - 1) / LUA_FIELDS_PER_FLUSH + 1;
    int b = (toStore == Lumen::RetMul) ? 0 : toStore;
    LumenAssert(toStore != 0);
    if (c <= Lumen::Code::CMaxArg)
        Lumen::FuncState::CodeABC(fs, Lumen::OpCodeSetList, base, b, c);
    else {
        Lumen::FuncState::CodeABC(fs, Lumen::OpCodeSetList, base, b, 0);
        LuaFuncStateCode(fs, cast(Lumen::Instruction, c), fs->Lexer->LastLine);
    }
    fs->FreeReg = base + 1;  /* free registers with list values */
}

