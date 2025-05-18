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

#include "lua/debug.h"
#include "lua/do.h"
#include "lua/gc.h"
#include "lua/object.h"
#include "lua/opcodes.h"
#include "lua/state.h"
#include "lua/string.h"
#include "lua/table.h"
#include "lua/tm.h"
#include "lua/vm.h"



/* limit for table tag-method chains (to avoid loops) */
#define LUA_VM_MAX_TAG_LOOP    100


const Lua::Value *Lua::VM::ToNumber(const Lua::Value *obj, Lua::Value *n) {
    Lua::Number num;
    if (LuaTypeIsNumber(obj)) return obj;
    if (LuaTypeIsString(obj) && Lua::String2Decimal(LuaStringValue2CString(obj), &num)) {
        LuaSetNumberValue(n, num);
        return n;
    } else
        return nullptr;
}


int Lua::VM::ToString(Lua::State *L, Lua::StkId obj) {
    if (!LuaTypeIsNumber(obj))
        return 0;
    else {
        char s[LUAI_MAXNUMBER2STR];
        Lua::Number n = LuaNumberValue(obj);
        lua_number2str(s, n);
        LuaSetStringValue2S(L, obj, Lua::String::New(L, s));
        return 1;
    }
}


static void traceExec(Lua::State *L, const Lua::Instruction *pc) {
    Lua::Byte mask = L->HookMask;
    const Lua::Instruction *oldpc = L->SavedPC;
    L->SavedPC = pc;
    if ((mask & LUA_MASKCOUNT) && L->HookCount == 0) {
        LuaDebugResetHookCount(L);
        Lua::Do::CallHook(L, LUA_HOOKCOUNT, -1);
    }
    if (mask & LUA_MASKLINE) {
        Lua::Proto *p = LuaCIFunc(L->CallInfo)->AsLua.Func;
        int npc = LuaDebugPCRel(pc, p);
        int newline = LuaDebugGetLine(p, npc);
        /* call linehook when enter a new function, when jump back (loop),
           or when enter a new line */
        if (npc == 0 || pc <= oldpc || newline != LuaDebugGetLine(p, LuaDebugPCRel(oldpc, p)))
            Lua::Do::CallHook(L, LUA_HOOKLINE, newline);
    }
}


static void callTMRes(Lua::State *L, Lua::StkId res, const Lua::Value *f,
                      const Lua::Value *p1, const Lua::Value *p2) {
    ptrdiff_t result = LuaSaveStack(L, res);
    LuaSetObject2S(L, L->Top, f);  /* push function */
    LuaSetObject2S(L, L->Top + 1, p1);  /* 1st argument */
    LuaSetObject2S(L, L->Top + 2, p2);  /* 2nd argument */
    LuaDoCheckStack(L, 3);
    L->Top += 3;
    Lua::Do::Call(L, L->Top - 3, 1);
    res = LuaRestoreStack(L, result);
    L->Top--;
    LuaSetObjectS2S(L, res, L->Top);
}


static void callTM(Lua::State *L, const Lua::Value *f, const Lua::Value *p1,
                   const Lua::Value *p2, const Lua::Value *p3) {
    LuaSetObject2S(L, L->Top, f);  /* push function */
    LuaSetObject2S(L, L->Top + 1, p1);  /* 1st argument */
    LuaSetObject2S(L, L->Top + 2, p2);  /* 2nd argument */
    LuaSetObject2S(L, L->Top + 3, p3);  /* 3th argument */
    LuaDoCheckStack(L, 4);
    L->Top += 4;
    Lua::Do::Call(L, L->Top - 4, 0);
}


