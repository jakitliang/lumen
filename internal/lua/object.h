/*!
 * @brief Type definitions for Lua objects
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_OBJECT_H
#define LUA_OBJECT_H


#include <cstdarg>


#include "lua/limits.h"
#include "lua.h"


/* tags for values visible from Lua */
#define LUA_LAST_TAG    LUA_TTHREAD

#define LUA_NUM_TAGS    (LUA_LAST_TAG+1)

/* table of globals */
#define LuaGlobalTable(L)    (&L->Global)

/* registry */
#define LuaRegistry(L)    (&LuaGlobal(L)->Registry)

#define LuaGlobal(L)    (L->GlobalState)

/*
** Extra tags for non-values
*/
enum {
    LUA_TPROTO = LUA_LAST_TAG + 1,
    LUA_TUPVAL = LUA_LAST_TAG + 2,
    LUA_TDEADKEY = LUA_LAST_TAG + 3
};

namespace Lua {
    struct Object : BasicObject {
        Lua::GC::Mark Marked;
        Lua::GCObject *GCNext;
    };

    struct String : Object {
        Lua::Byte Reserved;
        unsigned int Hash;
        size_t Length;

        static void Resize(Lua::State *L, int newSize);

        static Lua::String *New(Lua::State *L, const char *str, size_t l);

        static Lua::String *New(Lua::State *L, const char *str);
    };

    union Key {
        struct Value : Lua::Value {
            struct Node *Next;  /* for chaining */
        } KeyNext;
        Lua::Value KeyValue;
    };

    struct Node {
        Lua::Value Value;
        Lua::Key Key;
    };

    struct Table : Object {
        Lua::Byte Flags;  /* 1<<p means taggedMethod(p) is not present */
        Lua::Byte NodeCount;  /* log2 of size of `node` array */
        Table *Metatable;
        Lua::Value *Array;  /* array part */
        Lua::Node *Nodes;
        Lua::Node *LastFreeNode;  /* any free position is before this position */
        Lua::GCObject *GCList;
        int ArrayCount;  /* size of `array` array */

        static const Lua::Value *GetNum (Lua::Table *t, int key);
        static Lua::Value *SetNum (Lua::State *L, Lua::Table *t, int key);
        static const Lua::Value *GetString (Lua::Table *t, Lua::String *key);
        static Lua::Value *SetString (Lua::State *L, Lua::Table *t, Lua::String *key);
        static const Lua::Value *Get (Lua::Table *t, const Lua::Value *key);
        static Lua::Value *Set (Lua::State *L, Lua::Table *t, const Lua::Value *key);
        static Lua::Table *New (Lua::State *L, int nArray, int nHash);
        static void ResizeArray (Lua::State *L, Lua::Table *t, int nArraySize);
        static void Free (Lua::State *L, Lua::Table *t);
        static int Next (Lua::State *L, Lua::Table *t, Lua::StkId key);
        static int GetN (Lua::Table *t);

#if defined(LUA_DEBUG)
        static Lua::Node *MainPosition (const Lua::Table *t, const Lua::Value *key);
        static int IsDummy (Lua::Node *n);
#endif
    };

    struct Userdata : Object {
        Table *Metatable;
        Table *Env;
        size_t Length;

        static Lua::Userdata *New(Lua::State *L, size_t s, Lua::Table *e);
    };

    struct LocalVar {
        Lua::String *VarName;
        int StartPC;  /* first point where variable is active */
        int EndPC;    /* first point where variable is dead */
    };

    struct Proto : Object {
        /**
         * masks for new-style vararg
         */
        typedef Lua::Byte Vararg;
        enum {
            VarargHasArg = 1,
            VarargIsVararg = 2,
            VarargIsNeedsArg = 4
        };

        Lua::Value *K;  /* constants used by the function */
        Lua::Instruction *Code;
        Lua::Proto **SubProto;  /* functions defined inside the function */
        int *LineInfo;  /* map from opcodes to source lines */
        Lua::LocalVar *LocalVars;  /* information about local variables */
        Lua::String **UpValues;  /* upvalue names */
        Lua::String *Source;
        int UpValuesCount;
        int KCount;  /* size of `K` */
        int CodeCount;
        int LineInfoCount;
        int SubProtoCount;  /* size of `P` */
        int LocalVarsCount;
        int LineDefined;
        int LastLineDefined;
        Lua::GCObject *GCList;
        Lua::Byte NUpValues;  /* number of upvalues */
        Lua::Byte NUmParams;
        Vararg IsVararg;
        Lua::Byte MaxStackSize;

        static Lua::Proto *New(Lua::State *L);

        static void Free(Lua::State *L, Lua::Proto *f);

        static const char *GetLocalName (const Lua::Proto *func, int local_number,
                                              int pc);
    };

    struct UpValue : Object {
        Lua::Value *SelfValue;  /* points to stack or to its own value */
        union {
            Lua::Value Value;  /* the value (when closed) */
            struct {  /* double linked list (when open) */
                Lua::UpValue *Prev;
                Lua::UpValue *Next;
            };
        };

