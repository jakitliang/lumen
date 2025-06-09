/*!
 * @brief Global State
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_STATE_H
#define LUMEN_STATE_H

#include "lumen/object.h"
#include "lumen/tm.h"
#include "lumen/zio.h"

namespace Lumen {
    struct LongJump;  /* defined in ldo.c */

    struct StringTable {
        Lumen::GCObject **HashTable;
        Lumen::UInt32 Count;  /* number of elements */
        int Capacity;
    };

    /**
     * Information about a call
     */
    struct CallInfo {
        Lumen::StkId Base;  /* base for this function */
        Lumen::StkId Func;  /* function index in the stack */
        Lumen::StkId Top;  /* top for this function */
        const Lumen::Instruction *SavedPC;
        int NResults;  /* expected number of results from this function */
        int NTailCalls;  /* number of tail calls lost under this entry */
    };

    /**
     * `global state`, shared by all threads of this state
     */
    struct GlobalState {
        Lumen::StringTable StringMap;  /* hash table for strings */
        Lumen::Allocator ReAllocator;  /* function to reallocate memory */
        void *ReAllocatorUData;         /* auxiliary data to `ReAllocator` */
        Lumen::Byte CurrentWhite;
        Lumen::Byte GCState;  /* state of garbage collector */
        int GCStringMap;  /* position of sweep in `StringMap` */
        Lumen::GCObject *GCRoot;  /* list of all collectable objects */
        Lumen::GCObject **GCSweep;  /* position of sweep in `GCRoot` */
        Lumen::GCObject *GCGray;  /* list of gray objects */
        Lumen::GCObject *GCGrayAgain;  /* list of objects to be traversed atomically */
        Lumen::GCObject *GCWeak;  /* list of weak tables (to be cleared) */
        Lumen::GCObject *GCTMUData;  /* last element of list of userdata to be GC */
        Lumen::ZBuffer Buff;  /* temporary buffer for string concatenation */
        Lumen::MemorySize GCThreshold;
        Lumen::MemorySize TotalBytes;  /* number of bytes currently allocated */
        Lumen::MemorySize Estimate;  /* an estimate of number of bytes actually in use */
        Lumen::MemorySize GCDept;  /* how much GC is `behind schedule' */
        int GCPause;  /* size of pause between successive GCs */
        int GCStepMul;  /* GC `granularity' */
        Lumen::Delegate Panic;  /* to be called in unprotected errors */
        Lumen::Value Registry;
        Lumen::State *MainThread;
        Lumen::UpValue UpValueHead;  /* head of double-linked list of all open upValues */
        Lumen::Table *Metatable[LUA_NUM_TAGS];  /* metatables for basic types */
        Lumen::String *MetatableName[Lumen::TM::NameN];  /* array with tag-method names */
    };

    /* extra stack space to handle TM calls and some other extras */
    inline constexpr Lumen::UInteger ExtraStack = 5;

    inline constexpr Lumen::UInteger BasicCISize = 8;

    inline constexpr Lumen::UInteger BasicStackSize = 2 * LUA_MINSTACK;
}

#define LumenCurFunc(L)    (LumenClosureValue(L->CallInfo->Func))
#define LumenCIFunc(ci)    (LumenClosureValue((ci)->Func))
#define LumenCIFuncIsLua(ci)    (!LumenCIFunc(ci)->AsC.IsC)
#define LumenFuncIsLua(ci)    (LumenTypeIsFunction((ci)->Func) && LumenCIFuncIsLua(ci))

/*
** `per thread' state
*/
struct LumenState : Lumen::Object {
    Lumen::Byte Status;
    Lumen::StkId Top;  /* first free slot in the stack */
    Lumen::StkId Base;  /* base of current function */
    Lumen::GlobalState *GlobalState;
    Lumen::CallInfo *CallInfo;  /* call info for current function */
    const Lumen::Instruction *SavedPC;  /* `SavedPC` (Saved Position of Code) of current function */
    Lumen::StkId StackLast;  /* last free slot in the stack */
    Lumen::StkId Stack;  /* stack base */
    Lumen::CallInfo *EndCI;  /* points after end of ci array*/
    Lumen::CallInfo *BaseCI;  /* array of Lumen::CallInfo's */
    int StackCount;
    int BaseCICount;  /* size of array `BaseCI` */
    unsigned short NCCalls;  /* number of nested C calls */
    unsigned short BaseCCalls;  /* nested C calls when resuming coroutine */
    Lumen::Byte HookMask;
    Lumen::Byte AllowHook;
    int BaseHookCount;
    int HookCount;
    Lumen::Hook Hook;
    Lumen::Value Global;  /* table of globals */
    Lumen::Value Env;  /* temporary place for environments */
    Lumen::GCObject *OpenedUpValue;  /* list of open upValues in this stack */
    Lumen::GCObject *GCList;
    Lumen::LongJump *ErrorJmp;  /* current error recover point */
    Lumen::Integer ErrFunc;  /* current error handling function (stack index) */

    // MARK: Debug

    const char *FindLocal(Lumen::CallInfo *ci, int n);

    // MARK: Static

    static Lumen::State *NewThread(Lumen::State *L);

    static void FreeThread(Lumen::State *L, Lumen::State *L1);

    static Lumen::State *New(Lumen::Allocator allocator, void *userData);

    static void Close(Lumen::State *L);
};

namespace Lumen {
    /*
    ** Union of all collectable objects
    */
    struct GCObject {
        union {
            Lumen::Object AsObject;
            Lumen::String AsString;
            Lumen::Userdata AsUserdata;
            Lumen::Closure AsClosure;
            Lumen::Table AsTable;
            Lumen::Proto AsProto;
            Lumen::UpValue AsUpValue;
            Lumen::State AsThread;  /* thread */
        };
    };
}


/* macros to convert a Lumen::GCObject into a specific value */
#define LumenGCObject2String(o)    LumenCheckExp((o)->AsObject.Type == Lumen::TypeString, &((o)->AsString))
#define LumenGCObject2Userdata(o)    LumenCheckExp((o)->AsObject.Type == Lumen::TypeUserdata, &((o)->AsUserdata))
#define LumenGCObject2Closure(o)    LumenCheckExp((o)->AsObject.Type == Lumen::TypeFunction, &((o)->AsClosure))
#define LumenGCObject2Table(o)    LumenCheckExp((o)->AsObject.Type == Lumen::TypeTable, &((o)->AsTable))
#define LumenGCObject2Proto(o)    LumenCheckExp((o)->AsObject.Type == LUA_TPROTO, &((o)->AsProto))
#define LumenGCObject2UpValue(o)    LumenCheckExp((o)->AsObject.Type == LUA_TUPVAL, &((o)->AsUpValue))
#define LumenNullGCObject2UpValue(o) \
    LumenCheckExp((o) == nullptr || (o)->AsObject.Type == LUA_TUPVAL, &((o)->AsUpValue))
#define LumenGCObject2Thread(o)    LumenCheckExp((o)->AsObject.Type == Lumen::TypeThread, &((o)->AsThread))

/* macro to convert any Lua object into a Lumen::GCObject */
#define LumenObject2GCObject(v)    (cast(Lumen::GCObject *, (v)))

#endif