void Lua::VM::GetTable(Lua::State *L, const Lua::Value *t, Lua::Value *key, Lua::StkId val) {
    int loop;
    for (loop = 0; loop < LUA_VM_MAX_TAG_LOOP; loop++) {
        const Lua::Value *tm;
        if (LuaTypeIsTable(t)) {  /* `t' is a table? */
            Lua::Table *h = LuaTableValue(t);
            const Lua::Value *res = Lua::Table::Get(h, key); /* do a primitive get */
            if (!LuaTypeIsNil(res) ||  /* result is no nil? */
                (tm = LuaTMGetFast(L, h->Metatable, Lua::TM::NameIndex)) == nullptr) { /* or no TM? */
                LuaSetObject2S(L, val, res);
                return;
            }
            /* else will try the tag method */
        } else if (LuaTypeIsNil(tm = Lua::TM::GetByObject(L, t, Lua::TM::NameIndex)))
            Lua::Debug::TypeError(L, t, "index");
        if (LuaTypeIsFunction(tm)) {
            callTMRes(L, val, tm, t, key);
            return;
        }
        t = tm;  /* else repeat with `tm' */
    }
    Lua::Debug::RunError(L, "loop in gettable");
}


void Lua::VM::SetTable(Lua::State *L, const Lua::Value *t, Lua::Value *key, Lua::StkId val) {
    int loop;
    Lua::Value temp;
    for (loop = 0; loop < LUA_VM_MAX_TAG_LOOP; loop++) {
        const Lua::Value *tm;
        if (LuaTypeIsTable(t)) {  /* `t' is a table? */
            Lua::Table *h = LuaTableValue(t);
            Lua::Value *oldval = Lua::Table::Set(L, h, key); /* do a primitive set */
            if (!LuaTypeIsNil(oldval) ||  /* result is no nil? */
                (tm = LuaTMGetFast(L, h->Metatable, Lua::TM::NameNewIndex)) == nullptr) { /* or no TM? */
                LuaSetObject2T(L, oldval, val);
                h->Flags = 0;
                LuaGCBarrierTable(L, h, val);
                return;
            }
            /* else will try the tag method */
        } else if (LuaTypeIsNil(tm = Lua::TM::GetByObject(L, t, Lua::TM::NameNewIndex)))
            Lua::Debug::TypeError(L, t, "index");
        if (LuaTypeIsFunction(tm)) {
            callTM(L, tm, t, key, val);
            return;
        }
        /* else repeat with `tm' */
        LuaSetObject(L, &temp, tm);  /* avoid pointing inside table (may rehash) */
        t = &temp;
    }
    Lua::Debug::RunError(L, "loop in settable");
}


static int call_binTM(Lua::State *L, const Lua::Value *p1, const Lua::Value *p2,
                      Lua::StkId res, Lua::TM::Name event) {
    const Lua::Value *tm = Lua::TM::GetByObject(L, p1, event);  /* try first operand */
    if (LuaTypeIsNil(tm))
        tm = Lua::TM::GetByObject(L, p2, event);  /* try second operand */
    if (LuaTypeIsNil(tm)) return 0;
    callTMRes(L, res, tm, p1, p2);
    return 1;
}


static const Lua::Value *get_compTM(Lua::State *L, Lua::Table *mt1, Lua::Table *mt2,
                                    Lua::TM::Name event) {
    const Lua::Value *tm1 = LuaTMGetFast(L, mt1, event);
    const Lua::Value *tm2;
    if (tm1 == nullptr) return nullptr;  /* no metamethod */
    if (mt1 == mt2) return tm1;  /* same metatables => same metamethods */
    tm2 = LuaTMGetFast(L, mt2, event);
    if (tm2 == nullptr) return nullptr;  /* no metamethod */
    if (Lua::RawEqualObject(tm1, tm2))  /* same metamethods? */
        return tm1;
    return nullptr;
}


static int callOrderTM(Lua::State *L, const Lua::Value *p1, const Lua::Value *p2,
                       Lua::TM::Name event) {
    const Lua::Value *tm1 = Lua::TM::GetByObject(L, p1, event);
    const Lua::Value *tm2;
    if (LuaTypeIsNil(tm1)) return -1;  /* no metamethod? */
    tm2 = Lua::TM::GetByObject(L, p2, event);
    if (!Lua::RawEqualObject(tm1, tm2))  /* different metamethods? */
        return -1;
    callTMRes(L, L->Top, tm1, p1, p2);
    return !LuaIsFalse(L->Top);
}


