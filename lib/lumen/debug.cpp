/*!
 * @brief Debug Interface
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstdarg>
#include <cstring>

#define LUA_CORE

#include "lumen/code.h"
#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/opcodes.h"
#include "lumen/state.h"
#include "lumen/string.h"
#include "lumen/table.h"
#include "lumen/tm.h"
#include "lumen/vm.h"


static const char *getFuncName(Lumen::State *L, Lumen::CallInfo *ci, const char **name);

static int currentPC(Lumen::State *L, Lumen::CallInfo *ci) {
    if (!ci->IsFunctionOfLua()) return -1;  /* function is not a Lua function? */
    if (ci == L->CallInfo)
        ci->SavedPC = L->SavedPC;
    return LumenDebugPCRel(ci->SavedPC, ci->GetFunction()->AsLua.Func);
}

static int currentLine(Lumen::State *L, Lumen::CallInfo *ci) {
    int pc = currentPC(L, ci);
    if (pc < 0)
        return -1;  /* only active lua functions have current-line information */
    else
        return LumenDebugGetLine(ci->GetFunction()->AsLua.Func, pc);
}

static inline Lumen::Proto *getLuaProto(Lumen::CallInfo *ci) {
    return (ci->IsFunctionOfLua() ? ci->GetFunction()->AsLua.Func : nullptr);
}

const char *Lumen::State::FindLocal(Lumen::CallInfo *ci, int n) {
    const char *name;
    Lumen::Proto *fp = getLuaProto(ci);
    if (fp && (name = Lumen::Proto::GetLocalName(fp, n, currentPC(this, ci))) != nullptr)
        return name;  /* is a local variable in a Lua function */
    else {
        Lumen::Value limit = (ci == CallInfo) ? Top : (ci + 1)->Func;
        if (limit - ci->Base >= n && n > 0)  /* is 'n' inside 'ci' stack? */
            return "(*temporary)";
        else
            return nullptr;
    }
}


static inline void funcInfo(Lumen::DebugInfo *ar, Lumen::Closure *cl) {
    if (cl->AsC.IsC) {
        ar->Source = "=[C]";
        ar->LineDefined = -1;
        ar->LastLineDefined = -1;
        ar->Space = "C";
    } else {
        ar->Source = cl->AsLua.Func->Source->CString();
        ar->LineDefined = cl->AsLua.Func->LineDefined;
        ar->LastLineDefined = cl->AsLua.Func->LastLineDefined;
        ar->Space = (ar->LineDefined == 0) ? "main" : "Lua";
    }
    Lumen::ChunkId(ar->SourceHint, ar->Source, LUA_IDSIZE);
}


static inline void infoTailCall(Lumen::DebugInfo *ar) {
    ar->Name = ar->NameSpace = "";
    ar->Space = "tail";
    ar->LastLineDefined = ar->LineDefined = ar->CurrentLine = -1;
    ar->Source = "=(tail call)";
    Lumen::ChunkId(ar->SourceHint, ar->Source, LUA_IDSIZE);
    ar->NUpValues = 0;
}


void Lumen::Debug::CollectValidLines(Lumen::State *L, Lumen::Closure *f) {
    if (f == nullptr || f->AsC.IsC) {
        L->Top->SetNil();
    } else {
        Lumen::Table *t = Lumen::Table::New(L, 0, 0);
        int *lineinfo = f->AsLua.Func->LineInfo;
        int i;
        for (i = 0; i < f->AsLua.Func->LineInfoCount; i++)
            Lumen::Table::SetNum(L, t, lineinfo[i])->SetBool(1);
        L->Top->SetTable(L,  t);
    }
    LumenIncrTop(L);
}


