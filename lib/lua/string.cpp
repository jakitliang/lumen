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

#include "lua.h"

#include "lua/mem.h"
#include "lua/object.h"
#include "lua/state.h"
#include "lua/string.h"


void Lua::String::Resize(Lua::State *L, int newSize) {
    Lua::GCObject **newHash;
    Lua::StringTable *tb;
    int i;
    if (LuaGlobal(L)->GCState == Lua::GC::StateSweepString)
        return;  /* cannot resize during GC traverse */
    newHash = LuaMemoryNewVector(L, newSize, Lua::GCObject *);
    tb = &LuaGlobal(L)->StringMap;
    for (i = 0; i < newSize; i++) newHash[i] = nullptr;
    /* rehash */
    for (i = 0; i < tb->Capacity; i++) {
        Lua::GCObject *p = tb->HashTable[i];
        while (p) {  /* for each node in the list */
            Lua::GCObject *next = p->AsObject.GCNext;  /* save next */
            unsigned int h = LuaGCObject2String(p)->Hash;
            int h1 = LuaLogMod(h, newSize);  /* new position */
            lua_assert(cast_int(h % newSize) == LuaLogMod(h, newSize));
            p->AsObject.GCNext = newHash[h1];  /* chain it */
            newHash[h1] = p;
            p = next;
        }
    }
    LuaMemoryFreeArray(L, tb->HashTable, tb->Capacity, Lua::String *);
    tb->Capacity = newSize;
    tb->HashTable = newHash;
}


static Lua::String *newStringWithLength(Lua::State *L, const char *str, size_t l,
                                        unsigned int h) {
    Lua::String *ts;
    Lua::StringTable *tb;
    if (l + 1 > (Lua::MaxSize - sizeof(Lua::String)) / sizeof(char))
        Lua::Memory::TooBig(L);
    ts = cast(Lua::String *, LuaMemoryAlloc(L, (l + 1) * sizeof(char) + sizeof(Lua::String)));
    ts->Length = l;
    ts->Hash = h;
    ts->Marked = LuaGCWhite(LuaGlobal(L));
    ts->Type = LUA_TSTRING;
    ts->Reserved = 0;
    memcpy(ts + 1, str, l * sizeof(char));
    ((char *) (ts + 1))[l] = '\0';  /* ending 0 */
    tb = &LuaGlobal(L)->StringMap;
    h = LuaLogMod(h, tb->Capacity);
    ts->GCNext = tb->HashTable[h];  /* chain new entry */
    tb->HashTable[h] = LuaObject2GCObject(ts);
    tb->Count++;
    if (tb->Count > cast(Lua::UInt32, tb->Capacity) && tb->Capacity <= Lua::MaxInt / 2)
        Lua::String::Resize(L, tb->Capacity * 2);  /* too crowded */
    return ts;
}


Lua::String *Lua::String::New(Lua::State *L, const char *str, size_t l) {
    Lua::GCObject *o;
    unsigned int h = cast(unsigned int, l);  /* seed */
    size_t step = (l >> 5) + 1;  /* if string is too long, don't hash all its chars */
    size_t l1;
    for (l1 = l; l1 >= step; l1 -= step)  /* compute hash */
        h = h ^ ((h << 5) + (h >> 2) + cast(unsigned char, str[l1 - 1]));
    for (o = LuaGlobal(L)->StringMap.HashTable[LuaLogMod(h, LuaGlobal(L)->StringMap.Capacity)];
         o != nullptr;
         o = o->AsObject.GCNext) {
        Lua::String *ts = LuaGCObject2String(o);
        if (ts->Length == l && (memcmp(str, LuaStringCString(ts), l) == 0)) {
            /* string may be dead */
            if (LuaGCIsDead(LuaGlobal(L), o)) LuaGCChangeWhite(o);
            return ts;
        }
    }
    return newStringWithLength(L, str, l, h);  /* not found */
}

Lua::String *Lua::String::New(Lua::State *L, const char *str) {
    Lua::GCObject *o;
    size_t l = strlen(str);
    unsigned int h = cast(unsigned int, l);  /* seed */
    size_t step = (l >> 5) + 1;  /* if string is too long, don't hash all its chars */
    size_t l1;
    for (l1 = l; l1 >= step; l1 -= step)  /* compute hash */
        h = h ^ ((h << 5) + (h >> 2) + cast(unsigned char, str[l1 - 1]));
    for (o = LuaGlobal(L)->StringMap.HashTable[LuaLogMod(h, LuaGlobal(L)->StringMap.Capacity)];
         o != nullptr;
         o = o->AsObject.GCNext) {
        Lua::String *ts = LuaGCObject2String(o);
        if (ts->Length == l && (memcmp(str, LuaStringCString(ts), l) == 0)) {
            /* string may be dead */
            if (LuaGCIsDead(LuaGlobal(L), o)) LuaGCChangeWhite(o);
            return ts;
        }
    }
    return newStringWithLength(L, str, l, h);  /* not found */
}

Lua::Userdata *Lua::Userdata::New(Lua::State *L, size_t s, Lua::Table *e) {
    Lua::Userdata *u;
    if (s > Lua::MaxSize - sizeof(Lua::Userdata))
        Lua::Memory::TooBig(L);
    u = cast(Lua::Userdata *, LuaMemoryAlloc(L, s + sizeof(Lua::Userdata)));
    u->Marked = LuaGCWhite(LuaGlobal(L));  /* is not finalized */
    u->Type = LUA_TUSERDATA;
    u->Length = s;
    u->Metatable = nullptr;
    u->Env = e;
    /* chain it on uData list (after main thread) */
    u->GCNext = LuaGlobal(L)->MainThread->GCNext;
    LuaGlobal(L)->MainThread->GCNext = LuaObject2GCObject(u);
    return u;
}

