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
#include <cstring>
#include <cctype>
#include <cstdlib>

#include "lumen/limits.h"

/* table of globals */
#define LumenGlobalTable(L)    (&L->Global)

/* registry */
#define LumenRegistryTable(L)    (&LumenGlobalState(L)->Registry)

#define LumenGlobalState(L)    (L->GlobalState)

namespace Lumen {
    struct BasicObject : TypeInfo {
        /**
         * Layout for bit use in `marked' field:\n
         * bit 0 - object is white (type 0)\n
         * bit 1 - object is white (type 1)\n
         * bit 2 - object is black\n
         * bit 3 - for userdata: has been finalized\n
         * bit 3 - for tables: has weak keys\n
         * bit 4 - for tables: has weak values\n
         * bit 5 - object is fixed (should not be collected)\n
         * bit 6 - object is "super" fixed (only the main thread)
         * grep "ORDER Mark"
         */
        Lumen::Byte Marked;
        Lumen::GCObject *GCNext;
    };

    struct String : BasicObject {
        Lumen::Byte Reserved;
        unsigned int Hash;
        Lumen::UInteger Length;

        void Intern(Lumen::State *L);

        static void Resize(Lumen::State *L, int newSize);

        static Lumen::String *New(Lumen::State *L, const char *str, Lumen::UInteger l);

        static Lumen::String *New(Lumen::State *L, const char *str);

        static Lumen::String *NewRaw(Lumen::State *L, const char *str, Lumen::UInteger l);

        static Lumen::UInteger LengthOf(const char *cStr);
    };

    union Key {
        struct Value : Lumen::Object {
            struct Node *Next;  /* for chaining */
        } KeyNext;
        Lumen::Object KeyValue;
    };

    struct Node {
        Lumen::Object Value;
        Lumen::Key Key;
    };

    struct Table : BasicObject {
        Lumen::Byte Flags;  /* 1<<p means taggedMethod(p) is not present */
        Lumen::Byte NodeCount;  /* log2 of size of `node` array */
        Table *Metatable;
        Lumen::Object *Array;  /* array part */
        Lumen::Node *Nodes;
        Lumen::Node *LastFreeNode;  /* any free position is before this position */
        Lumen::GCObject *GCList;
        int ArrayCount;  /* size of `array` array */

        static const Lumen::Object *GetNum(Lumen::Table *t, int key);

        static Lumen::Object *SetNum(Lumen::State *L, Lumen::Table *t, int key);

        static const Lumen::Object *GetString(Lumen::Table *t, Lumen::String *key);

        static Lumen::Object *SetString(Lumen::State *L, Lumen::Table *t, Lumen::String *key);

        static const Lumen::Object *Get(Lumen::Table *t, const Lumen::Object *key);

        static Lumen::Object *Set(Lumen::State *L, Lumen::Table *t, const Lumen::Object *key);

        static Lumen::Table *New(Lumen::State *L, int nArray, int nHash);

        static void ResizeArray(Lumen::State *L, Lumen::Table *t, int nArraySize);

        static void Free(Lumen::State *L, Lumen::Table *t);

        static int Next(Lumen::State *L, Lumen::Table *t, Lumen::Value key);

        static int GetN(Lumen::Table *t);

#if defined(LUA_DEBUG)
        static Lumen::Node *MainPosition(const Lumen::Table *t, const Lumen::Object *key);

        static int IsDummy(Lumen::Node *n);
#endif
    };

    struct Userdata : BasicObject {
        Table *Metatable;
        Table *Env;
        Lumen::UInteger Length;

        static Lumen::Userdata *New(Lumen::State *L, Lumen::UInteger s, Lumen::Table *e);
    };

    struct LocalVar {
        Lumen::String *VarName;
        int StartPC;  /* first point where variable is active */
        int EndPC;    /* first point where variable is dead */
    };

    struct Proto : BasicObject {
        /**
         * masks for new-style vararg
         */
        typedef Lumen::Byte Vararg;
        enum {
            VarargHasArg = 1,
            VarargIsVararg = 2,
            VarargIsNeedsArg = 4
        };

