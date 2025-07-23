/*!
 * @brief object
 * @author Jakit
 * @date 2025/6/23
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#ifndef LUMEN_OBJECT_INL
#define LUMEN_OBJECT_INL

#include "lumen/object.h"
#include "lumen/state.h"
#include "lumen/gc.h"

inline bool Lumen::Object::IsFalse() const {
    return Type == Lumen::TypeNil || (Type == Lumen::TypeBool && value.b == 0);
}

inline bool Lumen::Object::IsCollectable() const {
    return Type >= Lumen::TypeString;
}

inline bool Lumen::Object::IsCFunction() const {
    return Type == Lumen::TypeFunction && GetClosure()->AsC.IsC;
}

inline bool Lumen::Object::IsLFunction() const {
    return Type == Lumen::TypeFunction && !GetClosure()->AsC.IsC;
}

inline bool Lumen::Object::IsWhite() const {
    return (IsCollectable() && GetGCObject()->IsWhite());
}

inline Lumen::GCObject *Lumen::Object::GetGCObject() const {
    return LumenCheckExp(IsCollectable(), value.gc);
}

inline void *Lumen::Object::GetLUData() const {
    return LumenCheckExp(IsLUData(), value.p);
}

inline Lumen::Number &Lumen::Object::GetNumber() {
    return LumenCheckExp(IsNumber(), value.n);
}

inline const Lumen::Number &Lumen::Object::GetNumber() const {
    return LumenCheckExp(IsNumber(), value.n);
}

inline Lumen::String *Lumen::Object::GetString() const {
    return LumenCheckExp(IsString(), &(value.gc->AsString));
}

inline Lumen::Userdata *Lumen::Object::GetUData() const {
    return LumenCheckExp(IsUData(), &(value.gc->AsUserdata));
}

inline Lumen::Closure *Lumen::Object::GetClosure() const {
    return LumenCheckExp(IsFunction(), &(value.gc->AsClosure));
}

inline Lumen::LClosure *Lumen::Object::GetLClosure() const {
    return LumenCheckExp(IsFunction(), &(value.gc->AsClosure.AsLua));
}

inline Lumen::CClosure *Lumen::Object::GetCClosure() const {
    return LumenCheckExp(IsFunction(), &(value.gc->AsClosure.AsC));
}

inline Lumen::Table *Lumen::Object::GetTable() const {
    return LumenCheckExp(IsTable(), &(value.gc->AsTable));
}

inline int Lumen::Object::GetBool() const {
    return LumenCheckExp(IsBoolean(), value.b);
}

inline Lumen::State *Lumen::Object::GetThread() const {
    return LumenCheckExp(IsThread(), &(value.gc->AsThread));
}

inline void Lumen::Object::SetType(Lumen::Type t) {
    Type = t;
}

inline void Lumen::Object::SetNil() {
    Type = Lumen::TypeNil;
}

inline void Lumen::Object::SetNumber(Lumen::Number x) {
    value.n = x;
    Type = Lumen::TypeNumber;
}

inline void Lumen::Object::SetLUData(void *x) {
    value.p = x;
    Type = Lumen::TypeLightUserdata;
}

inline void Lumen::Object::SetBool(int x) {
    value.b = x;
    Type = Lumen::TypeBool;
}

inline void Lumen::Object::SetString(Lumen::State *L, Lumen::String *x) {
    value.gc = reinterpret_cast<GCObject *>(x);
    Type = Lumen::TypeString;
    LumenCheckLiveness(LumenGlobalState(L), this);
}

inline void Lumen::Object::SetUData(Lumen::State *L, Lumen::Userdata *x) {
    value.gc = reinterpret_cast<GCObject *>(x);
    Type = Lumen::TypeUserdata;
    LumenCheckLiveness(LumenGlobalState(L), this);
}

inline void Lumen::Object::SetThread(Lumen::State *L, Lumen::State *x) {
    value.gc = reinterpret_cast<GCObject *>(x);
    Type = Lumen::TypeThread;
    LumenCheckLiveness(LumenGlobalState(L), this);
}

inline void Lumen::Object::SetClosure(Lumen::State *L, Lumen::Closure *x) {
    value.gc = reinterpret_cast<GCObject *>(x);
    Type = Lumen::TypeFunction;
    LumenCheckLiveness(LumenGlobalState(L), this);
}

inline void Lumen::Object::SetTable(Lumen::State *L, Lumen::Table *x) {
    value.gc = reinterpret_cast<GCObject *>(x);
    Type = Lumen::TypeTable;
    LumenCheckLiveness(LumenGlobalState(L), this);
}

inline void Lumen::Object::SetProto(Lumen::State *L, Lumen::Proto *x) {
    value.gc = reinterpret_cast<GCObject *>(x);
    Type = Lumen::TypeProto;
    LumenCheckLiveness(LumenGlobalState(L), this);
}

inline void Lumen::Object::SetObject(Lumen::State *L, const Lumen::Object *other) {
    value = other->value;
    Type = other->Type;
    LumenCheckLiveness(LumenGlobalState(L), this);
}

inline char *Lumen::Object::ToCString() const {
    return GetString()->CString();
}

inline int Lumen::Int2FB(unsigned int x) {
    int e = 0;  /* exponent */
    while (x >= 16) {
        x = (x + 1) >> 1;
        e++;
    }
    if (x < 8) return static_cast<int>(x);
    else return ((e + 1) << 3) | (static_cast<int>(x) - 8);
}

