/*!
 * @brief Lua tables (hash table)
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest `n' such that at
** least half the slots between 0 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the `original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/

#include <cmath>
#include <cstring>

#define ltable_c
#define LUA_CORE

#include "lua.h"

#include "lua/debug.h"
#include "lua/do.h"
#include "lua/gc.h"
#include "lua/mem.h"
#include "lua/object.h"
#include "lua/state.h"
#include "lua/table.h"


/*
** max size of array part is 2^MAXBITS
*/
#if LUAI_BITSINT > 26
#define LUA_MAX_BITS        26
#else
#define LUA_MAX_BITS		(LUAI_BITSINT-2)
#endif

#define LUA_MAX_A_SIZE    (1 << LUA_MAX_BITS)


#define hashPow2(t, n)      (LuaTableGetNode(t, LuaLogMod((n), LuaTableNodeCount(t))))

#define hashString(t, str)  hashPow2(t, (str)->Hash)
#define hashBoolean(t, p)        hashPow2(t, p)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
*/
#define hashMod(t, n)    (LuaTableGetNode(t, ((n) % ((LuaTableNodeCount(t)-1)|1))))


#define hashPointer(t, p)    hashMod(t, LuaIntPoint(p))

namespace Lua {
    /**
     * number of ints inside a Lua::Number
     */
    static inline constexpr int NumInts = sizeof(Lua::Number) / sizeof(int);

    static const Lua::Node DummyNode = {
            {LUA_TNIL, {nullptr}},  /* value */
            {{LUA_TNIL, {nullptr}, nullptr}}  /* key */
    };
}

#define dummyNode        (&Lua::DummyNode)

/*
** hash for lua_Numbers
*/
static Lua::Node *hashNum(const Lua::Table *t, Lua::Number n) {
    unsigned int a[Lua::NumInts];
    int i;
    if (luai_numeq(n, 0))  /* avoid problems with -0 */
        return LuaTableGetNode(t, 0);
    memcpy(a, &n, sizeof(a));
    for (i = 1; i < Lua::NumInts; i++) a[0] += a[i];
    return hashMod(t, a[0]);
}


/*
** returns the `main' position of an element in a table (that is, the index
** of its hash value)
*/
static Lua::Node *mainPosition(const Lua::Table *t, const Lua::Value *key) {
    switch (LuaTypeOf(key)) {
        case LUA_TNUMBER:
            return hashNum(t, LuaNumberValue(key));
        case LUA_TSTRING:
            return hashString(t, LuaStringValue(key));
        case LUA_TBOOLEAN:
            return hashBoolean(t, LuaBoolValue(key));
        case LUA_TLIGHTUSERDATA:
            return hashPointer(t, LuaLUDataValue(key));
        default:
            return hashPointer(t, LuaGCValue(key));
    }
}


/*
** returns the index for `key' if `key' is an appropriate key to live in
** the array part of the table, -1 otherwise.
*/
static int arrayIndex(const Lua::Value *key) {
    if (LuaTypeIsNumber(key)) {
        Lua::Number n = LuaNumberValue(key);
        int k;
        lua_number2int(k, n);
        if (luai_numeq(cast_num(k), n))
            return k;
    }
    return -1;  /* `key' did not match some condition */
}


/*
** returns the index of a `key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signalled by -1.
*/
static int findIndex(Lua::State *L, Lua::Table *t, Lua::StkId key) {
    int i;
    if (LuaTypeIsNil(key)) return -1;  /* first iteration */
    i = arrayIndex(key);
    if (0 < i && i <= t->ArrayCount)  /* is `key' inside array part? */
        return i - 1;  /* yes; that's the index (corrected to C) */
    else {
        Lua::Node *n = mainPosition(t, key);
        do {  /* check whether `key' is somewhere in the chain */
            /* key may be dead already, but it is ok to use it in `next' */
            if (Lua::RawEqualObject(LuaTableKey2KeyValue(n), key) ||
                (LuaTypeOf(LuaTableGetKey(n)) == LUA_TDEADKEY && LuaIsCollectable(key) &&
                 LuaGCValue(LuaTableGetKey(n)) == LuaGCValue(key))) {
                i = cast_int(n - LuaTableGetNode(t, 0));  /* key index in hash table */
                /* hash elements are numbered after array ones */
                return i + t->ArrayCount;
            } else n = LuaTableGetNext(n);
        } while (n);
        Lua::Debug::RunError(L, "invalid key to " LUA_QL("next"));  /* key not found */
        return 0;  /* to avoid warnings */
    }
}


