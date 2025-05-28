/*!
 * @brief Type definitions for Lua objects
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_OBJECT_H
#define LUMEN_OBJECT_H

#include <cstdarg>

#include "lumen/limits.h"

#include "lua.h"


/* tags for values visible from Lua */
#define LUA_LAST_TAG    LUA_TTHREAD

#define LUA_NUM_TAGS    (LUA_LAST_TAG+1)

/* table of globals */
#define LumenGlobalTable(L)    (&L->Global)

/* registry */
#define LumenRegistry(L)    (&LumenGlobal(L)->Registry)

#define LumenGlobal(L)    (L->GlobalState)

/*
** Extra tags for non-values
*/
enum {
    LUA_TPROTO = LUA_LAST_TAG + 1,
    LUA_TUPVAL = LUA_LAST_TAG + 2,
    LUA_TDEADKEY = LUA_LAST_TAG + 3
};

namespace Lumen {
    using Reader = lua_Reader;
    using Writer = lua_Writer;

    struct Object : BasicObject {
        Lumen::GC::Mark Marked;
        Lumen::GCObject *GCNext;
    };

    struct String : Object {
        Lumen::Byte Reserved;
        unsigned int Hash;
        size_t Length;

        static void Resize(Lumen::State *L, int newSize);

        static Lumen::String *New(Lumen::State *L, const char *str, size_t l);

        static Lumen::String *New(Lumen::State *L, const char *str);
    };

    union Key {
        struct Value : Lumen::Value {
            struct Node *Next;  /* for chaining */
        } KeyNext;
        Lumen::Value KeyValue;
    };

    struct Node {
        Lumen::Value Value;
        Lumen::Key Key;
    };

    struct Table : Object {
        Lumen::Byte Flags;  /* 1<<p means taggedMethod(p) is not present */
        Lumen::Byte NodeCount;  /* log2 of size of `node` array */
        Table *Metatable;
        Lumen::Value *Array;  /* array part */
        Lumen::Node *Nodes;
        Lumen::Node *LastFreeNode;  /* any free position is before this position */
        Lumen::GCObject *GCList;
        int ArrayCount;  /* size of `array` array */

        static const Lumen::Value *GetNum(Lumen::Table *t, int key);

        static Lumen::Value *SetNum(Lumen::State *L, Lumen::Table *t, int key);

        static const Lumen::Value *GetString(Lumen::Table *t, Lumen::String *key);

        static Lumen::Value *SetString(Lumen::State *L, Lumen::Table *t, Lumen::String *key);

        static const Lumen::Value *Get(Lumen::Table *t, const Lumen::Value *key);

        static Lumen::Value *Set(Lumen::State *L, Lumen::Table *t, const Lumen::Value *key);

        static Lumen::Table *New(Lumen::State *L, int nArray, int nHash);

        static void ResizeArray(Lumen::State *L, Lumen::Table *t, int nArraySize);

        static void Free(Lumen::State *L, Lumen::Table *t);

        static int Next(Lumen::State *L, Lumen::Table *t, Lumen::StkId key);

        static int GetN(Lumen::Table *t);

#if defined(LUA_DEBUG)
        static Lumen::Node *MainPosition(const Lumen::Table *t, const Lumen::Value *key);

        static int IsDummy(Lumen::Node *n);
#endif
    };

    struct Userdata : Object {
        Table *Metatable;
        Table *Env;
        size_t Length;

        static Lumen::Userdata *New(Lumen::State *L, size_t s, Lumen::Table *e);
    };

    struct LocalVar {
        Lumen::String *VarName;
        int StartPC;  /* first point where variable is active */
        int EndPC;    /* first point where variable is dead */
    };

    struct Proto : Object {
        /**
         * masks for new-style vararg
         */
        typedef Lumen::Byte Vararg;
        enum {
            VarargHasArg = 1,
            VarargIsVararg = 2,
            VarargIsNeedsArg = 4
        };

        Lumen::Value *K;  /* constants used by the function */
        Lumen::Instruction *Code;
        Lumen::Proto **SubProto;  /* functions defined inside the function */
        int *LineInfo;  /* map from opcodes to source lines */
        Lumen::LocalVar *LocalVars;  /* information about local variables */
        Lumen::String **UpValues;  /* upvalue names */
        Lumen::String *Source;
        int UpValuesCount;
        int KCount;  /* size of `K` */
        int CodeCount;
        int LineInfoCount;
        int SubProtoCount;  /* size of `P` */
        int LocalVarsCount;
        int LineDefined;
        int LastLineDefined;
        Lumen::GCObject *GCList;
        Lumen::Byte NUpValues;  /* number of upvalues */
        Lumen::Byte NUmParams;
        Vararg IsVararg;
        Lumen::Byte MaxStackSize;

        static Lumen::Proto *New(Lumen::State *L);

        static void Free(Lumen::State *L, Lumen::Proto *f);

        static const char *GetLocalName(const Lumen::Proto *func, int local_number,
                                        int pc);
    };

