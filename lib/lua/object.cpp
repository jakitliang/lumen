/*!
 * @brief Lumen Object Interface
 * @author Jakit
 * @date 2025/7/16
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */


#define LUA_LIB

#include "lumen.h"
#include "lumen/object.h"
#include "lumen/common.inl"

#define ToLumenObject(o) reinterpret_cast<Lumen::Object *>(o)
#define ToLumenConstObject(o) reinterpret_cast<const Lumen::Object *>(o)

bool Lumen::IObject::IsNil() const {
    return ToLumenConstObject(this)->IsNil();
}

bool Lumen::IObject::IsNumber() const {
    return ToLumenConstObject(this)->IsNumber();
}

bool Lumen::IObject::IsBoolean() const {
    return ToLumenConstObject(this)->IsBoolean();
}

bool Lumen::IObject::IsString() const {
    return ToLumenConstObject(this)->IsString();
}

bool Lumen::IObject::IsTable() const {
    return ToLumenConstObject(this)->IsTable();
}

bool Lumen::IObject::IsDelegate() const {
    return ToLumenConstObject(this)->IsCFunction();
}

bool Lumen::IObject::IsUData() const {
    return ToLumenConstObject(this)->IsUData();
}

bool Lumen::IObject::IsLUData() const {
    return ToLumenConstObject(this)->IsLUData();
}

Lumen::Number Lumen::IObject::ToNumber() const {
    auto self = ToLumenConstObject(this);
    return self->IsNumber()
           ? self->GetNumber()
           : 0;
}

bool Lumen::IObject::ToBoolean() const {
    auto self = ToLumenConstObject(this);
    return self->IsBoolean() && self->GetBool();
}

const Lumen::Number *Lumen::IObject::ToNumberRef() const {
    auto self = ToLumenConstObject(this);
    return self->IsNumber()
           ? &self->GetNumber()
           : nullptr;
}

Lumen::IString *Lumen::IObject::ToString() {
    auto self = ToLumenObject(this);
    return self->IsString() ? reinterpret_cast<Lumen::IString *>(self->GetString()) : nullptr;
}

Lumen::ITable *Lumen::IObject::ToTable() {
    auto self = ToLumenObject(this);
    return self->IsTable() ? reinterpret_cast<Lumen::ITable *>(self->GetTable()) : nullptr;
}

Lumen::Delegate Lumen::IObject::ToDelegate() {
    auto self = ToLumenObject(this);
    return self->IsCFunction() ? self->GetCClosure()->Func : nullptr;
}

Lumen::IUserdata *Lumen::IObject::ToUserdata() {
    auto self = ToLumenObject(this);
    return self->IsUData() ? reinterpret_cast<Lumen::IUserdata *>(self->GetUData()) : nullptr;
}

void *Lumen::IObject::ToLightUserdata() {
    auto self = ToLumenObject(this);
    return self->IsLUData() ? reinterpret_cast<Lumen::IUserdata *>(self->GetLUData()) : nullptr;
}

Lumen::IObject *Lumen::IObject::Get(Lumen::IState *l, int idx) {
    auto L = reinterpret_cast<Lumen::State *>(l);
    return reinterpret_cast<Lumen::IObject *>(L->ToObject(idx));
}