int Lua::Table::Next(Lua::State *L, Lua::Table *t, Lua::StkId key) {
    int i = findIndex(L, t, key);  /* find original element */
    for (i++; i < t->ArrayCount; i++) {  /* try first array part */
        if (!LuaTypeIsNil(&t->Array[i])) {  /* a non-nil value? */
            LuaSetNumberValue(key, cast_num(i + 1));
            LuaSetObject2S(L, key + 1, &t->Array[i]);
            return 1;
        }
    }
    for (i -= t->ArrayCount; i < LuaTableNodeCount(t); i++) {  /* then hash part */
        if (!LuaTypeIsNil(LuaTableGetValue(LuaTableGetNode(t, i)))) {  /* a non-nil value? */
            LuaSetObject2S(L, key, LuaTableKey2KeyValue(LuaTableGetNode(t, i)));
            LuaSetObject2S(L, key + 1, LuaTableGetValue(LuaTableGetNode(t, i)));
            return 1;
        }
    }
    return 0;  /* no more elements */
}


/*
** {=============================================================
** Rehash
** ==============================================================
*/


static int computeSizes(int nums[], int *nArray) {
    int i;
    int twoToInt;  /* 2^i */
    int a = 0;  /* number of elements smaller than 2^i */
    int na = 0;  /* number of elements to go to array part */
    int n = 0;  /* optimal size for array part */
    for (i = 0, twoToInt = 1; twoToInt / 2 < *nArray; i++, twoToInt *= 2) {
        if (nums[i] > 0) {
            a += nums[i];
            if (a > twoToInt / 2) {  /* more than half elements present? */
                n = twoToInt;  /* optimal size (till now) */
                na = a;  /* all elements smaller than n will go to array part */
            }
        }
        if (a == *nArray) break;  /* all elements already counted */
    }
    *nArray = n;
    lua_assert(*nArray / 2 <= na && na <= *nArray);
    return na;
}


static int countInt(const Lua::Value *key, int *nums) {
    int k = arrayIndex(key);
    if (0 < k && k <= LUA_MAX_A_SIZE) {  /* is `key' an appropriate array index? */
        nums[LuaTableCeilLog2(k)]++;  /* count as such */
        return 1;
    } else
        return 0;
}


static int numUseArray(const Lua::Table *t, int *nums) {
    int lg;
    int twoToLog;  /* 2^lg */
    int sumOfNums = 0;  /* summation of `nums' */
    int i = 1;  /* count to traverse all array keys */
    for (lg = 0, twoToLog = 1; lg <= LUA_MAX_BITS; lg++, twoToLog *= 2) {  /* for each slice */
        int lc = 0;  /* counter */
        int lim = twoToLog;
        if (lim > t->ArrayCount) {
            lim = t->ArrayCount;  /* adjust upper limit */
            if (i > lim)
                break;  /* no more elements to count */
        }
        /* count elements in range (2^(lg-1), 2^lg] */
        for (; i <= lim; i++) {
            if (!LuaTypeIsNil(&t->Array[i - 1]))
                lc++;
        }
        nums[lg] += lc;
        sumOfNums += lc;
    }
    return sumOfNums;
}


static int numUseHash(const Lua::Table *t, int *nums, int *pnasize) {
    int totalUse = 0;  /* total number of elements */
    int sumOfNums = 0;  /* summation of `nums' */
    int i = LuaTableNodeCount(t);
    while (i--) {
        Lua::Node *n = &t->Nodes[i];
        if (!LuaTypeIsNil(LuaTableGetValue(n))) {
            sumOfNums += countInt(LuaTableKey2KeyValue(n), nums);
            totalUse++;
        }
    }
    *pnasize += sumOfNums;
    return totalUse;
}


static void setArrayVector(Lua::State *L, Lua::Table *t, int size) {
    int i;
    LuaMemoryReAllocVector(L, t->Array, t->ArrayCount, size, Lua::Value);
    for (i = t->ArrayCount; i < size; i++)
        LuaSetNilValue(&t->Array[i]);
    t->ArrayCount = size;
}


