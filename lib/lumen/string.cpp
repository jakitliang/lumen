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

#define LUA_CORE

#include "lumen/memory.h"
#include "lumen/object.h"
#include "lumen/state.h"
#include "lumen/string.h"


void Lumen::String::Resize(Lumen::State *L, int newSize) {
    Lumen::GCObject **newHash;
    Lumen::StringTable *tb;
    int i;
    if (LumenGlobal(L)->GCState == Lumen::GC::StateSweepString)
        return;  /* cannot resize during GC traverse */
    newHash = LumenMemoryNewVector(L, newSize, Lumen::GCObject *);
    tb = &LumenGlobal(L)->StringMap;
    for (i = 0; i < newSize; i++) newHash[i] = nullptr;
    /* rehash */
    for (i = 0; i < tb->Capacity; i++) {
        Lumen::GCObject *p = tb->HashTable[i];
        while (p) {  /* for each node in the list */
            Lumen::GCObject *next = p->AsObject.GCNext;  /* save next */
            unsigned int h = LumenGCObject2String(p)->Hash;
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


static Lumen::String *newStringWithLength(Lumen::State *L, const char *str, size_t l,
                                          unsigned int h) {
    Lumen::String *ts;
    Lumen::StringTable *tb;
    if (l + 1 > (Lumen::MaxSize - sizeof(Lumen::String)) / sizeof(char))
        Lumen::Memory::TooBig(L);
    ts = cast(Lumen::String *, LumenMemoryAlloc(L, (l + 1) * sizeof(char) + sizeof(Lumen::String)));
    ts->Length = l;
    ts->Hash = h;
    ts->Marked = LumenGCWhite(LumenGlobal(L));
    ts->Type = LUA_TSTRING;
    ts->Reserved = 0;
    memcpy(ts + 1, str, l * sizeof(char));
    ((char *) (ts + 1))[l] = '\0';  /* ending 0 */
    tb = &LumenGlobal(L)->StringMap;
    h = LumenLogMod(h, tb->Capacity);
    ts->GCNext = tb->HashTable[h];  /* chain new entry */
    tb->HashTable[h] = LumenObject2GCObject(ts);
    tb->Count++;
    if (tb->Count > cast(Lumen::UInt32, tb->Capacity) && tb->Capacity <= Lumen::MaxInt / 2)
        Lumen::String::Resize(L, tb->Capacity * 2);  /* too crowded */
    return ts;
}

static inline uint32_t murmur32(const uint8_t *key, size_t len, uint32_t seed) {
    static const uint32_t c1 = 0xcc9e2d51;
    static const uint32_t c2 = 0x1b873593;
    static const uint32_t r1 = 15;
    static const uint32_t r2 = 13;
    static const uint32_t m = 5;
    static const uint32_t n = 0xe6546b64;
    uint32_t hash = seed;

    const size_t nBlocks = len / 4;
    auto blocks = (const uint32_t *) key;
    for (size_t i = 0; i < nBlocks; i++) {
        uint32_t k = blocks[i];
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;

        hash ^= k;
        hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;
    }

    auto tail = (const uint8_t *) (key + nBlocks * 4);
    uint32_t k1 = 0;
    switch (len & 3) {
        case 3:
            k1 ^= tail[2] << 16;
        case 2:
            k1 ^= tail[1] << 8;
        case 1:
            k1 ^= tail[0];
            k1 *= c1;
            k1 = (k1 << r1) | (k1 >> (32 - r1));
            k1 *= c2;
            hash ^= k1;
    }

    hash ^= len;
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);

    return hash;
}

Lumen::String *Lumen::String::New(Lumen::State *L, const char *str, size_t l) {
    Lumen::GCObject *o;
    unsigned int h = murmur32((uint8_t *) str, l, (uint32_t) l);
    for (o = LumenGlobal(L)->StringMap.HashTable[LumenLogMod(h, LumenGlobal(L)->StringMap.Capacity)];
         o != nullptr;
         o = o->AsObject.GCNext) {
        Lumen::String *ts = LumenGCObject2String(o);
        if (ts->Length == l && (memcmp(str, LumenStringCString(ts), l) == 0)) {
            /* string may be dead */
            if (LumenGCIsDead(LumenGlobal(L), o)) LumenGCChangeWhite(o);
            return ts;
        }
    }
    return newStringWithLength(L, str, l, h);  /* not found */
}

Lumen::String *Lumen::String::New(Lumen::State *L, const char *str) {
    Lumen::GCObject *o;
    size_t l = LengthOf(str);
    unsigned int h = murmur32((uint8_t *) str, l, (uint32_t) l);
    for (o = LumenGlobal(L)->StringMap.HashTable[LumenLogMod(h, LumenGlobal(L)->StringMap.Capacity)];
         o != nullptr;
         o = o->AsObject.GCNext) {
        Lumen::String *ts = LumenGCObject2String(o);
        if (ts->Length == l && (memcmp(str, LumenStringCString(ts), l) == 0)) {
            /* string may be dead */
            if (LumenGCIsDead(LumenGlobal(L), o)) LumenGCChangeWhite(o);
            return ts;
        }
    }
    return newStringWithLength(L, str, l, h);  /* not found */
}

size_t Lumen::String::LengthOf(const char *cStr) {
    return std::string_view(cStr).length();
}

Lumen::Userdata *Lumen::Userdata::New(Lumen::State *L, size_t s, Lumen::Table *e) {
    Lumen::Userdata *u;
    if (s > Lumen::MaxSize - sizeof(Lumen::Userdata))
        Lumen::Memory::TooBig(L);
    u = cast(Lumen::Userdata *, LumenMemoryAlloc(L, s + sizeof(Lumen::Userdata)));
    u->Marked = LumenGCWhite(LumenGlobal(L));  /* is not finalized */
    u->Type = LUA_TUSERDATA;
    u->Length = s;
    u->Metatable = nullptr;
    u->Env = e;
    /* chain it on uData list (after main thread) */
    u->GCNext = LumenGlobal(L)->MainThread->GCNext;
    LumenGlobal(L)->MainThread->GCNext = LumenObject2GCObject(u);
    return u;
}