inline int Lumen::FB2Int(int x) {
    int e = (x >> 3) & 31;
    if (e == 0) return x;
    else return ((x & 7) + 8) << (e - 1);
}

inline int Lumen::RawEqualObject(const Lumen::Object *t1, const Lumen::Object *t2) {
    if (t1->Type != t2->Type) return 0;
    switch (t1->Type) {
        case Lumen::TypeNil:
            return 1;
        case Lumen::TypeNumber:
            return t1->GetNumber() == t2->GetNumber();
        case Lumen::TypeBool:
            return t1->GetBool() == t2->GetBool();  /* boolean true must be 1 !! */
        case Lumen::TypeLightUserdata:
            return t1->GetLUData() == t2->GetLUData();
        default:
            LumenAssert(t1->IsCollectable());
            return t1->GetGCObject() == t2->GetGCObject();
    }
}

inline int Lumen::String2Decimal(const char *s, Lumen::Number *result) {
    char *endPtr;
    *result = LumenStr2Num(s, &endPtr);
    if (endPtr == s) return 0;  /* conversion failed */
    if (*endPtr == 'x' || *endPtr == 'X')  /* maybe an hexadecimal constant? */
        *result = cast_num(strtoul(s, &endPtr, 16));
    if (*endPtr == '\0') return 1;  /* most common case */
    while (isspace(cast(unsigned char, *endPtr))) endPtr++;
    if (*endPtr != '\0') return 0;  /* invalid trailing characters? */
    return 1;
}

inline int Lumen::Utf8Esc(char *buff, unsigned long x) {
    int n = 1;  /* number of bytes put in buffer (backwards) */
    LumenAssert(x <= 0x7FFFFFFFu);
    if (x < 0x80)  /* ascii? */
        buff[Lumen::UTF8BufferSize - 1] = cast_char(x);
    else {  /* need continuation bytes */
        unsigned int mfb = 0x3f;  /* maximum that fits in first byte */
        do {  /* add continuation bytes */
            buff[Lumen::UTF8BufferSize - (n++)] = cast_char(0x80 | (x & 0x3f));
            x >>= 6;  /* remove added bits */
            mfb >>= 1;  /* now there is one less bit available in first byte */
        } while (x > mfb);  /* still needs continuation byte? */
        buff[Lumen::UTF8BufferSize - n] = cast_char((~mfb << 1) | x);  /* add first byte */
    }
    return n;
}

inline char *Lumen::String::CString() {
    return reinterpret_cast<char *>(this + 1);
}

inline const char *Lumen::String::CString() const {
    return reinterpret_cast<const char *>(this + 1);
}

inline Lumen::String *Lumen::GCObject::ToString() {
    return LumenCheckExp(AsObject.Type == Lumen::TypeString, &AsString);
}

inline Lumen::Userdata *Lumen::GCObject::ToUserdata() {
    return LumenCheckExp(AsObject.Type == Lumen::TypeUserdata, &AsUserdata);
}

inline Lumen::Closure *Lumen::GCObject::ToClosure() {
    return LumenCheckExp(AsObject.Type == Lumen::TypeFunction, &AsClosure);
}

inline Lumen::Table *Lumen::GCObject::ToTable() {
    return LumenCheckExp(AsObject.Type == Lumen::TypeTable, &AsTable);
}

inline Lumen::Proto *Lumen::GCObject::ToProto() {
    return LumenCheckExp(AsObject.Type == Lumen::TypeProto, &AsProto);
}

inline Lumen::UpValue *Lumen::GCObject::ToUpValue() {
    return LumenCheckExp(AsObject.Type == Lumen::TypeUpValue, &AsUpValue);
}

inline Lumen::State *Lumen::GCObject::ToThread() {
    return LumenCheckExp(AsObject.Type == Lumen::TypeThread, &AsThread);
}

inline Lumen::UpValue *Lumen::GCObject::ToNullableUpValue(Lumen::GCObject *o) {
    return LumenCheckExp(o == nullptr || o->AsObject.Type == Lumen::TypeUpValue, &(o->AsUpValue));
}