int Lumen::Debug::GetInfo(Lumen::State *L, const char *what, Lumen::DebugInfo *ar,
                          Lumen::Closure *f, Lumen::CallInfo *ci) {
    int status = 1;
    if (f == nullptr) {
        infoTailCall(ar);
        return status;
    }
    for (; *what; what++) {
        switch (*what) {
            case 'S': {
                funcInfo(ar, f);
                break;
            }
            case 'l': {
                ar->CurrentLine = (ci) ? currentLine(L, ci) : -1;
                break;
            }
            case 'u': {
                ar->NUpValues = f->AsC.NUpValues;
                break;
            }
            case 'n': {
                ar->NameSpace = (ci) ? getFuncName(L, ci, &ar->Name) : nullptr;
                if (ar->NameSpace == nullptr) {
                    ar->NameSpace = "";  /* not found */
                    ar->Name = nullptr;
                }
                break;
            }
            case 'L':
            case 'f':  /* handled by lua_getinfo */
                break;
            default:
                status = 0;  /* invalid option */
        }
    }
    return status;
}


/*
** {======================================================
** Symbolic Execution and code checker
** =======================================================
*/

#define check(x)             do { if (!(x)) return 0; } while (0)

#define checkJump(pt, pc)    check(0 <= pc && pc < pt->CodeCount)

#define checkReg(pt, reg)    check((reg) < (pt)->MaxStackSize)


static inline int preCheck(const Lumen::Proto *pt) {
    check(pt->MaxStackSize <= Lumen::MaxStack);
    check(pt->NUmParams + (pt->IsVararg & Lumen::Proto::VarargHasArg) <= pt->MaxStackSize);
    check(!(pt->IsVararg & Lumen::Proto::VarargIsNeedsArg) ||
          (pt->IsVararg & Lumen::Proto::VarargHasArg));
    check(pt->UpValuesCount <= pt->NUpValues);
    check(pt->LineInfoCount == pt->CodeCount || pt->LineInfoCount == 0);
    check(pt->CodeCount > 0 && LumenOpCodeGet(pt->Code[pt->CodeCount - 1]) == Lumen::OpCodeReturn);
    return 1;
}


#define checkOpenOP(pt, pc)    Lumen::Debug::CheckOpenOP((pt)->Code[(pc)+1])

int Lumen::Debug::CheckOpenOP(Lumen::Instruction i) {
    switch (LumenOpCodeGet(i)) {
        case Lumen::OpCodeCall:
        case Lumen::OpCodeTailCall:
        case Lumen::OpCodeReturn:
        case Lumen::OpCodeSetList: {
            check(LumenOpCodeGetArgB(i) == 0);
            return 1;
        }
        default:
            return 0;  /* invalid instruction after an open call */
    }
}


static int checkArgMode(const Lumen::Proto *pt, int r, Lumen::OpArg mode) {
    switch (mode) {
        case Lumen::OpArgN:
            check(r == 0);
            break;
        case Lumen::OpArgU:
            break;
        case Lumen::OpArgR:
            checkReg(pt, r);
            break;
        case Lumen::OpArgK:
            check(LumenOpCodeIsK(r) ? LumenOpCodeIndexK(r) < pt->KCount : r < pt->MaxStackSize);
            break;
    }
    return 1;
}