static void setNodeVector(Lua::State *L, Lua::Table *t, int size) {
    int logSize;
    if (size == 0) {  /* no elements to hash part? */
        t->Nodes = cast(Lua::Node *, dummyNode);  /* use common `dummynode' */
        logSize = 0;
    } else {
        int i;
        logSize = LuaTableCeilLog2(size);
        if (logSize > LUA_MAX_BITS)
            Lua::Debug::RunError(L, "table overflow");
        size = LuaTableTwoTo(logSize);
        t->Nodes = LuaMemoryNewVector(L, size, Lua::Node);
        for (i = 0; i < size; i++) {
            Lua::Node *n = LuaTableGetNode(t, i);
            LuaTableGetNext(n) = nullptr;
            LuaSetNilValue(LuaTableGetKey(n));
            LuaSetNilValue(LuaTableGetValue(n));
        }
    }
    t->NodeCount = cast_byte(logSize);
    t->LastFreeNode = LuaTableGetNode(t, size);  /* all positions are free */
}


static void resize(Lua::State *L, Lua::Table *t, int nArraySize, int nHashSize) {
    int i;
    int oldArraySize = t->ArrayCount;
    int oldHashSize = t->NodeCount;
    Lua::Node *nOld = t->Nodes;  /* save old hash ... */
    if (nArraySize > oldArraySize)  /* array part must grow? */
        setArrayVector(L, t, nArraySize);
    /* create new hash part with appropriate size */
    setNodeVector(L, t, nHashSize);
    if (nArraySize < oldArraySize) {  /* array part must shrink? */
        t->ArrayCount = nArraySize;
        /* re-insert elements from vanishing slice */
        for (i = nArraySize; i < oldArraySize; i++) {
            if (!LuaTypeIsNil(&t->Array[i]))
                    LuaSetObjectT2T (L, Lua::Table::SetNum(L, t, i + 1), &t->Array[i]);
        }
        /* shrink array */
        LuaMemoryReAllocVector(L, t->Array, oldArraySize, nArraySize, Lua::Value);
    }
    /* re-insert elements from hash part */
    for (i = LuaTableTwoTo(oldHashSize) - 1; i >= 0; i--) {
        Lua::Node *old = nOld + i;
        if (!LuaTypeIsNil(LuaTableGetValue(old)))
                LuaSetObjectT2T (L, Lua::Table::Set(L, t, LuaTableKey2KeyValue(old)), LuaTableGetValue(old));
    }
    if (nOld != dummyNode)
        LuaMemoryFreeArray(L, nOld, LuaTableTwoTo(oldHashSize), Lua::Node);  /* free old array */
}


void Lua::Table::ResizeArray(Lua::State *L, Lua::Table *t, int nArraySize) {
    int nSize = (t->Nodes == dummyNode) ? 0 : LuaTableNodeCount(t);
    resize(L, t, nArraySize, nSize);
}


static void rehash(Lua::State *L, Lua::Table *t, const Lua::Value *ek) {
    int nArraySize, na;
    int nums[LUA_MAX_BITS + 1];  /* nums[i] = number of keys between 2^(i-1) and 2^i */
    int i;
    int totalUse;
    for (i = 0; i <= LUA_MAX_BITS; i++) nums[i] = 0;  /* reset counts */
    nArraySize = numUseArray(t, nums);  /* count keys in array part */
    totalUse = nArraySize;  /* all those keys are integer keys */
    totalUse += numUseHash(t, nums, &nArraySize);  /* count keys in hash part */
    /* count extra key */
    nArraySize += countInt(ek, nums);
    totalUse++;
    /* compute new size for array part */
    na = computeSizes(nums, &nArraySize);
    /* resize the table to new computed sizes */
    resize(L, t, nArraySize, totalUse - na);
}


/*
** }=============================================================
*/


Lua::Table *Lua::Table::New(Lua::State *L, int nArray, int nHash) {
    Lua::Table *t = LuaMemoryNew(L, Lua::Table);
    Lua::GC::Link(L, LuaObject2GCObject(t), LUA_TTABLE);
    t->Metatable = nullptr;
    t->Flags = cast_byte(~0);
    /* temporary values (kept only if some malloc fails) */
    t->Array = nullptr;
    t->ArrayCount = 0;
    t->NodeCount = 0;
    t->Nodes = cast(Lua::Node *, dummyNode);
    setArrayVector(L, t, nArray);
    setNodeVector(L, t, nHash);
    return t;
}


