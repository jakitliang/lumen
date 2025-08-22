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
#include <charconv>

#define LUA_CORE

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/opcodes.h"
#include "lumen/state.h"
#include "lumen/tm.h"
#include "lumen/vm.h"



/* limit for table tag-method chains (to avoid loops) */
#define LUA_VM_MAX_TAG_LOOP    100

static void traceExec(Lumen::State *L, const Lumen::Instruction *pc) {
    Lumen::Byte mask = L->HookMask;
    const Lumen::Instruction *oldpc = L->SavedPC;
    L->SavedPC = pc;
    if ((mask & Lumen::HookMaskCount) && L->HookCount == 0) {
        LumenDebugResetHookCount(L);
        Lumen::Do::CallHook(L, Lumen::HookCount, -1);
    }
    if (mask & Lumen::HookMaskLine) {
        Lumen::Proto *p = L->CallInfo->GetFunction()->AsLua.Func;
        int npc = LumenDebugPCRel(pc, p);
        int newline = LumenDebugGetLine(p, npc);
        /* call linehook when enter a new function, when jump back (loop),
           or when enter a new line */
        if (npc == 0 || pc <= oldpc || newline != LumenDebugGetLine(p, LumenDebugPCRel(oldpc, p)))
            Lumen::Do::CallHook(L, Lumen::HookLine, newline);
    }
}