        Lumen::Object *K;  /* constants used by the function */
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

    struct UpValue : BasicObject {
        Lumen::Object *SelfValue;  /* points to stack or to its own value */
        union {
            Lumen::Object Value;  /* the value (when closed) */
            struct {  /* double linked list (when open) */
                Lumen::UpValue *Prev;
                Lumen::UpValue *Next;
            };
        };

        static Lumen::UpValue *New(Lumen::State *L);

        static Lumen::UpValue *Find(Lumen::State *L, Lumen::Value level);

        static void Close(Lumen::State *L, Lumen::Value level);

        static void Free(Lumen::State *L, Lumen::UpValue *uv);
    };

    struct BasicClosure : BasicObject {
        typedef LUA_ENUM(Lumen::Byte, Kind) {
            KindLua = 0,
            KindC = 1
        };

        BasicClosure::Kind IsC;
        Lumen::Byte NUpValues;
        Lumen::GCObject *GCList;
        Lumen::Table *Env;
    };

    struct Closure;

    struct CClosure : Lumen::BasicClosure {
        Lumen::Delegate Func;
        Lumen::Object UpValues[1];

        static Lumen::Closure *New(Lumen::State *L, int nElements, Lumen::Table *e);
    };

    struct LClosure : Lumen::BasicClosure {
        Lumen::Proto *Func;
        Lumen::UpValue *UpValues[1];

        static Lumen::Closure *New(Lumen::State *L, int nElements, Lumen::Table *e);
    };

    struct Closure {
        union {
            Lumen::CClosure AsC;
            Lumen::LClosure AsLua;
        };

        static void Free(Lumen::State *L, Lumen::Closure *c);
    };

    int Log2(unsigned int x);

    /**
     * converts an integer to a "floating point byte", represented as
     * (eeeeexxx), where the real value is (1xxx) * 2^(eeeee - 1) if
     * eeeee != 0 and (xxx) otherwise.
     */
    int Int2FB(unsigned int x);

    int FB2Int(int x);

    int RawEqualObject(const Lumen::Object *t1, const Lumen::Object *t2);

    Lumen::Number Arith(Lumen::ArithOp op, Lumen::Number v1, Lumen::Number v2);

    int String2Decimal(const char *s, Lumen::Number *result);

    const char *PushVFString(Lumen::State *L, const char *fmt,
                             va_list argP);

    const char *PushFString(Lumen::State *L, const char *fmt, ...);

    void ChunkId(char *out, const char *source, Lumen::UInteger buffLen);

    LUAI_DATA const Lumen::Object NilValue;