static Lumen::Instruction symbolExec(const Lumen::Proto *pt, int lastPC, int reg) {
    int pc;
    int last;  /* stores position of last instruction that changed `reg` */
    last = pt->CodeCount - 1;  /* points to final return (a `neutral` instruction) */
    check(preCheck(pt));
    for (pc = 0; pc < lastPC; pc++) {
        Lumen::Instruction i = pt->Code[pc];
        Lumen::OpCode op = LumenOpCodeGet(i);
        int a = LumenOpCodeGetArgA(i);
        int b = 0;
        int c = 0;
        check(op < Lumen::OpCodeCount);
        checkReg(pt, a);
        switch (LumenGetOpMode(op)) {
            case Lumen::OpModeIABC: {
                b = LumenOpCodeGetArgB(i);
                c = LumenOpCodeGetArgC(i);
                check(checkArgMode(pt, b, LumenGetBMode(op)));
                check(checkArgMode(pt, c, LumenGetCMode(op)));
                break;
            }
            case Lumen::OpModeIABx: {
                b = LumenOpCodeGetArgBx(i);
                if (LumenGetBMode(op) == Lumen::OpArgK) check(b < pt->KCount);
                break;
            }
            case Lumen::OpModeIAsBx: {
                b = LumenOpCodeGetArgsBx(i);
                if (LumenGetBMode(op) == Lumen::OpArgR) {
                    int dest = pc + 1 + b;
                    check(0 <= dest && dest < pt->CodeCount);
                    if (dest > 0) {
                        int j;
                        /* check that it does not jump to a setlist count; this
                           is tricky, because the count from a previous setlist may
                           have the same value of an invalid setlist; so, we must
                           go all the way back to the first of them (if any) */
                        for (j = 0; j < dest; j++) {
                            Lumen::Instruction d = pt->Code[dest - 1 - j];
                            if (!(LumenOpCodeGet(d) == Lumen::OpCodeSetList && LumenOpCodeGetArgC(d) == 0)) break;
                        }
                        /* if 'j' is even, previous value is not a setlist (even if
                           it looks like one) */
                        check((j & 1) == 0);
                    }
                }
                break;
            }
        }
        if (LumenTestAMode(op)) {
            if (a == reg) last = pc;  /* change register `a' */
        }
        if (LumenTestTMode(op)) {
            check(pc + 2 < pt->CodeCount);  /* check skip */
            check(LumenOpCodeGet(pt->Code[pc + 1]) == Lumen::OpCodeJump);
        }
        switch (op) {
            case Lumen::OpCodeLoadBool: {
                if (c == 1) {  /* does it jump? */
                    check(pc + 2 < pt->CodeCount);  /* check its jump */
                    check(LumenOpCodeGet(pt->Code[pc + 1]) != Lumen::OpCodeSetList ||
                          LumenOpCodeGetArgC(pt->Code[pc + 1]) != 0);
                }
                break;
            }
            case Lumen::OpCodeLoadNil: {
                if (a <= reg && reg <= b)
                    last = pc;  /* set registers from `a' to `b' */
                break;
            }
            case Lumen::OpCodeGetUpVal:
            case Lumen::OpCodeSetUpVal: {
                check(b < pt->NUpValues);
                break;
            }
            case Lumen::OpCodeGetGlobal:
            case Lumen::OpCodeSetGlobal: {
                check((&pt->K[b])->IsString());
                break;
            }
            case Lumen::OpCodeSelf: {
                checkReg(pt, a + 1);
                if (reg == a + 1) last = pc;
                break;
            }
            case Lumen::OpCodeConcat: {
                check(b < c);  /* at least two operands */
                break;
            }
            case Lumen::OpCodeTForLoop: {
                check(c >= 1);  /* at least one result (control variable) */
                checkReg(pt, a + 2 + c);  /* space for results */
                if (reg >= a + 2) last = pc;  /* affect all regs above its base */
                break;
            }
            case Lumen::OpCodeForLoop:
            case Lumen::OpCodeForPrep:
                checkReg(pt, a + 3);
                /* go through */
            case Lumen::OpCodeJump: {
                int dest = pc + 1 + b;
                /* not full check and jump is forward and do not skip `lastPC`? */
                if (reg != NO_REG && pc < dest && dest <= lastPC)
                    pc += b;  /* do the jump */
                break;
            }
            case Lumen::OpCodeCall:
            case Lumen::OpCodeTailCall: {
                if (b != 0) {
                    checkReg(pt, a + b - 1);
                }
                c--;  /* c = num. returns */
                if (c == Lumen::RetMul) {
                    check(checkOpenOP(pt, pc));
                } else if (c != 0)
                    checkReg(pt, a + c - 1);
                if (reg >= a) last = pc;  /* affect all registers above base */
                break;
            }
            case Lumen::OpCodeReturn: {
                b--;  /* b = num. returns */
                if (b > 0) checkReg(pt, a + b - 1);
                break;
            }
            case Lumen::OpCodeSetList: {
                if (b > 0) checkReg(pt, a + b);
                if (c == 0) {
                    pc++;
                    check(pc < pt->CodeCount - 1);
                }
                break;
            }
            case Lumen::OpCodeClosure: {
                int nup, j;
                check(b < pt->SubProtoCount);
                nup = pt->SubProto[b]->NUpValues;
                check(pc + nup < pt->CodeCount);
                for (j = 1; j <= nup; j++) {
                    Lumen::OpCode op1 = LumenOpCodeGet(pt->Code[pc + j]);
                    check(op1 == Lumen::OpCodeGetUpVal || op1 == Lumen::OpCodeMove);
                }
                if (reg != NO_REG)  /* tracing? */
                    pc += nup;  /* do not 'execute' these pseudo-instructions */
                break;
            }
            case Lumen::OpCodeVararg: {
                check((pt->IsVararg & Lumen::Proto::VarargIsVararg) &&
                      !(pt->IsVararg & Lumen::Proto::VarargIsNeedsArg));
                b--;
                if (b == Lumen::RetMul) check(checkOpenOP(pt, pc));
                checkReg(pt, a + b - 1);
                break;
            }
            default:
                break;
        }
    }
    return pt->Code[last];
}