    struct UpValue : Object {
        Lumen::Value *SelfValue;  /* points to stack or to its own value */
        union {
            Lumen::Value Value;  /* the value (when closed) */
            struct {  /* double linked list (when open) */
                Lumen::UpValue *Prev;
                Lumen::UpValue *Next;
            };
        };

        static Lumen::UpValue *New(Lumen::State *L);

        static Lumen::UpValue *Find(Lumen::State *L, Lumen::StkId level);

        static void Close(Lumen::State *L, Lumen::StkId level);

        static void Free(Lumen::State *L, Lumen::UpValue *uv);
    };

    struct BasicClosure : Lumen::Object {
        Lumen::Byte IsC;
        Lumen::Byte NUpValues;
        Lumen::GCObject *GCList;
        Lumen::Table *Env;
    };

    union Closure;

    struct CClosure : Lumen::BasicClosure {
        lua_CFunction Func;
        Lumen::Value UpValues[1];

        static Lumen::Closure *New(Lumen::State *L, int nElements, Lumen::Table *e);
    };

    struct LClosure : Lumen::BasicClosure {
        Lumen::Proto *Func;
        Lumen::UpValue *UpValues[1];

        static Lumen::Closure *New(Lumen::State *L, int nElements, Lumen::Table *e);
    };

    union Closure {
        Lumen::CClosure AsC;
        Lumen::LClosure AsLua;

        static void Free(Lumen::State *L, Lumen::Closure *c);
    };

    int Log2(unsigned int x);

    int Int2FB(unsigned int x);

    int FB2Int(int x);

    int RawEqualObject(const Lumen::Value *t1, const Lumen::Value *t2);

    int String2Decimal(const char *s, Lumen::Number *result);

    const char *PushVFString(Lumen::State *L, const char *fmt,
                             va_list argP);

    const char *PushFString(Lumen::State *L, const char *fmt, ...);

    void ChunkId(char *out, const char *source, size_t buffLen);

    LUAI_DATA const Lumen::Value NilValue;

    inline const Lumen::Value *NilObject = &NilValue;
}


/* Macros to test type */
#define LumenTypeIsNil(o)    (LumenTypeOf(o) == LUA_TNIL)
#define LumenTypeIsNumber(o)    (LumenTypeOf(o) == LUA_TNUMBER)
#define LumenTypeIsString(o)    (LumenTypeOf(o) == LUA_TSTRING)
#define LumenTypeIsTable(o)    (LumenTypeOf(o) == LUA_TTABLE)
#define LumenTypeIsFunction(o)    (LumenTypeOf(o) == LUA_TFUNCTION)
#define LumenTypeIsBoolean(o)    (LumenTypeOf(o) == LUA_TBOOLEAN)
#define LumenTypeIsUData(o)    (LumenTypeOf(o) == LUA_TUSERDATA)
#define LumenTypeIsThread(o)    (LumenTypeOf(o) == LUA_TTHREAD)
#define LumenTypeIsLUData(o)    (LumenTypeOf(o) == LUA_TLIGHTUSERDATA)

/* Macros to access values */
#define LumenTypeOf(o)    (o)->Type
#define LumenGCValue(o)    LumenCheckExp(LumenIsCollectable(o), (o)->value.gc)
#define LumenLUDataValue(o)    LumenCheckExp(LumenTypeIsLUData(o), (o)->value.p)
#define LumenNumberValue(o)    LumenCheckExp(LumenTypeIsNumber(o), (o)->value.n)
#define LumenStringValue(o)    LumenCheckExp(LumenTypeIsString(o), &(o)->value.gc->AsString)
#define LumenUDataValue(o)    LumenCheckExp(LumenTypeIsUData(o), &(o)->value.gc->AsUserdata)
#define LumenClosureValue(o)    LumenCheckExp(LumenTypeIsFunction(o), &(o)->value.gc->AsClosure)
#define LumenTableValue(o)    LumenCheckExp(LumenTypeIsTable(o), &(o)->value.gc->AsTable)
#define LumenBoolValue(o)    LumenCheckExp(LumenTypeIsBoolean(o), (o)->value.b)
#define LumenThreadValue(o)    LumenCheckExp(LumenTypeIsThread(o), &(o)->value.gc->AsThread)

#define LumenIsFalse(o)    (LumenTypeIsNil(o) || (LumenTypeIsBoolean(o) && LumenBoolValue(o) == 0))
#define LumenIsCollectable(o)    (LumenTypeOf(o) >= LUA_TSTRING)

/*
** for internal debug only
*/
#define LumenCheckConsistency(obj) \
lua_assert(!LumenIsCollectable(obj) || (LumenTypeOf(obj) == (obj)->value.gc->AsObject.Type))

#define LumenCheckLiveness(g, obj) \
lua_assert(!LumenIsCollectable(obj) || \
    ((LumenTypeOf(obj) == (obj)->value.gc->AsObject.Type) && !LumenGCIsDead(g, (obj)->value.gc)))