    inline const Lumen::Object *NilObject = &NilValue;
}


/* Macros to test type */
#define LumenTypeOf(o)    (o)->Type
#define LumenTypeIsNil(o)    (LumenTypeOf(o) == Lumen::TypeNil)
#define LumenTypeIsNumber(o)    (LumenTypeOf(o) == Lumen::TypeNumber)
#define LumenTypeIsString(o)    (LumenTypeOf(o) == Lumen::TypeString)
#define LumenTypeIsTable(o)    (LumenTypeOf(o) == Lumen::TypeTable)
#define LumenTypeIsFunction(o)    (LumenTypeOf(o) == Lumen::TypeFunction)
#define LumenTypeIsBoolean(o)    (LumenTypeOf(o) == Lumen::TypeBool)
#define LumenTypeIsUData(o)    (LumenTypeOf(o) == Lumen::TypeUserdata)
#define LumenTypeIsThread(o)    (LumenTypeOf(o) == Lumen::TypeThread)
#define LumenTypeIsLUData(o)    (LumenTypeOf(o) == Lumen::TypeLightUserdata)
#define LumenTypeIsEqual(o1, o2)    (LumenTypeOf(o1) == LumenTypeOf(o2))

/* Macros to access values */
#define LumenGCValue(o)    LumenCheckExp(LumenIsCollectable(o), (o)->value.gc)
#define LumenLUDataValue(o)    LumenCheckExp(LumenTypeIsLUData(o), (o)->value.p)
#define LumenNumberValue(o)    LumenCheckExp(LumenTypeIsNumber(o), (o)->value.n)
#define LumenStringValue(o)    LumenCheckExp(LumenTypeIsString(o), &(o)->value.gc->AsString)
#define LumenUDataValue(o)    LumenCheckExp(LumenTypeIsUData(o), &(o)->value.gc->AsUserdata)
#define LumenClosureValue(o)    LumenCheckExp(LumenTypeIsFunction(o), &(o)->value.gc->AsClosure)
#define LumenLClosureValue(o)    LumenCheckExp(LumenTypeIsFunction(o), &(o)->value.gc->AsClosure.AsLua)
#define LumenCClosureValue(o)    LumenCheckExp(LumenTypeIsFunction(o), &(o)->value.gc->AsClosure.AsC)
#define LumenTableValue(o)    LumenCheckExp(LumenTypeIsTable(o), &(o)->value.gc->AsTable)
#define LumenBoolValue(o)    LumenCheckExp(LumenTypeIsBoolean(o), (o)->value.b)
#define LumenThreadValue(o)    LumenCheckExp(LumenTypeIsThread(o), &(o)->value.gc->AsThread)

#define LumenIsFalse(o)    (LumenTypeIsNil(o) || (LumenTypeIsBoolean(o) && LumenBoolValue(o) == 0))
#define LumenIsCollectable(o)    (LumenTypeOf(o) >= Lumen::TypeString)

/*
** for internal debug only
*/
#define LumenCheckConsistency(obj) \
LumenAssert(!LumenIsCollectable(obj) || (LumenTypeOf(obj) == (obj)->value.gc->AsObject.Type))

#define LumenCheckLiveness(g, obj) \
LumenAssert(!LumenIsCollectable(obj) || \
    ((LumenTypeOf(obj) == (obj)->value.gc->AsObject.Type) && !LumenGCIsDead(g, (obj)->value.gc)))


/* Macros to set values */
#define LumenSetNilValue(obj) ((obj)->Type=Lumen::TypeNil)

#define LumenSetNumberValue(obj, x) \
LumenDo( Lumen::Object *i_o=(obj); i_o->value.n=(x); i_o->Type=Lumen::TypeNumber; )

#define LumenSetLUDataValue(obj, x) \
LumenDo( Lumen::Object *i_o=(obj); i_o->value.p=(x); i_o->Type=Lumen::TypeLightUserdata; )

#define LumenSetBoolValue(obj, x) \
LumenDo( Lumen::Object *i_o=(obj); i_o->value.b=(x); i_o->Type=Lumen::TypeBool; )

#define LumenSetStringValue(L, obj, x) \
LumenDo(                               \
    Lumen::Object *i_o=(obj);           \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=Lumen::TypeString; \
    LumenCheckLiveness(LumenGlobalState(L), i_o);                              \
)

#define LumenSetUDataValue(L, obj, x) \
LumenDo(                              \
    Lumen::Object *i_o=(obj);          \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=Lumen::TypeUserdata; \
    LumenCheckLiveness(LumenGlobalState(L), i_o);                                \
)

#define LumenSetThreadValue(L, obj, x) \
LumenDo(                               \
    Lumen::Object *i_o=(obj);           \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=Lumen::TypeThread; \
    LumenCheckLiveness(LumenGlobalState(L), i_o);                              \
)

#define LumenSetClosureValue(L, obj, x) \
LumenDo(                                \
    Lumen::Object *i_o=(obj);            \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=Lumen::TypeFunction; \
    LumenCheckLiveness(LumenGlobalState(L), i_o);                                \
)

#define LumenSetTableValue(L, obj, x) \
LumenDo(                              \
    Lumen::Object *i_o=(obj);          \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=Lumen::TypeTable; \
    LumenCheckLiveness(LumenGlobalState(L), i_o);                             \
)

#define LumenSetProtoValue(L, obj, x) \
LumenDo(                              \
    Lumen::Object *i_o=(obj);          \
    i_o->value.gc=cast(Lumen::GCObject *, (x)); i_o->Type=Lumen::TypeProto; \
    LumenCheckLiveness(LumenGlobalState(L),i_o);                             \
)

#define LumenSetObject(L, obj1, obj2) \
LumenDo(                              \
    const Lumen::Object *i_o2=(obj2); \
    Lumen::Object *i_o1=(obj1);       \
    i_o1->value = i_o2->value;        \
    i_o1->Type = i_o2->Type;          \
    LumenCheckLiveness(LumenGlobalState(L), i_o1); \
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

#define LumenStringCString(ts)         cast(const char *, (ts) + 1)
#define LumenStringValue2CString(o)    LumenStringCString(LumenStringValue(o))

// Closure helpers

#define LumenIsCFunction(o)    (LumenTypeOf(o) == Lumen::TypeFunction && LumenClosureValue(o)->AsC.IsC)
#define LumenIsLFunction(o)    (LumenTypeOf(o) == Lumen::TypeFunction && !LumenClosureValue(o)->AsC.IsC)

#define LumenCClosureSize(n)    (cast(int, sizeof(Lumen::CClosure)) + \
                         cast(int, sizeof(Lumen::Object) * ((n) - 1)))

#define LumenLClosureSize(n)    (cast(int, sizeof(Lumen::LClosure)) + \
                         cast(int, sizeof(Lumen::Object *) * ((n) - 1)))

