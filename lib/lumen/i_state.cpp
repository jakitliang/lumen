/*!
 * @brief Lumen State APIs
 * @author Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/7/8
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */

#include <cstdarg>
#include <cstring>
#include <string>

#define LUA_CORE

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/gc.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/state.h"
#include "lumen/tm.h"
#include "lumen/undump.h"
#include "lumen/vm.h"
#include "lumen/protected_call.h"
#include "lumen/api.h"

// NOLINTNEXTLINE
#define ToState(L) reinterpret_cast<Lumen::State *>(L)
#define ToIState(L) reinterpret_cast<Lumen::IState *>(L)

static inline bool InstanceOf(Lumen::State *L, const Lumen::Object *oChild, const Lumen::Object *oSuper) {
    if (oChild == Lumen::NilObject || oSuper == Lumen::NilObject) return false;
    int loop;
    for (loop = 0; loop < LumenMaxTagLoop; loop++) {
        if (oChild->IsTable()) {  /* `t` is a table? */
            Lumen::Table *h = oChild->GetTable();
            if (Lumen::RawEqualObject(oChild, oSuper)) {
                return true;
            }
            if ((oChild = LumenMetaMethodGetFast(L, h->Metatable, Lumen::MetaMethod::NameIndex)) == nullptr) { /* no TM? */
                return false;
            }
            /* else will try the tag method */
        } else if ((oChild = Lumen::MetaMethod::GetByObject(L, oChild, Lumen::MetaMethod::NameIndex))->IsNil()) {
            Lumen::Debug::TypeError(L, oChild, "index");
        }
        if (oChild->IsFunction()) {
            return false;
        }
    }
    Lumen::Debug::RunError(L, "loop in gettable");
    return false;
}

// MARK: state manipulation

Lumen::IState *Lumen::IState::New(Lumen::Allocator allocator, void *userdata) {
    auto L = Lumen::State::New(allocator, userdata);
    return L == nullptr ? nullptr : ToIState(L);
}

Lumen::IState *Lumen::IState::NewThread() {
    auto L = ToState(this);
    Lumen::State *L1;
    LumenLock(L);
    L->CheckGC();
    L1 = Lumen::State::NewThread(L);
    L->Top->SetThread(L, L1);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    luai_userstatethread(L, L1);
    return L1 == nullptr ? nullptr : ToIState(L1);
}

Lumen::Delegate Lumen::IState::AtPanic(Lumen::Delegate pInvoke) {
    auto L = ToState(this);
    Lumen::Delegate old;
    LumenLock(L);
    old = reinterpret_cast<Lumen::Delegate>(LumenGlobalState(L)->Panic);
    LumenGlobalState(L)->Panic = reinterpret_cast<Lumen::Delegate>(pInvoke);
    LumenUnlock(L);
    return old;
}

const Lumen::Number *Lumen::IState::Version() {
    static const Lumen::Number lua_version_number = LUA_VERSION_NUM;
    return &lua_version_number;
}

// MARK: basic stack manipulation

int Lumen::IState::AbsIndex(int idx) {
    auto L = ToState(this);
    return (idx > 0 || LumenApiIsPseudo(idx))
           ? idx
           : cast_int((L->Top - L->Base) + 1 + idx);
}

int Lumen::IState::GetTop() {
    auto L = ToState(this);
    return cast_int(L->Top - L->Base);
}

void Lumen::IState::SetTop(int idx) {
    auto L = ToState(this);
    LumenLock(L);
    if (idx >= 0) {
        LumenApiCheck(L, idx <= L->StackLast - L->Base);
        while (L->Top < L->Base + idx)
            (L->Top++)->SetNil();
        L->Top = L->Base + idx;
    } else {
        LumenApiCheck(L, -(idx + 1) <= (L->Top - L->Base));
        L->Top += idx + 1;  /* `subtract` index (index is negative) */
    }
    LumenUnlock(L);
}