#undef check
#undef checkJump
#undef checkReg

/* }====================================================== */


int Lumen::Debug::CheckCode(const Lumen::Proto *pt) {
    return (symbolExec(pt, pt->CodeCount, NO_REG) != 0);
}

static inline const char *kName(Lumen::Proto *p, int c) {
    if (LumenOpCodeIsK(c) && (&p->K[LumenOpCodeIndexK(c)])->IsString())
        return (&p->K[LumenOpCodeIndexK(c)])->ToCString();
    else
        return "?";
}

static const char *getObjName(Lumen::State *L, Lumen::CallInfo *ci, int stackPos,
                              const char **name) {
    if (ci->IsFunctionOfLua()) {  /* a Lua function? */
        Lumen::Proto *p = ci->GetFunction()->AsLua.Func;
        int pc = currentPC(L, ci);
        Lumen::Instruction i;
        *name = Lumen::Proto::GetLocalName(p, stackPos + 1, pc);
        if (*name)  /* is a local? */
            return "local";
        i = symbolExec(p, pc, stackPos);  /* try symbolic execution */
        LumenAssert(pc != -1);
        switch (LumenOpCodeGet(i)) {
            case Lumen::OpCodeGetGlobal: {
                int g = LumenOpCodeGetArgBx(i);  /* global index */
                LumenAssert((&p->K[g])->IsString());
                *name = (&p->K[g])->ToCString();
                return "global";
            }
            case Lumen::OpCodeMove: {
                int a = LumenOpCodeGetArgA(i);
                int b = LumenOpCodeGetArgB(i);  /* move from `b` to `a` */
                if (b < a)
                    return getObjName(L, ci, b, name);  /* get name for `b` */
                break;
            }
            case Lumen::OpCodeGetTable: {
                int k = LumenOpCodeGetArgC(i);  /* key index */
                *name = kName(p, k);
                return "field";
            }
            case Lumen::OpCodeGetUpVal: {
                int u = LumenOpCodeGetArgB(i);  /* upvalue index */
                *name = p->UpValues ? p->UpValues[u]->CString() : "?";
                return "upvalue";
            }
            case Lumen::OpCodeSelf: {
                int k = LumenOpCodeGetArgC(i);  /* key index */
                *name = kName(p, k);
                return "method";
            }
            default:
                break;
        }
    }
    return nullptr;  /* no useful name found */
}


static const char *getFuncName(Lumen::State *L, Lumen::CallInfo *ci, const char **name) {
    Lumen::Instruction i;
    if ((ci->IsFunctionOfLua() && ci->NTailCalls > 0) || !(ci - 1)->IsFunctionOfLua())
        return nullptr;  /* calling function is not Lua (or is unknown) */
    ci--;  /* calling function */
    i = ci->GetFunction()->AsLua.Func->Code[currentPC(L, ci)];
    if (LumenOpCodeGet(i) == Lumen::OpCodeCall || LumenOpCodeGet(i) == Lumen::OpCodeTailCall ||
        LumenOpCodeGet(i) == Lumen::OpCodeTForLoop)
        return getObjName(L, ci, LumenOpCodeGetArgA(i), name);
    else
        return nullptr;  /* no useful name can be found */
}