inline Lumen::Closure *Lumen::State::GetCurrentFunction() const {
    return CallInfo->Func->GetClosure();
}

inline Lumen::Closure *Lumen::CallInfo::GetFunction() const {
    return Func->GetClosure();
}

inline bool Lumen::CallInfo::IsLuaFunction() const {
    return !Func->GetClosure()->AsC.IsC;
}

inline bool Lumen::CallInfo::IsFunctionOfLua() const {
    return (Func->IsFunction() && IsLuaFunction());
}

inline int Lumen::Closure::Size() const {
    return static_cast<int>(Basic.IsC ? Lumen::CClosure::SizeOf(Basic.NUpValues)
                                      : Lumen::LClosure::SizeOf(Basic.NUpValues));
}

inline Lumen::UInteger Lumen::CClosure::SizeOf(Lumen::UInteger nUpValues) {
    return sizeof(Lumen::Closure) + sizeof(Lumen::Object) * (nUpValues - 1);
}

inline Lumen::UInteger Lumen::LClosure::SizeOf(Lumen::UInteger nUpValues) {
    return sizeof(Lumen::Closure) + sizeof(Lumen::Object *) * (nUpValues - 1);
}

inline bool Lumen::GCObject::IsWhite() const {
    return Lumen::GC::Test2Bits(AsObject.Marked, Lumen::GC::MarkWhite0Bit, Lumen::GC::MarkWhite1Bit);
}

inline bool Lumen::GCObject::IsBlack() const {
    return Lumen::GC::TestBit(AsObject.Marked, Lumen::GC::MarkBlackBit);
}

inline bool Lumen::GCObject::IsGray() const {
    return (!IsBlack() && !IsWhite());
}

inline void Lumen::GCObject::ChangeWhite() {
    AsObject.Marked ^= Lumen::GC::MarkWhiteBits;
}

inline void Lumen::GCObject::Gray2Black() {
    Lumen::GC::LSetBit(AsObject.Marked, Lumen::GC::MarkBlackBit);
}

inline Lumen::Byte Lumen::GlobalState::GetWhite() const {
    return CurrentWhite & Lumen::GC::MarkWhiteBits;
}

inline Lumen::Byte Lumen::GlobalState::GetOtherWhite() const {
    return CurrentWhite ^ Lumen::GC::MarkWhiteBits;
}

inline bool Lumen::GlobalState::IsDead(Lumen::GCObject *v) const {
    return ((v)->AsObject.Marked & GetOtherWhite() & Lumen::GC::MarkWhiteBits);
}

inline void Lumen::State::CheckGC() {
    LumenCondHardStackTests(Lumen::Do::ReAllocStack(this, StackCount - Lumen::ExtraStack - 1));
    if (LumenGlobalState(this)->TotalBytes >= LumenGlobalState(this)->GCThreshold)
        Lumen::GC::Step(this);
}

inline void Lumen::State::Barrier(void *p, const Lumen::Object *v) {
    if (v->IsWhite() && LumenObject2GCObject(p)->IsBlack())
        Lumen::GC::BarrierF(this, LumenObject2GCObject(p), v->GetGCObject());
}

inline void Lumen::State::BarrierTable(Lumen::Table *t, const Lumen::Object *v) {
    if (v->IsWhite() && LumenObject2GCObject(t)->IsBlack())
        Lumen::GC::BarrierBack(this, t);
}

inline void Lumen::State::BarrierGCObject(void *p, void *o) {
    if (LumenObject2GCObject(o)->IsWhite() && LumenObject2GCObject(p)->IsBlack())
        Lumen::GC::BarrierF(this, LumenObject2GCObject(p), LumenObject2GCObject(o));
}

inline void Lumen::State::BarrierGCObjectTable(Lumen::Table *t, void *o) {
    if (LumenObject2GCObject(o)->IsWhite() && LumenObject2GCObject(t)->IsBlack())
        Lumen::GC::BarrierBack(this, t);
}

inline const Lumen::Object *Lumen::MetaMethod::GetByObject(Lumen::State *L, const Lumen::Object *o, Lumen::MetaMethod::Name event) {
    Lumen::Table *mt;
    switch (o->Type) {
        case Lumen::TypeTable:
            mt = o->GetTable()->Metatable;
            break;
        case Lumen::TypeUserdata:
            mt = o->GetUData()->Metatable;
            break;
        default:
            mt = LumenGlobalState(L)->Metatable[(o)->Type];
    }
    return (mt ? Lumen::Table::GetString(mt, LumenGlobalState(L)->MetatableName[event]) : Lumen::NilObject);
}

#endif //LUMEN_OBJECT_INL
