/*!
 * @brief String table (keeps all strings handled by Lua)
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstring>

#define LUA_CORE

#include "lumen/mem.h"
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


Lumen::String *Lumen::String::New(Lumen::State *L, const char *str, size_t l) {
    Lumen::GCObject *o;
    unsigned int h = cast(unsigned int, l);  /* seed */
    size_t step = (l >> 5) + 1;  /* if string is too long, don't hash all its chars */
    size_t l1;
    for (l1 = l; l1 >= step; l1 -= step)  /* compute hash */
        h = h ^ ((h << 5) + (h >> 2) + cast(unsigned char, str[l1 - 1]));
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
    size_t l = strlen(str);
    unsigned int h = cast(unsigned int, l);  /* seed */
    size_t step = (l >> 5) + 1;  /* if string is too long, don't hash all its chars */
    size_t l1;
    for (l1 = l; l1 >= step; l1 -= step)  /* compute hash */
        h = h ^ ((h << 5) + (h >> 2) + cast(unsigned char, str[l1 - 1]));
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