// Other helpers

/**
 * `module` operation for hashing (size is always a power of 2)
 */
#define LumenLogMod(s, size) \
    (LumenCheckExp((size & (size - 1)) == 0, (cast(int, (s) & (size - 1)))))

#define LumenTableTwoTo(x)    (1 << (x))
#define LumenTableNodeCount(t)    (LumenTableTwoTo((t)->NodeCount))

#define LumenTableCeilLog2(x)    (Lumen::Log2((x) - 1) + 1)

inline int Lumen::Int2FB(unsigned int x) {
    int e = 0;  /* exponent */
    while (x >= 16) {
        x = (x + 1) >> 1;
        e++;
    }
    if (x < 8) return static_cast<int>(x);
    else return ((e + 1) << 3) | (static_cast<int>(x) - 8);
}

inline int Lumen::FB2Int(int x) {
    int e = (x >> 3) & 31;
    if (e == 0) return x;
    else return ((x & 7) + 8) << (e - 1);
}

inline int Lumen::RawEqualObject(const Lumen::Object *t1, const Lumen::Object *t2) {
    if (LumenTypeOf(t1) != LumenTypeOf(t2)) return 0;
    switch (LumenTypeOf(t1)) {
        case Lumen::TypeNil:
            return 1;
        case Lumen::TypeNumber:
            return LumenNumberValue(t1) == LumenNumberValue(t2);
        case Lumen::TypeBool:
            return LumenBoolValue(t1) == LumenBoolValue(t2);  /* boolean true must be 1 !! */
        case Lumen::TypeLightUserdata:
            return LumenLUDataValue(t1) == LumenLUDataValue(t2);
        default:
            LumenAssert(LumenIsCollectable(t1));
            return LumenGCValue(t1) == LumenGCValue(t2);
    }
}

inline int Lumen::String2Decimal(const char *s, Lumen::Number *result) {
    char *endPtr;
    *result = lua_str2number(s, &endPtr);
    if (endPtr == s) return 0;  /* conversion failed */
    if (*endPtr == 'x' || *endPtr == 'X')  /* maybe an hexadecimal constant? */
        *result = cast_num(strtoul(s, &endPtr, 16));
    if (*endPtr == '\0') return 1;  /* most common case */
    while (isspace(cast(unsigned char, *endPtr))) endPtr++;
    if (*endPtr != '\0') return 0;  /* invalid trailing characters? */
    return 1;
}

#endif