void Lumen::IState::PushValue(int idx) {
    auto L = ToState(this);
    LumenLock(L);
    LumenSetObject2S(L, L->Top, L->ToObject(idx));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lumen::IState::Remove(int idx) {
    auto L = ToState(this);
    Lumen::Value p;
    LumenLock(L);
    p = L->ToObject(idx);
    LumenApiCheckValidIndex(L, p);
    while (++p < L->Top) LumenSetObjectS2S(L, p - 1, p);
    L->Top--;
    LumenUnlock(L);
}

void Lumen::IState::Insert(int idx) {
    auto L = ToState(this);
    Lumen::Value p;
    Lumen::Value q;
    LumenLock(L);
    p = L->ToObject(idx);
    LumenApiCheckValidIndex(L, p);
    for (q = L->Top; q > p; q--) LumenSetObjectS2S(L, q, q - 1);
    LumenSetObjectS2S(L, p, L->Top);
    LumenUnlock(L);
}

static void moveTo(Lumen::State *L, Lumen::Object *from, int idx) {
    Lumen::Object *to = L->ToObject(idx);
    LumenApiCheckValidIndex(L, to);
    if (idx == Lumen::EnvIndex) {
        Lumen::Closure *func = L->GetCurrentFunction();
        LumenApiCheck(L, from->IsTable());
        func->AsC.Env = from->GetTable();
        L->Barrier(func, from);
    } else {
        to->SetObject(L, from);
        if (idx < Lumen::GlobalIndex)  /* function upvalue? */
            L->Barrier(L->GetCurrentFunction(), from);
    }
}

void Lumen::IState::Replace(int idx) {
    auto L = ToState(this);
    LumenLock(L);
    /* explicit test for incompatible code */
    if (idx == Lumen::EnvIndex && L->CallInfo == L->BaseCI)
        Lumen::Debug::RunError(L, "no calling environment");
    LumenApiCheckElementCount(L, 1);
    moveTo(L, L->Top - 1, idx);
    L->Top--;
    LumenUnlock(L);
}

void Lumen::IState::Copy(int fromIdx, int toIdx) {
    auto L = ToState(this);
    Lumen::Object *from;
    LumenLock(L);
    if (toIdx == Lumen::EnvIndex && L->CallInfo == L->BaseCI)
        Lumen::Debug::RunError(L, "no calling environment");
    from = L->ToObject(fromIdx);
    moveTo(L, from, toIdx);
    LumenUnlock(L);
}

static void compatReverse(Lumen::IState *L, int a, int b) {
    for (; a < b; ++a, --b) {
        L->PushValue(a);
        L->PushValue(b);
        L->Replace(a);
        L->Replace(b);
    }
}

void Lumen::IState::Rotate(int idx, int n) {
    auto L = ToState(this);
    int n_elems = 0;
    idx = AbsIndex(idx);
    n_elems = GetTop() - idx + 1;
    if (n < 0)
        n += n_elems;
    if (n > 0 && n < n_elems) {
        if (!CheckStack(2)) {
            PushString("not enough stack slots available");
            Error();
        }
        n = n_elems - n;
        compatReverse(this, idx, idx + n - 1);
        compatReverse(this, idx + n, idx + n_elems - 1);
        compatReverse(this, idx, idx + n_elems - 1);
    }
}

bool Lumen::IState::CheckStack(int size) {
    auto L = ToState(this);
    int res = 1;
    LumenLock(L);
    if (size > LUA_MAX_C_STACK || (L->Top - L->Base + size) > LUA_MAX_C_STACK)
        res = 0;  /* stack overflow */
    else if (size > 0) {
        LumenDoCheckStack(L, size);
        if (L->CallInfo->Top < L->Top + size)
            L->CallInfo->Top = L->Top + size;
    }
    LumenUnlock(L);
    return res;
}

// MARK: access functions (stack -> C)

bool Lumen::IState::IsNumber(int idx) {
    auto L = ToState(this);
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
    return Lumen::VM::FastToNumber(o, &n);
}

bool Lumen::IState::IsString(int idx) {
    auto L = ToState(this);
    Lumen::Value o = L->ToObject(idx);
    return o->IsString();
}

bool Lumen::IState::IsDelegate(int idx) {
    auto L = ToState(this);
    Lumen::Value o = L->ToObject(idx);
    return o->IsCFunction();
}

bool Lumen::IState::IsUserdata(int idx) {
    auto L = ToState(this);
    const Lumen::Object *o = L->ToObject(idx);
    return (o->IsUData() || o->IsLUData());
}

Lumen::Type Lumen::IState::TypeId(int idx) {
    auto L = ToState(this);
    Lumen::Value o = L->ToObject(idx);
    return (o == Lumen::NilObject) ? Lumen::TypeNone : (o)->Type;
}

const char *Lumen::IState::TypeOf(int tp) {
    return (tp == Lumen::TypeNone) ? "no value" : Lumen::MetaMethod::TypeNames[tp];
}

void Lumen::IState::Arith(Lumen::ArithOp op) {
    auto L = ToState(this);
    Lumen::Value o1;  /* 1st operand */
    Lumen::Value o2;  /* 2nd operand */
    LumenLock(L);
    if (op != Lumen::ArithOpUnm) /* all other operations expect two operands */
        LumenApiCheckElementCount(L, 2);
    else {  /* for unary minus, add fake 2nd operand */
        LumenApiCheckElementCount(L, 1);
        LumenSetObjectS2S(L, L->Top, L->Top - 1);
        L->Top++;
    }
    o1 = L->Top - 2;
    o2 = L->Top - 1;
    if (o1->IsNumber() && o2->IsNumber()) {
        o1->SetNumber(Lumen::Arith(op, o1->GetNumber(), o2->GetNumber()));
    } else
        Lumen::VM::ArithValue(L, o1, o1, o2, cast(Lumen::MetaMethod::Name, op - Lumen::ArithOpAdd + Lumen::MetaMethod::NameAdd));
    L->Top--;
    LumenUnlock(L);
}

int Lumen::IState::Compare(int idx1, int idx2, Lumen::ArithOp op) {
    auto L = ToState(this);
    Lumen::Value o1, o2;
    int i = 0;
    LumenLock(L);  /* may call tag method */
    o1 = L->ToObject(idx1);
    o2 = L->ToObject(idx2);
    if (LumenApiIsValid(o1) && LumenApiIsValid(o2)) {
        switch (op) {
            case Lumen::CompareOpEQ:
                i = Lumen::VM::EqualObject(L, o1, o2);
                break;
            case Lumen::CompareOpLT:
                i = Lumen::VM::LessThan(L, o1, o2);
                break;
            case Lumen::CompareOpLE:
                i = Lumen::VM::LessEqual(L, o1, o2);
                break;
            default:
                LumenApiCheck(L, 0);
        }
    }
    LumenUnlock(L);
    return i;
}

bool Lumen::IState::Equal(int idx1, int idx2) {
    auto L = ToState(this);
    Lumen::Value o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = L->ToObject(idx1);
    o2 = L->ToObject(idx2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0 : Lumen::VM::FastEqualObject(L, o1, o2);
    LumenUnlock(L);
    return i;
}

bool Lumen::IState::RawEqual(int idx1, int idx2) {
    auto L = ToState(this);
    Lumen::Value o1 = L->ToObject(idx1);
    Lumen::Value o2 = L->ToObject(idx2);
    return !(o1 == Lumen::NilObject || o2 == Lumen::NilObject) && Lumen::RawEqualObject(o1, o2);
}

bool Lumen::IState::LessThan(int idx1, int idx2) {
    auto L = ToState(this);
    Lumen::Value o1, o2;
    int i;
    LumenLock(L);  /* may call tag method */
    o1 = L->ToObject(idx1);
    o2 = L->ToObject(idx2);
    i = (o1 == Lumen::NilObject || o2 == Lumen::NilObject) ? 0
                                                           : Lumen::VM::LessThan(L, o1, o2);
    LumenUnlock(L);
    return i;
}

Lumen::Number Lumen::IState::ToNumber(int idx) {
    auto L = ToState(this);
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
    if (Lumen::VM::FastToNumber(o, &n))
        return o->GetNumber();
    else
        return 0;
}

Lumen::Integer Lumen::IState::ToInteger(int idx) {
    auto L = ToState(this);
    Lumen::Object n; // NOLINT
    const Lumen::Object *o = L->ToObject(idx);
    if (Lumen::VM::FastToNumber(o, &n)) {
        Lumen::Integer res;
        Lumen::Number num = o->GetNumber();
        lua_number2integer(res, num);
        return res;
    } else
        return 0;
}

bool Lumen::IState::ToBoolean(int idx) {
    auto L = ToState(this);
    const Lumen::Object *o = L->ToObject(idx);
    return !o->IsFalse();
}

const char *Lumen::IState::ToString(int idx, Lumen::UInteger *len) {
    auto L = ToState(this);
    Lumen::Value o = L->ToObject(idx);
    if (!o->IsString()) {
        LumenLock(L);  /* `Lumen::VM::ToString' may create a new string */
        if (!Lumen::VM::ToString(L, o)) {  /* conversion failed? */
            if (len != nullptr) *len = 0;
            LumenUnlock(L);
            return nullptr;
        }
        L->CheckGC();
        o = L->ToObject(idx);  /* previous call may reallocate the stack */
        LumenUnlock(L);
    }
    if (len != nullptr) *len = o->GetString()->Length;
    return o->ToCString();
}

bool Lumen::IState::InstanceOf(int idxChild, int idxSuper) {
    auto L = ToState(this);
    const Lumen::Object *oChild = L->ToObject(idxChild);
    const Lumen::Object *oSuper = L->ToObject(idxSuper);
    if (oChild == Lumen::NilObject || oSuper == Lumen::NilObject) return false;
    int loop;
    for (loop = 0; loop < LumenMaxTagLoop; loop++) {
        if (oChild->IsTable()) {  /* `t` is a table? */
            Lumen::Table *h = oChild->GetTable();
            if (Lumen::RawEqualObject(oChild, oSuper)) {
                return true;
            }
            if ((oChild = LumenMetaMethodGetFast(L, h->Metatable, Lumen::MetaMethod::NameIndex)) == nullptr) { /* no TM? */
                return false;
            }
            /* else will try the tag method */
        } else if ((oChild = Lumen::MetaMethod::GetByObject(L, oChild, Lumen::MetaMethod::NameIndex))->IsNil()) {
            Lumen::Debug::TypeError(L, oChild, "index");
        }
    }
    Lumen::Debug::RunError(L, "loop in gettable");
    return false;
}

Lumen::UInteger Lumen::IState::ObjectLength(int idx) {
    auto L = ToState(this);
    Lumen::Value o = L->ToObject(idx);
    switch (o->Type) {
        case Lumen::TypeString:
            return o->GetString()->Length;
        case Lumen::TypeUserdata:
            return o->GetUData()->Length;
        case Lumen::TypeTable:
            return Lumen::Table::GetN(o->GetTable());
        case Lumen::TypeNumber: {
            Lumen::UInteger l;
            LumenLock(L);  /* `Lumen::VM::ToString' may create a new string */
            l = (Lumen::VM::ToString(L, o) ? o->GetString()->Length : 0);
            LumenUnlock(L);
            return l;
        }
        default:
            return 0;
    }
}

Lumen::Delegate Lumen::IState::ToDelegate(int idx) {
    auto L = ToState(this);
    Lumen::Value o = L->ToObject(idx);
    return (!o->IsCFunction()) ? nullptr : reinterpret_cast<Lumen::Delegate>(o->GetClosure()->AsC.Func);
}

void *Lumen::IState::ToUserdata(int idx) {
    auto L = ToState(this);
    Lumen::Value o = L->ToObject(idx);
    switch (o->Type) {
        case Lumen::TypeUserdata:
            return (o->GetUData() + 1);
        case Lumen::TypeLightUserdata:
            return o->GetLUData();
        default:
            return nullptr;
    }
}

Lumen::IState *Lumen::IState::ToThread(int idx) {
    auto L = ToState(this);
    Lumen::Value o = L->ToObject(idx);
    return (!o->IsThread()) ? nullptr : ToIState(o->GetThread());
}

const void *Lumen::IState::ToPointer(int idx) {
    auto L = ToState(this);
    Lumen::Value o = L->ToObject(idx);
    switch (o->Type) {
        case Lumen::TypeTable:
            return o->GetTable();
        case Lumen::TypeFunction:
            return o->GetClosure();
        case Lumen::TypeThread:
            return o->GetThread();
        case Lumen::TypeUserdata:
        case Lumen::TypeLightUserdata:
            return ToUserdata(idx);
        default:
            return nullptr;
    }
}

// MARK: push functions (C -> stack)

void Lumen::IState::PushNil() {
    auto L = ToState(this);
    LumenLock(L);
    L->Top->SetNil();
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lumen::IState::PushNumber(Lumen::Number n) {
    auto L = ToState(this);
    LumenLock(L);
    L->Top->SetNumber(n);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lumen::IState::PushInteger(Lumen::Integer n) {
    auto L = ToState(this);
    LumenLock(L);
    L->Top->SetNumber(cast_num(n));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

const char *Lumen::IState::PushString(const char *s, Lumen::UInteger length) {
    auto L = ToState(this);
    LumenLock(L);
    L->CheckGC();
    auto str = Lumen::String::New(L, s, length);
    LumenSetStringValue2S(L, L->Top, str);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return str->CString();
}

const char *Lumen::IState::PushString(const char *s) {
    if (s == nullptr) {
        PushNil();
        return s;
    }
    return PushString(s, Lumen::String::LengthOf(s));
}

const char *Lumen::IState::PushVFString(const char *fmt, va_list argP) {
    auto L = ToState(this);
    const char *ret;
    LumenLock(L);
    L->CheckGC();
    ret = Lumen::PushVFString(L, fmt, argP);
    LumenUnlock(L);
    return ret;
}

const char *Lumen::IState::PushFString(const char *fmt, ...) {
    auto L = ToState(this);
    const char *ret;
    va_list argP;
    LumenLock(L);
    L->CheckGC();
        va_start(argP, fmt);
    ret = Lumen::PushVFString(L, fmt, argP);
        va_end(argP);
    LumenUnlock(L);
    return ret;
}

void Lumen::IState::PushDelegate(Lumen::Delegate invoke, int n) {
    auto L = ToState(this);
    Lumen::Closure *cl;
    LumenLock(L);
    L->CheckGC();
    LumenApiCheckElementCount(L, n);
    cl = Lumen::CClosure::New(L, n, L->GetCurrentEnv());
    cl->AsC.Func = reinterpret_cast<Lumen::Delegate>(invoke);
    L->Top -= n;
    while (n--)
        LumenSetObject2N(L, &cl->AsC.UpValues[n], L->Top + n);
    L->Top->SetClosure(L, cl);
    LumenAssert(LumenObject2GCObject(cl)->IsWhite());
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lumen::IState::PushBoolean(int b) {
    auto L = ToState(this);
    LumenLock(L);
    L->Top->SetBool(b != 0);  /* ensure that true is 1 */
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void Lumen::IState::PushLightUserdata(void *p) {
    auto L = ToState(this);
    LumenLock(L);
    L->Top->SetLUData(p);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

int Lumen::IState::PushThread() {
    auto L = ToState(this);
    LumenLock(L);
    L->Top->SetThread(L, L);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return (LumenGlobalState(L)->MainThread == L);
}

void Lumen::IState::PushObject(const Lumen::IObject *o) {
    auto L = ToState(this);
    LumenSetObject2S(L, L->Top, reinterpret_cast<const Lumen::Object *>(o)); // NOLINT
    LumenApiIncrTop(ToState(this));
}

// MARK: get functions (lua -> stack)

Lumen::Type Lumen::IState::GetTable(int idx) {
    auto L = ToState(this);
    Lumen::Value t;
    Lumen::Object *key;
    const Lumen::Object *slot;
    LumenLock(L);
    t = L->ToObject(idx);
    key = L->Top - 1;
    LumenApiCheckValidIndex(L, t);
    if (!t->IsTable()) {
        const Lumen::Object *tm = Lumen::MetaMethod::GetByObject(L, t, Lumen::MetaMethod::NameIndex);
        switch (tm->Type) {
            case Lumen::TypeNil:
                Lumen::Debug::TypeError(L, t, "index");
                break;
            case Lumen::TypeFunction:
                Lumen::VM::CallTMRes(L, L->Top - 1, tm, t, key);
                break;
            case Lumen::TypeTable:
                if (key->IsNumber()
                    ? LumenVMFastFetchTable(L, tm, cast_int(key->GetNumber()), slot, Lumen::Table::GetNum)
                    : LumenVMFastFetchTable(L, tm, key, slot, Lumen::Table::Get)) {
                    LumenSetObject2S(L, (L->Top - 1), slot);
                } else {
                    Lumen::VM::FinishGetTable(L, tm, key, L->Top - 1, slot);
                }
                break;
            default:
                Lumen::VM::FinishGetTable(L, tm, key, L->Top - 1, nullptr);
        }
    } else if (key->IsNumber()
        ? LumenVMFastFetchTable(L, t, cast_int(key->GetNumber()), slot, Lumen::Table::GetNum)
        : LumenVMFastFetchTable(L, t, key, slot, Lumen::Table::Get)) {
        LumenSetObject2S(L, (L->Top - 1), slot);
    } else {
        Lumen::VM::FinishGetTable(L, t, L->Top - 1, L->Top - 1, slot);
    }
    LumenUnlock(L);
    return (L->Top - 1)->Type;
}

Lumen::Type Lumen::IState::GetField(int idx, const char *k) {
    auto L = ToState(this);
    Lumen::Value t;
    Lumen::Object key; // NOLINT
    const Lumen::Object *slot;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheckValidIndex(L, t);
    key.SetString(L, Lumen::String::New(L, k));
    if (!t->IsTable()) {
        const Lumen::Object *tm = Lumen::MetaMethod::GetByObject(L, t, Lumen::MetaMethod::NameIndex);
        switch (tm->Type) {
            case Lumen::TypeNil:
                Lumen::Debug::TypeError(L, t, "index");
                break;
            case Lumen::TypeFunction:
                Lumen::VM::CallTMRes(L, L->Top, tm, t, &key);
                break;
            case Lumen::TypeTable:
                if (LumenVMFastFetchTable(L, tm, (&key), slot, Lumen::Table::Get)) {
                    LumenSetObject2S(L, L->Top, slot);
                } else {
                    Lumen::VM::FinishGetTable(L, tm, &key, L->Top, slot);
                }
                break;
            default:
                Lumen::VM::FinishGetTable(L, tm, &key, L->Top, nullptr);
        }
    } else if (LumenVMFastFetchTable(L, t, (&key), slot, Lumen::Table::Get)) {
        LumenSetObject2S(L, L->Top, slot);
    } else {
        Lumen::VM::FinishGetTable(L, t, &key, L->Top, slot);
    }
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return (L->Top - 1)->Type;
}

Lumen::Type Lumen::IState::RawGet(int idx) {
    auto L = ToState(this);
    Lumen::Value t;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheck(L, t->IsTable());
    LumenSetObject2S(L, L->Top - 1, Lumen::Table::Get(t->GetTable(), L->Top - 1));
    LumenUnlock(L);
    return (L->Top - 1)->Type;
}

Lumen::Type Lumen::IState::RawGetAt(int idx, int n) {
    auto L = ToState(this);
    Lumen::Value o;
    LumenLock(L);
    o = L->ToObject(idx);
    LumenApiCheck(L, o->IsTable());
    LumenSetObject2S(L, L->Top, Lumen::Table::GetNum(o->GetTable(), n));
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return (L->Top - 1)->Type;
}

Lumen::Type Lumen::IState::RawGetPtr(int idx, const void *p) {
    auto L = ToState(this);
    Lumen::Value t;
    Lumen::Object k; // NOLINT
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheck(L, t->IsTable());
    k.SetLUData(cast(void *, p));
    LumenSetObject2S(L, L->Top, Lumen::Table::Get(t->GetTable(), &k));
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return (L->Top - 1)->Type;
}

void Lumen::IState::CreateTable(int nArray, int nRec) {
    auto L = ToState(this);
    LumenLock(L);
    L->CheckGC();
    L->Top->SetTable(L, Lumen::Table::New(L, nArray, nRec));
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

void *Lumen::IState::NewUserdata(Lumen::UInteger size) {
    auto L = ToState(this);
    Lumen::Userdata *u;
    LumenLock(L);
    L->CheckGC();
    u = Lumen::Userdata::New(L, size, L->GetCurrentEnv());
    L->Top->SetUData(L, u);
    LumenApiIncrTop(L);
    LumenUnlock(L);
    return u + 1;
}

bool Lumen::IState::GetMetatable(int objIndex) {
    auto L = ToState(this);
    const Lumen::Object *obj;
    Lumen::Table *mt;
    int res;
    LumenLock(L);
    obj = L->ToObject(objIndex);
    switch (obj->Type) {
        case Lumen::TypeTable:
            mt = obj->GetTable()->Metatable;
            break;
        case Lumen::TypeUserdata:
            mt = obj->GetUData()->Metatable;
            break;
        default:
            mt = LumenGlobalState(L)->Metatable[obj->Type];
            break;
    }
    if (mt == nullptr)
        res = 0;
    else {
        L->Top->SetTable(L, mt);
        LumenApiIncrTop(L);
        res = 1;
    }
    LumenUnlock(L);
    return res;
}

void Lumen::IState::GetFEnv(int idx) {
    auto L = ToState(this);
    Lumen::Value o;
    LumenLock(L);
    o = L->ToObject(idx);
    LumenApiCheckValidIndex(L, o);
    switch (o->Type) {
        case Lumen::TypeFunction:
            L->Top->SetTable(L, o->GetClosure()->AsC.Env);
            break;
        case Lumen::TypeUserdata:
            L->Top->SetTable(L, o->GetUData()->Env);
            break;
        case Lumen::TypeThread:
            LumenSetObject2S(L, L->Top, LumenGlobalTable(o->GetThread()));
            break;
        default:
            L->Top->SetNil();
            break;
    }
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

// MARK: set functions (stack -> Lua)

void Lumen::IState::SetTable(int idx) {
    auto L = ToState(this);
    Lumen::Value t;
    Lumen::Object *key;
    const Lumen::Object *slot;
    LumenLock(L);
    LumenApiCheckElementCount(L, 2);
    t = L->ToObject(idx);
    key = L->Top - 2;
    LumenApiCheckValidIndex(L, t);
    if (!t->IsTable()) {
        const Lumen::Object *tm = Lumen::MetaMethod::GetByObject(L, t, Lumen::MetaMethod::NameNewIndex);
        switch (tm->Type) {
            case Lumen::TypeNil:
                Lumen::Debug::TypeError(L, t, "index");
                break;
            case Lumen::TypeFunction:
                Lumen::VM::CallTM(L, tm, t, key, L->Top - 1);
                break;
            case Lumen::TypeTable:
                if (key->IsNumber()
                    ? LumenVMFastFetchTable(L, tm, (int) key->GetNumber(), slot, Lumen::Table::GetNum)
                    : LumenVMFastFetchTable(L, tm, key, slot, Lumen::Table::Get)) {
                    LumenVMFastSetTable(L, tm->GetTable(), slot, L->Top - 1);
                } else {
                    Lumen::VM::FinishSetTable(L, tm, key, L->Top - 1, slot);
                }
                break;
            default:
                Lumen::VM::FinishSetTable(L, tm, key, L->Top - 1, nullptr);
        }
    } else if (key->IsNumber()
        ? LumenVMFastFetchTable(L, t, cast_int(key->GetNumber()), slot, Lumen::Table::GetNum)
        : LumenVMFastFetchTable(L, t, key, slot, Lumen::Table::Get)) {
        LumenVMFastSetTable(L, t->GetTable(), slot, L->Top - 1);
    } else {
        Lumen::VM::FinishSetTable(L, t, L->Top - 2, L->Top - 1, slot);
    }
    L->Top -= 2;  /* pop index and value */
    LumenUnlock(L);
}

void Lumen::IState::SetField(int idx, const char *k) {
    auto L = ToState(this);
    Lumen::Value t;
    Lumen::Object key; // NOLINT
    const Lumen::Object *slot;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    t = L->ToObject(idx);
    LumenApiCheckValidIndex(L, t);
    key.SetString(L, Lumen::String::New(L, k));
    if (!t->IsTable()) {
        const Lumen::Object *tm = Lumen::MetaMethod::GetByObject(L, t, Lumen::MetaMethod::NameNewIndex);
        switch (tm->Type) {
            case Lumen::TypeNil:
                Lumen::Debug::TypeError(L, t, "index");
                break;
            case Lumen::TypeFunction:
                Lumen::VM::CallTM(L, tm, t, (&key), L->Top - 1);
                break;
            case Lumen::TypeTable:
                if ((&key)->IsNumber()
                    ? LumenVMFastFetchTable(L, tm, (int) (&key)->GetNumber(), slot, Lumen::Table::GetNum)
                    : LumenVMFastFetchTable(L, tm, (&key), slot, Lumen::Table::Get)) {
                    LumenVMFastSetTable(L, tm->GetTable(), slot, L->Top - 1);
                } else {
                    Lumen::VM::FinishSetTable(L, tm, (&key), L->Top - 1, slot);
                }
                break;
            default:
                Lumen::VM::FinishSetTable(L, tm, (&key), L->Top - 1, nullptr);
        }
    } else if (LumenVMFastGetTable(L, t, (&key), slot, Lumen::Table::Get)) {
        LumenVMFastSetTable(L, t->GetTable(), slot, L->Top - 1);
    } else {
        Lumen::VM::FinishSetTable(L, t, &key, L->Top - 1, slot);
    }
    L->Top--;  /* pop value */
    LumenUnlock(L);
}

void Lumen::IState::RawSet(int idx) {
    auto L = ToState(this);
    Lumen::Value t;
    LumenLock(L);
    LumenApiCheckElementCount(L, 2);
    t = L->ToObject(idx);
    LumenApiCheck(L, t->IsTable());
    LumenSetObject2T(L, Lumen::Table::Set(L, t->GetTable(), L->Top - 2), L->Top - 1);
    L->BarrierTable(t->GetTable(), L->Top - 1);
    L->Top -= 2;
    LumenUnlock(L);
}

void Lumen::IState::RawSetAt(int idx, int n) {
    auto L = ToState(this);
    Lumen::Value o;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    o = L->ToObject(idx);
    LumenApiCheck(L, o->IsTable());
    LumenSetObject2T(L, Lumen::Table::SetNum(L, o->GetTable(), n), L->Top - 1);
    L->BarrierTable(o->GetTable(), L->Top - 1);
    L->Top--;
    LumenUnlock(L);
}

void Lumen::IState::RawSetPtr(int idx, const void *p) {
    auto L = ToState(this);
    Lumen::Value t;
    Lumen::Object k; // NOLINT
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    t = L->ToObject(idx);
    LumenApiCheck(L, t->IsTable());
    k.SetLUData(cast(void *, p));
    LumenSetObject2T(L, Lumen::Table::Set(L, t->GetTable(), &k), L->Top - 1);
    L->BarrierTable(t->GetTable(), L->Top - 1);
    L->Top--;
    LumenUnlock(L);
}

bool Lumen::IState::SetMetatable(int objIndex) {
    auto L = ToState(this);
    Lumen::Object *obj;
    Lumen::Table *mt;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    obj = L->ToObject(objIndex);
    LumenApiCheckValidIndex(L, obj);
    if ((L->Top - 1)->IsNil())
        mt = nullptr;
    else {
        LumenApiCheck(L, (L->Top - 1)->IsTable());
        mt = (L->Top - 1)->GetTable();
    }
    switch (obj->Type) {
        case Lumen::TypeTable: {
            obj->GetTable()->Metatable = mt;
            if (mt)
                L->BarrierGCObjectTable(obj->GetTable(), mt);
            break;
        }
        case Lumen::TypeUserdata: {
            obj->GetUData()->Metatable = mt;
            if (mt)
                L->BarrierGCObject(obj->GetUData(), mt);
            break;
        }
        default: {
            LumenGlobalState(L)->Metatable[obj->Type] = mt;
            break;
        }
    }
    L->Top--;
    LumenUnlock(L);
    return true;
}

bool Lumen::IState::SetFEnv(int idx) {
    auto L = ToState(this);
    Lumen::Value o;
    int res = 1;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    o = L->ToObject(idx);
    LumenApiCheckValidIndex(L, o);
    LumenApiCheck(L, (L->Top - 1)->IsTable());
    switch (o->Type) {
        case Lumen::TypeFunction:
            o->GetClosure()->AsC.Env = (L->Top - 1)->GetTable();
            break;
        case Lumen::TypeUserdata:
            o->GetUData()->Env = (L->Top - 1)->GetTable();
            break;
        case Lumen::TypeThread:
            LumenGlobalTable(o->GetThread())->SetTable(L, (L->Top - 1)->GetTable());
            break;
        default:
            res = 0;
            break;
    }
    if (res) L->BarrierGCObject(o->GetGCObject(), (L->Top - 1)->GetTable());
    L->Top--;
    LumenUnlock(L);
    return res;
}

// MARK: `load' and `call' functions (load and run Lua code)

void Lumen::IState::Call(int nargs, int nResults) {
    auto L = ToState(this);
    Lumen::Value func;
    LumenLock(L);
    LumenApiCheckElementCount(L, nargs + 1);
    LumenApiCheckResults(L, nargs, nResults);
    func = L->Top - (nargs + 1);
    Lumen::Do::Call(L, func, nResults);
    LumenApiAdjustResults(L, nResults);
    LumenUnlock(L);
}

Lumen::Ret Lumen::IState::TryCall(int nargs, int nResults, int errFunc) {
    auto L = ToState(this);
    Lumen::ProtectedCall c; // NOLINT
    int status;
    ptrdiff_t func;
    LumenLock(L);
    LumenApiCheckElementCount(L, nargs + 1);
    LumenApiCheckResults(L, nargs, nResults);
    if (errFunc == 0)
        func = 0;
    else {
        Lumen::Value o = L->ToObject(errFunc);
        LumenApiCheckValidIndex(L, o);
        func = LumenSaveStack(L, o);
    }
    c.Func = L->Top - (nargs + 1);  /* function to be called */
    c.NResults = nResults;
    status = Lumen::Do::PCall(L,
                              &Lumen::ProtectedCall::Call, &c,
                              LumenSaveStack(L, c.Func), func);
    LumenApiAdjustResults(L, nResults);
    LumenUnlock(L);
    return status;
}

Lumen::Ret Lumen::IState::TryCall(Lumen::Delegate invoke, void *userdata) {
    auto L = ToState(this);
    Lumen::ProtectedCCall c; // NOLINT
    int status;
    LumenLock(L);
    c.Func = reinterpret_cast<Lumen::Delegate>(invoke);
    c.UData = userdata;
    status = Lumen::Do::PCall(L,
                              &Lumen::ProtectedCCall::Call, &c,
                              LumenSaveStack(L, L->Top), 0);
    LumenUnlock(L);
    return status;
}

Lumen::Ret Lumen::IState::Load(Lumen::Reader reader, void *data, const char *chunkName) {
    auto L = ToState(this);
    Lumen::ZIO z; // NOLINT
    int status;
    LumenLock(L);
    if (!chunkName) chunkName = "?";
    Lumen::ZIO::Init(L, &z, reinterpret_cast<Lumen::Reader>(reader), data);
    status = Lumen::Do::ProtectedParser(L, &z, chunkName);
    LumenUnlock(L);
    return status;
}

Lumen::Ret Lumen::IState::Dump(Lumen::Writer writer, void *data) {
    auto L = ToState(this);
    int status;
    Lumen::Object *o;
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    o = L->Top - 1;
    if (o->IsLFunction())
        status = Lumen::Dumper::Dump(L,
                                     o->GetClosure()->AsLua.Func,
                                     reinterpret_cast<Lumen::Writer>(writer), data, 0);
    else
        status = 1;
    LumenUnlock(L);
    return status;
}

// MARK: coroutine functions

Lumen::Ret Lumen::IState::Yield(int nResults) {
    auto L = ToState(this);
    luai_userstateyield(L, nResults);
    LumenLock(L);
    if (L->NCCalls > L->BaseCCalls)
        Lumen::Debug::RunError(L, "attempt to yield across metaMethod/C-call boundary");
    L->Base = L->Top - nResults;  /* protect stack slots below */
    L->Status = Lumen::RetYield;
    LumenUnlock(L);
    return -1;
}

Lumen::Ret Lumen::IState::Resume(int nArgs) {
    auto L = ToState(this);
    int status;
    LumenLock(L);
    if (L->Status != Lumen::RetYield && (L->Status != 0 || L->CallInfo != L->BaseCI))
        return Lumen::Do::ResumeError(L, "cannot resume non-suspended coroutine");
    if (L->NCCalls >= LUA_MAX_C_CALLS)
        return Lumen::Do::ResumeError(L, "C stack overflow");
    luai_userstateresume(L, nArgs);
    LumenAssert(L->ErrFunc == 0);
    L->BaseCCalls = ++L->NCCalls;
    status = Lumen::Do::RawRunProtected(L, Lumen::Do::Resume, L->Top - nArgs);
    if (status != 0) {  /* error? */
        L->Status = cast_byte(status);  /* mark thread as `dead' */
        Lumen::Do::SetErrorObject(L, status, L->Top);
        L->CallInfo->Top = L->Top;
    } else {
        LumenAssert(L->NCCalls == L->BaseCCalls);
        status = L->Status;
    }
    --L->NCCalls;
    LumenUnlock(L);
    return status;
}

Lumen::Ret Lumen::IState::Status() {
    return ToState(this)->Status;
}

bool Lumen::IState::CanYield() {
    return ToState(this)->NCCalls == 0;
}

// MARK: garbage-collection function and options

int Lumen::IState::GC(Lumen::GCAction what, int data) {
    auto L = ToState(this);
    int res = 0;
    Lumen::GlobalState *g;
    LumenLock(L);
    g = LumenGlobalState(L);
    switch (what) {
        case Lumen::GCStop: {
            g->GCThreshold = Lumen::MaxUMemory;
            break;
        }
        case Lumen::GCRestart: {
            g->GCThreshold = g->TotalBytes;
            break;
        }
        case Lumen::GCCollect: {
            Lumen::GC::FullGC(L);
            break;
        }
        case Lumen::GCCount: {
            /* GC values are expressed in KBytes: #bytes/2^10 */
            res = cast_int(g->TotalBytes >> 10);
            break;
        }
        case Lumen::GCCountB: {
            res = cast_int(g->TotalBytes & 0x3ff);
            break;
        }
        case Lumen::GCStep: {
            Lumen::MemorySize a = (cast(Lumen::MemorySize, data) << 10);
            if (a <= g->TotalBytes)
                g->GCThreshold = g->TotalBytes - a;
            else
                g->GCThreshold = 0;
            while (g->GCThreshold <= g->TotalBytes) {
                Lumen::GC::Step(L);
                if (g->GCState == Lumen::GC::StatePause) {  /* end of cycle? */
                    res = 1;  /* signal it */
                    break;
                }
            }
            break;
        }
        case Lumen::GCSetPause: {
            res = g->GCPause;
            g->GCPause = data;
            break;
        }
        case Lumen::GCSetStepMul: {
            res = g->GCStepMul;
            g->GCStepMul = data;
            break;
        }
        default:
            res = -1;  /* invalid option */
    }
    LumenUnlock(L);
    return res;
}

// MARK: miscellaneous functions

Lumen::Ret Lumen::IState::Error() {
    auto L = ToState(this);
    LumenLock(L);
    LumenApiCheckElementCount(L, 1);
    Lumen::Debug::ErrorMessage(L);
    LumenUnlock(L);
    return 0;  /* to avoid warnings */
}

bool Lumen::IState::Next(int idx) {
    auto L = ToState(this);
    Lumen::Value t;
    int more;
    LumenLock(L);
    t = L->ToObject(idx);
    LumenApiCheck(L, t->IsTable());
    more = Lumen::Table::Next(L, t->GetTable(), L->Top - 1);
    if (more) {
        LumenApiIncrTop(L);
    } else  /* no more elements */
        L->Top -= 1;  /* remove key */
    LumenUnlock(L);
    return more;
}

void Lumen::IState::Concat(int n) {
    auto L = ToState(this);
    LumenLock(L);
    LumenApiCheckElementCount(L, n);
    if (n >= 2) {
        L->CheckGC();
        Lumen::VM::Concat(L, n, cast_int(L->Top - L->Base) - 1);
        L->Top -= (n - 1);
    } else if (n == 0) {  /* push empty string */
        LumenSetStringValue2S(L, L->Top, Lumen::String::New(L, "", 0));
        LumenApiIncrTop(L);
    }
    /* else n == 1; nothing to do */
    LumenUnlock(L);
}

void Lumen::IState::LengthOf(int idx) {
    auto L = ToState(this);
    Lumen::Value t;
    LumenLock(L);
    t = L->ToObject(idx);
    Lumen::VM::ObjectLength(L, L->Top, t);
    LumenApiIncrTop(L);
    LumenUnlock(L);
}

Lumen::Allocator Lumen::IState::GetAllocator(void **ud) {
    auto L = ToState(this);
    Lumen::Allocator f;
    LumenLock(L);
    if (ud) *ud = LumenGlobalState(L)->ReAllocatorUData;
    f = LumenGlobalState(L)->ReAllocator;
    LumenUnlock(L);
    return f;
}

void Lumen::IState::SetAllocator(Lumen::Allocator f, void *ud) {
    auto L = ToState(this);
    LumenLock(L);
    LumenGlobalState(L)->ReAllocatorUData = ud;
    LumenGlobalState(L)->ReAllocator = f;
    LumenUnlock(L);
}

// MARK: Debug APIs

bool Lumen::IState::GetStack(int level, Lumen::DebugInfo *ar) {
    auto L = ToState(this);
    bool status;
    Lumen::CallInfo *ci;
    LumenLock(L);
    for (ci = L->CallInfo; level > 0 && ci > L->BaseCI; ci--) {
        level--;
        if (ci->IsLuaFunction())  /* Lua function? */
            level -= ci->NTailCalls;  /* skip lost tail calls */
    }
    if (level == 0 && ci > L->BaseCI) {  /* level found? */
        status = true;
        reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI = cast_int(ci - L->BaseCI);
    } else if (level < 0) {  /* level is of a lost tail call? */
        status = true;
        reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI = 0;
    } else status = false;  /* no such level */
    LumenUnlock(L);
    return status;
}

bool Lumen::IState::GetInfo(const char *what, Lumen::DebugInfo *ar) {
    auto L = ToState(this);
    int status;
    Lumen::Closure *f = nullptr;
    Lumen::CallInfo *ci = nullptr;
    LumenLock(L);
    if (*what == '>') {
        Lumen::Value func = L->Top - 1;
        LumenApiCheck(L, func->IsFunction());
        what++;  /* skip the '>' */
        f = func->GetClosure();
        L->Top--;  /* pop function */
    } else if (reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI != 0) {  /* no tail call? */
        ci = L->BaseCI + reinterpret_cast<Lumen::DebugInfo *>(ar)->CurrentCI;
        LumenAssert(ci->Func->IsFunction());
        f = ci->Func->GetClosure();
    }
    status = Lumen::Debug::GetInfo(L, what, reinterpret_cast<Lumen::DebugInfo *>(ar), f, ci);
    if (strchr(what, 'f')) {
        if (f == nullptr) L->Top->SetNil();
        else
            L->Top->SetClosure(L, f);
        LumenIncrTop(L);
    }
    if (strchr(what, 'L'))
        Lumen::Debug::CollectValidLines(L, f);
    LumenUnlock(L);
    return status;
}

const char *Lumen::IState::GetLocal(const Lumen::DebugInfo *ar, int n) {
    auto L = ToState(this);
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name)
        PushObject(reinterpret_cast<Lumen::IObject *>(ci->Base + (n - 1)));
    LumenUnlock(L);
    return name;
}

const char *Lumen::IState::SetLocal(const Lumen::DebugInfo *ar, int n) {
    auto L = ToState(this);
    Lumen::CallInfo *ci = L->BaseCI + reinterpret_cast<const Lumen::DebugInfo *>(ar)->CurrentCI;
    const char *name = L->FindLocal(ci, n);
    LumenLock(L);
    if (name)
        LumenSetObjectS2S (L, ci->Base + (n - 1), L->Top - 1);
    L->Top--;  /* pop value */
    LumenUnlock(L);
    return name;
}

const char *Lumen::IState::GetUpValue(int funcIndex, int n) {
    auto L = ToState(this);
    const char *name;
    Lumen::Object *val;
    LumenLock(L);
    name = L->ToObject(funcIndex)->GetUpValueInfo(n, &val);
    if (name) {
        LumenSetObject2S(L, L->Top, val);
        LumenApiIncrTop(L);
    }
    LumenUnlock(L);
    return name;
}

const char *Lumen::IState::SetUpValue(int funcIndex, int n) {
    auto L = ToState(this);
    const char *name;
    Lumen::Object *val;
    Lumen::Value fi;
    LumenLock(L);
    fi = L->ToObject(funcIndex);
    LumenApiCheckElementCount(L, 1);
    name = fi->GetUpValueInfo(n, &val);
    if (name) {
        L->Top--;
        val->SetObject(L, L->Top);
        L->Barrier(fi->GetClosure(), L->Top);
    }
    LumenUnlock(L);
    return name;
}

static Lumen::UpValue **getUpValueRef(Lumen::State *L, int fIdx, int n, Lumen::LClosure **pf) {
    Lumen::LClosure *f;
    Lumen::Value fi = L->ToObject(fIdx);
    LumenApiCheck(L, fi->IsLFunction());
    f = fi->GetLClosure();
    LumenApiCheck(L, (1 <= n && n <= f->Func->UpValuesCount));
    if (pf) *pf = f;
    return &f->UpValues[n - 1];  /* get its upvalue pointer */
}

void *Lumen::IState::GetUpValueId(int fIdx, int n) {
    auto L = ToState(this);
    Lumen::Value fi = L->ToObject(fIdx);
    if (fi->IsLFunction()) { /* lua closure */
        return *getUpValueRef(L, fIdx, n, nullptr);
    } else if (fi->IsCFunction()) { /* C closure */
        Lumen::CClosure *f = fi->GetCClosure();
        LumenApiCheck(L, 1 <= n && n <= f->NUpValues);
        return &f->UpValues[n - 1];
    } else {
        LumenApiCheck(L, 0);
        return nullptr;
    }
}

void Lumen::IState::JoinUpValue(int fIdx1, int n1, int fIdx2, int n2) {
    auto L = ToState(this);
    Lumen::LClosure *f1;
    Lumen::UpValue **up1 = getUpValueRef(L, fIdx1, n1, &f1);
    Lumen::UpValue **up2 = getUpValueRef(L, fIdx2, n2, nullptr);
    *up1 = *up2;
    L->BarrierGCObject(f1, *up2);
}

bool Lumen::IState::SetHook(Lumen::Hook func, Lumen::HookMask mask, int count) {
    auto L = ToState(this);
    if (func == nullptr || mask == 0) {  /* turn off hooks? */
        mask = 0;
        func = nullptr;
    }
    L->Hook = reinterpret_cast<Lumen::Hook>(func);
    L->BaseHookCount = count;
    LumenDebugResetHookCount(L);
    L->HookMask = cast_byte(mask);
    return true;
}

Lumen::Hook Lumen::IState::GetHook() {
    return reinterpret_cast<Lumen::Hook>(ToState(this)->Hook);
}

int Lumen::IState::GetHookMask() {
    return ToState(this)->HookMask;
}

int Lumen::IState::GetHookCount() {
    return ToState(this)->BaseHookCount;
}

// MARK: Auxiliary

#define FREELIST_REF    0    /* free list of references */

static inline int infSize(const Lumen::Interface *l) {
    int size = 0;
    for (; l->Name; l++) size++;
    return size;
}

void Lumen::IState::OpenLib(const char *name, const Lumen::Interface *inf, int nUpValue) {
    if (name) {
        int size = infSize(inf);
        /* check whether lib already exists */
        FindTable(Lumen::RegistryIndex, Lumen::RegKeyLoaded, 1);
        GetField(-1, name);  /* get _LOADED[name] */
        if (!IsTable(-1)) {  /* not found? */
            Pop(1);  /* remove previous result */
            /* try global variable (and create one if it does not exist) */
            if (FindTable(Lumen::GlobalIndex, name, size) != nullptr)
                Error("name conflict for module " LUA_QS, name);
            PushValue(-1);
            SetField(-3, name);  /* _LOADED[name] = new table */
        }
        Remove(-2);  /* remove _LOADED table */
        Insert(-(nUpValue + 1));  /* move library table to below upvalues */
    }
    for (; inf->Name; inf++) {
        int i;
        for (i = 0; i < nUpValue; i++)  /* copy upvalues to the top */
            PushValue(-nUpValue);
        PushDelegate(inf->Invoke, nUpValue);
        SetField(-(nUpValue + 2), inf->Name);
    }
    Pop(nUpValue);  /* remove upvalues */
}

void Lumen::IState::Register(const Lumen::Interface *l, int nUpValue) {
    CheckStack(nUpValue, "too many upvalues");
    for (; l->Name != nullptr; l++) {  /* fill the table with given functions */
        int i;
        for (i = 0; i < nUpValue; i++)  /* copy upvalues to the top */
            PushValue(-nUpValue);
        PushDelegate(l->Invoke, nUpValue);  /* closure with those upvalues */
        SetField(-(nUpValue + 2), l->Name);
    }
    Pop(nUpValue);  /* remove upvalues */
}

bool Lumen::IState::GetMetaField(int obj, const char *e) {
    if (!GetMetatable(obj))  /* no metatable? */
        return false;
    PushString(e);
    RawGet(-2);
    if (IsNil(-1)) {
        Pop(2);  /* remove metatable and metaField */
        return false;
    } else {
        Remove(-2);  /* remove only metatable */
        return true;
    }
}

bool Lumen::IState::CallMeta(int obj, const char *e) {
    obj = AbsIndex(obj);
    if (!GetMetaField(obj, e))  /* no metaField? */
        return false;
    PushValue(obj);
    Call(1, 1);
    return true;
}

int Lumen::IState::TypeError(int nArg, const char *tName) {
    const char *msg = PushFString("%s expected, got %s",
                                  tName, TypeName(nArg));
    return ArgError(nArg, msg);
}

int Lumen::IState::ArgError(int nArg, const char *extraMsg) {
    Lumen::DebugInfo ar; // NOLINT
    if (!GetStack(0, &ar))  /* no stack frame? */
        return Error("bad argument #%d (%s)", nArg, extraMsg);
    GetInfo("n", &ar);
    if (strcmp(ar.NameSpace, "method") == 0) {
        nArg--;  /* do not count `self` */
        if (nArg == 0)  /* error is in the self argument itself? */
            return Error("calling " LUA_QS " on bad self (%s)",
                         ar.Name, extraMsg);
    }
    if (ar.Name == nullptr)
        ar.Name = "?";
    return Error("bad argument #%d to " LUA_QS " (%s)",
                 nArg, ar.Name, extraMsg);
}

static void tagError(Lumen::IState *L, int nArg, int tag) {
    L->TypeError(nArg, L->TypeName(tag));
}

const char *Lumen::IState::CheckString(int nArg, size_t *length) {
    const char *s = ToString(nArg, length);
    if (!s) tagError(this, nArg, Lumen::TypeString);
    return s;
}

const char *Lumen::IState::OptString(int nArg, const char *def, size_t *length) {
    if (IsNoneOrNil(nArg)) {
        if (length)
            *length = (def ? strlen(def) : 0);
        return def;
    }
    return CheckString(nArg, length);
}

Lumen::Number Lumen::IState::CheckNumber(int nArg) {
    auto d = ToNumber(nArg);
    if (d == 0 && !IsNumber(nArg))  /* avoid extra test when d is not 0 */
        tagError(this, nArg, Lumen::TypeNumber);
    return d;
}

Lumen::Number Lumen::IState::OptNumber(int nArg, Lumen::Number def) {
    return Opt(&Lumen::IState::CheckNumber, nArg, def);
}

Lumen::Integer Lumen::IState::CheckInteger(int nArg) {
    auto d = ToInteger(nArg);
    if (d == 0 && !IsNumber(nArg))  /* avoid extra test when d is not 0 */
        tagError(this, nArg, Lumen::TypeNumber);
    return d;
}

Lumen::Integer Lumen::IState::OptInteger(int nArg, Lumen::Integer def) {
    return Opt(&Lumen::IState::CheckInteger, nArg, def);
}

void Lumen::IState::CheckStack(int sz, const char *msg) {
    if (!CheckStack(sz)) {
        if (msg)
            Error("stack overflow (%s)", msg);
        else
            Error("stack overflow");
    }
}

void Lumen::IState::CheckType(int nArg, Lumen::Type t) {
    if (TypeId(nArg) != t)
        tagError(this, nArg, t);
}

void Lumen::IState::CheckAny(int nArg) {
    if (TypeId(nArg) == Lumen::TypeNone)
        ArgError(nArg, "value expected");
}

bool Lumen::IState::NewMetatable(const char *tName) {
    GetField(Lumen::RegistryIndex, tName);  /* get registry.name */
    if (!IsNil(-1))  /* name already in use? */
        return false;  /* leave previous value on top, but return false */
    Pop(1);
    NewTable();  /* create metatable */
    PushValue(-1);
    SetField(Lumen::RegistryIndex, tName);  /* registry.name = metatable */
    return true;
}

void *Lumen::IState::TestUserdata(int ud, const char *tName) {
    void *p;
    auto L = ToState(this);
    LumenLock(L);
    const Object *o = L->ToObject(ud); // NOLINT
    Lumen::Table *mt;
    // ToUserdata
    switch (o->Type) {
        case Lumen::TypeUserdata:
            p = (o->GetUData() + 1);
            break;
        case Lumen::TypeLightUserdata:
            p = o->GetLUData();
            break;
        default:
            p = nullptr;
            break;
    }
    if (p != nullptr) { /* value is a userdata? */
        // GetMetatable
        switch (o->Type) {
            case Lumen::TypeTable:
                mt = o->GetTable()->Metatable;
                break;
            case Lumen::TypeUserdata:
                mt = o->GetUData()->Metatable;
                break;
            default:
                mt = LumenGlobalState(L)->Metatable[o->Type];
                break;
        }
        if (mt != nullptr) { /* does it have a metatable? */
            const Lumen::Object *slot;
            Lumen::Value t;
            Lumen::Object key; // NOLINT
            Lumen::Object val; // NOLINT
            Lumen::Object child; // NOLINT
            child.SetTable(L, mt);
            t = L->ToObject(Lumen::RegistryIndex);
            LumenApiCheckValidIndex(L, t);
            key.SetString(L, Lumen::String::New(L, tName));
            if (LumenVMFastGetTable(L, t, key.GetString(), slot, Lumen::Table::GetString)) {
                LumenSetObject2S(L, &val, slot);
            } else {
                Lumen::VM::FinishGetTable(L, t, &key, &val, slot);
            }
            /* get correct metatable && does it have the correct mt? */
            if (!val.IsNil() && Lumen::RawEqualObject(&child, &val)) {
                LumenUnlock(L);
                return p;
            }
        }
    }
    LumenUnlock(L);
    return nullptr;
}

void *Lumen::IState::TestUserdataInstance(int ud, const char *tName) {
    void *p;
    auto L = ToState(this);
    LumenLock(L);
    const Object *o = L->ToObject(ud); // NOLINT
    Lumen::Table *mt;
    // ToUserdata
    switch (o->Type) {
        case Lumen::TypeUserdata:
            p = (o->GetUData() + 1);
            break;
        case Lumen::TypeLightUserdata:
            p = o->GetLUData();
            break;
        default:
            p = nullptr;
            break;
    }
    if (p != nullptr) { /* value is a userdata? */
        // GetMetatable
        switch (o->Type) {
            case Lumen::TypeTable:
                mt = o->GetTable()->Metatable;
                break;
            case Lumen::TypeUserdata:
                mt = o->GetUData()->Metatable;
                break;
            default:
                mt = LumenGlobalState(L)->Metatable[o->Type];
                break;
        }
        if (mt != nullptr) { /* does it have a metatable? */
            const Lumen::Object *slot;
            Lumen::Value t;
            Lumen::Object key; // NOLINT
            Lumen::Object val; // NOLINT
            Lumen::Object child; // NOLINT
            child.SetTable(L, mt);
            t = L->ToObject(Lumen::RegistryIndex);
            LumenApiCheckValidIndex(L, t);
            key.SetString(L, Lumen::String::New(L, tName));
            if (LumenVMFastGetTable(L, t, key.GetString(), slot, Lumen::Table::GetString)) {
                LumenSetObject2S(L, &val, slot);
            } else {
                Lumen::VM::FinishGetTable(L, t, &key, &val, slot);
            }
            if (!val.IsNil()) { /* get correct metatable */
                if (::InstanceOf(L, &child, &val)) { /* does it have the correct mt? */
                    LumenUnlock(L);
                    return p;
                }
            }
        }
    }
    LumenUnlock(L);
    return nullptr;
}

void *Lumen::IState::CheckUserdata(int ud, const char *tName) {
    auto p = TestUserdata(ud, tName);
    if (p == nullptr) TypeError(ud, tName);  /* else error */
    return p;  /* to avoid warnings */
}

void *Lumen::IState::CheckUserdataInstance(int ud, const char *tName) {
    auto p = TestUserdataInstance(ud, tName);
    if (p == nullptr) TypeError(ud, tName);  /* else error */
    return p;  /* to avoid warnings */
}

void Lumen::IState::Where(int level) {
    Lumen::DebugInfo ar; // NOLINT
    if (GetStack(level, &ar)) {  /* check function at level */
        GetInfo("Sl", &ar);  /* get info about it */
        if (ar.CurrentLine > 0) {  /* is there info? */
            PushFString("%s:%d: ", ar.SourceHint, ar.CurrentLine);
            return;
        }
    }
    PushLiteral("");  /* else, no information available... */
}

int Lumen::IState::Error(const char *fmt, ...) {
    va_list argP;
        va_start(argP, fmt);
    Where(1);
    PushVFString(fmt, argP);
        va_end(argP);
    Concat(2);
    return Error();
}

int Lumen::IState::CheckOption(int nArg, const char *def, const char *const *lst) {
    const char *name = (def) ? OptString(nArg, def) : CheckString(nArg);
    int i;
    for (i = 0; lst[i]; i++)
        if (strcmp(lst[i], name) == 0)
            return i;
    return ArgError(nArg, PushFString("invalid option " LUA_QS, name));
}

Lumen::Ref Lumen::IState::Ref(int t) {
    int ref;
    t = AbsIndex(t);
    if (IsNil(-1)) {
        Pop(1);  /* remove from stack */
        return Lumen::RefNil;  /* `nil' has a unique fixed reference */
    }
    RawGetAt(t, FREELIST_REF);  /* get first free element */
    ref = (int) ToInteger(-1);  /* ref = t[FREELIST_REF] */
    Pop(1);  /* remove it from stack */
    if (ref != 0) {  /* any free element? */
        RawGetAt(t, ref);  /* remove it from list */
        RawSetAt(t, FREELIST_REF);  /* (t[FREELIST_REF] = t[ref]) */
    } else {  /* no free elements */
        ref = (int) ObjectLength(t);
        ref++;  /* create new reference */
    }
    RawSetAt(t, ref);
    return ref;
}

void Lumen::IState::Unref(int t, Lumen::Ref ref) {
    if (ref >= 0) {
        t = AbsIndex(t);
        RawGetAt(t, FREELIST_REF);
        RawSetAt(t, ref);  /* t[ref] = t[FREELIST_REF] */
        PushInteger(ref);
        RawSetAt(t, FREELIST_REF);  /* t[FREELIST_REF] = ref */
    }
}

struct LoadFunc {
    int ExtraLine;
    FILE *f;
    char Buff[LUAL_BUFFERSIZE];
};

static const char *getF(Lumen::IState *, void *ud, size_t *size) {
    auto lf = (LoadFunc *) ud;
    if (lf->ExtraLine) {
        lf->ExtraLine = 0;
        *size = 1;
        return "\n";
    }
    if (feof(lf->f)) return nullptr;
    *size = fread(lf->Buff, 1, sizeof(lf->Buff), lf->f);
    return (*size > 0) ? lf->Buff : nullptr;
}

static int fileErr(Lumen::IState *L, const char *what, int fileNameIdx) {
    const char *strErr = strerror(errno);
    const char *filename = L->ToString(fileNameIdx) + 1;
    L->PushFString("cannot %s %s: %s", what, filename, strErr);
    L->Remove(fileNameIdx);
    return Lumen::RetErrFile;
}

Lumen::Ret Lumen::IState::LoadFile(const char *filename) {
    LoadFunc lf; // NOLINT
    int status, readStatus;
    int c;
    int fileNameIndex = GetTop() + 1;  /* index of filename on the stack */
    lf.ExtraLine = 0;
    if (filename == nullptr) {
        PushLiteral("=stdin");
        lf.f = stdin;
    } else {
        PushFString("@%s", filename);
        lf.f = fopen(filename, "r");
        if (lf.f == nullptr) return fileErr(this, "open", fileNameIndex);
    }
    c = getc(lf.f);
    if (c == '#') {  /* Unix exec. file? */
        lf.ExtraLine = 1;
        while ((c = getc(lf.f)) != EOF && c != '\n');  /* skip first line */
        if (c == '\n') c = getc(lf.f);
    }
    if (c == LUA_SIGNATURE[0] && filename) {  /* binary file? */
        lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
        if (lf.f == nullptr) return fileErr(this, "reopen", fileNameIndex);
        /* skip eventual `#!...' */
        while ((c = getc(lf.f)) != EOF && c != LUA_SIGNATURE[0]);
        lf.ExtraLine = 0;
    }
    ungetc(c, lf.f);
    status = Load(reinterpret_cast<Lumen::Reader>(getF), &lf, ToString(-1));
    readStatus = ferror(lf.f);
    if (filename) fclose(lf.f);  /* close file (even in case of errors) */
    if (readStatus) {
        SetTop(fileNameIndex);  /* ignore results from `lua_load' */
        return fileErr(this, "read", fileNameIndex);
    }
    Remove(fileNameIndex);
    return status;
}

struct LoadState {
    const char *s;
    size_t size;
};

static const char *getS(Lumen::IState *, void *ud, size_t *size) {
    auto ls = (LoadState *) ud;
    if (ls->size == 0) return nullptr;
    *size = ls->size;
    ls->size = 0;
    return ls->s;
}

Lumen::Ret Lumen::IState::LoadBuffer(const char *buff, size_t size, const char *name) {
    LoadState ls{buff, size};
    return Load(reinterpret_cast<Lumen::Reader>(getS), &ls, name);
}

Lumen::Ret Lumen::IState::LoadString(const char *s) {
    return LoadBuffer(s, strlen(s), s);
}

static int panic(Lumen::IState *L) {
    (void) L;  /* to avoid warnings */
    fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n",
            L->ToString(-1));
    return false;
}

Lumen::IState *Lumen::IState::New() {
    auto L = New(&Lumen::Memory::Alloc, nullptr);
    if (L) L->AtPanic(reinterpret_cast<Lumen::Delegate>(panic));
    return L;
}

const char *Lumen::IState::GSub(const char *s, const char *p, const char *r) {
    std::string result;
    size_t l = strlen(p);
    if (l == 0) {
        PushString(s);
        return ToString(-1);
    }

    const char *wild;
    while ((wild = strstr(s, p)) != nullptr) {
        result.append(s, wild);
        result.append(r);
        s = wild + l;
    }

    result.append(s);
    PushString(result.c_str());
    return ToString(-1);
}

const char *Lumen::IState::FindTable(int idx, const char *name, int hintSize) {
    const char *e;
    PushValue(idx); // _Reg
    do {
        e = strchr(name, '.');
        if (e == nullptr) e = name + strlen(name);
        PushString(name, e - name);
        RawGet(-2); // _Reg[name]
        if (IsNil(-1)) {  /* no such field? */
            Pop(1);  /* remove this nil */
            CreateTable(0, (*e == '.' ? 1 : hintSize)); /* new table for field */
            PushString(name, e - name);
            PushValue(-2);
            SetTable(-4);  /* set new table into field */
        } else if (!IsTable(-1)) {  /* field has a non-table value? */
            Pop(2);  /* remove table and value */
            return name;  /* return problematic part of the name */
        }
        Remove(-2);  /* remove previous table */
        name = e + 1;
    } while (*e == '.');
    return nullptr;
}

bool Lumen::IState::FindOrCreateTable(int idx, const char *name) {
    if (GetField(idx, name) == Lumen::TypeTable)
        return true;  /* table already there */
    else {
        Pop(1);  /* remove previous result */
        idx = AbsIndex(idx);
        NewTable();
        PushValue(-1);  /* copy to be left at top */
        SetField(idx, name);  /* assign new table to field */
        return false;  /* false, because did not find table there */
    }
}

void Lumen::IState::Require(const char *modName, Lumen::Delegate loader, bool exported) {
    FindOrCreateTable(Lumen::RegistryIndex, Lumen::RegKeyLoaded);
    GetField(-1, modName);  /* LOADED[modname] */
    if (!ToBoolean(-1)) {  /* package not already loaded? */
        Pop(1);  /* remove field */
        PushDelegate(loader);
        PushString(modName);  /* argument to open function */
        Call(1, 1);  /* call 'loader' to open module */
        PushValue(-1);  /* make copy of module (call result) */
        SetField(-3, modName);  /* LOADED[modname] = module */
    }
    Remove(-2);  /* remove LOADED table */
    if (exported) {
        PushValue(-1);  /* copy of module */
        SetGlobal(modName);  /* _G[modname] = module */
    }
}

void Lumen::IState::PrintStack() {
    int top = GetTop();
    printf("Lumen stack [%d]:\n", top);

    for (int i = 1; i <= top; ++i) {
        auto t = TypeId(i);
        switch (t) {
            case Lumen::TypeString:
                printf("[%d][str]: %s\n", i, ToString(i));
                break;

            case Lumen::TypeBool:
                printf("[%d][bol]: %s\n", i, ToBoolean(i) ? "true" : "false");
                break;

            case Lumen::TypeNumber:
                printf("[%d][num]: %lf\n", i, ToNumber(i));
                break;

            case Lumen::TypeTable:
                printf("[%d][tbl]: %p\n", i, ToPointer(i));
                break;

            case Lumen::TypeFunction:
                printf("[%d][fun]: %p\n", i, ToPointer(i));
                break;

            case Lumen::TypeUserdata:
                printf("[%d][udt]: %p\n", i, ToPointer(i));
                break;

            case Lumen::TypeLightUserdata:
                printf("[%d][lud]: %p\n", i, ToPointer(i));
                break;

            case Lumen::TypeThread:
                printf("[%d][thr]: %p\n", i, ToPointer(i));
                break;

            case Lumen::TypeNil:
                printf("[%d][nil]: nil\n", i);
                break;

            default:
                printf("[%d][???]: ???\n", i);
                break;
        }
    }

    fflush(stdout);
}