static int luaStrCmp(const Lua::String *ls, const Lua::String *rs) {
    const char *l = LuaStringCString(ls);
    size_t ll = ls->Length;
    const char *r = LuaStringCString(rs);
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


int Lua::VM::LessThan(Lua::State *L, const Lua::Value *l, const Lua::Value *r) {
    int res;
    if (LuaTypeOf(l) != LuaTypeOf(r))
        return Lua::Debug::OrderError(L, l, r);
    else if (LuaTypeIsNumber(l))
        return luai_numlt(LuaNumberValue(l), LuaNumberValue(r));
    else if (LuaTypeIsString(l))
        return luaStrCmp(LuaStringValue(l), LuaStringValue(r)) < 0;
    else if ((res = callOrderTM(L, l, r, Lua::TM::NameLT)) != -1)
        return res;
    return Lua::Debug::OrderError(L, l, r);
}


static int lessEqual(Lua::State *L, const Lua::Value *l, const Lua::Value *r) {
    int res;
    if (LuaTypeOf(l) != LuaTypeOf(r))
        return Lua::Debug::OrderError(L, l, r);
    else if (LuaTypeIsNumber(l))
        return luai_numle(LuaNumberValue(l), LuaNumberValue(r));
    else if (LuaTypeIsString(l))
        return luaStrCmp(LuaStringValue(l), LuaStringValue(r)) <= 0;
    else if ((res = callOrderTM(L, l, r, Lua::TM::NameLE)) != -1)  /* first try `le' */
        return res;
    else if ((res = callOrderTM(L, r, l, Lua::TM::NameLT)) != -1)  /* else try `lt' */
        return !res;
    return Lua::Debug::OrderError(L, l, r);
}


int Lua::VM::EqualVal(Lua::State *L, const Lua::Value *t1, const Lua::Value *t2) {
    const Lua::Value *tm;
    lua_assert(LuaTypeOf(t1) == LuaTypeOf(t2));
    switch (LuaTypeOf(t1)) {
        case LUA_TNIL:
            return 1;
        case LUA_TNUMBER:
            return luai_numeq(LuaNumberValue(t1), LuaNumberValue(t2));
        case LUA_TBOOLEAN:
            return LuaBoolValue(t1) == LuaBoolValue(t2);  /* true must be 1 !! */
        case LUA_TLIGHTUSERDATA:
            return LuaLUDataValue(t1) == LuaLUDataValue(t2);
        case LUA_TUSERDATA: {
            if (LuaUDataValue(t1) == LuaUDataValue(t2)) return 1;
            tm = get_compTM(L, LuaUDataValue(t1)->Metatable, LuaUDataValue(t2)->Metatable,
                            Lua::TM::NameEQ);
            break;  /* will try TM */
        }
        case LUA_TTABLE: {
            if (LuaTableValue(t1) == LuaTableValue(t2)) return 1;
            tm = get_compTM(L, LuaTableValue(t1)->Metatable, LuaTableValue(t2)->Metatable, Lua::TM::NameEQ);
            break;  /* will try TM */
        }
        default:
            return LuaGCValue(t1) == LuaGCValue(t2);
    }
    if (tm == nullptr) return 0;  /* no TM? */
    callTMRes(L, L->Top, tm, t1, t2);  /* call TM */
    return !LuaIsFalse(L->Top);
}


void Lua::VM::Concat(Lua::State *L, int total, int last) {
    do {
        Lua::StkId top = L->Base + last + 1;
        int n = 2;  /* number of elements handled in this pass (at least 2) */
        if (!(LuaTypeIsString(top - 2) || LuaTypeIsNumber(top - 2)) || !LuaVMToString(L, top - 1)) {
            if (!call_binTM(L, top - 2, top - 1, top - 2, Lua::TM::NameConcat))
                Lua::Debug::ConcatError(L, top - 2, top - 1);
        } else if (LuaStringValue(top - 1)->Length == 0)  /* second op is empty? */
            (void) LuaVMToString(L, top - 2);  /* result is first op (as string) */
        else {
            /* at least two string values; get as many as possible */
            size_t tl = LuaStringValue(top - 1)->Length;
            char *buffer;
            int i;
            /* collect total length */
            for (n = 1; n < total && LuaVMToString(L, top - n - 1); n++) {
                size_t l = LuaStringValue(top - n - 1)->Length;
                if (l >= Lua::MaxSize - tl) Lua::Debug::RunError(L, "string length overflow");
                tl += l;
            }
            buffer = Lua::ZBuffer::OpenSpace(L, &LuaGlobal(L)->Buff, tl);
            tl = 0;
            for (i = n; i > 0; i--) {  /* concat all strings */
                size_t l = LuaStringValue(top - i)->Length;
                memcpy(buffer + tl, LuaStringValue2CString(top - i), l);
                tl += l;
            }
            LuaSetStringValue2S(L, top - n, Lua::String::New(L, buffer, tl));
        }
        total -= n - 1;  /* got `n' strings to create 1 new */
        last -= n - 1;
    } while (total > 1);  /* repeat until only 1 result left */
}


static void Arith(Lua::State *L, Lua::StkId ra, const Lua::Value *rb,
                  const Lua::Value *rc, Lua::TM::Name op) {
    Lua::Value tempB, tempC;
    const Lua::Value *b, *c;
    if ((b = Lua::VM::ToNumber(rb, &tempB)) != nullptr &&
        (c = Lua::VM::ToNumber(rc, &tempC)) != nullptr) {
        Lua::Number nb = LuaNumberValue(b), nc = LuaNumberValue(c);
        switch (op) {
            case Lua::TM::NameAdd:
                LuaSetNumberValue(ra, luai_numadd(nb, nc));
                break;
            case Lua::TM::NameSub:
                LuaSetNumberValue(ra, luai_numsub(nb, nc));
                break;
            case Lua::TM::NameMul:
                LuaSetNumberValue(ra, luai_nummul(nb, nc));
                break;
            case Lua::TM::NameDiv:
                LuaSetNumberValue(ra, luai_numdiv(nb, nc));
                break;
            case Lua::TM::NameMod:
                LuaSetNumberValue(ra, luai_nummod(nb, nc));
                break;
            case Lua::TM::NamePow:
                LuaSetNumberValue(ra, luai_numpow(nb, nc));
                break;
            case Lua::TM::NameUnm:
                LuaSetNumberValue(ra, luai_numunm(nb));
                break;
            default:
                lua_assert(0);
                break;
        }
    } else if (!call_binTM(L, rb, rc, ra, op))
        Lua::Debug::ArithError(L, rb, rc);
}

/*
** some macros for common tasks in `Lua::VM::Execute'
*/

#define runtime_check(L, c)    { if (!(c)) break; }

#define RA(i)    (base+LuaOpCodeGetArgA(i))
/* to be used after possible stack reallocation */
#define RB(i)    LuaCheckExp(LuaGetBMode(LuaOpCodeGet(i)) == Lua::OpArgR, base+LuaOpCodeGetArgB(i))
#define RC(i)    LuaCheckExp(LuaGetCMode(LuaOpCodeGet(i)) == Lua::OpArgR, base+LuaOpCodeGetArgC(i))
#define RKB(i)    LuaCheckExp(LuaGetBMode(LuaOpCodeGet(i)) == Lua::OpArgK, \
    LuaOpCodeIsK(LuaOpCodeGetArgB(i)) ? k+LuaOpCodeIndexK(LuaOpCodeGetArgB(i)) : base+LuaOpCodeGetArgB(i))
