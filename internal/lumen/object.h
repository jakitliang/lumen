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

    /**
     * Union of all Lua values
     */
    union Variant {
        Lumen::GCObject *gc;
        void *p;
        Lumen::Number n;
        int b;
    };

    struct String;

    struct Userdata;

    struct Closure;

    struct LClosure;

    struct CClosure;

    struct Table;

    struct Proto;

    struct Object : TypeInfo {
        Variant value;

        const char *GetUpValueInfo(int n, Lumen::Object **val);

        bool IsFalse() const; // NOLINT

        bool IsCollectable() const; // NOLINT

        bool IsCFunction() const; // NOLINT

        bool IsLFunction() const; // NOLINT

        bool IsWhite() const; // NOLINT

        GCObject *GetGCObject() const; // NOLINT

        void *GetLUData() const; // NOLINT

        Lumen::Number &GetNumber();

        const Lumen::Number &GetNumber() const; // NOLINT

        String *GetString() const; // NOLINT

        Userdata *GetUData() const; // NOLINT

        Closure *GetClosure() const; // NOLINT

        LClosure *GetLClosure() const; // NOLINT

        CClosure *GetCClosure() const; // NOLINT

        Table *GetTable() const; // NOLINT

        int GetBool() const; // NOLINT

        Lumen::State *GetThread() const; // NOLINT

        void SetType(Lumen::Type t);

        void SetNil();

        void SetNumber(Lumen::Number x);

        void SetLUData(void *x);

        void SetBool(int x);

        void SetString(Lumen::State *L, String *x);

        void SetUData(Lumen::State *L, Userdata *x);

        void SetThread(Lumen::State *L, Lumen::State *x);

        void SetClosure(Lumen::State *L, Closure *x);

        void SetTable(Lumen::State *L, Table *x);

        void SetProto(Lumen::State *L, Proto *x);

        void SetObject(Lumen::State *L, const Object *other);

        char *ToCString() const; // NOLINT
    };

    /**
     * Value is a pointer to Object
     * and index to stack elements
     */
    typedef Lumen::Object *Value;

    struct String : BasicObject {
        Lumen::Byte Reserved;
        unsigned int Hash;
        Lumen::UInteger Length;

        void Intern(Lumen::State *L);

        char *CString();

        const char *CString() const; // NOLINT

        static void Resize(Lumen::State *L, int newSize);

        static Lumen::String *New(Lumen::State *L, const char *str, Lumen::UInteger l);

        static Lumen::String *New(Lumen::State *L, const char *str);

        template<Lumen::UInteger S>
        static inline Lumen::String *New(Lumen::State *L, const char (&s)[S]) {
            return New(L, s, S - 1);
        }

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
        Lumen::UInt32 ArrayCount;  /* size of `array` array */

        static const Lumen::Object *GetNum(Lumen::Table *t, int key);

        static Lumen::Object *SetNum(Lumen::State *L, Lumen::Table *t, int key);

        static const Lumen::Object *GetString(Lumen::Table *t, Lumen::String *key);

        static Lumen::Object *SetString(Lumen::State *L, Lumen::Table *t, Lumen::String *key);

        static const Lumen::Object *Get(Lumen::Table *t, const Lumen::Object *key);

        static Lumen::Object *Set(Lumen::State *L, Lumen::Table *t, const Lumen::Object *key);

        static Lumen::Object *Set(Lumen::State *L, Lumen::Table *t,
                                  const Lumen::Object *key, const Lumen::Object *value);

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

    struct CClosure : Lumen::BasicClosure {
        Lumen::Delegate Func;
        Lumen::Object UpValues[1];

        static Lumen::UInteger SizeOf(Lumen::UInteger nUpValues);

        static Lumen::Closure *New(Lumen::State *L, int nElements, Lumen::Table *e);
    };

    struct LClosure : Lumen::BasicClosure {
        Lumen::Proto *Func;
        Lumen::UpValue *UpValues[1];

        static Lumen::UInteger SizeOf(Lumen::UInteger nUpValues);

        static Lumen::Closure *New(Lumen::State *L, int nElements, Lumen::Table *e);
    };

    struct Closure {
        union {
            Lumen::BasicClosure Basic;
            Lumen::CClosure AsC;
            Lumen::LClosure AsLua;
        };

        int Size() const; // NOLINT

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

    int Utf8Esc(char *buff, unsigned long x);

    const char *PushVFString(Lumen::State *L, const char *fmt,
                             va_list argP);

    const char *PushFString(Lumen::State *L, const char *fmt, ...);

    void ChunkId(char *out, const char *source, Lumen::UInteger buffLen);

    LUAI_DATA const Lumen::Object NilValue;

    inline const Lumen::Object *NilObject = &NilValue;
}

/*
** for internal debug only
*/
#define LumenCheckConsistency(obj) \
LumenAssert(!(obj)->IsCollectable() || ((obj)->Type == (obj)->value.gc->AsObject.Type))

#define LumenCheckLiveness(g, obj) \
LumenAssert(!(obj)->IsCollectable() || \
    (((obj)->Type == (obj)->value.gc->AsObject.Type) && !(g)->IsDead((obj)->value.gc)))

/*
** different types of sets, according to destination
*/

/* from stack to (same) stack */
#define LumenSetObjectS2S(L, obj1, obj2)    (obj1)->SetObject(L, obj2)
/* to stack (not from same stack) */
#define LumenSetObject2S(L, obj1, obj2)    (obj1)->SetObject(L, obj2)
#define LumenSetStringValue2S(L, obj, x)   (obj)->SetString(L, x)
#define LumenSetTableValue2S(L, obj, x)    (obj)->SetTable(L, x)
#define LumenSetProtoValue2S(L, obj, x)    (obj)->SetProto(L, x)
/* from table to same table */
#define LumenSetObjectT2T(L, obj1, obj2)   (obj1)->SetObject(L, obj2)
/* to table */
#define LumenSetObject2T(L, obj1, obj2)    (obj1)->SetObject(L, obj2)
/* to new object */
#define LumenSetObject2N(L, obj1, obj2)    (obj1)->SetObject(L, obj2)
#define LumenSetStringValue2N(L, obj, x)   (obj)->SetString(L, x)

// Other helpers

/**
 * `module` operation for hashing (size is always a power of 2)
 */
#define LumenLogMod(s, size) \
    (LumenCheckExp((size & (size - 1)) == 0, (cast(unsigned int, (s) & (size - 1)))))

#define LumenTableTwoTo(x)    (1ULL << (x))
#define LumenTableNodeCount(t)    (LumenTableTwoTo((t)->NodeCount))

#define LumenTableCeilLog2(x)    (Lumen::Log2((x) - 1) + 1)

#endif