        static Lua::UpValue *New(Lua::State *L);

        static Lua::UpValue *Find(Lua::State *L, Lua::StkId level);

        static void Close(Lua::State *L, Lua::StkId level);

        static void Free (Lua::State *L, Lua::UpValue *uv);
    };

    struct BasicClosure : Lua::Object {
        Lua::Byte IsC;
        Lua::Byte NUpValues;
        Lua::GCObject *GCList;
        Lua::Table *Env;
    };

    union Closure;

    struct CClosure : Lua::BasicClosure {
        lua_CFunction Func;
        Lua::Value UpValues[1];

        static Lua::Closure *New(Lua::State *L, int nElements, Lua::Table *e);
    };

    struct LClosure : Lua::BasicClosure {
        Lua::Proto *Func;
        Lua::UpValue *UpValues[1];

        static Lua::Closure *New(Lua::State *L, int nElements, Lua::Table *e);
    };

    union Closure {
        Lua::CClosure AsC;
        Lua::LClosure AsLua;

        static void Free(Lua::State *L, Lua::Closure *c);
    };

    int Log2(unsigned int x);

    int Int2FB(unsigned int x);

    int FB2Int(int x);

    int RawEqualObject(const Lua::Value *t1, const Lua::Value *t2);

    int String2Decimal(const char *s, Lua::Number *result);

    const char *PushVFString(Lua::State *L, const char *fmt,
                             va_list argP);

    const char *PushFString(Lua::State *L, const char *fmt, ...);

    void ChunkId(char *out, const char *source, size_t buffLen);

    LUAI_DATA const Lua::Value NilValue;

    inline const Lua::Value *NilObject = &NilValue;
}


/* Macros to test type */
#define LuaTypeIsNil(o)    (LuaTypeOf(o) == LUA_TNIL)
#define LuaTypeIsNumber(o)    (LuaTypeOf(o) == LUA_TNUMBER)
#define LuaTypeIsString(o)    (LuaTypeOf(o) == LUA_TSTRING)
#define LuaTypeIsTable(o)    (LuaTypeOf(o) == LUA_TTABLE)
#define LuaTypeIsFunction(o)    (LuaTypeOf(o) == LUA_TFUNCTION)
#define LuaTypeIsBoolean(o)    (LuaTypeOf(o) == LUA_TBOOLEAN)
#define LuaTypeIsUData(o)    (LuaTypeOf(o) == LUA_TUSERDATA)
#define LuaTypeIsThread(o)    (LuaTypeOf(o) == LUA_TTHREAD)
#define LuaTypeIsLUData(o)    (LuaTypeOf(o) == LUA_TLIGHTUSERDATA)

/* Macros to access values */
#define LuaTypeOf(o)    (o)->Type
#define LuaGCValue(o)    LuaCheckExp(LuaIsCollectable(o), (o)->value.gc)
#define LuaLUDataValue(o)    LuaCheckExp(LuaTypeIsLUData(o), (o)->value.p)
#define LuaNumberValue(o)    LuaCheckExp(LuaTypeIsNumber(o), (o)->value.n)
#define LuaStringValue(o)    LuaCheckExp(LuaTypeIsString(o), &(o)->value.gc->AsString)
#define LuaUDataValue(o)    LuaCheckExp(LuaTypeIsUData(o), &(o)->value.gc->AsUserdata)
#define LuaClosureValue(o)    LuaCheckExp(LuaTypeIsFunction(o), &(o)->value.gc->AsClosure)
#define LuaTableValue(o)    LuaCheckExp(LuaTypeIsTable(o), &(o)->value.gc->AsTable)
#define LuaBoolValue(o)    LuaCheckExp(LuaTypeIsBoolean(o), (o)->value.b)
#define LuaThreadValue(o)    LuaCheckExp(LuaTypeIsThread(o), &(o)->value.gc->AsThread)

#define LuaIsFalse(o)    (LuaTypeIsNil(o) || (LuaTypeIsBoolean(o) && LuaBoolValue(o) == 0))
#define LuaIsCollectable(o)    (LuaTypeOf(o) >= LUA_TSTRING)

/*
** for internal debug only
*/
#define LuaCheckConsistency(obj) \
lua_assert(!LuaIsCollectable(obj) || (LuaTypeOf(obj) == (obj)->value.gc->AsObject.Type))

#define LuaCheckLiveness(g, obj) \
lua_assert(!LuaIsCollectable(obj) || \
    ((LuaTypeOf(obj) == (obj)->value.gc->AsObject.Type) && !LuaGCIsDead(g, (obj)->value.gc)))


/* Macros to set values */
#define LuaSetNilValue(obj) ((obj)->Type=LUA_TNIL)

#define LuaSetNumberValue(obj, x) \
LuaDo( Lua::Value *i_o=(obj); i_o->value.n=(x); i_o->Type=LUA_TNUMBER; )

#define LuaSetLUDataValue(obj, x) \
LuaDo( Lua::Value *i_o=(obj); i_o->value.p=(x); i_o->Type=LUA_TLIGHTUSERDATA; )