void Lumen::VM::CallTMRes(Lumen::State *L, Lumen::Value res, const Lumen::Object *f,
                          const Lumen::Object *p1, const Lumen::Object *p2) {
    Lumen::Integer result = LumenSaveStack(L, res);
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


void Lumen::VM::CallTM(Lumen::State *L, const Lumen::Object *f, const Lumen::Object *p1,
                       const Lumen::Object *p2, const Lumen::Object *p3) {
    LumenSetObject2S(L, L->Top, f);  /* push function */
    LumenSetObject2S(L, L->Top + 1, p1);  /* 1st argument */
    LumenSetObject2S(L, L->Top + 2, p2);  /* 2nd argument */
    LumenSetObject2S(L, L->Top + 3, p3);  /* 3rd argument */
    LumenDoCheckStack(L, 4);
    L->Top += 4;
    Lumen::Do::Call(L, L->Top - 4, 0);
}


void Lumen::VM::GetTable(Lumen::State *L, const Lumen::Object *t, Lumen::Object *key, Lumen::Value val) {
    int loop;
    for (loop = 0; loop < LUA_VM_MAX_TAG_LOOP; loop++) {
        const Lumen::Object *tm;
        if (t->IsTable()) {  /* `t` is a table? */
            Lumen::Table *h = t->GetTable();
            const Lumen::Object *res = Lumen::Table::Get(h, key); /* do a primitive get */
            if (!res->IsNil() ||  /* result is no nil? */
                (tm = LumenMetaMethodGetFast(L, h->Metatable, Lumen::MetaMethod::NameIndex)) ==
                nullptr) { /* or no TM? */
                LumenSetObject2S(L, val, res);
                return;
            }
            /* else will try the tag method */
        } else if ((tm = Lumen::MetaMethod::GetByObject(L, t, Lumen::MetaMethod::NameIndex))->IsNil())
            Lumen::Debug::TypeError(L, t, "index");
        if (tm->IsFunction()) {
            CallTMRes(L, val, tm, t, key);
            return;
        }
        t = tm;  /* else repeat with `tm` */
    }
    Lumen::Debug::RunError(L, "loop in gettable");
}

void Lumen::VM::FinishGetTable(Lumen::State *L,
                               const Lumen::Object *t, Lumen::Object *key, Lumen::Value val,
                               const Lumen::Object *cachedSlot) {
    int loop;
    for (loop = 0; loop < LUA_VM_MAX_TAG_LOOP; loop++) {
        const Lumen::Object *tm;
        if (t->IsTable()) {  /* `t` is a table? */
            Lumen::Table *h = t->GetTable();
            const Lumen::Object *res;
            if (cachedSlot == nullptr) {
                res = Lumen::Table::Get(h, key); /* do a primitive get */
            } else {
                res = cachedSlot;
                cachedSlot = nullptr;
            }
            if (!res->IsNil() ||  /* result is no nil? */
                (tm = LumenMetaMethodGetFast(L, h->Metatable, Lumen::MetaMethod::NameIndex)) ==
                nullptr) { /* or no TM? */
                LumenSetObject2S(L, val, res);
                return;
            }
            /* else will try the tag method */
        } else if ((tm = Lumen::MetaMethod::GetByObject(L, t, Lumen::MetaMethod::NameIndex))->IsNil())
            Lumen::Debug::TypeError(L, t, "index");
        if (tm->IsFunction()) {
            CallTMRes(L, val, tm, t, key);
            return;
        }
        t = tm;  /* else repeat with `tm` */
    }
    Lumen::Debug::RunError(L, "loop in gettable");
}

void Lumen::VM::SetTable(Lumen::State *L, const Lumen::Object *t, Lumen::Object *key, Lumen::Value val) {
    int loop;
    Lumen::Object temp; // NOLINT
    for (loop = 0; loop < LUA_VM_MAX_TAG_LOOP; loop++) {
        const Lumen::Object *tm;
        if (t->IsTable()) {  /* `t` is a table? */
            Lumen::Table *h = t->GetTable();
            Lumen::Object *oldVal = Lumen::Table::Set(L, h, key); /* do a primitive set */
            if (!oldVal->IsNil() ||  /* result is no nil? */
                (tm = LumenMetaMethodGetFast(L, h->Metatable, Lumen::MetaMethod::NameNewIndex)) ==
                nullptr) { /* or no TM? */
                LumenSetObject2T(L, oldVal, val);
                h->Flags = 0;
                L->BarrierTable(h, val);
                return;
            }
            /* else will try the tag method */
        } else if ((tm = Lumen::MetaMethod::GetByObject(L, t, Lumen::MetaMethod::NameNewIndex))->IsNil())
            Lumen::Debug::TypeError(L, t, "index");
        if (tm->IsFunction()) {
            Lumen::VM::CallTM(L, tm, t, key, val);
            return;
        }
        /* else repeat with `tm` */
        (&temp)->SetObject(L, tm);  /* avoid pointing inside table (may rehash) */
        t = &temp;
    }
    Lumen::Debug::RunError(L, "loop in settable");
}

void Lumen::VM::FinishSetTable(Lumen::State *L,
                               const Lumen::Object *t, Lumen::Object *key, Lumen::Value val,
                               const Lumen::Object *cachedSlot) {
    int loop;
    Lumen::Object temp; // NOLINT
    for (loop = 0; loop < LUA_VM_MAX_TAG_LOOP; loop++) {
        const Lumen::Object *tm;
        if (t->IsTable()) {  /* `t` is a table? */
            Lumen::Table *h = t->GetTable();
            Lumen::Object *oldVal;
            if (cachedSlot == nullptr) {
                oldVal = Lumen::Table::Set(L, h, key); /* do a primitive set */
            } else {
                oldVal = Lumen::Table::Set(L, h, key, cachedSlot);
                cachedSlot = nullptr;
            }
            if (!oldVal->IsNil() ||  /* result is no nil? */
                (tm = LumenMetaMethodGetFast(L, h->Metatable, Lumen::MetaMethod::NameNewIndex)) ==
                nullptr) { /* or no TM? */
                LumenSetObject2T(L, oldVal, val);
                h->Flags = 0;
                L->BarrierTable(h, val);
                return;
            }
            /* else will try the tag method */
        } else if ((tm = Lumen::MetaMethod::GetByObject(L, t, Lumen::MetaMethod::NameNewIndex))->IsNil())
            Lumen::Debug::TypeError(L, t, "index");
        if (tm->IsFunction()) {
            Lumen::VM::CallTM(L, tm, t, key, val);
            return;
        }
        /* else repeat with `tm` */
        (&temp)->SetObject(L, tm);  /* avoid pointing inside table (may rehash) */
        t = &temp;
    }
    Lumen::Debug::RunError(L, "loop in settable");
}

static inline int call_binTM(Lumen::State *L, const Lumen::Object *p1, const Lumen::Object *p2,
                             Lumen::Value res, Lumen::MetaMethod::Name event) {
    const Lumen::Object *tm = Lumen::MetaMethod::GetByObject(L, p1, event);  /* try first operand */
    if (tm->IsNil())
        tm = Lumen::MetaMethod::GetByObject(L, p2, event);  /* try second operand */
    if (tm->IsNil()) return 0;
    Lumen::VM::CallTMRes(L, res, tm, p1, p2);
    return 1;
}


static inline const Lumen::Object *get_compTM(Lumen::State *L, Lumen::Table *mt1, Lumen::Table *mt2,
                                              Lumen::MetaMethod::Name event) {
    const Lumen::Object *tm1 = LumenMetaMethodGetFast(L, mt1, event);
    const Lumen::Object *tm2;
    if (tm1 == nullptr) return nullptr;  /* no metamethod */
    if (mt1 == mt2) return tm1;  /* same metatables => same metamethods */
    tm2 = LumenMetaMethodGetFast(L, mt2, event);
    if (tm2 == nullptr) return nullptr;  /* no metamethod */
    if (Lumen::RawEqualObject(tm1, tm2))  /* same metamethods? */
        return tm1;
    return nullptr;
}


static inline int callOrderTM(Lumen::State *L, const Lumen::Object *p1, const Lumen::Object *p2,
                              Lumen::MetaMethod::Name event) {
    const Lumen::Object *tm1 = Lumen::MetaMethod::GetByObject(L, p1, event);
    const Lumen::Object *tm2;
    if (tm1->IsNil()) return -1;  /* no metamethod? */
    tm2 = Lumen::MetaMethod::GetByObject(L, p2, event);
    if (!Lumen::RawEqualObject(tm1, tm2))  /* different metamethods? */
        return -1;
    Lumen::VM::CallTMRes(L, L->Top, tm1, p1, p2);
    return !(L->Top)->IsFalse();
}


static int luaStrCmp(const Lumen::String *ls, const Lumen::String *rs) {
    const char *l = ls->CString();
    Lumen::UInteger ll = ls->Length;
    const char *r = rs->CString();
    Lumen::UInteger lr = rs->Length;
    for (;;) {
        int temp = strcoll(l, r);
        if (temp != 0) return temp;
        else {  /* strings are equal up to a `\0' */
            Lumen::UInteger len = Lumen::String::LengthOf(l);  /* index of first `\0' in both strings */
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


int Lumen::VM::LessThan(Lumen::State *L, const Lumen::Object *l, const Lumen::Object *r) {
    int res;
    if (l->Type != r->Type)
        return Lumen::Debug::OrderError(L, l, r);
    else if (l->IsNumber())
        return LumenNumLT(l->GetNumber(), r->GetNumber());
    else if (l->IsString())
        return luaStrCmp(l->GetString(), r->GetString()) < 0;
    else if ((res = callOrderTM(L, l, r, Lumen::MetaMethod::NameLT)) != -1)
        return res;
    return Lumen::Debug::OrderError(L, l, r);
}


int Lumen::VM::LessEqual(Lumen::State *L, const Lumen::Object *l, const Lumen::Object *r) {
    int res;
    if (l->Type != r->Type)
        return Lumen::Debug::OrderError(L, l, r);
    else if (l->IsNumber())
        return LumenNumLE(l->GetNumber(), r->GetNumber());
    else if (l->IsString())
        return luaStrCmp(l->GetString(), r->GetString()) <= 0;
    else if ((res = callOrderTM(L, l, r, Lumen::MetaMethod::NameLE)) != -1)  /* first try `le' */
        return res;
    else if ((res = callOrderTM(L, r, l, Lumen::MetaMethod::NameLT)) != -1)  /* else try `lt' */
        return !res;
    return Lumen::Debug::OrderError(L, l, r);
}


int Lumen::VM::EqualObject(Lumen::State *L, const Lumen::Object *t1, const Lumen::Object *t2) {
    const Lumen::Object *tm;
    LumenAssert(t1->Type == t2->Type);
    switch (t1->Type) {
        case Lumen::TypeNil:
            return 1;
        case Lumen::TypeNumber:
            return LumenNumEQ(t1->GetNumber(), t2->GetNumber());
        case Lumen::TypeBool:
            return t1->GetBool() == t2->GetBool();  /* true must be 1 !! */
        case Lumen::TypeLightUserdata:
            return t1->GetLUData() == t2->GetLUData();
        case Lumen::TypeUserdata: {
            if (t1->GetUData() == t2->GetUData()) return 1;
            tm = get_compTM(L, t1->GetUData()->Metatable, t2->GetUData()->Metatable,
                            Lumen::MetaMethod::NameEQ);
            break;  /* will try TM */
        }
        case Lumen::TypeTable: {
            if (t1->GetTable() == t2->GetTable()) return 1;
            tm = get_compTM(L, t1->GetTable()->Metatable, t2->GetTable()->Metatable, Lumen::MetaMethod::NameEQ);
            break;  /* will try TM */
        }
        default:
            return t1->GetGCObject() == t2->GetGCObject();
    }
    if (tm == nullptr) return 0;  /* no TM? */
    CallTMRes(L, L->Top, tm, t1, t2);  /* call TM */
    return !L->Top->IsFalse();
}


void Lumen::VM::Concat(Lumen::State *L, int total, int last) {
    do {
        Lumen::Value top = L->Base + last + 1;
        int n = 2;  /* number of elements handled in this pass (at least 2) */

        Lumen::String *s1 = nullptr;
        Lumen::String *s2 = nullptr;
        bool is_str1 = Lumen::VM::FastToString(L, top - 2);
        bool is_str2 = Lumen::VM::FastToString(L, top - 1);

        if (!(is_str1 || (top - 2)->IsNumber()) || !is_str2) {
            if (!call_binTM(L, top - 2, top - 1, top - 2, Lumen::MetaMethod::NameConcat))
                Lumen::Debug::ConcatError(L, top - 2, top - 1);
        } else {
            s1 = (top - 2)->GetString();
            s2 = (top - 1)->GetString();

            if (s2->Length != 0) { /* second op is not empty? */
                /* at least two string values; get as many as possible */
                Lumen::UInteger tl = s2->Length;
                char *buffer;
                int i;
                /* collect total length */
                for (n = 1; n < total && Lumen::VM::FastToString(L, top - n - 1); n++) {
                    Lumen::UInteger l = (top - n - 1)->GetString()->Length;
                    if (l >= Lumen::MaxSize - tl) Lumen::Debug::RunError(L, "string length overflow");
                    tl += l;
                }
                buffer = Lumen::ZBuffer::OpenSpace(L, &LumenGlobalState(L)->Buff, tl);
                tl = 0;
                for (i = n; i > 0; i--) {  /* concat all strings */
                    auto s3 = (top - i)->GetString();
                    Lumen::UInteger l = s3->Length;
                    memcpy(buffer + tl, reinterpret_cast<char *>(s3 + 1), l);
                    tl += l;
                }
                LumenSetStringValue2S(L, top - n, Lumen::String::New(L, buffer, tl));
            }
        }
        total -= n - 1;  /* got `n' strings to create 1 new */
        last -= n - 1;
    } while (total > 1);  /* repeat until only 1 result left */
}

void Lumen::VM::ArithValue(Lumen::State *L, Lumen::Value ra, const Lumen::Object *rb, const Lumen::Object *rc,
                           Lumen::MetaMethod::Name op) {
    Lumen::Object tempB, tempC; // NOLINT
    const Lumen::Object *b, *c;
    if ((b = ToNumber(rb, &tempB)) != nullptr &&
        (c = ToNumber(rc, &tempC)) != nullptr) {
        Lumen::Number res = Lumen::Arith(op - Lumen::MetaMethod::NameAdd + Lumen::ArithOpAdd,
                                         b->GetNumber(), c->GetNumber());
        ra->SetNumber(res);
    } else if (!call_binTM(L, rb, rc, ra, op))
        Lumen::Debug::ArithError(L, rb, rc);
}

void Lumen::VM::ObjectLength(Lumen::State *L, Lumen::Value ra, const Lumen::Object *rb) {
    switch (rb->Type) {
        case Lumen::TypeTable: {
            ra->SetNumber(cast_num(Lumen::Table::GetN(rb->GetTable())));
            break;
        }
        case Lumen::TypeString: {
            ra->SetNumber(cast_num(rb->GetString()->Length));
            break;
        }
        default: {  /* try metamethod */
            if (!call_binTM(L, rb, Lumen::NilObject, ra, Lumen::MetaMethod::NameLen))
                Lumen::Debug::TypeError(L, rb, "get length of");
        }
    }
}

int Lumen::VM::ToString(Lumen::State *L, Lumen::Value obj) {
    if (!obj->IsNumber())
        return 0;
    else {
        char s[LUA_MAX_NUMBER2STR];
        Lumen::Number n = obj->GetNumber();
        LumenNum2Str(s, n);
        auto [ptr, ec] = std::to_chars(
            s,
            s + sizeof(s),
            n,
            std::chars_format::general,
            14 // %.14g
        );
        if (ec != std::errc()) {
            return 0;
        }
        LumenSetStringValue2S(L, obj, Lumen::String::New(L, s));
        return 1;
    }
}

static void arith(Lumen::State *L, Lumen::Value ra, const Lumen::Object *rb,
                  const Lumen::Object *rc, Lumen::MetaMethod::Name op) {
    Lumen::Object tempB, tempC; // NOLINT
    const Lumen::Object *b, *c;
    if ((b = Lumen::VM::ToNumber(rb, &tempB)) != nullptr &&
        (c = Lumen::VM::ToNumber(rc, &tempC)) != nullptr) {
        Lumen::Number nb = b->GetNumber(), nc = c->GetNumber();
        switch (op) {
            case Lumen::MetaMethod::NameAdd:
                ra->SetNumber(LumenNumAdd(nb, nc));
                break;
            case Lumen::MetaMethod::NameSub:
                ra->SetNumber(LumenNumSub(nb, nc));
                break;
            case Lumen::MetaMethod::NameMul:
                ra->SetNumber(LumenNumMul(nb, nc));
                break;
            case Lumen::MetaMethod::NameDiv:
                ra->SetNumber(LumenNumDiv(nb, nc));
                break;
            case Lumen::MetaMethod::NameMod:
                ra->SetNumber(LumenNumMod(nb, nc));
                break;
            case Lumen::MetaMethod::NamePow:
                ra->SetNumber(LumenNumPow(nb, nc));
                break;
            case Lumen::MetaMethod::NameUnm:
                ra->SetNumber(LumenNumUnm(nb));
                break;
            default:
                LumenAssert(0);
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
    Lumen::Object *rb = RKB(i); \
    Lumen::Object *rc = RKC(i); \
    if ((rb)->IsNumber() && (rc)->IsNumber()) { \
        Lumen::Number nb = (rb)->GetNumber(), nc = (rc)->GetNumber(); \
        ra->SetNumber(op(nb, nc));            \
    } else {                  \
        Protect(arith(L, ra, rb, rc, tm));            \
    }                         \
} while (0)


void Lumen::VM::Execute(Lumen::State *L, int nExecCalls) {
    Lumen::LClosure *cl;
    Lumen::Value base;
    Lumen::Object *k;
    const Lumen::Instruction *pc;
    reentry:  /* entry point */
    LumenAssert(L->CallInfo->IsFunctionOfLua());
    pc = L->SavedPC;
    cl = &L->CallInfo->Func->GetClosure()->AsLua;
    base = L->Base;
    k = cl->Func->K;
    /* main loop of interpreter */
    constexpr Lumen::Byte hookMaskLineAndCount = Lumen::HookMaskLine | Lumen::HookMaskCount;
    for (;;) {
        const Lumen::Instruction i = *pc++;
        Lumen::Value ra;
        if ((L->HookMask & hookMaskLineAndCount) &&
            (--L->HookCount == 0 || L->HookMask & Lumen::HookMaskLine)) {
            traceExec(L, pc);
            if (L->Status == Lumen::RetYield) {  /* did hook yield? */
                L->SavedPC = pc - 1;
                return;
            }
            base = L->Base;
        }
        /* warning!! several calls may realloc the stack and invalidate `ra' */
        ra = RA(i);
        LumenAssert(base == L->Base && L->Base == L->CallInfo->Base);
        LumenAssert(base <= L->Top && L->Top <= L->Stack + L->StackCount);
        LumenAssert(L->Top == L->CallInfo->Top || Lumen::Debug::CheckOpenOP(i));
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
                ra->SetBool(LumenOpCodeGetArgB(i));
                if (LumenOpCodeGetArgC(i)) pc++;  /* skip next instruction (if C) */
                continue;
            }
            case Lumen::OpCodeLoadNil: {
                Lumen::Object *rb = RB(i);
                do {
                    (rb--)->SetNil();
                } while (rb >= ra);
                continue;
            }
            case Lumen::OpCodeGetUpVal: {
                int b = LumenOpCodeGetArgB(i);
                LumenSetObject2S(L, ra, cl->UpValues[b]->SelfValue);
                continue;
            }
            case Lumen::OpCodeGetGlobal: {
                Lumen::Object g; // NOLINT
                auto rb = KBx(i);
                const Lumen::Object *slot;
                g.SetTable(L, cl->Env);
                LumenAssert(rb->IsString());
                if (rb->IsString()
                    ? LumenVMFastGetTable(L, (&g), rb->GetString(), slot, Lumen::Table::GetString)
                    : LumenVMFastGetTable(L, (&g), rb, slot, Lumen::Table::Get)) {
                    LumenSetObject2S(L, ra, slot);
                } else {
                    Protect(Lumen::VM::FinishGetTable(L, &g, rb, ra, slot));
                }
                continue;
            }
            case Lumen::OpCodeGetTable: {
                const Lumen::Object *slot;
                auto rb = RB(i);
                auto rc = RKC(i);
                if (!rb->IsTable()) {
                    const Lumen::Object *tm = Lumen::MetaMethod::GetByObject(L, rb, Lumen::MetaMethod::NameIndex);
                    switch (tm->Type) {
                        case Lumen::TypeNil:
                            Protect(Lumen::Debug::TypeError(L, rb, "index"));
                            break;
                        case Lumen::TypeFunction:
                            Protect(Lumen::VM::CallTMRes(L, ra, tm, rb, rc));
                            break;
                        case Lumen::TypeTable:
                            if (rc->IsNumber()
                                ? LumenVMFastFetchTable(L, tm, (int) rc->GetNumber(), slot, Lumen::Table::GetNum)
                                : LumenVMFastFetchTable(L, tm, rc, slot, Lumen::Table::Get)) {
                                LumenSetObject2S(L, ra, slot);
                            } else {
                                Protect(Lumen::VM::FinishGetTable(L, tm, rc, ra, slot));
                            }
                            break;
                        default:
                            Protect(Lumen::VM::FinishGetTable(L, tm, rc, ra, nullptr));
                    }
                } else if (rc->IsNumber()
                           ? LumenVMFastFetchTable(L, rb, (int) rc->GetNumber(), slot, Lumen::Table::GetNum)
                           : LumenVMFastFetchTable(L, rb, rc, slot, Lumen::Table::Get)) {
                    LumenSetObject2S(L, ra, slot);
                } else {
                    Protect(Lumen::VM::FinishGetTable(L, rb, rc, ra, slot));
                }
                continue;
            }
            case Lumen::OpCodeSetGlobal: {
                Lumen::Object g; // NOLINT
                const Lumen::Object *slot;
                auto rb = KBx(i); // key
                g.SetTable(L, cl->Env);
                LumenAssert(rb->IsString());
                if (rb->IsString()
                    ? LumenVMFastGetTable(L, (&g), rb->GetString(), slot, Lumen::Table::GetString)
                    : LumenVMFastGetTable(L, (&g), rb, slot, Lumen::Table::Get)) {
                    LumenVMFastSetTable(L, g.GetTable(), slot, ra);
                } else {
                    Protect(Lumen::VM::FinishSetTable(L, &g, rb, ra, slot));
                }
                continue;
            }
            case Lumen::OpCodeSetUpVal: {
                Lumen::UpValue *uv = cl->UpValues[LumenOpCodeGetArgB(i)];
                uv->SelfValue->SetObject(L, ra);
                L->Barrier(uv, ra);
                continue;
            }
            case Lumen::OpCodeSetTable: {
                const Lumen::Object *slot;
                auto rb = RKB(i);
                auto rc = RKC(i);
                if (!ra->IsTable()) {
                    const Lumen::Object *tm = Lumen::MetaMethod::GetByObject(L, ra, Lumen::MetaMethod::NameNewIndex);
                    switch (tm->Type) {
                        case Lumen::TypeNil:
                            Protect(Lumen::Debug::TypeError(L, ra, "index"));
                            break;
                        case Lumen::TypeFunction:
                            Protect(Lumen::VM::CallTM(L, tm, ra, rb, rc));
                            break;
                        case Lumen::TypeTable:
                            if (rb->IsNumber()
                                ? LumenVMFastFetchTable(L, tm, (int) rb->GetNumber(), slot, Lumen::Table::GetNum)
                                : LumenVMFastFetchTable(L, tm, rb, slot, Lumen::Table::Get)) {
                                LumenVMFastSetTable(L, tm->GetTable(), slot, rc);
                            } else {
                                Protect(Lumen::VM::FinishSetTable(L, tm, rb, rc, slot));
                            }
                            break;
                        default:
                            Protect(Lumen::VM::FinishSetTable(L, tm, rb, rc, nullptr));
                    }
                } else if (rb->IsNumber()
                           ? LumenVMFastFetchTable(L, ra, (int) rb->GetNumber(), slot, Lumen::Table::GetNum)
                           : LumenVMFastFetchTable(L, ra, rb, slot, Lumen::Table::Get)) {
                    LumenVMFastSetTable(L, ra->GetTable(), slot, rc);
                } else {
                    Protect(Lumen::VM::FinishSetTable(L, ra, rb, rc, slot));
                }
                continue;
            }
            case Lumen::OpCodeNewTable: {
                int b = LumenOpCodeGetArgB(i);
                int c = LumenOpCodeGetArgC(i);
                ra->SetTable(L, Lumen::Table::New(L, Lumen::FB2Int(b), Lumen::FB2Int(c)));
                Protect(L->CheckGC());
                continue;
            }
            case Lumen::OpCodeSelf: {
                const Lumen::Object *slot;
                Lumen::Value rb = RB(i);
                auto rc = RKC(i);
                LumenSetObjectS2S(L, ra + 1, rb);
                if (!rb->IsTable()) {
                    const Lumen::Object *tm = Lumen::MetaMethod::GetByObject(L, rb, Lumen::MetaMethod::NameIndex);
                    switch (tm->Type) {
                        case Lumen::TypeNil:
                            Protect(Lumen::Debug::TypeError(L, rb, "index"));
                            break;
                        case Lumen::TypeFunction:
                            Protect(Lumen::VM::CallTMRes(L, ra, tm, rb, rc));
                            break;
                        case Lumen::TypeTable:
                            if (rc->IsNumber()
                                ? LumenVMFastFetchTable(L, tm, (int) rc->GetNumber(), slot, Lumen::Table::GetNum)
                                : LumenVMFastFetchTable(L, tm, rc, slot, Lumen::Table::Get)) {
                                LumenSetObject2S(L, ra, slot);
                            } else {
                                Protect(Lumen::VM::FinishGetTable(L, tm, rc, ra, slot));
                            }
                            break;
                        default:
                            Protect(Lumen::VM::FinishGetTable(L, tm, rc, ra, nullptr));
                    }
                } else if (LumenVMFastFetchTable(L, rb, rc, slot, Lumen::Table::Get)) {
                    LumenSetObject2S(L, ra, slot);
                } else {
                    Protect(Lumen::VM::FinishGetTable(L, rb, rc, ra, slot));
                }
                continue;
            }
            case Lumen::OpCodeAdd: {
                arith_op(LumenNumAdd, Lumen::MetaMethod::NameAdd);
                continue;
            }
            case Lumen::OpCodeSub: {
                arith_op(LumenNumSub, Lumen::MetaMethod::NameSub);
                continue;
            }
            case Lumen::OpCodeMul: {
                arith_op(LumenNumMul, Lumen::MetaMethod::NameMul);
                continue;
            }
            case Lumen::OpCodeDiv: {
                arith_op(LumenNumDiv, Lumen::MetaMethod::NameDiv);
                continue;
            }
            case Lumen::OpCodeMod: {
                arith_op(LumenNumMod, Lumen::MetaMethod::NameMod);
                continue;
            }
            case Lumen::OpCodePow: {
                arith_op(LumenNumPow, Lumen::MetaMethod::NamePow);
                continue;
            }
            case Lumen::OpCodeUnm: {
                Lumen::Object *rb = RB(i);
                if (rb->IsNumber()) {
                    Lumen::Number nb = rb->GetNumber();
                    ra->SetNumber(LumenNumUnm(nb));
                } else {
                    Protect(arith(L, ra, rb, rb, Lumen::MetaMethod::NameUnm));
                }
                continue;
            }
            case Lumen::OpCodeNot: {
                int res = RB(i)->IsFalse();  /* next assignment may change this value */
                ra->SetBool(res);
                continue;
            }
            case Lumen::OpCodeLen: {
                const Lumen::Object *rb = RB(i);
                switch (rb->Type) {
                    case Lumen::TypeTable: {
                        ra->SetNumber(cast_num(Lumen::Table::GetN(rb->GetTable())));
                        break;
                    }
                    case Lumen::TypeString: {
                        ra->SetNumber(cast_num(rb->GetString()->Length));
                        break;
                    }
                    default: {  /* try metamethod */
                        Protect(
                            if (!call_binTM(L, rb, Lumen::NilObject, ra, Lumen::MetaMethod::NameLen))
                                Lumen::Debug::TypeError(L, rb, "get length of");
                        );
                    }
                }
                continue;
            }
            case Lumen::OpCodeConcat: {
                int b = LumenOpCodeGetArgB(i);
                int c = LumenOpCodeGetArgC(i);
                Protect(Lumen::VM::Concat(L, c - b + 1, c); L->CheckGC());
                LumenSetObjectS2S(L, RA(i), base + b);
                continue;
            }
            case Lumen::OpCodeJump: {
                doJump(L, pc, LumenOpCodeGetArgsBx(i));
                continue;
            }
            case Lumen::OpCodeEQ: {
                Lumen::Object *rb = RKB(i);
                Lumen::Object *rc = RKC(i);
                Protect(
                    if (static_cast<int>(Lumen::VM::FastEqualObject(L, rb, rc)) == LumenOpCodeGetArgA(i))
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
                    if (LessEqual(L, RKB(i), RKC(i)) == LumenOpCodeGetArgA(i))
                        doJump(L, pc, LumenOpCodeGetArgsBx(*pc));
                );
                pc++;
                continue;
            }
            case Lumen::OpCodeTest: {
                if (static_cast<int>(ra->IsFalse()) != LumenOpCodeGetArgC(i)) doJump(L, pc, LumenOpCodeGetArgsBx(*pc));
                pc++;
                continue;
            }
            case Lumen::OpCodeTestTest: {
                Lumen::Object *rb = RB(i);
                if (static_cast<int>(rb->IsFalse()) != LumenOpCodeGetArgC(i)) {
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
                        /* it was a C function (`precall` called it); adjust results */
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
                LumenAssert(LumenOpCodeGetArgC(i) - 1 == Lumen::RetMul);
                switch (Lumen::Do::PreCall(L, ra, Lumen::RetMul)) {
                    case Lumen::Do::PCRetLua: {
                        /* tail call: put new frame in place of previous one */
                        Lumen::CallInfo *ci = L->CallInfo - 1;  /* previous frame */
                        int aux;
                        Lumen::Value func = ci->Func;
                        Lumen::Value pfunc = (ci + 1)->Func;  /* previous function index */
                        if (L->OpenedUpValue) Lumen::UpValue::Close(L, ci->Base);
                        L->Base = ci->Base = ci->Func + ((ci + 1)->Base - pfunc);
                        for (aux = 0; pfunc + aux < L->Top; aux++)  /* move frame down */
                            LumenSetObjectS2S (L, func + aux, pfunc + aux);
                        ci->Top = L->Top = func + aux;  /* correct top */
                        LumenAssert(L->Top == L->Base + func->GetClosure()->AsLua.Func->MaxStackSize);
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
                if (--nExecCalls == 0)  /* was previous function running `here`? */
                    return;  /* no: return */
                else {  /* yes: continue its execution */
                    if (b) L->Top = L->CallInfo->Top;
                    LumenAssert(L->CallInfo->IsFunctionOfLua());
                    LumenAssert(LumenOpCodeGet(*((L->CallInfo)->SavedPC - 1)) == Lumen::OpCodeCall);
                    goto reentry;
                }
            }
            case Lumen::OpCodeForLoop: {
                Lumen::Number step = (ra + 2)->GetNumber();
                Lumen::Number idx = LumenNumAdd(ra->GetNumber(), step); /* increment index */
                Lumen::Number limit = (ra + 1)->GetNumber();
                if (LumenNumLT(0, step) ? LumenNumLE(idx, limit)
                                        : LumenNumLE(limit, idx)) {
                    doJump(L, pc, LumenOpCodeGetArgsBx(i));  /* jump back */
                    ra->SetNumber(idx);  /* update internal index... */
                    (ra + 3)->SetNumber(idx);  /* ...and external index */
                }
                continue;
            }
            case Lumen::OpCodeForPrep: {
                const Lumen::Object *init = ra;
                const Lumen::Object *plimit = ra + 1;
                const Lumen::Object *pstep = ra + 2;
                L->SavedPC = pc;  /* next steps may throw errors */
                if (!Lumen::VM::FastToNumber(init, ra))
                    Lumen::Debug::RunError(L, LUA_QL("for") " initial value must be a number");
                else if (!Lumen::VM::FastToNumber(plimit, ra + 1))
                    Lumen::Debug::RunError(L, LUA_QL("for") " limit must be a number");
                else if (!Lumen::VM::FastToNumber(pstep, ra + 2))
                    Lumen::Debug::RunError(L, LUA_QL("for") " step must be a number");
                ra->SetNumber(LumenNumSub(ra->GetNumber(), pstep->GetNumber()));
                doJump(L, pc, LumenOpCodeGetArgsBx(i));
                continue;
            }
            case Lumen::OpCodeTForLoop: {
                Lumen::Value cb = ra + 3;  /* call base */
                LumenSetObjectS2S(L, cb + 2, ra + 2);
                LumenSetObjectS2S(L, cb + 1, ra + 1);
                LumenSetObjectS2S(L, cb, ra);
                L->Top = cb + 3;  /* func. + 2 args (state and index) */
                Protect(Lumen::Do::Call(L, cb, LumenOpCodeGetArgC(i)));
                L->Top = L->CallInfo->Top;
                cb = RA(i) + 3;  /* previous call may change the stack */
                if (!cb->IsNil()) {  /* continue loop? */
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
                runtime_check(L, ra->IsTable());
                h = ra->GetTable();
                last = ((c - 1) * LUA_FIELDS_PER_FLUSH) + n;
                if (cast_uint(last) > h->ArrayCount)  /* needs more space? */
                    Lumen::Table::ResizeArray(L, h, last);  /* pre-alloc it at once */
                for (; n > 0; n--) {
                    Lumen::Object *val = ra + n;
                    LumenSetObject2T(L, Lumen::Table::SetNum(L, h, last--), val);
                    L->BarrierTable(h, val);
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
                        LumenAssert(LumenOpCodeGet(*pc) == Lumen::OpCodeMove);
                        ncl->AsLua.UpValues[j] = Lumen::UpValue::Find(L, base + LumenOpCodeGetArgB(*pc));
                    }
                }
                ra->SetClosure(L, ncl);
                Protect(L->CheckGC());
                continue;
            }
            case Lumen::OpCodeVararg: {
                int b = LumenOpCodeGetArgB(i) - 1;
                int j;
                Lumen::CallInfo *ci = L->CallInfo;
                int n = cast_int(ci->Base - ci->Func) - cl->Func->NUmParams - 1;
                if (b == Lumen::RetMul) {
                    Protect(LumenDoCheckStack(L, n));
                    ra = RA(i);  /* previous call may change the stack */
                    b = n;
                    L->Top = ra + n;
                }
                for (j = 0; j < b; j++) {
                    if (j < n) {
                        LumenSetObjectS2S(L, ra + j, ci->Base - n + j);
                    } else {
                        (ra + j)->SetNil();
                    }
                }
                continue;
            }
        }
    }
}