void Lua::Table::Free(Lua::State *L, Lua::Table *t) {
    if (t->Nodes != dummyNode)
        LuaMemoryFreeArray(L, t->Nodes, LuaTableNodeCount(t), Lua::Node);
    LuaMemoryFreeArray(L, t->Array, t->ArrayCount, Lua::Value);
    LuaMemoryFree(L, t);
}


static Lua::Node *getfreepos(Lua::Table *t) {
    while (t->LastFreeNode-- > t->Nodes) {
        if (LuaTypeIsNil(LuaTableGetKey(t->LastFreeNode)))
            return t->LastFreeNode;
    }
    return nullptr;  /* could not find a free place */
}


/*
** inserts a new key into a hash table; first, check whether key's main 
** position is free. If not, check whether colliding node is in its main 
** position or not: if it is not, move colliding node to an empty place and 
** put new key in its main position; otherwise (colliding node is in its main 
** position), new key goes to an empty position. 
*/
static Lua::Value *newKey(Lua::State *L, Lua::Table *t, const Lua::Value *key) {
    Lua::Node *mp = mainPosition(t, key);
    if (!LuaTypeIsNil(LuaTableGetValue(mp)) || mp == dummyNode) {
        Lua::Node *otherN;
        Lua::Node *n = getfreepos(t);  /* get a free place */
        if (n == nullptr) {  /* cannot find a free place? */
            rehash(L, t, key);  /* grow table */
            return Lua::Table::Set(L, t, key);  /* re-insert key into grown table */
        }
        lua_assert(n != dummyNode);
        otherN = mainPosition(t, LuaTableKey2KeyValue(mp));
        if (otherN != mp) {  /* is colliding node out of its main position? */
            /* yes; move colliding node into free position */
            while (LuaTableGetNext(otherN) != mp) otherN = LuaTableGetNext(otherN);  /* find previous */
            LuaTableGetNext(otherN) = n;  /* redo the chain with `n' in place of `mp' */
            *n = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
            LuaTableGetNext(mp) = nullptr;  /* now `mp' is free */
            LuaSetNilValue(LuaTableGetValue(mp));
        } else {  /* colliding node is in its own main position */
            /* new node will go into free position */
            LuaTableGetNext(n) = LuaTableGetNext(mp);  /* chain new position */
            LuaTableGetNext(mp) = n;
            mp = n;
        }
    }
    LuaTableGetKey(mp)->value = key->value;
    LuaTableGetKey(mp)->Type = key->Type;
    LuaGCBarrierTable(L, t, key);
    lua_assert(LuaTypeIsNil(LuaTableGetValue(mp)));
    return LuaTableGetValue(mp);
}


/*
** search function for integers
*/
const Lua::Value *Lua::Table::GetNum(Lua::Table *t, int key) {
    /* (1 <= key && key <= t->sizeArray) */
    if (cast(unsigned int, key - 1) < cast(unsigned int, t->ArrayCount))
        return &t->Array[key - 1];
    else {
        Lua::Number nk = cast_num(key);
        Lua::Node *n = hashNum(t, nk);
        do {  /* check whether `key' is somewhere in the chain */
            if (LuaTypeIsNumber(LuaTableGetKey(n)) && luai_numeq(LuaNumberValue(LuaTableGetKey(n)), nk))
                return LuaTableGetValue(n);  /* that's it */
            else n = LuaTableGetNext(n);
        } while (n);
        return Lua::NilObject;
    }
}


/*
** search function for strings
*/
const Lua::Value *Lua::Table::GetString(Lua::Table *t, Lua::String *key) {
    Lua::Node *n = hashString(t, key);
    do {  /* check whether `key' is somewhere in the chain */
        if (LuaTypeIsString(LuaTableGetKey(n)) && LuaStringValue(LuaTableGetKey(n)) == key)
            return LuaTableGetValue(n);  /* that's it */
        else n = LuaTableGetNext(n);
    } while (n);
    return Lua::NilObject;
}


