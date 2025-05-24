/*!
 * @brief Lua virtual machine
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstdio>
#include <cstdlib>
#include <cstring>

#define LUA_CORE

#include "lua.h"

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/gc.h"
#include "lumen/object.h"
#include "lumen/opcodes.h"
#include "lumen/state.h"
#include "lumen/string.h"
#include "lumen/table.h"
#include "lumen/tm.h"
#include "lumen/vm.h"



/* limit for table tag-method chains (to avoid loops) */
#define LUA_VM_MAX_TAG_LOOP    100


const Lumen::Value *Lumen::VM::ToNumber(const Lumen::Value *obj, Lumen::Value *n) {
    Lumen::Number num;
    if (LumenTypeIsNumber(obj)) return obj;
    if (LumenTypeIsString(obj) && Lumen::String2Decimal(LumenStringValue2CString(obj), &num)) {
        LumenSetNumberValue(n, num);
        return n;
    } else
        return nullptr;
}


int Lumen::VM::ToString(Lumen::State *L, Lumen::StkId obj) {
    if (!LumenTypeIsNumber(obj))
        return 0;
    else {
        char s[LUAI_MAXNUMBER2STR];
        Lumen::Number n = LumenNumberValue(obj);
        lua_number2str(s, n);
        LumenSetStringValue2S(L, obj, Lumen::String::New(L, s));
        return 1;
    }
}


static void traceExec(Lumen::State *L, const Lumen::Instruction *pc) {
    Lumen::Byte mask = L->HookMask;
    const Lumen::Instruction *oldpc = L->SavedPC;
    L->SavedPC = pc;
    if ((mask & LUA_MASKCOUNT) && L->HookCount == 0) {
        LumenDebugResetHookCount(L);
        Lumen::Do::CallHook(L, LUA_HOOKCOUNT, -1);
    }
    if (mask & LUA_MASKLINE) {
        Lumen::Proto *p = LumenCIFunc(L->CallInfo)->AsLua.Func;
        int npc = LumenDebugPCRel(pc, p);
        int newline = LumenDebugGetLine(p, npc);
        /* call linehook when enter a new function, when jump back (loop),
           or when enter a new line */
        if (npc == 0 || pc <= oldpc || newline != LumenDebugGetLine(p, LumenDebugPCRel(oldpc, p)))
            Lumen::Do::CallHook(L, LUA_HOOKLINE, newline);
    }
}


static void callTMRes(Lumen::State *L, Lumen::StkId res, const Lumen::Value *f,
                      const Lumen::Value *p1, const Lumen::Value *p2) {
    ptrdiff_t result = LumenSaveStack(L, res);
    LumenSetObject2S(L, L->Top, f);  /* push function */
    LumenSetObject2S(L, L->Top + 1, p1);  /* 1st argument */
    LumenSetObject2S(L, L->Top + 2, p2);  /* 2nd argument */
    LumenDoCheckStack(L, 3);
    L->Top += 3;
    Lumen::Do::Call(L, L->Top - 3, 1);
    res = LumenRestoreStack(L, result);
    L->Top--;
    LumenSetObjectS2S(L, res, L->Top);
}


static void callTM(Lumen::State *L, const Lumen::Value *f, const Lumen::Value *p1,
                   const Lumen::Value *p2, const Lumen::Value *p3) {
    LumenSetObject2S(L, L->Top, f);  /* push function */
    LumenSetObject2S(L, L->Top + 1, p1);  /* 1st argument */
    LumenSetObject2S(L, L->Top + 2, p2);  /* 2nd argument */
    LumenSetObject2S(L, L->Top + 3, p3);  /* 3th argument */
    LumenDoCheckStack(L, 4);
    L->Top += 4;
    Lumen::Do::Call(L, L->Top - 4, 0);
}