#define RKC(i)    LuaCheckExp(LuaGetCMode(LuaOpCodeGet(i)) == Lua::OpArgK, \
    LuaOpCodeIsK(LuaOpCodeGetArgC(i)) ? k+LuaOpCodeIndexK(LuaOpCodeGetArgC(i)) : base+LuaOpCodeGetArgC(i))
#define KBx(i)    LuaCheckExp(LuaGetBMode(LuaOpCodeGet(i)) == Lua::OpArgK, k+LuaOpCodeGetArgBx(i))


#define doJump(L, pc, i) \
LuaDo(                   \
    (pc) += (i);         \
    LuaThreadYield(L);   \
)


#define Protect(x) do { \
    L->SavedPC = pc;    \
    {                   \
        x;              \
    }                   \
    base = L->Base;     \
} while (0)


#define arith_op(op, tm) do { \
    Lua::Value *rb = RKB(i); \
    Lua::Value *rc = RKC(i); \
    if (LuaTypeIsNumber(rb) && LuaTypeIsNumber(rc)) { \
        Lua::Number nb = LuaNumberValue(rb), nc = LuaNumberValue(rc); \
        LuaSetNumberValue(ra, op(nb, nc));            \
    } else {                  \
        Protect(Arith(L, ra, rb, rc, tm));            \
    }                         \
} while (0)


