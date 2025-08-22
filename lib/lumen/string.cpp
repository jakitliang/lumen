/*!
 * @brief String table (keeps all strings handled by Lua)
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstring>
#include <cstdint>
#include <string_view>
#include <unordered_map>

#define LUA_CORE

#include "lumen/memory.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/state.h"

#define XXH_STATIC_LINKING_ONLY /* access advanced declarations */
#define XXH_IMPLEMENTATION      /* access definitions */
#include "xxhash/xxhash.h"

#ifdef LUA_USE_HP_HASH

Lumen::UInt32 Lumen::Hash::EncodeUInt32(const Lumen::Byte *key, Lumen::UInteger len, Lumen::UInt32 seed) {
    return (Lumen::UInt32) XXH64(key, len, seed);
}

#else

Lumen::UInt32 Lumen::Hash::EncodeUInt32(const Lumen::Byte *key, Lumen::UInteger len, Lumen::UInt32 seed) {
    uint32_t h = cast(uint32_t, len);
    size_t step = (len >> 5) + 1;
    size_t l1;
    for (l1 = len; l1 >= step; l1 -= step)  /* compute hash */
        h = h ^ ((h << 5) + (h >> 2) + cast(Lumen::Byte, key[l1 - 1]));
    return h;
}

#endif

inline Lumen::UInt32 LumenStringHash(const Lumen::Byte *key, Lumen::UInteger len, Lumen::UInt32 seed) {
    return (Lumen::UInt32) XXH64(key, len, seed);
}

void Lumen::String::Intern(Lumen::State *L) {
    if (Hash != 0) return;

    unsigned int h = LumenStringHash((uint8_t *) CString(), Length, cast_uint(Length << 1));
    Hash = h;

    Lumen::StringTable *tb = &LumenGlobalState(L)->StringMap;
    h = LumenLogMod(h, tb->Capacity);

    GCNext = tb->HashTable[h];
    tb->HashTable[h] = LumenObject2GCObject(this);
    tb->Count++;

    if (tb->Count > cast(Lumen::UInt32, tb->Capacity) && tb->Capacity <= Lumen::MaxInt / 2) {
        Lumen::String::Resize(L, tb->Capacity * 2);
    }
}

void Lumen::String::Resize(Lumen::State *L, int newSize) {
    Lumen::GCObject **newHash;
    Lumen::StringTable *tb;
    int i;
    if (LumenGlobalState(L)->GCState == Lumen::GC::StateSweepString)
        return;  /* cannot resize during GC traverse */
    newHash = LumenMemoryNewVector(L, newSize, Lumen::GCObject *);
    tb = &LumenGlobalState(L)->StringMap;
    for (i = 0; i < newSize; i++) newHash[i] = nullptr;
    /* rehash */
    for (i = 0; i < tb->Capacity; i++) {
        Lumen::GCObject *p = tb->HashTable[i];
        while (p) {  /* for each node in the list */
            Lumen::GCObject *next = p->AsObject.GCNext;  /* save next */
            unsigned int h = p->ToString()->Hash;
            int h1 = LumenLogMod(h, newSize);  /* new position */
            LumenAssert(cast_int(h % newSize) == LumenLogMod(h, newSize));
            p->AsObject.GCNext = newHash[h1];  /* chain it */
            newHash[h1] = p;
            p = next;
        }
    }
    LumenMemoryFreeArray(L, tb->HashTable, tb->Capacity, Lumen::String *);
    tb->Capacity = newSize;
    tb->HashTable = newHash;
}


