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

#include "lua.h"

#include "lua/api.h"
#include "lua/code.h"
#include "lua/debug.h"
#include "lua/do.h"
#include "lua/object.h"
#include "lua/opcodes.h"
#include "lua/state.h"
#include "lua/string.h"
#include "lua/table.h"
#include "lua/tm.h"
#include "lua/vm.h"


static const char *getFuncName(Lua::State *L, Lua::CallInfo *ci, const char **name);


static int currentPC(Lua::State *L, Lua::CallInfo *ci) {
    if (!LuaFuncIsLua(ci)) return -1;  /* function is not a Lua function? */
    if (ci == L->CallInfo)
        ci->SavedPC = L->SavedPC;
    return LuaDebugPCRel(ci->SavedPC, LuaCIFunc(ci)->AsLua.Func);
}


static int currentLine(Lua::State *L, Lua::CallInfo *ci) {
    int pc = currentPC(L, ci);
    if (pc < 0)
        return -1;  /* only active lua functions have current-line information */
    else
        return LuaDebugGetLine(LuaCIFunc(ci)->AsLua.Func, pc);
}


/*
** this function can be called asynchronous (e.g. during a signal)
*/
LUA_API int lua_sethook(Lua::State *L, lua_Hook func, int mask, int count) {
    if (func == nullptr || mask == 0) {  /* turn off hooks? */
        mask = 0;
        func = nullptr;
    }
    L->Hook = func;
    L->BaseHookCount = count;
    LuaDebugResetHookCount(L);
    L->HookMask = cast_byte(mask);
    return 1;
}


LUA_API lua_Hook lua_gethook(Lua::State *L) {
    return L->Hook;
}


LUA_API int lua_gethookmask(Lua::State *L) {
    return L->HookMask;
}


LUA_API int lua_gethookcount(Lua::State *L) {
    return L->BaseHookCount;
}


LUA_API int lua_getstack(Lua::State *L, int level, lua_Debug *ar) {
    int status;
    Lua::CallInfo *ci;
    LuaLock(L);
    for (ci = L->CallInfo; level > 0 && ci > L->BaseCI; ci--) {
        level--;
        if (LuaCIFuncIsLua(ci))  /* Lua function? */
            level -= ci->NTailCalls;  /* skip lost tail calls */
    }
    if (level == 0 && ci > L->BaseCI) {  /* level found? */
        status = 1;
        ar->i_ci = cast_int(ci - L->BaseCI);
    } else if (level < 0) {  /* level is of a lost tail call? */
        status = 1;
        ar->i_ci = 0;
    } else status = 0;  /* no such level */
    LuaUnlock(L);
    return status;
}


static Lua::Proto *getLuaProto(Lua::CallInfo *ci) {
    return (LuaFuncIsLua(ci) ? LuaCIFunc(ci)->AsLua.Func : nullptr);
}


static const char *findLocal(Lua::State *L, Lua::CallInfo *ci, int n) {
    const char *name;
    Lua::Proto *fp = getLuaProto(ci);
    if (fp && (name = Lua::Proto::GetLocalName(fp, n, currentPC(L, ci))) != nullptr)
        return name;  /* is a local variable in a Lua function */
    else {
        Lua::StkId limit = (ci == L->CallInfo) ? L->Top : (ci + 1)->Func;
        if (limit - ci->Base >= n && n > 0)  /* is 'n' inside 'ci' stack? */
            return "(*temporary)";
        else
            return nullptr;
    }
}


LUA_API const char *lua_getlocal(Lua::State *L, const lua_Debug *ar, int n) {
    Lua::CallInfo *ci = L->BaseCI + ar->i_ci;
    const char *name = findLocal(L, ci, n);
    LuaLock(L);
    if (name)
        Lua::PushObject(L, ci->Base + (n - 1));
    LuaUnlock(L);
    return name;
}


LUA_API const char *lua_setlocal(Lua::State *L, const lua_Debug *ar, int n) {
    Lua::CallInfo *ci = L->BaseCI + ar->i_ci;
    const char *name = findLocal(L, ci, n);
    LuaLock(L);
    if (name)
        LuaSetObjectS2S (L, ci->Base + (n - 1), L->Top - 1);
    L->Top--;  /* pop value */
    LuaUnlock(L);
    return name;
}