/* only ANSI way to check whether a pointer points to an array */
static int isInStack(Lumen::CallInfo *ci, const Lumen::Object *o) {
    Lumen::Value p;
    for (p = ci->Base; p < ci->Top; p++)
        if (o == p) return 1;
    return 0;
}


void Lumen::Debug::TypeError(Lumen::State *L, const Lumen::Object *o, const char *op) {
    const char *name = nullptr;
    const char *t = Lumen::MetaMethod::TypeNames[(o)->Type];
    const char *kind = (isInStack(L->CallInfo, o)) ?
                       getObjName(L, L->CallInfo, cast_int(o - L->Base), &name) :
                       nullptr;
    if (kind)
        Lumen::Debug::RunError(L, "attempt to %s %s " LUA_QS " (a %s value)",
                               op, kind, name, t);
    else
        Lumen::Debug::RunError(L, "attempt to %s a %s value", op, t);
}


void Lumen::Debug::ConcatError(Lumen::State *L, Lumen::Value p1, Lumen::Value p2) {
    if (p1->IsString() || p1->IsNumber()) p1 = p2;
    LumenAssert(!p1->IsString() && !p1->IsNumber());
    Lumen::Debug::TypeError(L, p1, "concatenate");
}


void Lumen::Debug::ArithError(Lumen::State *L, const Lumen::Object *p1, const Lumen::Object *p2) {
    Lumen::Object temp; // NOLINT
    if (Lumen::VM::ToNumber(p1, &temp) == nullptr)
        p2 = p1;  /* first operand is wrong */
    Lumen::Debug::TypeError(L, p2, "perform arithmetic on");
}


int Lumen::Debug::OrderError(Lumen::State *L, const Lumen::Object *p1, const Lumen::Object *p2) {
    const char *t1 = Lumen::MetaMethod::TypeNames[p1->Type];
    const char *t2 = Lumen::MetaMethod::TypeNames[p2->Type];
    if (t1[2] == t2[2])
        Lumen::Debug::RunError(L, "attempt to compare two %s values", t1);
    else
        Lumen::Debug::RunError(L, "attempt to compare %s with %s", t1, t2);
    return 0;
}


static void addInfo(Lumen::State *L, const char *msg) {
    Lumen::CallInfo *ci = L->CallInfo;
    if (ci->IsFunctionOfLua()) {  /* is Lua code? */
        char buff[LUA_IDSIZE];  /* add file:line information */
        int line = currentLine(L, ci);
        Lumen::ChunkId(buff, getLuaProto(ci)->Source->CString(), LUA_IDSIZE);
        Lumen::PushFString(L, "%s:%d: %s", buff, line, msg);
    }
}


void Lumen::Debug::ErrorMessage(Lumen::State *L) {
    if (L->ErrFunc != 0) {  /* is there an error handling function? */
        Lumen::Value errFunc = LumenRestoreStack(L, L->ErrFunc);
        if (!errFunc->IsFunction()) Lumen::Do::Throw(L, Lumen::RetErr);
        LumenSetObjectS2S(L, L->Top, L->Top - 1);  /* move argument */
        LumenSetObjectS2S(L, L->Top - 1, errFunc);  /* push function */
        LumenIncrTop(L);
        Lumen::Do::Call(L, L->Top - 2, 1);  /* call it */
    }
    Lumen::Do::Throw(L, Lumen::RetErrRun);
}


void Lumen::Debug::RunError(Lumen::State *L, const char *fmt, ...) {
    va_list argP;
        va_start(argP, fmt);
    addInfo(L, Lumen::PushVFString(L, fmt, argP));
        va_end(argP);
    Lumen::Debug::ErrorMessage(L);
}