void Lua::VM::Execute(Lua::State *L, int nExecCalls) {
    Lua::LClosure *cl;
    Lua::StkId base;
    Lua::Value *k;
    const Lua::Instruction *pc;
    reentry:  /* entry point */
    lua_assert(LuaFuncIsLua(L->CallInfo));
    pc = L->SavedPC;
    cl = &LuaClosureValue(L->CallInfo->Func)->AsLua;
    base = L->Base;
    k = cl->Func->K;
    /* main loop of interpreter */
    for (;;) {
        const Lua::Instruction i = *pc++;
        Lua::StkId ra;
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
        lua_assert(L->Top == L->CallInfo->Top || Lua::Debug::CheckOpenOP(i));
        switch (LuaOpCodeGet(i)) {
            case Lua::OpCodeMove: {
                LuaSetObjectS2S(L, ra, RB(i));
                continue;
            }
            case Lua::OpCodeLoadK: {
                LuaSetObject2S(L, ra, KBx(i));
                continue;
            }
            case Lua::OpCodeLoadBool: {
                LuaSetBoolValue(ra, LuaOpCodeGetArgB(i));
                if (LuaOpCodeGetArgC(i)) pc++;  /* skip next instruction (if C) */
                continue;
            }
            case Lua::OpCodeLoadNil: {
                Lua::Value *rb = RB(i);
                do {
                    LuaSetNilValue(rb--);
                } while (rb >= ra);
                continue;
            }
            case Lua::OpCodeGetUpVal: {
                int b = LuaOpCodeGetArgB(i);
                LuaSetObject2S(L, ra, cl->UpValues[b]->SelfValue);
                continue;
            }
            case Lua::OpCodeGetGlobal: {
                Lua::Value g;
                Lua::Value *rb = KBx(i);
                LuaSetTableValue(L, &g, cl->Env);
                lua_assert(LuaTypeIsString(rb));
                Protect(Lua::VM::GetTable(L, &g, rb, ra));
                continue;
            }
            case Lua::OpCodeGetTable: {
                Protect(Lua::VM::GetTable(L, RB(i), RKC(i), ra));
                continue;
            }
            case Lua::OpCodeSetGlobal: {
                Lua::Value g;
                LuaSetTableValue(L, &g, cl->Env);
                lua_assert(LuaTypeIsString(KBx(i)));
                Protect(Lua::VM::SetTable(L, &g, KBx(i), ra));
                continue;
            }
            case Lua::OpCodeSetUpVal: {
                Lua::UpValue *uv = cl->UpValues[LuaOpCodeGetArgB(i)];
                LuaSetObject(L, uv->SelfValue, ra);
                LuaGCBarrier(L, uv, ra);
                continue;
            }
            case Lua::OpCodeSetTable: {
                Protect(Lua::VM::SetTable(L, ra, RKB(i), RKC(i)));
                continue;
            }
            case Lua::OpCodeNewTable: {
                int b = LuaOpCodeGetArgB(i);
                int c = LuaOpCodeGetArgC(i);
                LuaSetTableValue(L, ra, Lua::Table::New(L, Lua::FB2Int(b), Lua::FB2Int(c)));
                Protect(LuaGCCheckGC(L));
                continue;
            }
            case Lua::OpCodeSelf: {
                Lua::StkId rb = RB(i);
                LuaSetObjectS2S(L, ra + 1, rb);
                Protect(Lua::VM::GetTable(L, rb, RKC(i), ra));
                continue;
            }
            case Lua::OpCodeAdd: {
                arith_op(luai_numadd, Lua::TM::NameAdd);
                continue;
            }
            case Lua::OpCodeSub: {
                arith_op(luai_numsub, Lua::TM::NameSub);
                continue;
            }
            case Lua::OpCodeMul: {
                arith_op(luai_nummul, Lua::TM::NameMul);
                continue;
            }
            case Lua::OpCodeDiv: {
                arith_op(luai_numdiv, Lua::TM::NameDiv);
                continue;
            }
            case Lua::OpCodeMod: {
                arith_op(luai_nummod, Lua::TM::NameMod);
                continue;
            }
            case Lua::OpCodePow: {
                arith_op(luai_numpow, Lua::TM::NamePow);
                continue;
            }
            case Lua::OpCodeUnm: {
                Lua::Value *rb = RB(i);
                if (LuaTypeIsNumber(rb)) {
                    Lua::Number nb = LuaNumberValue(rb);
                    LuaSetNumberValue(ra, luai_numunm(nb));
                } else {
                    Protect(Arith(L, ra, rb, rb, Lua::TM::NameUnm));
                }
                continue;
            }
            case Lua::OpCodeNot: {
                int res = LuaIsFalse(RB(i));  /* next assignment may change this value */
                LuaSetBoolValue(ra, res);
                continue;
            }
            case Lua::OpCodeLen: {
                const Lua::Value *rb = RB(i);
                switch (LuaTypeOf(rb)) {
                    case LUA_TTABLE: {
                        LuaSetNumberValue(ra, cast_num(Lua::Table::GetN(LuaTableValue(rb))));
                        break;
                    }
                    case LUA_TSTRING: {
                        LuaSetNumberValue(ra, cast_num(LuaStringValue(rb)->Length));
                        break;
                    }
                    default: {  /* try metamethod */
                        Protect(
                                if (!call_binTM(L, rb, Lua::NilObject, ra, Lua::TM::NameLen))
                                    Lua::Debug::TypeError(L, rb, "get length of");
                        );
                    }
                }
                continue;
            }
            case Lua::OpCodeConcat: {
                int b = LuaOpCodeGetArgB(i);
                int c = LuaOpCodeGetArgC(i);
                Protect(Lua::VM::Concat(L, c - b + 1, c); LuaGCCheckGC(L));
                LuaSetObjectS2S(L, RA(i), base + b);
                continue;
            }
            case Lua::OpCodeJump: {
                doJump(L, pc, LuaOpCodeGetArgsBx(i));
                continue;
            }
            case Lua::OpCodeEQ: {
                Lua::Value *rb = RKB(i);
                Lua::Value *rc = RKC(i);
                Protect(
                        if (LuaVMEqualObj(L, rb, rc) == LuaOpCodeGetArgA(i))
                            doJump(L, pc, LuaOpCodeGetArgsBx(*pc));
                );
                pc++;
                continue;
            }
            case Lua::OpCodeLT: {
                Protect(
                        if (Lua::VM::LessThan(L, RKB(i), RKC(i)) == LuaOpCodeGetArgA(i))
                            doJump(L, pc, LuaOpCodeGetArgsBx(*pc));
                );
                pc++;
                continue;
            }
            case Lua::OpCodeLE: {
                Protect(
                        if (lessEqual(L, RKB(i), RKC(i)) == LuaOpCodeGetArgA(i))
                            doJump(L, pc, LuaOpCodeGetArgsBx(*pc));
                );
                pc++;
                continue;
            }
            case Lua::OpCodeTest: {
                if (LuaIsFalse(ra) != LuaOpCodeGetArgC(i)) doJump(L, pc, LuaOpCodeGetArgsBx(*pc));
                pc++;
                continue;
            }
            case Lua::OpCodeTestTest: {
                Lua::Value *rb = RB(i);
                if (LuaIsFalse(rb) != LuaOpCodeGetArgC(i)) {
                    LuaSetObjectS2S(L, ra, rb);
                    doJump(L, pc, LuaOpCodeGetArgsBx(*pc));
                }
                pc++;
                continue;
            }
            case Lua::OpCodeCall: {
                int b = LuaOpCodeGetArgB(i);
                int nresults = LuaOpCodeGetArgC(i) - 1;
                if (b != 0) L->Top = ra + b;  /* else previous instruction set top */
                L->SavedPC = pc;
                switch (Lua::Do::PreCall(L, ra, nresults)) {
                    case Lua::Do::PCRetLua: {
                        nExecCalls++;
                        goto reentry;  /* restart Lua::VM::Execute over new Lua function */
                    }
                    case Lua::Do::PCRetC: {
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
            case Lua::OpCodeTailCall: {
                int b = LuaOpCodeGetArgB(i);
                if (b != 0) L->Top = ra + b;  /* else previous instruction set top */
                L->SavedPC = pc;
                lua_assert(LuaOpCodeGetArgC(i) - 1 == LUA_MULTRET);
                switch (Lua::Do::PreCall(L, ra, LUA_MULTRET)) {
                    case Lua::Do::PCRetLua: {
                        /* tail call: put new frame in place of previous one */
                        Lua::CallInfo *ci = L->CallInfo - 1;  /* previous frame */
                        int aux;
                        Lua::StkId func = ci->Func;
                        Lua::StkId pfunc = (ci + 1)->Func;  /* previous function index */
                        if (L->OpenedUpValue) Lua::UpValue::Close(L, ci->Base);
                        L->Base = ci->Base = ci->Func + ((ci + 1)->Base - pfunc);
                        for (aux = 0; pfunc + aux < L->Top; aux++)  /* move frame down */
                            LuaSetObjectS2S (L, func + aux, pfunc + aux);
                        ci->Top = L->Top = func + aux;  /* correct top */
                        lua_assert(L->Top == L->Base + LuaClosureValue(func)->AsLua.Func->MaxStackSize);
                        ci->SavedPC = L->SavedPC;
                        ci->NTailCalls++;  /* one more call lost */
                        L->CallInfo--;  /* remove new frame */
                        goto reentry;
                    }
                    case Lua::Do::PCRetC: {  /* it was a C function (`precall' called it) */
                        base = L->Base;
                        continue;
                    }
                    default: {
                        return;  /* yield */
                    }
                }
            }
            case Lua::OpCodeReturn: {
                int b = LuaOpCodeGetArgB(i);
                if (b != 0) L->Top = ra + b - 1;
                if (L->OpenedUpValue) Lua::UpValue::Close(L, base);
                L->SavedPC = pc;
                b = Lua::Do::PosCall(L, ra);
                if (--nExecCalls == 0)  /* was previous function running `here'? */
                    return;  /* no: return */
                else {  /* yes: continue its execution */
                    if (b) L->Top = L->CallInfo->Top;
                    lua_assert(LuaFuncIsLua(L->CallInfo));
                    lua_assert(LuaOpCodeGet(*((L->CallInfo)->SavedPC - 1)) == Lua::OpCodeCall);
                    goto reentry;
                }
            }
            case Lua::OpCodeForLoop: {
                Lua::Number step = LuaNumberValue(ra + 2);
                Lua::Number idx = luai_numadd(LuaNumberValue(ra), step); /* increment index */
                Lua::Number limit = LuaNumberValue(ra + 1);
                if (luai_numlt(0, step) ? luai_numle(idx, limit)
                                        : luai_numle(limit, idx)) {
                    doJump(L, pc, LuaOpCodeGetArgsBx(i));  /* jump back */
                    LuaSetNumberValue(ra, idx);  /* update internal index... */
                    LuaSetNumberValue(ra + 3, idx);  /* ...and external index */
                }
                continue;
            }
            case Lua::OpCodeForPrep: {
                const Lua::Value *init = ra;
                const Lua::Value *plimit = ra + 1;
                const Lua::Value *pstep = ra + 2;
                L->SavedPC = pc;  /* next steps may throw errors */
                if (!LuaVMToNumber(init, ra))
                    Lua::Debug::RunError(L, LUA_QL("for") " initial value must be a number");
                else if (!LuaVMToNumber(plimit, ra + 1))
                    Lua::Debug::RunError(L, LUA_QL("for") " limit must be a number");
                else if (!LuaVMToNumber(pstep, ra + 2))
                    Lua::Debug::RunError(L, LUA_QL("for") " step must be a number");
                LuaSetNumberValue(ra, luai_numsub(LuaNumberValue(ra), LuaNumberValue(pstep)));
                doJump(L, pc, LuaOpCodeGetArgsBx(i));
                continue;
            }
            case Lua::OpCodeTForLoop: {
                Lua::StkId cb = ra + 3;  /* call base */
                LuaSetObjectS2S(L, cb + 2, ra + 2);
                LuaSetObjectS2S(L, cb + 1, ra + 1);
                LuaSetObjectS2S(L, cb, ra);
                L->Top = cb + 3;  /* func. + 2 args (state and index) */
                Protect(Lua::Do::Call(L, cb, LuaOpCodeGetArgC(i)));
                L->Top = L->CallInfo->Top;
                cb = RA(i) + 3;  /* previous call may change the stack */
                if (!LuaTypeIsNil(cb)) {  /* continue loop? */
                    LuaSetObjectS2S(L, cb - 1, cb);  /* save control variable */
                    doJump(L, pc, LuaOpCodeGetArgsBx(*pc));  /* jump back */
                }
                pc++;
                continue;
            }
            case Lua::OpCodeSetList: {
                int n = LuaOpCodeGetArgB(i);
                int c = LuaOpCodeGetArgC(i);
                int last;
                Lua::Table *h;
                if (n == 0) {
                    n = cast_int(L->Top - ra) - 1;
                    L->Top = L->CallInfo->Top;
                }
                if (c == 0) c = cast_int(*pc++);
                runtime_check(L, LuaTypeIsTable(ra));
                h = LuaTableValue(ra);
                last = ((c - 1) * LUA_FIELDS_PER_FLUSH) + n;
                if (last > h->ArrayCount)  /* needs more space? */
                    Lua::Table::ResizeArray(L, h, last);  /* pre-alloc it at once */
                for (; n > 0; n--) {
                    Lua::Value *val = ra + n;
                    LuaSetObject2T(L, Lua::Table::SetNum(L, h, last--), val);
                    LuaGCBarrierTable(L, h, val);
                }
                continue;
            }
            case Lua::OpCodeClose: {
                Lua::UpValue::Close(L, ra);
                continue;
            }
            case Lua::OpCodeClosure: {
                Lua::Proto *p;
                Lua::Closure *ncl;
                int nup, j;
                p = cl->Func->SubProto[LuaOpCodeGetArgBx(i)];
                nup = p->NUpValues;
                ncl = Lua::LClosure::New(L, nup, cl->Env);
                ncl->AsLua.Func = p;
                for (j = 0; j < nup; j++, pc++) {
                    if (LuaOpCodeGet(*pc) == Lua::OpCodeGetUpVal)
                        ncl->AsLua.UpValues[j] = cl->UpValues[LuaOpCodeGetArgB(*pc)];
                    else {
                        lua_assert(LuaOpCodeGet(*pc) == Lua::OpCodeMove);
                        ncl->AsLua.UpValues[j] = Lua::UpValue::Find(L, base + LuaOpCodeGetArgB(*pc));
                    }
                }
                LuaSetClosureValue(L, ra, ncl);
                Protect(LuaGCCheckGC(L));
                continue;
            }
            case Lua::OpCodeVararg: {
                int b = LuaOpCodeGetArgB(i) - 1;
                int j;
                Lua::CallInfo *ci = L->CallInfo;
                int n = cast_int(ci->Base - ci->Func) - cl->Func->NUmParams - 1;
                if (b == LUA_MULTRET) {
                    Protect(LuaDoCheckStack(L, n));
                    ra = RA(i);  /* previous call may change the stack */
                    b = n;
                    L->Top = ra + n;
                }
                for (j = 0; j < b; j++) {
                    if (j < n) {
                        LuaSetObjectS2S(L, ra + j, ci->Base - n + j);
                    } else {
                        LuaSetNilValue(ra + j);
                    }
                }
                continue;
            }
        }
    }
}

