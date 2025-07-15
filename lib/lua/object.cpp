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

#define ToObject(o) reinterpret_cast<Lumen::Object *>(o)
#define ToObjectConst(o) reinterpret_cast<const Lumen::Object *>(o)

bool Lumen::IObject::IsNil() const {
    return ToObjectConst(this)->IsNil();
}

bool Lumen::IObject::IsNumber() const {
    return ToObjectConst(this)->IsNumber();
}

bool Lumen::IObject::IsBoolean() const {
    return ToObjectConst(this)->IsBoolean();
}

bool Lumen::IObject::IsString() const {
    return ToObjectConst(this)->IsString();
}

bool Lumen::IObject::IsTable() const {
    return ToObjectConst(this)->IsTable();
}

bool Lumen::IObject::IsDelegate() const {
    return ToObjectConst(this)->IsCFunction();
}

bool Lumen::IObject::IsUData() const {
    return ToObjectConst(this)->IsUData();
}

bool Lumen::IObject::IsLUData() const {
    return ToObjectConst(this)->IsLUData();
}

Lumen::Number Lumen::IObject::ToNumber() const {
    auto self = ToObjectConst(this);
    return self->IsNumber()
           ? self->GetNumber()
           : 0;
}

bool Lumen::IObject::ToBoolean() const {
    auto self = ToObjectConst(this);
    return self->IsBoolean() && self->GetBool();
}

Lumen::IString *Lumen::IObject::ToString() {
    auto self = ToObject(this);
    return self->IsString() ? reinterpret_cast<Lumen::IString *>(self->GetString()) : nullptr;
}

Lumen::ITable *Lumen::IObject::ToTable() {
    auto self = ToObject(this);
    return self->IsTable() ? reinterpret_cast<Lumen::ITable *>(self->GetTable()) : nullptr;
}

Lumen::Delegate Lumen::IObject::ToDelegate() {
    auto self = ToObject(this);
    return self->IsCFunction() ? self->GetCClosure()->Func : nullptr;
}

Lumen::IUserdata *Lumen::IObject::ToUserdata() {
    auto self = ToObject(this);
    return self->IsUData() ? reinterpret_cast<Lumen::IUserdata *>(self->GetUData()) : nullptr;
}

void *Lumen::IObject::ToLightUserdata() {
    auto self = ToObject(this);
    return self->IsLUData() ? reinterpret_cast<Lumen::IUserdata *>(self->GetLUData()) : nullptr;
}