static Lumen::String *newStringWithLength(Lumen::State *L, const char *str, Lumen::UInteger l,
                                          unsigned int h) {
    Lumen::String *ts;
    Lumen::StringTable *tb;
    if (l + 1 > (Lumen::MaxSize - sizeof(Lumen::String)) / sizeof(char))
        Lumen::Memory::TooBig(L);
    ts = cast(Lumen::String *, LumenMemoryAlloc(L, (l + 1) * sizeof(char) + sizeof(Lumen::String)));
    ts->Length = l;
    ts->Hash = h;
    ts->Marked = LumenGlobalState(L)->GetWhite();
    ts->Type = Lumen::TypeString;
    ts->Reserved = 0;
    memcpy(ts + 1, str, l * sizeof(char));
    ((char *) (ts + 1))[l] = '\0';  /* ending 0 */
    tb = &LumenGlobalState(L)->StringMap;
    h = LumenLogMod(h, tb->Capacity);
    ts->GCNext = tb->HashTable[h];  /* chain new entry */
    tb->HashTable[h] = LumenObject2GCObject(ts);
    tb->Count++;
    if (tb->Count > cast(Lumen::UInt32, tb->Capacity) && tb->Capacity <= Lumen::MaxInt / 2)
        Lumen::String::Resize(L, tb->Capacity * 2);  /* too crowded */
    return ts;
}

Lumen::String *Lumen::String::New(Lumen::State *L, const char *str, Lumen::UInteger l) {
    Lumen::GCObject *o;
    unsigned int h = LumenStringHash((uint8_t *) str, l, cast_uint(l << 1));
    for (o = LumenGlobalState(L)->StringMap.HashTable[LumenLogMod(h, LumenGlobalState(L)->StringMap.Capacity)];
         o != nullptr;
         o = o->AsObject.GCNext) {
        Lumen::String *ts = o->ToString();
        if (ts->Length == l && (memcmp(str, ts->CString(), l) == 0)) {
            /* string may be dead */
            if (LumenGlobalState(L)->IsDead(o)) o->ChangeWhite();
            return ts;
        }
    }
    return newStringWithLength(L, str, l, h);  /* not found */
}

Lumen::String *Lumen::String::New(Lumen::State *L, const char *str) {
    Lumen::GCObject *o;
    Lumen::UInteger l = LengthOf(str);
    unsigned int h = LumenStringHash((uint8_t *) str, l, cast_uint(l << 1));
    for (o = LumenGlobalState(L)->StringMap.HashTable[LumenLogMod(h, LumenGlobalState(L)->StringMap.Capacity)];
         o != nullptr;
         o = o->AsObject.GCNext) {
        Lumen::String *ts = o->ToString();
        if (ts->Length == l && (memcmp(str, ts->CString(), l) == 0)) {
            /* string may be dead */
            if (LumenGlobalState(L)->IsDead(o)) o->ChangeWhite();
            return ts;
        }
    }
    return newStringWithLength(L, str, l, h);  /* not found */
}

Lumen::String *Lumen::String::NewRaw(Lumen::State *L, const char *str, Lumen::UInteger l) {
    if (l + 1 > (Lumen::MaxSize - sizeof(Lumen::String)) / sizeof(char))
        Lumen::Memory::TooBig(L);

    Lumen::String *ts = cast(Lumen::String *,
                             LumenMemoryAlloc(L, (l + 1) * sizeof(char) + sizeof(Lumen::String)));

    ts->Length = l;
    ts->Hash = 0;
    ts->Marked = LumenGlobalState(L)->GetWhite();
    ts->Type = Lumen::TypeString;
    ts->Reserved = 0;

    if (str) {
        memcpy(ts + 1, str, l);
    }
    ((char *) (ts + 1))[l] = '\0';

    return ts;
}

Lumen::UInteger Lumen::String::LengthOf(const char *cStr) {
    return std::string_view(cStr).length();
}

Lumen::Userdata *Lumen::Userdata::New(Lumen::State *L, Lumen::UInteger s, Lumen::Table *e) {
    Lumen::Userdata *u;
    if (s > Lumen::MaxSize - sizeof(Lumen::Userdata))
        Lumen::Memory::TooBig(L);
    u = cast(Lumen::Userdata *, LumenMemoryAlloc(L, s + sizeof(Lumen::Userdata)));
    u->Marked = LumenGlobalState(L)->GetWhite();  /* is not finalized */
    u->Type = Lumen::TypeUserdata;
    u->Length = s;
    u->Metatable = nullptr;
    u->Env = e;
    /* chain it on uData list (after main thread) */
    u->GCNext = LumenGlobalState(L)->MainThread->GCNext;
    LumenGlobalState(L)->MainThread->GCNext = LumenObject2GCObject(u);
    return u;
}