/* Macros to set values */
#define LumenSetNilValue(obj) ((obj)->Type=LUA_TNIL)

#define LumenSetNumberValue(obj, x) \
LumenDo( Lumen::Value *i_o=(obj); i_o->value.n=(x); i_o->Type=LUA_TNUMBER; )

#define LumenSetLUDataValue(obj, x) \
LumenDo( Lumen::Value *i_o=(obj); i_o->value.p=(x); i_o->Type=LUA_TLIGHTUSERDATA; )

#define LumenSetBoolValue(obj, x) \
LumenDo( Lumen::Value *i_o=(obj); i_o->value.b=(x); i_o->Type=LUA_TBOOLEAN; )

#define LumenSetStringValue(L, obj, x) \
LumenDo(                               \
    Lumen::Value *i_o=(obj);           \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=LUA_TSTRING; \
    LumenCheckLiveness(LumenGlobal(L), i_o);                              \
)

#define LumenSetUDataValue(L, obj, x) \
LumenDo(                              \
    Lumen::Value *i_o=(obj);          \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=LUA_TUSERDATA; \
    LumenCheckLiveness(LumenGlobal(L), i_o);                                \
)

#define LumenSetThreadValue(L, obj, x) \
LumenDo(                               \
    Lumen::Value *i_o=(obj);           \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=LUA_TTHREAD; \
    LumenCheckLiveness(LumenGlobal(L), i_o);                              \
)

#define LumenSetClosureValue(L, obj, x) \
LumenDo(                                \
    Lumen::Value *i_o=(obj);            \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=LUA_TFUNCTION; \
    LumenCheckLiveness(LumenGlobal(L), i_o);                                \
)

#define LumenSetTableValue(L, obj, x) \
LumenDo(                              \
    Lumen::Value *i_o=(obj);          \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=LUA_TTABLE; \
    LumenCheckLiveness(LumenGlobal(L), i_o);                             \
)

#define LumenSetProtoValue(L, obj, x) \
LumenDo(                              \
    Lumen::Value *i_o=(obj);          \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=LUA_TPROTO; \
    LumenCheckLiveness(LumenGlobal(L),i_o);                             \
)

#define LumenSetObject(L, obj1, obj2) \
LumenDo(                              \
    const Lumen::Value *o2=(obj2);    \
    Lumen::Value *o1=(obj1);          \
    o1->value = o2->value;          \
    o1->Type=o2->Type;              \
    LumenCheckLiveness(LumenGlobal(L),o1); \
)


/*
** different types of sets, according to destination
*/

/* from stack to (same) stack */
#define LumenSetObjectS2S(L, obj1, obj2)    LumenSetObject(L, obj1, obj2)
/* to stack (not from same stack) */
#define LumenSetObject2S(L, obj1, obj2)    LumenSetObject(L, obj1, obj2)
#define LumenSetStringValue2S(L, obj, x)    LumenSetStringValue(L, obj, x)
#define LumenSetTableValue2S(L, obj, x)    LumenSetTableValue(L, obj, x)
#define LumenSetProtoValue2S(L, obj, x)    LumenSetProtoValue(L, obj, x)
/* from table to same table */
#define LumenSetObjectT2T(L, obj1, obj2)    LumenSetObject(L, obj1, obj2)
/* to table */
#define LumenSetObject2T(L, obj1, obj2)    LumenSetObject(L, obj1, obj2)
/* to new object */
#define LumenSetObject2N(L, obj1, obj2)    LumenSetObject(L, obj1, obj2)
#define LumenSetStringValue2N(L, obj, x)    LumenSetStringValue(L, obj, x)

#define LumenSetType(obj, Type) (LumenTypeOf(obj) = (Type))


// String helpers

#define LumenStringCString(ts)    cast(const char *, (ts) + 1)
#define LumenStringValue2CString(o)       LumenStringCString(LumenStringValue(o))

// Closure helpers

#define LumenIsCFunction(o)    (LumenTypeOf(o) == LUA_TFUNCTION && LumenClosureValue(o)->AsC.IsC)
#define LumenIsLFunction(o)    (LumenTypeOf(o) == LUA_TFUNCTION && !LumenClosureValue(o)->AsC.IsC)

#define LumenCClosureSize(n)    (cast(int, sizeof(Lumen::CClosure)) + \
                         cast(int, sizeof(Lumen::Value)*((n)-1)))

#define LumenLClosureSize(n)    (cast(int, sizeof(Lumen::LClosure)) + \
                         cast(int, sizeof(Lumen::Value *)*((n)-1)))

// Other helpers

/**
 * `module` operation for hashing (size is always a power of 2)
 */
#define LumenLogMod(s, size) \
    (LumenCheckExp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))

#define LumenTableTwoTo(x)    (1 << (x))
#define LumenTableNodeCount(t)    (LumenTableTwoTo((t)->NodeCount))

#define LumenTableCeilLog2(x)    (Lumen::Log2((x) - 1) + 1)

#endif

