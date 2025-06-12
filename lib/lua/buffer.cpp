/*!
 * @brief Buffer Helper
 * @author Jakit
 * @date 2025/6/12
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#define LUA_LIB

#include <vector>
#include <string_view>
#include <algorithm>

#include "lua.hpp"

namespace Lua {
    using BufferRef = std::vector<Lua::Byte> *;
}

Lua::Buffer *Lua::Buffer::Get() {
    thread_local std::vector<Lua::Byte> buffer;
    return reinterpret_cast<Lua::Buffer *>(&buffer);
}

void Lua::Buffer::Push(char c) {
    reinterpret_cast<BufferRef>(this)->push_back(c);
}

void Lua::Buffer::Push(const char *cStr) {
    auto buffer = reinterpret_cast<BufferRef>(this);
    buffer->insert(buffer->end(), cStr, cStr + std::string_view(cStr).length());
}

void Lua::Buffer::Push(const void *cBuffer, Lua::UInteger size) {
    auto buffer = reinterpret_cast<BufferRef>(this);
    auto cStr = static_cast<const char *>(cBuffer);
    buffer->insert(buffer->end(), cStr, cStr + size);
}

void Lua::Buffer::Reverse() {
    auto buffer = reinterpret_cast<BufferRef>(this);
    std::reverse(buffer->begin(), buffer->end());
}

void Lua::Buffer::Reserve(Lua::UInteger size) {
    auto buffer = reinterpret_cast<BufferRef>(this);
    buffer->reserve(size);
}

void Lua::Buffer::Resize(Lua::UInteger size) {
    auto buffer = reinterpret_cast<BufferRef>(this);
    buffer->resize(size);
}

Lua::UInteger Lua::Buffer::Length() {
    auto buffer = reinterpret_cast<BufferRef>(this);
    return buffer->size();
}

void Lua::Buffer::Clear() {
    auto buffer = reinterpret_cast<BufferRef>(this);
    buffer->clear();
}

char *Lua::Buffer::CString() {
    auto buffer = reinterpret_cast<BufferRef>(this);
    return reinterpret_cast<char *>(buffer->data());
}

void *Lua::Buffer::CBuffer() {
    auto buffer = reinterpret_cast<BufferRef>(this);
    return buffer->data();
}

void Lua::Buffer::AddValue(Lua::State *L) {
    size_t len;
    const char *str = L->ToString(-1, &len);
    Push(str, len);
    L->Pop();
}