static void funcInfo(lua_Debug *ar, Lua::Closure *cl) {
    if (cl->AsC.IsC) {
        ar->source = "=[C]";
        ar->linedefined = -1;
        ar->lastlinedefined = -1;
        ar->what = "C";
    } else {
        ar->source = LuaStringCString(cl->AsLua.Func->Source);
        ar->linedefined = cl->AsLua.Func->LineDefined;
        ar->lastlinedefined = cl->AsLua.Func->LastLineDefined;
        ar->what = (ar->linedefined == 0) ? "main" : "Lua";
    }
    Lua::ChunkId(ar->short_src, ar->source, LUA_IDSIZE);
}


static void infoTailCall(lua_Debug *ar) {
    ar->name = ar->namewhat = "";
    ar->what = "tail";
    ar->lastlinedefined = ar->linedefined = ar->currentline = -1;
    ar->source = "=(tail call)";
    Lua::ChunkId(ar->short_src, ar->source, LUA_IDSIZE);
    ar->nups = 0;
}


static void collectValidLines(Lua::State *L, Lua::Closure *f) {
    if (f == nullptr || f->AsC.IsC) {
        LuaSetNilValue(L->Top);
    } else {
        Lua::Table *t = Lua::Table::New(L, 0, 0);
        int *lineinfo = f->AsLua.Func->LineInfo;
        int i;
        for (i = 0; i < f->AsLua.Func->LineInfoCount; i++) LuaSetBoolValue(Lua::Table::SetNum(L, t, lineinfo[i]), 1);
        LuaSetTableValue(L, L->Top, t);
    }
    LuaIncrTop(L);
}