void Lumen::VM::GetTable(Lumen::State *L, const Lumen::Value *t, Lumen::Value *key, Lumen::StkId val) {
    int loop;
    for (loop = 0; loop < LUA_VM_MAX_TAG_LOOP; loop++) {
        const Lumen::Value *tm;
        if (LumenTypeIsTable(t)) {  /* `t' is a table? */
            Lumen::Table *h = LumenTableValue(t);
            const Lumen::Value *res = Lumen::Table::Get(h, key); /* do a primitive get */
            if (!LumenTypeIsNil(res) ||  /* result is no nil? */
                (tm = LumenTMGetFast(L, h->Metatable, Lumen::TM::NameIndex)) == nullptr) { /* or no TM? */
                LumenSetObject2S(L, val, res);
                return;
            }
            /* else will try the tag method */
        } else if (LumenTypeIsNil(tm = Lumen::TM::GetByObject(L, t, Lumen::TM::NameIndex)))
            Lumen::Debug::TypeError(L, t, "index");
        if (LumenTypeIsFunction(tm)) {
            callTMRes(L, val, tm, t, key);
            return;
        }
        t = tm;  /* else repeat with `tm' */
    }
    Lumen::Debug::RunError(L, "loop in gettable");
}


void Lumen::VM::SetTable(Lumen::State *L, const Lumen::Value *t, Lumen::Value *key, Lumen::StkId val) {
    int loop;
    Lumen::Value temp;
    for (loop = 0; loop < LUA_VM_MAX_TAG_LOOP; loop++) {
        const Lumen::Value *tm;
        if (LumenTypeIsTable(t)) {  /* `t' is a table? */
            Lumen::Table *h = LumenTableValue(t);
            Lumen::Value *oldval = Lumen::Table::Set(L, h, key); /* do a primitive set */
            if (!LumenTypeIsNil(oldval) ||  /* result is no nil? */
                (tm = LumenTMGetFast(L, h->Metatable, Lumen::TM::NameNewIndex)) == nullptr) { /* or no TM? */
                LumenSetObject2T(L, oldval, val);
                h->Flags = 0;
                LumenGCBarrierTable(L, h, val);
                return;
            }
            /* else will try the tag method */
        } else if (LumenTypeIsNil(tm = Lumen::TM::GetByObject(L, t, Lumen::TM::NameNewIndex)))
            Lumen::Debug::TypeError(L, t, "index");
        if (LumenTypeIsFunction(tm)) {
            callTM(L, tm, t, key, val);
            return;
        }
        /* else repeat with `tm' */
        LumenSetObject(L, &temp, tm);  /* avoid pointing inside table (may rehash) */
        t = &temp;
    }
    Lumen::Debug::RunError(L, "loop in settable");
}


static int call_binTM(Lumen::State *L, const Lumen::Value *p1, const Lumen::Value *p2,
                      Lumen::StkId res, Lumen::TM::Name event) {
    const Lumen::Value *tm = Lumen::TM::GetByObject(L, p1, event);  /* try first operand */
    if (LumenTypeIsNil(tm))
        tm = Lumen::TM::GetByObject(L, p2, event);  /* try second operand */
    if (LumenTypeIsNil(tm)) return 0;
    callTMRes(L, res, tm, p1, p2);
    return 1;
}


static const Lumen::Value *get_compTM(Lumen::State *L, Lumen::Table *mt1, Lumen::Table *mt2,
                                    Lumen::TM::Name event) {
    const Lumen::Value *tm1 = LumenTMGetFast(L, mt1, event);
    const Lumen::Value *tm2;
    if (tm1 == nullptr) return nullptr;  /* no metamethod */
    if (mt1 == mt2) return tm1;  /* same metatables => same metamethods */
    tm2 = LumenTMGetFast(L, mt2, event);
    if (tm2 == nullptr) return nullptr;  /* no metamethod */
    if (Lumen::RawEqualObject(tm1, tm2))  /* same metamethods? */
        return tm1;
    return nullptr;
}


static int callOrderTM(Lumen::State *L, const Lumen::Value *p1, const Lumen::Value *p2,
                       Lumen::TM::Name event) {
    const Lumen::Value *tm1 = Lumen::TM::GetByObject(L, p1, event);
    const Lumen::Value *tm2;
    if (LumenTypeIsNil(tm1)) return -1;  /* no metamethod? */
    tm2 = Lumen::TM::GetByObject(L, p2, event);
    if (!Lumen::RawEqualObject(tm1, tm2))  /* different metamethods? */
        return -1;
    callTMRes(L, L->Top, tm1, p1, p2);
    return !LumenIsFalse(L->Top);
}