/*
** main search function
*/
const Lua::Value *Lua::Table::Get(Lua::Table *t, const Lua::Value *key) {
    switch (LuaTypeOf(key)) {
        case LUA_TNIL:
            return Lua::NilObject;
        case LUA_TSTRING:
            return Lua::Table::GetString(t, LuaStringValue(key));
        case LUA_TNUMBER: {
            int k;
            Lua::Number n = LuaNumberValue(key);
            lua_number2int(k, n);
            if (luai_numeq(cast_num(k), LuaNumberValue(key))) /* index is int? */
                return Lua::Table::GetNum(t, k);  /* use specialized version */
            /* else go through */
        }
        default: {
            Lua::Node *n = mainPosition(t, key);
            do {  /* check whether `key' is somewhere in the chain */
                if (Lua::RawEqualObject(LuaTableKey2KeyValue(n), key))
                    return LuaTableGetValue(n);  /* that's it */
                else n = LuaTableGetNext(n);
            } while (n);
            return Lua::NilObject;
        }
    }
}


Lua::Value *Lua::Table::Set(Lua::State *L, Lua::Table *t, const Lua::Value *key) {
    const Lua::Value *p = Lua::Table::Get(t, key);
    t->Flags = 0;
    if (p != Lua::NilObject)
        return cast(Lua::Value *, p);
    else {
        if (LuaTypeIsNil(key)) Lua::Debug::RunError(L, "table index is nil");
        else if (LuaTypeIsNumber(key) && luai_numisnan(LuaNumberValue(key)))
            Lua::Debug::RunError(L, "table index is NaN");
        return newKey(L, t, key);
    }
}


Lua::Value *Lua::Table::SetNum(Lua::State *L, Lua::Table *t, int key) {
    const Lua::Value *p = Lua::Table::GetNum(t, key);
    if (p != Lua::NilObject)
        return cast(Lua::Value *, p);
    else {
        Lua::Value k;
        LuaSetNumberValue(&k, cast_num(key));
        return newKey(L, t, &k);
    }
}


Lua::Value *Lua::Table::SetString(Lua::State *L, Lua::Table *t, Lua::String *key) {
    const Lua::Value *p = Lua::Table::GetString(t, key);
    if (p != Lua::NilObject)
        return cast(Lua::Value *, p);
    else {
        Lua::Value k;
        LuaSetStringValue(L, &k, key);
        return newKey(L, t, &k);
    }
}


static int unbound_search(Lua::Table *t, unsigned int j) {
    unsigned int i = j;  /* i is zero or a present index */
    j++;
    /* find `i' and `j' such that i is present and j is not */
    while (!LuaTypeIsNil(Lua::Table::GetNum(t, j))) {
        i = j;
        j *= 2;
        if (j > cast(unsigned int, Lua::MaxInt)) {  /* overflow? */
            /* table was built with bad purposes: resort to linear search */
            i = 1;
            while (!LuaTypeIsNil(Lua::Table::GetNum(t, i))) i++;
            return i - 1;
        }
    }
    /* now do a binary search between them */
    while (j - i > 1) {
        unsigned int m = (i + j) / 2;
        if (LuaTypeIsNil(Lua::Table::GetNum(t, m))) j = m;
        else i = m;
    }
    return i;
}


/*
** Try to find a boundary in table `t'. A `boundary' is an integer index
** such that t[i] is non-nil and t[i+1] is nil (and 0 if t[1] is nil).
*/
int Lua::Table::GetN(Lua::Table *t) {
    unsigned int j = t->ArrayCount;
    if (j > 0 && LuaTypeIsNil(&t->Array[j - 1])) {
        /* there is a boundary in the array part: (binary) search for it */
        unsigned int i = 0;
        while (j - i > 1) {
            unsigned int m = (i + j) / 2;
            if (LuaTypeIsNil(&t->Array[m - 1])) j = m;
            else i = m;
        }
        return i;
    }
        /* else must find a boundary in hash part */
    else if (t->Nodes == dummyNode)  /* hash part is empty? */
        return j;  /* that is easy... */
    else return unbound_search(t, j);
}


#if defined(LUA_DEBUG)

Lua::Node *Lua::Table::MainPosition (const Lua::Table *t, const Lua::Value *key) {
  return mainPosition(t, key);
}

int Lua::Table::IsDummy (Lua::Node *n) { return n == dummyNode; }

#endif