static int auxGetInfo(Lua::State *L, const char *what, lua_Debug *ar,
                      Lua::Closure *f, Lua::CallInfo *ci) {
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
                ar->currentline = (ci) ? currentLine(L, ci) : -1;
                break;
            }
            case 'u': {
                ar->nups = f->AsC.NUpValues;
                break;
            }
            case 'n': {
                ar->namewhat = (ci) ? getFuncName(L, ci, &ar->name) : nullptr;
                if (ar->namewhat == nullptr) {
                    ar->namewhat = "";  /* not found */
                    ar->name = nullptr;
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


LUA_API int lua_getinfo(Lua::State *L, const char *what, lua_Debug *ar) {
    int status;
    Lua::Closure *f = nullptr;
    Lua::CallInfo *ci = nullptr;
    LuaLock(L);
    if (*what == '>') {
        Lua::StkId func = L->Top - 1;
        luai_apicheck(L, LuaTypeIsFunction(func));
        what++;  /* skip the '>' */
        f = LuaClosureValue(func);
        L->Top--;  /* pop function */
    } else if (ar->i_ci != 0) {  /* no tail call? */
        ci = L->BaseCI + ar->i_ci;
        lua_assert(LuaTypeIsFunction(ci->Func));
        f = LuaClosureValue(ci->Func);
    }
    status = auxGetInfo(L, what, ar, f, ci);
    if (strchr(what, 'f')) {
        if (f == nullptr) LuaSetNilValue(L->Top);
        else
            LuaSetClosureValue(L, L->Top, f);
        LuaIncrTop(L);
    }
    if (strchr(what, 'L'))
        collectValidLines(L, f);
    LuaUnlock(L);
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


static int preCheck(const Lua::Proto *pt) {
    check(pt->MaxStackSize <= Lua::MaxStack);
    check(pt->NUmParams + (pt->IsVararg & Lua::Proto::VarargHasArg) <= pt->MaxStackSize);
    check(!(pt->IsVararg & Lua::Proto::VarargIsNeedsArg) ||
          (pt->IsVararg & Lua::Proto::VarargHasArg));
    check(pt->UpValuesCount <= pt->NUpValues);
    check(pt->LineInfoCount == pt->CodeCount || pt->LineInfoCount == 0);
    check(pt->CodeCount > 0 && LuaOpCodeGet(pt->Code[pt->CodeCount - 1]) == Lua::OpCodeReturn);
    return 1;
}


#define checkOpenOP(pt, pc)    Lua::Debug::CheckOpenOP((pt)->Code[(pc)+1])

int Lua::Debug::CheckOpenOP(Lua::Instruction i) {
    switch (LuaOpCodeGet(i)) {
        case Lua::OpCodeCall:
        case Lua::OpCodeTailCall:
        case Lua::OpCodeReturn:
        case Lua::OpCodeSetList: {
            check(LuaOpCodeGetArgB(i) == 0);
            return 1;
        }
        default:
            return 0;  /* invalid instruction after an open call */
    }
}


static int checkArgMode(const Lua::Proto *pt, int r, Lua::OpArg mode) {
    switch (mode) {
        case Lua::OpArgN:
            check(r == 0);
            break;
        case Lua::OpArgU:
            break;
        case Lua::OpArgR:
            checkReg(pt, r);
            break;
        case Lua::OpArgK:
            check(LuaOpCodeIsK(r) ? LuaOpCodeIndexK(r) < pt->KCount : r < pt->MaxStackSize);
            break;
    }
    return 1;
}


static Lua::Instruction symbolExec(const Lua::Proto *pt, int lastPC, int reg) {
    int pc;
    int last;  /* stores position of last instruction that changed `reg` */
    last = pt->CodeCount - 1;  /* points to final return (a `neutral` instruction) */
    check(preCheck(pt));
    for (pc = 0; pc < lastPC; pc++) {
        Lua::Instruction i = pt->Code[pc];
        Lua::OpCode op = LuaOpCodeGet(i);
        int a = LuaOpCodeGetArgA(i);
        int b = 0;
        int c = 0;
        check(op < Lua::OpCodeCount);
        checkReg(pt, a);
        switch (LuaGetOpMode(op)) {
            case Lua::OpModeIABC: {
                b = LuaOpCodeGetArgB(i);
                c = LuaOpCodeGetArgC(i);
                check(checkArgMode(pt, b, LuaGetBMode(op)));
                check(checkArgMode(pt, c, LuaGetCMode(op)));
                break;
            }
            case Lua::OpModeIABx: {
                b = LuaOpCodeGetArgBx(i);
                if (LuaGetBMode(op) == Lua::OpArgK) check(b < pt->KCount);
                break;
            }
            case Lua::OpModeIAsBx: {
                b = LuaOpCodeGetArgsBx(i);
                if (LuaGetBMode(op) == Lua::OpArgR) {
                    int dest = pc + 1 + b;
                    check(0 <= dest && dest < pt->CodeCount);
                    if (dest > 0) {
                        int j;
                        /* check that it does not jump to a setlist count; this
                           is tricky, because the count from a previous setlist may
                           have the same value of an invalid setlist; so, we must
                           go all the way back to the first of them (if any) */
                        for (j = 0; j < dest; j++) {
                            Lua::Instruction d = pt->Code[dest - 1 - j];
                            if (!(LuaOpCodeGet(d) == Lua::OpCodeSetList && LuaOpCodeGetArgC(d) == 0)) break;
                        }
                        /* if 'j' is even, previous value is not a setlist (even if
                           it looks like one) */
                        check((j & 1) == 0);
                    }
                }
                break;
            }
        }
        if (LuaTestAMode(op)) {
            if (a == reg) last = pc;  /* change register `a' */
        }
        if (LuaTestTMode(op)) {
            check(pc + 2 < pt->CodeCount);  /* check skip */
            check(LuaOpCodeGet(pt->Code[pc + 1]) == Lua::OpCodeJump);
        }
        switch (op) {
            case Lua::OpCodeLoadBool: {
                if (c == 1) {  /* does it jump? */
                    check(pc + 2 < pt->CodeCount);  /* check its jump */
                    check(LuaOpCodeGet(pt->Code[pc + 1]) != Lua::OpCodeSetList ||
                          LuaOpCodeGetArgC(pt->Code[pc + 1]) != 0);
                }
                break;
            }
            case Lua::OpCodeLoadNil: {
                if (a <= reg && reg <= b)
                    last = pc;  /* set registers from `a' to `b' */
                break;
            }
            case Lua::OpCodeGetUpVal:
            case Lua::OpCodeSetUpVal: {
                check(b < pt->NUpValues);
                break;
            }
            case Lua::OpCodeGetGlobal:
            case Lua::OpCodeSetGlobal: {
                check(LuaTypeIsString(&pt->K[b]));
                break;
            }
            case Lua::OpCodeSelf: {
                checkReg(pt, a + 1);
                if (reg == a + 1) last = pc;
                break;
            }
            case Lua::OpCodeConcat: {
                check(b < c);  /* at least two operands */
                break;
            }
            case Lua::OpCodeTForLoop: {
                check(c >= 1);  /* at least one result (control variable) */
                checkReg(pt, a + 2 + c);  /* space for results */
                if (reg >= a + 2) last = pc;  /* affect all regs above its base */
                break;
            }
            case Lua::OpCodeForLoop:
            case Lua::OpCodeForPrep:
                checkReg(pt, a + 3);
                /* go through */
            case Lua::OpCodeJump: {
                int dest = pc + 1 + b;
                /* not full check and jump is forward and do not skip `lastPC`? */
                if (reg != NO_REG && pc < dest && dest <= lastPC)
                    pc += b;  /* do the jump */
                break;
            }
            case Lua::OpCodeCall:
            case Lua::OpCodeTailCall: {
                if (b != 0) {
                    checkReg(pt, a + b - 1);
                }
                c--;  /* c = num. returns */
                if (c == LUA_MULTRET) {
                    check(checkOpenOP(pt, pc));
                } else if (c != 0)
                    checkReg(pt, a + c - 1);
                if (reg >= a) last = pc;  /* affect all registers above base */
                break;
            }
            case Lua::OpCodeReturn: {
                b--;  /* b = num. returns */
                if (b > 0) checkReg(pt, a + b - 1);
                break;
            }
            case Lua::OpCodeSetList: {
                if (b > 0) checkReg(pt, a + b);
                if (c == 0) {
                    pc++;
                    check(pc < pt->CodeCount - 1);
                }
                break;
            }
            case Lua::OpCodeClosure: {
                int nup, j;
                check(b < pt->SubProtoCount);
                nup = pt->SubProto[b]->NUpValues;
                check(pc + nup < pt->CodeCount);
                for (j = 1; j <= nup; j++) {
                    Lua::OpCode op1 = LuaOpCodeGet(pt->Code[pc + j]);
                    check(op1 == Lua::OpCodeGetUpVal || op1 == Lua::OpCodeMove);
                }
                if (reg != NO_REG)  /* tracing? */
                    pc += nup;  /* do not 'execute' these pseudo-instructions */
                break;
            }
            case Lua::OpCodeVararg: {
                check((pt->IsVararg & Lua::Proto::VarargIsVararg) &&
                      !(pt->IsVararg & Lua::Proto::VarargIsNeedsArg));
                b--;
                if (b == LUA_MULTRET) check(checkOpenOP(pt, pc));
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


int Lua::Debug::CheckCode(const Lua::Proto *pt) {
    return (symbolExec(pt, pt->CodeCount, NO_REG) != 0);
}


static const char *kName(Lua::Proto *p, int c) {
    if (LuaOpCodeIsK(c) && LuaTypeIsString(&p->K[LuaOpCodeIndexK(c)]))
        return LuaStringValue2CString(&p->K[LuaOpCodeIndexK(c)]);
    else
        return "?";
}


static const char *getObjName(Lua::State *L, Lua::CallInfo *ci, int stackPos,
                              const char **name) {
    if (LuaFuncIsLua(ci)) {  /* a Lua function? */
        Lua::Proto *p = LuaCIFunc(ci)->AsLua.Func;
        int pc = currentPC(L, ci);
        Lua::Instruction i;
        *name = Lua::Proto::GetLocalName(p, stackPos + 1, pc);
        if (*name)  /* is a local? */
            return "local";
        i = symbolExec(p, pc, stackPos);  /* try symbolic execution */
        lua_assert(pc != -1);
        switch (LuaOpCodeGet(i)) {
            case Lua::OpCodeGetGlobal: {
                int g = LuaOpCodeGetArgBx(i);  /* global index */
                lua_assert(LuaTypeIsString(&p->K[g]));
                *name = LuaStringValue2CString(&p->K[g]);
                return "global";
            }
            case Lua::OpCodeMove: {
                int a = LuaOpCodeGetArgA(i);
                int b = LuaOpCodeGetArgB(i);  /* move from `b` to `a` */
                if (b < a)
                    return getObjName(L, ci, b, name);  /* get name for `b` */
                break;
            }
            case Lua::OpCodeGetTable: {
                int k = LuaOpCodeGetArgC(i);  /* key index */
                *name = kName(p, k);
                return "field";
            }
            case Lua::OpCodeGetUpVal: {
                int u = LuaOpCodeGetArgB(i);  /* upvalue index */
                *name = p->UpValues ? LuaStringCString(p->UpValues[u]) : "?";
                return "upvalue";
            }
            case Lua::OpCodeSelf: {
                int k = LuaOpCodeGetArgC(i);  /* key index */
                *name = kName(p, k);
                return "method";
            }
            default:
                break;
        }
    }
    return nullptr;  /* no useful name found */
}


static const char *getFuncName(Lua::State *L, Lua::CallInfo *ci, const char **name) {
    Lua::Instruction i;
    if ((LuaFuncIsLua(ci) && ci->NTailCalls > 0) || !LuaFuncIsLua(ci - 1))
        return nullptr;  /* calling function is not Lua (or is unknown) */
    ci--;  /* calling function */
    i = LuaCIFunc(ci)->AsLua.Func->Code[currentPC(L, ci)];
    if (LuaOpCodeGet(i) == Lua::OpCodeCall || LuaOpCodeGet(i) == Lua::OpCodeTailCall ||
        LuaOpCodeGet(i) == Lua::OpCodeTForLoop)
        return getObjName(L, ci, LuaOpCodeGetArgA(i), name);
    else
        return nullptr;  /* no useful name can be found */
}


/* only ANSI way to check whether a pointer points to an array */
static int isInStack(Lua::CallInfo *ci, const Lua::Value *o) {
    Lua::StkId p;
    for (p = ci->Base; p < ci->Top; p++)
        if (o == p) return 1;
    return 0;
}


void Lua::Debug::TypeError(Lua::State *L, const Lua::Value *o, const char *op) {
    const char *name = nullptr;
    const char *t = Lua::TM::TypeNames[LuaTypeOf(o)];
    const char *kind = (isInStack(L->CallInfo, o)) ?
                       getObjName(L, L->CallInfo, cast_int(o - L->Base), &name) :
                       nullptr;
    if (kind)
        Lua::Debug::RunError(L, "attempt to %s %s " LUA_QS " (a %s value)",
                             op, kind, name, t);
    else
        Lua::Debug::RunError(L, "attempt to %s a %s value", op, t);
}


void Lua::Debug::ConcatError(Lua::State *L, Lua::StkId p1, Lua::StkId p2) {
    if (LuaTypeIsString(p1) || LuaTypeIsNumber(p1)) p1 = p2;
    lua_assert(!LuaTypeIsString(p1) && !LuaTypeIsNumber(p1));
    Lua::Debug::TypeError(L, p1, "concatenate");
}


void Lua::Debug::ArithError(lua_State *L, const Lua::Value *p1, const Lua::Value *p2) {
    Lua::Value temp; // NOLINT
    if (Lua::VM::ToNumber(p1, &temp) == nullptr)
        p2 = p1;  /* first operand is wrong */
    Lua::Debug::TypeError(L, p2, "perform arithmetic on");
}


int Lua::Debug::OrderError(lua_State *L, const Lua::Value *p1, const Lua::Value *p2) {
    const char *t1 = Lua::TM::TypeNames[LuaTypeOf(p1)];
    const char *t2 = Lua::TM::TypeNames[LuaTypeOf(p2)];
    if (t1[2] == t2[2])
        Lua::Debug::RunError(L, "attempt to compare two %s values", t1);
    else
        Lua::Debug::RunError(L, "attempt to compare %s with %s", t1, t2);
    return 0;
}


static void addInfo(lua_State *L, const char *msg) {
    Lua::CallInfo *ci = L->CallInfo;
    if (LuaFuncIsLua(ci)) {  /* is Lua code? */
        char buff[LUA_IDSIZE];  /* add file:line information */
        int line = currentLine(L, ci);
        Lua::ChunkId(buff, LuaStringCString(getLuaProto(ci)->Source), LUA_IDSIZE);
        Lua::PushFString(L, "%s:%d: %s", buff, line, msg);
    }
}


void Lua::Debug::ErrorMessage(lua_State *L) {
    if (L->ErrFunc != 0) {  /* is there an error handling function? */
        Lua::StkId errFunc = LuaRestoreStack(L, L->ErrFunc);
        if (!LuaTypeIsFunction(errFunc)) Lua::Do::Throw(L, LUA_ERRERR);
        LuaSetObjectS2S(L, L->Top, L->Top - 1);  /* move argument */
        LuaSetObjectS2S(L, L->Top - 1, errFunc);  /* push function */
        LuaIncrTop(L);
        Lua::Do::Call(L, L->Top - 2, 1);  /* call it */
    }
    Lua::Do::Throw(L, LUA_ERRRUN);
}


void Lua::Debug::RunError(lua_State *L, const char *fmt, ...) {
    va_list argP;
    va_start(argP, fmt);
    addInfo(L, Lua::PushVFString(L, fmt, argP));
    va_end(argP);
    Lua::Debug::ErrorMessage(L);
}

