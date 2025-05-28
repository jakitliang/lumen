/*!
 * @brief Lua tables (hash table)
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @details Implementation of tables (aka arrays, objects, or hash tables).
 * Tables keep its elements in two parts: an array part and a hash part.
 * Non-negative integer keys are all candidates to be kept in the array
 * part. The actual size of the array is the largest `n' such that at
 * least half the slots between 0 and n are in use.
 * Hash uses a mix of chained scatter table with Brent's variation.
 * A main invariant of these tables is that, if an element is not
 * in its main position (i.e. the `original` position that its hash gives
 * to it), then the colliding element is in its own main position.
 * Even when the load factor reaches 100%, performance remains good.
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cmath>
#include <cstring>

#define LUA_CORE

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/gc.h"
#include "lumen/mem.h"
#include "lumen/object.h"
#include "lumen/state.h"
#include "lumen/table.h"


/*
** max size of array part is 2^MAXBITS
*/
#if LUAI_BITSINT > 26
#define LUA_MAX_BITS        26
#else
#define LUA_MAX_BITS		(LUAI_BITSINT-2)
#endif

#define LUA_MAX_A_SIZE    (1 << LUA_MAX_BITS)


#define hashPow2(t, n)      (LumenTableGetNode(t, LumenLogMod((n), LumenTableNodeCount(t))))

#define hashString(t, str)  hashPow2(t, (str)->Hash)
#define hashBoolean(t, p)        hashPow2(t, p)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
*/
#define hashMod(t, n)    (LumenTableGetNode(t, ((n) % ((LumenTableNodeCount(t)-1)|1))))


#define hashPointer(t, p)    hashMod(t, LumenIntPoint(p))

namespace Lumen {
    /**
     * number of ints inside a Lumen::Number
     */
    static inline constexpr int NumInts = sizeof(Lumen::Number) / sizeof(int);

    static const Lumen::Node DummyNode = {
            {LUA_TNIL, {nullptr}},  /* value */
            {{LUA_TNIL, {nullptr}, nullptr}}  /* key */
    };
}

#define dummyNode        (&Lumen::DummyNode)

/*
** hash for lua_Numbers
*/
static Lumen::Node *hashNum(const Lumen::Table *t, Lumen::Number n) {
    unsigned int a[Lumen::NumInts];
    int i;
    if (luai_numeq(n, 0))  /* avoid problems with -0 */
        return LumenTableGetNode(t, 0);
    memcpy(a, &n, sizeof(a));
    for (i = 1; i < Lumen::NumInts; i++) a[0] += a[i];
    return hashMod(t, a[0]);
}


/*
** returns the `main' position of an element in a table (that is, the index
** of its hash value)
*/
static Lumen::Node *mainPosition(const Lumen::Table *t, const Lumen::Value *key) {
    switch (LumenTypeOf(key)) {
        case LUA_TNUMBER:
            return hashNum(t, LumenNumberValue(key));
        case LUA_TSTRING:
            return hashString(t, LumenStringValue(key));
        case LUA_TBOOLEAN:
            return hashBoolean(t, LumenBoolValue(key));
        case LUA_TLIGHTUSERDATA:
            return hashPointer(t, LumenLUDataValue(key));
        default:
            return hashPointer(t, LumenGCValue(key));
    }
}