static int luaStrCmp(const Lumen::String *ls, const Lumen::String *rs) {
    const char *l = LumenStringCString(ls);
    size_t ll = ls->Length;
    const char *r = LumenStringCString(rs);
    size_t lr = rs->Length;
    for (;;) {
        int temp = strcoll(l, r);
        if (temp != 0) return temp;
        else {  /* strings are equal up to a `\0' */
            size_t len = strlen(l);  /* index of first `\0' in both strings */
            if (len == lr)  /* r is finished? */
                return (len == ll) ? 0 : 1;
            else if (len == ll)  /* l is finished? */
                return -1;  /* l is smaller than r (because r is not finished) */
            /* both strings longer than `len'; go on comparing (after the `\0') */
            len++;
            l += len;
            ll -= len;
            r += len;
            lr -= len;
        }
    }
}


int Lumen::VM::LessThan(Lumen::State *L, const Lumen::Value *l, const Lumen::Value *r) {
    int res;
    if (LumenTypeOf(l) != LumenTypeOf(r))
        return Lumen::Debug::OrderError(L, l, r);
    else if (LumenTypeIsNumber(l))
        return luai_numlt(LumenNumberValue(l), LumenNumberValue(r));
    else if (LumenTypeIsString(l))
        return luaStrCmp(LumenStringValue(l), LumenStringValue(r)) < 0;
    else if ((res = callOrderTM(L, l, r, Lumen::TM::NameLT)) != -1)
        return res;
    return Lumen::Debug::OrderError(L, l, r);
}


static int lessEqual(Lumen::State *L, const Lumen::Value *l, const Lumen::Value *r) {
    int res;
    if (LumenTypeOf(l) != LumenTypeOf(r))
        return Lumen::Debug::OrderError(L, l, r);
    else if (LumenTypeIsNumber(l))
        return luai_numle(LumenNumberValue(l), LumenNumberValue(r));
    else if (LumenTypeIsString(l))
        return luaStrCmp(LumenStringValue(l), LumenStringValue(r)) <= 0;
    else if ((res = callOrderTM(L, l, r, Lumen::TM::NameLE)) != -1)  /* first try `le' */
        return res;
    else if ((res = callOrderTM(L, r, l, Lumen::TM::NameLT)) != -1)  /* else try `lt' */
        return !res;
    return Lumen::Debug::OrderError(L, l, r);
}


int Lumen::VM::EqualVal(Lumen::State *L, const Lumen::Value *t1, const Lumen::Value *t2) {
    const Lumen::Value *tm;
    lua_assert(LumenTypeOf(t1) == LumenTypeOf(t2));
    switch (LumenTypeOf(t1)) {
        case LUA_TNIL:
            return 1;
        case LUA_TNUMBER:
            return luai_numeq(LumenNumberValue(t1), LumenNumberValue(t2));
        case LUA_TBOOLEAN:
            return LumenBoolValue(t1) == LumenBoolValue(t2);  /* true must be 1 !! */
        case LUA_TLIGHTUSERDATA:
            return LumenLUDataValue(t1) == LumenLUDataValue(t2);
        case LUA_TUSERDATA: {
            if (LumenUDataValue(t1) == LumenUDataValue(t2)) return 1;
            tm = get_compTM(L, LumenUDataValue(t1)->Metatable, LumenUDataValue(t2)->Metatable,
                            Lumen::TM::NameEQ);
            break;  /* will try TM */
        }
        case LUA_TTABLE: {
            if (LumenTableValue(t1) == LumenTableValue(t2)) return 1;
            tm = get_compTM(L, LumenTableValue(t1)->Metatable, LumenTableValue(t2)->Metatable, Lumen::TM::NameEQ);
            break;  /* will try TM */
        }
        default:
            return LumenGCValue(t1) == LumenGCValue(t2);
    }
    if (tm == nullptr) return 0;  /* no TM? */
    callTMRes(L, L->Top, tm, t1, t2);  /* call TM */
    return !LumenIsFalse(L->Top);
}