#define LuaSetBoolValue(obj, x) \
LuaDo( Lua::Value *i_o=(obj); i_o->value.b=(x); i_o->Type=LUA_TBOOLEAN; )

#define LuaSetStringValue(L, obj, x) \
LuaDo(                               \
    Lua::Value *i_o=(obj);           \
    i_o->value.gc=cast(Lua::GCObject *, (x)); i_o->Type=LUA_TSTRING; \
    LuaCheckLiveness(LuaGlobal(L), i_o);                              \
)

#define LuaSetUDataValue(L, obj, x) \
LuaDo(                              \
    Lua::Value *i_o=(obj);          \
    i_o->value.gc=cast(Lua::GCObject *, (x)); i_o->Type=LUA_TUSERDATA; \
    LuaCheckLiveness(LuaGlobal(L), i_o);                                \
)

#define LuaSetThreadValue(L, obj, x) \
LuaDo(                               \
    Lua::Value *i_o=(obj);           \
    i_o->value.gc=cast(Lua::GCObject *, (x)); i_o->Type=LUA_TTHREAD; \
    LuaCheckLiveness(LuaGlobal(L), i_o);                              \
)

#define LuaSetClosureValue(L, obj, x) \
LuaDo(                                \
    Lua::Value *i_o=(obj);            \
    i_o->value.gc=cast(Lua::GCObject *, (x)); i_o->Type=LUA_TFUNCTION; \
    LuaCheckLiveness(LuaGlobal(L), i_o);                                \
)

#define LuaSetTableValue(L, obj, x) \
LuaDo(                              \
    Lua::Value *i_o=(obj);          \
    i_o->value.gc=cast(Lua::GCObject *, (x)); i_o->Type=LUA_TTABLE; \
    LuaCheckLiveness(LuaGlobal(L), i_o);                             \
)

#define LuaSetProtoValue(L, obj, x) \
LuaDo(                              \
    Lua::Value *i_o=(obj);          \
    i_o->value.gc=cast(Lua::GCObject *, (x)); i_o->Type=LUA_TPROTO; \
    LuaCheckLiveness(LuaGlobal(L),i_o);                             \
)

#define LuaSetObject(L, obj1, obj2) \
LuaDo(                              \
    const Lua::Value *o2=(obj2);    \
    Lua::Value *o1=(obj1);          \
    o1->value = o2->value;          \
    o1->Type=o2->Type;              \
    LuaCheckLiveness(LuaGlobal(L),o1); \
)


/*
** different types of sets, according to destination
*/

/* from stack to (same) stack */
#define LuaSetObjectS2S(L, obj1, obj2)    LuaSetObject(L, obj1, obj2)
/* to stack (not from same stack) */
#define LuaSetObject2S(L, obj1, obj2)    LuaSetObject(L, obj1, obj2)
#define LuaSetStringValue2S(L, obj, x)    LuaSetStringValue(L, obj, x)
#define LuaSetTableValue2S(L, obj, x)    LuaSetTableValue(L, obj, x)
#define LuaSetProtoValue2S(L, obj, x)    LuaSetProtoValue(L, obj, x)
/* from table to same table */
#define LuaSetObjectT2T(L, obj1, obj2)    LuaSetObject(L, obj1, obj2)
/* to table */
#define LuaSetObject2T(L, obj1, obj2)    LuaSetObject(L, obj1, obj2)
/* to new object */
#define LuaSetObject2N(L, obj1, obj2)    LuaSetObject(L, obj1, obj2)
#define LuaSetStringValue2N(L, obj, x)    LuaSetStringValue(L, obj, x)

#define LuaSetType(obj, Type) (LuaTypeOf(obj) = (Type))


// String helpers

#define LuaStringCString(ts)    cast(const char *, (ts) + 1)
#define LuaStringValue2CString(o)       LuaStringCString(LuaStringValue(o))

// Closure helpers

#define LuaIsCFunction(o)    (LuaTypeOf(o) == LUA_TFUNCTION && LuaClosureValue(o)->AsC.IsC)
#define LuaIsLFunction(o)    (LuaTypeOf(o) == LUA_TFUNCTION && !LuaClosureValue(o)->AsC.IsC)

#define LuaCClosureSize(n)	(cast(int, sizeof(Lua::CClosure)) + \
                         cast(int, sizeof(Lua::Value)*((n)-1)))

#define LuaLClosureSize(n)	(cast(int, sizeof(Lua::LClosure)) + \
                         cast(int, sizeof(Lua::Value *)*((n)-1)))

// Other helpers

/**
 * `module` operation for hashing (size is always a power of 2)
 */
#define LuaLogMod(s, size) \
    (LuaCheckExp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))

#define LuaTableTwoTo(x)    (1 << (x))
#define LuaTableNodeCount(t)    (LuaTableTwoTo((t)->NodeCount))

#define LuaTableCeilLog2(x)    (Lua::Log2((x) - 1) + 1)

#endif