/*
** returns the index for `key' if `key' is an appropriate key to live in
** the array part of the table, -1 otherwise.
*/
static int arrayIndex(const Lumen::Value *key) {
    if (LumenTypeIsNumber(key)) {
        Lumen::Number n = LumenNumberValue(key);
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
static int findIndex(Lumen::State *L, Lumen::Table *t, Lumen::StkId key) {
    int i;
    if (LumenTypeIsNil(key)) return -1;  /* first iteration */
    i = arrayIndex(key);
    if (0 < i && i <= t->ArrayCount)  /* is `key' inside array part? */
        return i - 1;  /* yes; that's the index (corrected to C) */
    else {
        Lumen::Node *n = mainPosition(t, key);
        do {  /* check whether `key' is somewhere in the chain */
            /* key may be dead already, but it is ok to use it in `next' */
            if (Lumen::RawEqualObject(LumenTableKey2KeyValue(n), key) ||
                (LumenTypeOf(LumenTableGetKey(n)) == LUA_TDEADKEY && LumenIsCollectable(key) &&
                 LumenGCValue(LumenTableGetKey(n)) == LumenGCValue(key))) {
                i = cast_int(n - LumenTableGetNode(t, 0));  /* key index in hash table */
                /* hash elements are numbered after array ones */
                return i + t->ArrayCount;
            } else n = LumenTableGetNext(n);
        } while (n);
        Lumen::Debug::RunError(L, "invalid key to " LUA_QL("next"));  /* key not found */
        return 0;  /* to avoid warnings */
    }
}


int Lumen::Table::Next(Lumen::State *L, Lumen::Table *t, Lumen::StkId key) {
    int i = findIndex(L, t, key);  /* find original element */
    for (i++; i < t->ArrayCount; i++) {  /* try first array part */
        if (!LumenTypeIsNil(&t->Array[i])) {  /* a non-nil value? */
            LumenSetNumberValue(key, cast_num(i + 1));
            LumenSetObject2S(L, key + 1, &t->Array[i]);
            return 1;
        }
    }
    for (i -= t->ArrayCount; i < LumenTableNodeCount(t); i++) {  /* then hash part */
        if (!LumenTypeIsNil(LumenTableGetValue(LumenTableGetNode(t, i)))) {  /* a non-nil value? */
            LumenSetObject2S(L, key, LumenTableKey2KeyValue(LumenTableGetNode(t, i)));
            LumenSetObject2S(L, key + 1, LumenTableGetValue(LumenTableGetNode(t, i)));
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


static int computeSizes(const int nums[], int *nArray) {
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
    LumenAssert(*nArray / 2 <= na && na <= *nArray);
    return na;
}


static int countInt(const Lumen::Value *key, int *nums) {
    int k = arrayIndex(key);
    if (0 < k && k <= LUA_MAX_A_SIZE) {  /* is `key' an appropriate array index? */
        nums[LumenTableCeilLog2(k)]++;  /* count as such */
        return 1;
    } else
        return 0;
}


static int numUseArray(const Lumen::Table *t, int *nums) {
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
            if (!LumenTypeIsNil(&t->Array[i - 1]))
                lc++;
        }
        nums[lg] += lc;
        sumOfNums += lc;
    }
    return sumOfNums;
}


static int numUseHash(const Lumen::Table *t, int *nums, int *pnasize) {
    int totalUse = 0;  /* total number of elements */
    int sumOfNums = 0;  /* summation of `nums' */
    int i = LumenTableNodeCount(t);
    while (i--) {
        Lumen::Node *n = &t->Nodes[i];
        if (!LumenTypeIsNil(LumenTableGetValue(n))) {
            sumOfNums += countInt(LumenTableKey2KeyValue(n), nums);
            totalUse++;
        }
    }
    *pnasize += sumOfNums;
    return totalUse;
}


static void setArrayVector(Lumen::State *L, Lumen::Table *t, int size) {
    int i;
    LumenMemoryReAllocVector(L, t->Array, t->ArrayCount, size, Lumen::Value);
    for (i = t->ArrayCount; i < size; i++)
        LumenSetNilValue(&t->Array[i]);
    t->ArrayCount = size;
}


static void setNodeVector(Lumen::State *L, Lumen::Table *t, int size) {
    int logSize;
    if (size == 0) {  /* no elements to hash part? */
        t->Nodes = cast(Lumen::Node *, dummyNode);  /* use common `dummynode' */
        logSize = 0;
    } else {
        int i;
        logSize = LumenTableCeilLog2(size);
        if (logSize > LUA_MAX_BITS)
            Lumen::Debug::RunError(L, "table overflow");
        size = LumenTableTwoTo(logSize);
        t->Nodes = LumenMemoryNewVector(L, size, Lumen::Node);
        for (i = 0; i < size; i++) {
            Lumen::Node *n = LumenTableGetNode(t, i);
            LumenTableGetNext(n) = nullptr;
            LumenSetNilValue(LumenTableGetKey(n));
            LumenSetNilValue(LumenTableGetValue(n));
        }
    }
    t->NodeCount = cast_byte(logSize);
    t->LastFreeNode = LumenTableGetNode(t, size);  /* all positions are free */
}


static void resize(Lumen::State *L, Lumen::Table *t, int nArraySize, int nHashSize) {
    int i;
    int oldArraySize = t->ArrayCount;
    int oldHashSize = t->NodeCount;
    Lumen::Node *nOld = t->Nodes;  /* save old hash ... */
    if (nArraySize > oldArraySize)  /* array part must grow? */
        setArrayVector(L, t, nArraySize);
    /* create new hash part with appropriate size */
    setNodeVector(L, t, nHashSize);
    if (nArraySize < oldArraySize) {  /* array part must shrink? */
        t->ArrayCount = nArraySize;
        /* re-insert elements from vanishing slice */
        for (i = nArraySize; i < oldArraySize; i++) {
            if (!LumenTypeIsNil(&t->Array[i]))
                LumenSetObjectT2T (L, Lumen::Table::SetNum(L, t, i + 1), &t->Array[i]);
        }
        /* shrink array */
        LumenMemoryReAllocVector(L, t->Array, oldArraySize, nArraySize, Lumen::Value);
    }
    /* re-insert elements from hash part */
    for (i = LumenTableTwoTo(oldHashSize) - 1; i >= 0; i--) {
        Lumen::Node *old = nOld + i;
        if (!LumenTypeIsNil(LumenTableGetValue(old)))
            LumenSetObjectT2T (L, Lumen::Table::Set(L, t, LumenTableKey2KeyValue(old)), LumenTableGetValue(old));
    }
    if (nOld != dummyNode)
        LumenMemoryFreeArray(L, nOld, LumenTableTwoTo(oldHashSize), Lumen::Node);  /* free old array */
}


void Lumen::Table::ResizeArray(Lumen::State *L, Lumen::Table *t, int nArraySize) {
    int nSize = (t->Nodes == dummyNode) ? 0 : LumenTableNodeCount(t);
    resize(L, t, nArraySize, nSize);
}


static void rehash(Lumen::State *L, Lumen::Table *t, const Lumen::Value *ek) {
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


Lumen::Table *Lumen::Table::New(Lumen::State *L, int nArray, int nHash) {
    Lumen::Table *t = LumenMemoryNew(L, Lumen::Table);
    Lumen::GC::Link(L, LumenObject2GCObject(t), LUA_TTABLE);
    t->Metatable = nullptr;
    t->Flags = cast_byte(~0);
    /* temporary values (kept only if some malloc fails) */
    t->Array = nullptr;
    t->ArrayCount = 0;
    t->NodeCount = 0;
    t->Nodes = cast(Lumen::Node *, dummyNode);
    setArrayVector(L, t, nArray);
    setNodeVector(L, t, nHash);
    return t;
}


void Lumen::Table::Free(Lumen::State *L, Lumen::Table *t) {
    if (t->Nodes != dummyNode)
        LumenMemoryFreeArray(L, t->Nodes, LumenTableNodeCount(t), Lumen::Node);
    LumenMemoryFreeArray(L, t->Array, t->ArrayCount, Lumen::Value);
    LumenMemoryFree(L, t);
}


static Lumen::Node *getfreepos(Lumen::Table *t) {
    while (t->LastFreeNode-- > t->Nodes) {
        if (LumenTypeIsNil(LumenTableGetKey(t->LastFreeNode)))
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
static Lumen::Value *newKey(Lumen::State *L, Lumen::Table *t, const Lumen::Value *key) {
    Lumen::Node *mp = mainPosition(t, key);
    if (!LumenTypeIsNil(LumenTableGetValue(mp)) || mp == dummyNode) {
        Lumen::Node *otherN;
        Lumen::Node *n = getfreepos(t);  /* get a free place */
        if (n == nullptr) {  /* cannot find a free place? */
            rehash(L, t, key);  /* grow table */
            return Lumen::Table::Set(L, t, key);  /* re-insert key into grown table */
        }
        LumenAssert(n != dummyNode);
        otherN = mainPosition(t, LumenTableKey2KeyValue(mp));
        if (otherN != mp) {  /* is colliding node out of its main position? */
            /* yes; move colliding node into free position */
            while (LumenTableGetNext(otherN) != mp) otherN = LumenTableGetNext(otherN);  /* find previous */
            LumenTableGetNext(otherN) = n;  /* redo the chain with `n' in place of `mp' */
            *n = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
            LumenTableGetNext(mp) = nullptr;  /* now `mp' is free */
            LumenSetNilValue(LumenTableGetValue(mp));
        } else {  /* colliding node is in its own main position */
            /* new node will go into free position */
            LumenTableGetNext(n) = LumenTableGetNext(mp);  /* chain new position */
            LumenTableGetNext(mp) = n;
            mp = n;
        }
    }
    LumenTableGetKey(mp)->value = key->value;
    LumenTableGetKey(mp)->Type = key->Type;
    LumenGCBarrierTable(L, t, key);
    LumenAssert(LumenTypeIsNil(LumenTableGetValue(mp)));
    return LumenTableGetValue(mp);
}


/*
** search function for integers
*/
const Lumen::Value *Lumen::Table::GetNum(Lumen::Table *t, int key) {
    /* (1 <= key && key <= t->sizeArray) */
    if (cast(unsigned int, key - 1) < cast(unsigned int, t->ArrayCount))
        return &t->Array[key - 1];
    else {
        Lumen::Number nk = cast_num(key);
        Lumen::Node *n = hashNum(t, nk);
        do {  /* check whether `key' is somewhere in the chain */
            if (LumenTypeIsNumber(LumenTableGetKey(n)) && luai_numeq(LumenNumberValue(LumenTableGetKey(n)), nk))
                return LumenTableGetValue(n);  /* that's it */
            else n = LumenTableGetNext(n);
        } while (n);
        return Lumen::NilObject;
    }
}


/*
** search function for strings
*/
const Lumen::Value *Lumen::Table::GetString(Lumen::Table *t, Lumen::String *key) {
    Lumen::Node *n = hashString(t, key);
    do {  /* check whether `key' is somewhere in the chain */
        if (LumenTypeIsString(LumenTableGetKey(n)) && LumenStringValue(LumenTableGetKey(n)) == key)
            return LumenTableGetValue(n);  /* that's it */
        else n = LumenTableGetNext(n);
    } while (n);
    return Lumen::NilObject;
}


/*
** main search function
*/
const Lumen::Value *Lumen::Table::Get(Lumen::Table *t, const Lumen::Value *key) {
    switch (LumenTypeOf(key)) {
        case LUA_TNIL:
            return Lumen::NilObject;
        case LUA_TSTRING:
            return Lumen::Table::GetString(t, LumenStringValue(key));
        case LUA_TNUMBER: {
            int k;
            Lumen::Number n = LumenNumberValue(key);
            lua_number2int(k, n);
            if (luai_numeq(cast_num(k), LumenNumberValue(key))) /* index is int? */
                return Lumen::Table::GetNum(t, k);  /* use specialized version */
            /* else go through */
        }
        default: {
            Lumen::Node *n = mainPosition(t, key);
            do {  /* check whether `key' is somewhere in the chain */
                if (Lumen::RawEqualObject(LumenTableKey2KeyValue(n), key))
                    return LumenTableGetValue(n);  /* that's it */
                else n = LumenTableGetNext(n);
            } while (n);
            return Lumen::NilObject;
        }
    }
}


Lumen::Value *Lumen::Table::Set(Lumen::State *L, Lumen::Table *t, const Lumen::Value *key) {
    const Lumen::Value *p = Lumen::Table::Get(t, key);
    t->Flags = 0;
    if (p != Lumen::NilObject)
        return cast(Lumen::Value *, p);
    else {
        if (LumenTypeIsNil(key)) Lumen::Debug::RunError(L, "table index is nil");
        else if (LumenTypeIsNumber(key) && luai_numisnan(LumenNumberValue(key)))
            Lumen::Debug::RunError(L, "table index is NaN");
        return newKey(L, t, key);
    }
}


Lumen::Value *Lumen::Table::SetNum(Lumen::State *L, Lumen::Table *t, int key) {
    const Lumen::Value *p = Lumen::Table::GetNum(t, key);
    if (p != Lumen::NilObject)
        return cast(Lumen::Value *, p);
    else {
        Lumen::Value k;
        LumenSetNumberValue(&k, cast_num(key));
        return newKey(L, t, &k);
    }
}


Lumen::Value *Lumen::Table::SetString(Lumen::State *L, Lumen::Table *t, Lumen::String *key) {
    const Lumen::Value *p = Lumen::Table::GetString(t, key);
    if (p != Lumen::NilObject)
        return cast(Lumen::Value *, p);
    else {
        Lumen::Value k;
        LumenSetStringValue(L, &k, key);
        return newKey(L, t, &k);
    }
}


static int unbound_search(Lumen::Table *t, unsigned int j) {
    unsigned int i = j;  /* i is zero or a present index */
    j++;
    /* find `i' and `j' such that i is present and j is not */
    while (!LumenTypeIsNil(Lumen::Table::GetNum(t, j))) {
        i = j;
        j *= 2;
        if (j > cast(unsigned int, Lumen::MaxInt)) {  /* overflow? */
            /* table was built with bad purposes: resort to linear search */
            i = 1;
            while (!LumenTypeIsNil(Lumen::Table::GetNum(t, i))) i++;
            return i - 1;
        }
    }
    /* now do a binary search between them */
    while (j - i > 1) {
        unsigned int m = (i + j) / 2;
        if (LumenTypeIsNil(Lumen::Table::GetNum(t, m))) j = m;
        else i = m;
    }
    return i;
}


/*
** Try to find a boundary in table `t`. A `boundary` is an integer index
** such that t[i] is non-nil and t[i+1] is nil (and 0 if t[1] is nil).
*/
int Lumen::Table::GetN(Lumen::Table *t) {
    unsigned int j = t->ArrayCount;
    if (j > 0 && LumenTypeIsNil(&t->Array[j - 1])) {
        /* there is a boundary in the array part: (binary) search for it */
        unsigned int i = 0;
        while (j - i > 1) {
            unsigned int m = (i + j) / 2;
            if (LumenTypeIsNil(&t->Array[m - 1])) j = m;
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

Lumen::Node *Lumen::Table::MainPosition (const Lumen::Table *t, const Lumen::Value *key) {
  return mainPosition(t, key);
}

int Lumen::Table::IsDummy (Lumen::Node *n) { return n == dummyNode; }

#endif