void Lumen::VM::Concat(Lumen::State *L, int total, int last) {
    do {
        Lumen::StkId top = L->Base + last + 1;
        int n = 2;  /* number of elements handled in this pass (at least 2) */
        if (!(LumenTypeIsString(top - 2) || LumenTypeIsNumber(top - 2)) || !LumenVMToString(L, top - 1)) {
            if (!call_binTM(L, top - 2, top - 1, top - 2, Lumen::TM::NameConcat))
                Lumen::Debug::ConcatError(L, top - 2, top - 1);
        } else if (LumenStringValue(top - 1)->Length == 0)  /* second op is empty? */
            (void) LumenVMToString(L, top - 2);  /* result is first op (as string) */
        else {
            /* at least two string values; get as many as possible */
            size_t tl = LumenStringValue(top - 1)->Length;
            char *buffer;
            int i;
            /* collect total length */
            for (n = 1; n < total && LumenVMToString(L, top - n - 1); n++) {
                size_t l = LumenStringValue(top - n - 1)->Length;
                if (l >= Lumen::MaxSize - tl) Lumen::Debug::RunError(L, "string length overflow");
                tl += l;
            }
            buffer = Lumen::ZBuffer::OpenSpace(L, &LumenGlobal(L)->Buff, tl);
            tl = 0;
            for (i = n; i > 0; i--) {  /* concat all strings */
                size_t l = LumenStringValue(top - i)->Length;
                memcpy(buffer + tl, LumenStringValue2CString(top - i), l);
                tl += l;
            }
            LumenSetStringValue2S(L, top - n, Lumen::String::New(L, buffer, tl));
        }
        total -= n - 1;  /* got `n' strings to create 1 new */
        last -= n - 1;
    } while (total > 1);  /* repeat until only 1 result left */
}


static void Arith(Lumen::State *L, Lumen::StkId ra, const Lumen::Value *rb,
                  const Lumen::Value *rc, Lumen::TM::Name op) {
    Lumen::Value tempB, tempC;
    const Lumen::Value *b, *c;
    if ((b = Lumen::VM::ToNumber(rb, &tempB)) != nullptr &&
        (c = Lumen::VM::ToNumber(rc, &tempC)) != nullptr) {
        Lumen::Number nb = LumenNumberValue(b), nc = LumenNumberValue(c);
        switch (op) {
            case Lumen::TM::NameAdd:
                LumenSetNumberValue(ra, luai_numadd(nb, nc));
                break;
            case Lumen::TM::NameSub:
                LumenSetNumberValue(ra, luai_numsub(nb, nc));
                break;
            case Lumen::TM::NameMul:
                LumenSetNumberValue(ra, luai_nummul(nb, nc));
                break;
            case Lumen::TM::NameDiv:
                LumenSetNumberValue(ra, luai_numdiv(nb, nc));
                break;
            case Lumen::TM::NameMod:
                LumenSetNumberValue(ra, luai_nummod(nb, nc));
                break;
            case Lumen::TM::NamePow:
                LumenSetNumberValue(ra, luai_numpow(nb, nc));
                break;
            case Lumen::TM::NameUnm:
                LumenSetNumberValue(ra, luai_numunm(nb));
                break;
            default:
                lua_assert(0);
                break;
        }
    } else if (!call_binTM(L, rb, rc, ra, op))
        Lumen::Debug::ArithError(L, rb, rc);
}

/*
** some macros for common tasks in `Lumen::VM::Execute'
*/

#define runtime_check(L, c)    { if (!(c)) break; }

#define RA(i)    (base+LumenOpCodeGetArgA(i))
/* to be used after possible stack reallocation */
#define RB(i)    LumenCheckExp(LumenGetBMode(LumenOpCodeGet(i)) == Lumen::OpArgR, base+LumenOpCodeGetArgB(i))
#define RC(i)    LumenCheckExp(LumenGetCMode(LumenOpCodeGet(i)) == Lumen::OpArgR, base+LumenOpCodeGetArgC(i))
#define RKB(i)    LumenCheckExp(LumenGetBMode(LumenOpCodeGet(i)) == Lumen::OpArgK, \
    LumenOpCodeIsK(LumenOpCodeGetArgB(i)) ? k+LumenOpCodeIndexK(LumenOpCodeGetArgB(i)) : base+LumenOpCodeGetArgB(i))
#define RKC(i)    LumenCheckExp(LumenGetCMode(LumenOpCodeGet(i)) == Lumen::OpArgK, \
    LumenOpCodeIsK(LumenOpCodeGetArgC(i)) ? k+LumenOpCodeIndexK(LumenOpCodeGetArgC(i)) : base+LumenOpCodeGetArgC(i))
#define KBx(i)    LumenCheckExp(LumenGetBMode(LumenOpCodeGet(i)) == Lumen::OpArgK, k+LumenOpCodeGetArgBx(i))


#define doJump(L, pc, i) \
LumenDo(                   \
    (pc) += (i);         \
    LumenThreadYield(L);   \
)


#define Protect(x) do { \
    L->SavedPC = pc;    \
    {                   \
        x;              \
    }                   \
    base = L->Base;     \
} while (0)


