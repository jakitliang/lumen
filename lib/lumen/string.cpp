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

#define XXH_STATIC_LINKING_ONLY /* access advanced declarations */
#define XXH_IMPLEMENTATION      /* access definitions */
#include "xxhash/xxhash.h"

namespace Lumen::Hash {
    struct State {
        ~State();

        uint32_t Hash(const uint8_t *key, Lumen::UInteger len, uint32_t seed) const;

        static State &Get();

        static uint32_t DoHash(const uint8_t *key, Lumen::UInteger len, uint32_t seed);

        XXH32_state_t *state;
    };
}

Lumen::Hash::State::~State() {
    XXH32_freeState(state);
}

inline uint32_t Lumen::Hash::State::Hash(const uint8_t *key, Lumen::UInteger len, uint32_t seed) const {
    XXH32_reset(state, seed);

    XXH32_update(state, key, len);

    return XXH32_digest(state);
}

inline Lumen::Hash::State &Lumen::Hash::State::Get() {
    thread_local Lumen::Hash::State state{XXH32_createState()};
    return state;
}

inline uint32_t Lumen::Hash::State::DoHash(const uint8_t *key, Lumen::UInteger len, uint32_t seed) {
    return Get().Hash(key, len, seed);
}

void Lumen::String::Intern(Lumen::State *L) {
    if (Hash != 0) return;

    unsigned int h = Lumen::Hash::State::DoHash((uint8_t *) LumenStringCString(this), Length, (uint32_t) Length);
    Hash = h;

    Lumen::StringTable *tb = &LumenGlobal(L)->StringMap;
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


static Lumen::String *newStringWithLength(Lumen::State *L, const char *str, Lumen::UInteger l,
                                          unsigned int h) {
    Lumen::String *ts;
    Lumen::StringTable *tb;
    if (l + 1 > (Lumen::MaxSize - sizeof(Lumen::String)) / sizeof(char))
        Lumen::Memory::TooBig(L);
    ts = cast(Lumen::String *, LumenMemoryAlloc(L, (l + 1) * sizeof(char) + sizeof(Lumen::String)));
    ts->Length = l;
    ts->Hash = h;
    ts->Marked = LumenGCWhite(LumenGlobal(L));
    ts->Type = Lumen::TypeString;
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

Lumen::String *Lumen::String::New(Lumen::State *L, const char *str, Lumen::UInteger l) {
    Lumen::GCObject *o;
    unsigned int h = Lumen::Hash::State::DoHash((uint8_t *) str, l, (uint32_t) l);
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
    Lumen::UInteger l = LengthOf(str);
    unsigned int h = Lumen::Hash::State::DoHash((uint8_t *) str, l, (uint32_t) l);
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

Lumen::String *Lumen::String::NewRaw(Lumen::State *L, const char *str, Lumen::UInteger l) {
    if (l + 1 > (Lumen::MaxSize - sizeof(Lumen::String)) / sizeof(char))
        Lumen::Memory::TooBig(L);

    Lumen::String *ts = cast(Lumen::String *,
                             LumenMemoryAlloc(L, (l + 1) * sizeof(char) + sizeof(Lumen::String)));

    ts->Length = l;
    ts->Hash = 0;
    ts->Marked = LumenGCWhite(LumenGlobal(L));
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
    u->Marked = LumenGCWhite(LumenGlobal(L));  /* is not finalized */
    u->Type = Lumen::TypeUserdata;
    u->Length = s;
    u->Metatable = nullptr;
    u->Env = e;
    /* chain it on uData list (after main thread) */
    u->GCNext = LumenGlobal(L)->MainThread->GCNext;
    LumenGlobal(L)->MainThread->GCNext = LumenObject2GCObject(u);
    return u;
}