#define arith_op(op, tm) do { \
    Lumen::Value *rb = RKB(i); \
    Lumen::Value *rc = RKC(i); \
    if (LumenTypeIsNumber(rb) && LumenTypeIsNumber(rc)) { \
        Lumen::Number nb = LumenNumberValue(rb), nc = LumenNumberValue(rc); \
        LumenSetNumberValue(ra, op(nb, nc));            \
    } else {                  \
        Protect(Arith(L, ra, rb, rc, tm));            \
    }                         \
} while (0)


void Lumen::VM::Execute(Lumen::State *L, int nExecCalls) {
    Lumen::LClosure *cl;
    Lumen::StkId base;
    Lumen::Value *k;
    const Lumen::Instruction *pc;
    reentry:  /* entry point */
    lua_assert(LumenFuncIsLua(L->CallInfo));
    pc = L->SavedPC;
    cl = &LumenClosureValue(L->CallInfo->Func)->AsLua;
    base = L->Base;
    k = cl->Func->K;
    /* main loop of interpreter */
    for (;;) {
        const Lumen::Instruction i = *pc++;
        Lumen::StkId ra;
        if ((L->HookMask & (LUA_MASKLINE | LUA_MASKCOUNT)) &&
            (--L->HookCount == 0 || L->HookMask & LUA_MASKLINE)) {
            traceExec(L, pc);
            if (L->Status == LUA_YIELD) {  /* did hook yield? */
                L->SavedPC = pc - 1;
                return;
            }
            base = L->Base;
        }
        /* warning!! several calls may realloc the stack and invalidate `ra' */
        ra = RA(i);
        lua_assert(base == L->Base && L->Base == L->CallInfo->Base);
        lua_assert(base <= L->Top && L->Top <= L->Stack + L->StackCount);
        lua_assert(L->Top == L->CallInfo->Top || Lumen::Debug::CheckOpenOP(i));
        switch (LumenOpCodeGet(i)) {
            case Lumen::OpCodeMove: {
                LumenSetObjectS2S(L, ra, RB(i));
                continue;
            }
            case Lumen::OpCodeLoadK: {
                LumenSetObject2S(L, ra, KBx(i));
                continue;
            }
            case Lumen::OpCodeLoadBool: {
                LumenSetBoolValue(ra, LumenOpCodeGetArgB(i));
                if (LumenOpCodeGetArgC(i)) pc++;  /* skip next instruction (if C) */
                continue;
            }
            case Lumen::OpCodeLoadNil: {
                Lumen::Value *rb = RB(i);
                do {
                    LumenSetNilValue(rb--);
                } while (rb >= ra);
                continue;
            }
            case Lumen::OpCodeGetUpVal: {
                int b = LumenOpCodeGetArgB(i);
                LumenSetObject2S(L, ra, cl->UpValues[b]->SelfValue);
                continue;
            }
            case Lumen::OpCodeGetGlobal: {
                Lumen::Value g;
                Lumen::Value *rb = KBx(i);
                LumenSetTableValue(L, &g, cl->Env);
                lua_assert(LumenTypeIsString(rb));
                Protect(Lumen::VM::GetTable(L, &g, rb, ra));
                continue;
            }
            case Lumen::OpCodeGetTable: {
                Protect(Lumen::VM::GetTable(L, RB(i), RKC(i), ra));
                continue;
            }
            case Lumen::OpCodeSetGlobal: {
                Lumen::Value g;
                LumenSetTableValue(L, &g, cl->Env);
                lua_assert(LumenTypeIsString(KBx(i)));
                Protect(Lumen::VM::SetTable(L, &g, KBx(i), ra));
                continue;
            }
            case Lumen::OpCodeSetUpVal: {
                Lumen::UpValue *uv = cl->UpValues[LumenOpCodeGetArgB(i)];
                LumenSetObject(L, uv->SelfValue, ra);
                LumenGCBarrier(L, uv, ra);
                continue;
            }
            case Lumen::OpCodeSetTable: {
                Protect(Lumen::VM::SetTable(L, ra, RKB(i), RKC(i)));
                continue;
            }
            case Lumen::OpCodeNewTable: {
                int b = LumenOpCodeGetArgB(i);
                int c = LumenOpCodeGetArgC(i);
                LumenSetTableValue(L, ra, Lumen::Table::New(L, Lumen::FB2Int(b), Lumen::FB2Int(c)));
                Protect(LumenGCCheckGC(L));
                continue;
            }
            case Lumen::OpCodeSelf: {
                Lumen::StkId rb = RB(i);
                LumenSetObjectS2S(L, ra + 1, rb);
                Protect(Lumen::VM::GetTable(L, rb, RKC(i), ra));
                continue;
            }
            case Lumen::OpCodeAdd: {
                arith_op(luai_numadd, Lumen::TM::NameAdd);
                continue;
            }
            case Lumen::OpCodeSub: {
                arith_op(luai_numsub, Lumen::TM::NameSub);
                continue;
            }
            case Lumen::OpCodeMul: {
                arith_op(luai_nummul, Lumen::TM::NameMul);
                continue;
            }
            case Lumen::OpCodeDiv: {
                arith_op(luai_numdiv, Lumen::TM::NameDiv);
                continue;
            }
            case Lumen::OpCodeMod: {
                arith_op(luai_nummod, Lumen::TM::NameMod);
                continue;
            }
            case Lumen::OpCodePow: {
                arith_op(luai_numpow, Lumen::TM::NamePow);
                continue;
            }
            case Lumen::OpCodeUnm: {
                Lumen::Value *rb = RB(i);
                if (LumenTypeIsNumber(rb)) {
                    Lumen::Number nb = LumenNumberValue(rb);
                    LumenSetNumberValue(ra, luai_numunm(nb));
                } else {
                    Protect(Arith(L, ra, rb, rb, Lumen::TM::NameUnm));
                }
                continue;
            }
            case Lumen::OpCodeNot: {
                int res = LumenIsFalse(RB(i));  /* next assignment may change this value */
                LumenSetBoolValue(ra, res);
                continue;
            }
            case Lumen::OpCodeLen: {
                const Lumen::Value *rb = RB(i);
                switch (LumenTypeOf(rb)) {
                    case LUA_TTABLE: {
                        LumenSetNumberValue(ra, cast_num(Lumen::Table::GetN(LumenTableValue(rb))));
                        break;
                    }
                    case LUA_TSTRING: {
                        LumenSetNumberValue(ra, cast_num(LumenStringValue(rb)->Length));
                        break;
                    }
                    default: {  /* try metamethod */
                        Protect(
                                if (!call_binTM(L, rb, Lumen::NilObject, ra, Lumen::TM::NameLen))
                                    Lumen::Debug::TypeError(L, rb, "get length of");
                        );
                    }
                }
                continue;
            }
            case Lumen::OpCodeConcat: {
                int b = LumenOpCodeGetArgB(i);
                int c = LumenOpCodeGetArgC(i);
                Protect(Lumen::VM::Concat(L, c - b + 1, c); LumenGCCheckGC(L));
                LumenSetObjectS2S(L, RA(i), base + b);
                continue;
            }
            case Lumen::OpCodeJump: {
                doJump(L, pc, LumenOpCodeGetArgsBx(i));
                continue;
            }
            case Lumen::OpCodeEQ: {
                Lumen::Value *rb = RKB(i);
                Lumen::Value *rc = RKC(i);
                Protect(
                        if (LumenVMEqualObj(L, rb, rc) == LumenOpCodeGetArgA(i))
                            doJump(L, pc, LumenOpCodeGetArgsBx(*pc));
                );
                pc++;
                continue;
            }
            case Lumen::OpCodeLT: {
                Protect(
                        if (Lumen::VM::LessThan(L, RKB(i), RKC(i)) == LumenOpCodeGetArgA(i))
                            doJump(L, pc, LumenOpCodeGetArgsBx(*pc));
                );
                pc++;
                continue;
            }
            case Lumen::OpCodeLE: {
                Protect(
                        if (lessEqual(L, RKB(i), RKC(i)) == LumenOpCodeGetArgA(i))
                            doJump(L, pc, LumenOpCodeGetArgsBx(*pc));
                );
                pc++;
                continue;
            }
            case Lumen::OpCodeTest: {
                if (LumenIsFalse(ra) != LumenOpCodeGetArgC(i)) doJump(L, pc, LumenOpCodeGetArgsBx(*pc));
                pc++;
                continue;
            }
            case Lumen::OpCodeTestTest: {
                Lumen::Value *rb = RB(i);
                if (LumenIsFalse(rb) != LumenOpCodeGetArgC(i)) {
                    LumenSetObjectS2S(L, ra, rb);
                    doJump(L, pc, LumenOpCodeGetArgsBx(*pc));
                }
                pc++;
                continue;
            }
            case Lumen::OpCodeCall: {
                int b = LumenOpCodeGetArgB(i);
                int nresults = LumenOpCodeGetArgC(i) - 1;
                if (b != 0) L->Top = ra + b;  /* else previous instruction set top */
                L->SavedPC = pc;
                switch (Lumen::Do::PreCall(L, ra, nresults)) {
                    case Lumen::Do::PCRetLua: {
                        nExecCalls++;
                        goto reentry;  /* restart Lumen::VM::Execute over new Lua function */
                    }
                    case Lumen::Do::PCRetC: {
                        /* it was a C function (`precall' called it); adjust results */
                        if (nresults >= 0) L->Top = L->CallInfo->Top;
                        base = L->Base;
                        continue;
                    }
                    default: {
                        return;  /* yield */
                    }
                }
            }
            case Lumen::OpCodeTailCall: {
                int b = LumenOpCodeGetArgB(i);
                if (b != 0) L->Top = ra + b;  /* else previous instruction set top */
                L->SavedPC = pc;
                lua_assert(LumenOpCodeGetArgC(i) - 1 == LUA_MULTRET);
                switch (Lumen::Do::PreCall(L, ra, LUA_MULTRET)) {
                    case Lumen::Do::PCRetLua: {
                        /* tail call: put new frame in place of previous one */
                        Lumen::CallInfo *ci = L->CallInfo - 1;  /* previous frame */
                        int aux;
                        Lumen::StkId func = ci->Func;
                        Lumen::StkId pfunc = (ci + 1)->Func;  /* previous function index */
                        if (L->OpenedUpValue) Lumen::UpValue::Close(L, ci->Base);
                        L->Base = ci->Base = ci->Func + ((ci + 1)->Base - pfunc);
                        for (aux = 0; pfunc + aux < L->Top; aux++)  /* move frame down */
                            LumenSetObjectS2S (L, func + aux, pfunc + aux);
                        ci->Top = L->Top = func + aux;  /* correct top */
                        lua_assert(L->Top == L->Base + LumenClosureValue(func)->AsLua.Func->MaxStackSize);
                        ci->SavedPC = L->SavedPC;
                        ci->NTailCalls++;  /* one more call lost */
                        L->CallInfo--;  /* remove new frame */
                        goto reentry;
                    }
                    case Lumen::Do::PCRetC: {  /* it was a C function (`precall' called it) */
                        base = L->Base;
                        continue;
                    }
                    default: {
                        return;  /* yield */
                    }
                }
            }
            case Lumen::OpCodeReturn: {
                int b = LumenOpCodeGetArgB(i);
                if (b != 0) L->Top = ra + b - 1;
                if (L->OpenedUpValue) Lumen::UpValue::Close(L, base);
                L->SavedPC = pc;
                b = Lumen::Do::PosCall(L, ra);
                if (--nExecCalls == 0)  /* was previous function running `here'? */
                    return;  /* no: return */
                else {  /* yes: continue its execution */
                    if (b) L->Top = L->CallInfo->Top;
                    lua_assert(LumenFuncIsLua(L->CallInfo));
                    lua_assert(LumenOpCodeGet(*((L->CallInfo)->SavedPC - 1)) == Lumen::OpCodeCall);
                    goto reentry;
                }
            }
            case Lumen::OpCodeForLoop: {
                Lumen::Number step = LumenNumberValue(ra + 2);
                Lumen::Number idx = luai_numadd(LumenNumberValue(ra), step); /* increment index */
                Lumen::Number limit = LumenNumberValue(ra + 1);
                if (luai_numlt(0, step) ? luai_numle(idx, limit)
                                        : luai_numle(limit, idx)) {
                    doJump(L, pc, LumenOpCodeGetArgsBx(i));  /* jump back */
                    LumenSetNumberValue(ra, idx);  /* update internal index... */
                    LumenSetNumberValue(ra + 3, idx);  /* ...and external index */
                }
                continue;
            }
            case Lumen::OpCodeForPrep: {
                const Lumen::Value *init = ra;
                const Lumen::Value *plimit = ra + 1;
                const Lumen::Value *pstep = ra + 2;
                L->SavedPC = pc;  /* next steps may throw errors */
                if (!LumenVMToNumber(init, ra))
                    Lumen::Debug::RunError(L, LUA_QL("for") " initial value must be a number");
                else if (!LumenVMToNumber(plimit, ra + 1))
                    Lumen::Debug::RunError(L, LUA_QL("for") " limit must be a number");
                else if (!LumenVMToNumber(pstep, ra + 2))
                    Lumen::Debug::RunError(L, LUA_QL("for") " step must be a number");
                LumenSetNumberValue(ra, luai_numsub(LumenNumberValue(ra), LumenNumberValue(pstep)));
                doJump(L, pc, LumenOpCodeGetArgsBx(i));
                continue;
            }
            case Lumen::OpCodeTForLoop: {
                Lumen::StkId cb = ra + 3;  /* call base */
                LumenSetObjectS2S(L, cb + 2, ra + 2);
                LumenSetObjectS2S(L, cb + 1, ra + 1);
                LumenSetObjectS2S(L, cb, ra);
                L->Top = cb + 3;  /* func. + 2 args (state and index) */
                Protect(Lumen::Do::Call(L, cb, LumenOpCodeGetArgC(i)));
                L->Top = L->CallInfo->Top;
                cb = RA(i) + 3;  /* previous call may change the stack */
                if (!LumenTypeIsNil(cb)) {  /* continue loop? */
                    LumenSetObjectS2S(L, cb - 1, cb);  /* save control variable */
                    doJump(L, pc, LumenOpCodeGetArgsBx(*pc));  /* jump back */
                }
                pc++;
                continue;
            }
            case Lumen::OpCodeSetList: {
                int n = LumenOpCodeGetArgB(i);
                int c = LumenOpCodeGetArgC(i);
                int last;
                Lumen::Table *h;
                if (n == 0) {
                    n = cast_int(L->Top - ra) - 1;
                    L->Top = L->CallInfo->Top;
                }
                if (c == 0) c = cast_int(*pc++);
                runtime_check(L, LumenTypeIsTable(ra));
                h = LumenTableValue(ra);
                last = ((c - 1) * LUA_FIELDS_PER_FLUSH) + n;
                if (last > h->ArrayCount)  /* needs more space? */
                    Lumen::Table::ResizeArray(L, h, last);  /* pre-alloc it at once */
                for (; n > 0; n--) {
                    Lumen::Value *val = ra + n;
                    LumenSetObject2T(L, Lumen::Table::SetNum(L, h, last--), val);
                    LumenGCBarrierTable(L, h, val);
                }
                continue;
            }
            case Lumen::OpCodeClose: {
                Lumen::UpValue::Close(L, ra);
                continue;
            }
            case Lumen::OpCodeClosure: {
                Lumen::Proto *p;
                Lumen::Closure *ncl;
                int nup, j;
                p = cl->Func->SubProto[LumenOpCodeGetArgBx(i)];
                nup = p->NUpValues;
                ncl = Lumen::LClosure::New(L, nup, cl->Env);
                ncl->AsLua.Func = p;
                for (j = 0; j < nup; j++, pc++) {
                    if (LumenOpCodeGet(*pc) == Lumen::OpCodeGetUpVal)
                        ncl->AsLua.UpValues[j] = cl->UpValues[LumenOpCodeGetArgB(*pc)];
                    else {
                        lua_assert(LumenOpCodeGet(*pc) == Lumen::OpCodeMove);
                        ncl->AsLua.UpValues[j] = Lumen::UpValue::Find(L, base + LumenOpCodeGetArgB(*pc));
                    }
                }
                LumenSetClosureValue(L, ra, ncl);
                Protect(LumenGCCheckGC(L));
                continue;
            }
            case Lumen::OpCodeVararg: {
                int b = LumenOpCodeGetArgB(i) - 1;
                int j;
                Lumen::CallInfo *ci = L->CallInfo;
                int n = cast_int(ci->Base - ci->Func) - cl->Func->NUmParams - 1;
                if (b == LUA_MULTRET) {
                    Protect(LumenDoCheckStack(L, n));
                    ra = RA(i);  /* previous call may change the stack */
                    b = n;
                    L->Top = ra + n;
                }
                for (j = 0; j < b; j++) {
                    if (j < n) {
                        LumenSetObjectS2S(L, ra + j, ci->Base - n + j);
                    } else {
                        LumenSetNilValue(ra + j);
                    }
                }
                continue;
            }
        }
    }
}

