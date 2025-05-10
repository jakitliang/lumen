/*!
 * @brief Global State
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_STATE_H
#define LUA_STATE_H

#include "lua.h"

#include "lua/object.h"
#include "lua/tm.h"
#include "lua/zio.h"


namespace Lua {
    struct LongJump;  /* defined in ldo.c */

    struct StringTable {
        Lua::GCObject **HashTable;
        Lua::UInt32 Count;  /* number of elements */
        int Capacity;
    };

    /**
     * Information about a call
     */
    struct CallInfo {
        Lua::StkId Base;  /* base for this function */
        Lua::StkId Func;  /* function index in the stack */
        Lua::StkId Top;  /* top for this function */
        const Lua::Instruction *SavedPC;
        int NResults;  /* expected number of results from this function */
        int NTailCalls;  /* number of tail calls lost under this entry */
    };

    /**
     * `global state`, shared by all threads of this state
     */
    struct GlobalState {
        Lua::StringTable StringMap;  /* hash table for strings */
        lua_Alloc ReAllocator;  /* function to reallocate memory */
        void *ReAllocatorUData;         /* auxiliary data to `ReAllocator` */
        Lua::Byte CurrentWhite;
        Lua::Byte GCState;  /* state of garbage collector */
        int GCStringMap;  /* position of sweep in `StringMap` */
        Lua::GCObject *GCRoot;  /* list of all collectable objects */
        Lua::GCObject **GCSweep;  /* position of sweep in `GCRoot` */
        Lua::GCObject *GCGray;  /* list of gray objects */
        Lua::GCObject *GCGrayAgain;  /* list of objects to be traversed atomically */
        Lua::GCObject *GCWeak;  /* list of weak tables (to be cleared) */
        Lua::GCObject *GCTMUData;  /* last element of list of userdata to be GC */
        Lua::ZBuffer Buff;  /* temporary buffer for string concatenation */
        Lua::MemorySize GCThreshold;
        Lua::MemorySize TotalBytes;  /* number of bytes currently allocated */
        Lua::MemorySize Estimate;  /* an estimate of number of bytes actually in use */
        Lua::MemorySize GCDept;  /* how much GC is `behind schedule' */
        int GCPause;  /* size of pause between successive GCs */
        int GCStepMul;  /* GC `granularity' */
        lua_CFunction Panic;  /* to be called in unprotected errors */
        Lua::Value Registry;
        lua_State *MainThread;
        Lua::UpValue UpValueHead;  /* head of double-linked list of all open upValues */
        Lua::Table *Metatable[LUA_NUM_TAGS];  /* metatables for basic types */
        Lua::String *MetatableName[Lua::TM::NameN];  /* array with tag-method names */
    };

    /* extra stack space to handle TM calls and some other extras */
    inline constexpr size_t ExtraStack = 5;

    inline constexpr size_t BasicCISize = 8;

    inline constexpr size_t BasicStackSize = 2 * LUA_MINSTACK;
}

#define LuaCurFunc(L)    (LuaClosureValue(L->CallInfo->Func))
#define LuaCIFunc(ci)    (LuaClosureValue((ci)->Func))
#define LuaCIFuncIsLua(ci)    (!LuaCIFunc(ci)->AsC.IsC)
#define LuaFuncIsLua(ci)    (LuaTypeIsFunction((ci)->Func) && LuaCIFuncIsLua(ci))

/*
** `per thread' state
*/
struct lua_State : Lua::Object {
    Lua::Byte Status;
    Lua::StkId Top;  /* first free slot in the stack */
    Lua::StkId Base;  /* base of current function */
    Lua::GlobalState *GlobalState;
    Lua::CallInfo *CallInfo;  /* call info for current function */
    const Lua::Instruction *SavedPC;  /* `SavedPC` (Saved Position of Code) of current function */
    Lua::StkId StackLast;  /* last free slot in the stack */
    Lua::StkId Stack;  /* stack base */
    Lua::CallInfo *EndCI;  /* points after end of ci array*/
    Lua::CallInfo *BaseCI;  /* array of Lua::CallInfo's */
    int StackCount;
    int BaseCICount;  /* size of array `BaseCI` */
    unsigned short NCCalls;  /* number of nested C calls */
    unsigned short BaseCCalls;  /* nested C calls when resuming coroutine */
    Lua::Byte HookMask;
    Lua::Byte AllowHook;
    int BaseHookCount;
    int HookCount;
    lua_Hook Hook;
    Lua::Value Global;  /* table of globals */
    Lua::Value Env;  /* temporary place for environments */
    union Lua::GCObject *OpenedUpValue;  /* list of open upValues in this stack */
    union Lua::GCObject *GCList;
    struct Lua::LongJump *ErrorJmp;  /* current error recover point */
    ptrdiff_t ErrFunc;  /* current error handling function (stack index) */

    static Lua::State *NewThread(Lua::State *L);

    static void FreeThread(lua_State *L, lua_State *L1);
};

namespace Lua {
    /*
    ** Union of all collectable objects
    */
    union GCObject {
        Lua::Object AsObject;
        Lua::String AsString;
        Lua::Userdata AsUserdata;
        Lua::Closure AsClosure;
        Lua::Table AsTable;
        Lua::Proto AsProto;
        Lua::UpValue AsUpValue;
        Lua::State AsThread;  /* thread */
    };
}


/* macros to convert a Lua::GCObject into a specific value */
#define LuaGCObject2String(o)    LuaCheckExp((o)->AsObject.Type == LUA_TSTRING, &((o)->AsString))
#define LuaGCObject2Userdata(o)    LuaCheckExp((o)->AsObject.Type == LUA_TUSERDATA, &((o)->AsUserdata))
#define LuaGCObject2Closure(o)    LuaCheckExp((o)->AsObject.Type == LUA_TFUNCTION, &((o)->AsClosure))
#define LuaGCObject2Table(o)    LuaCheckExp((o)->AsObject.Type == LUA_TTABLE, &((o)->AsTable))
#define LuaGCObject2Proto(o)    LuaCheckExp((o)->AsObject.Type == LUA_TPROTO, &((o)->AsProto))
#define LuaGCObject2UpValue(o)    LuaCheckExp((o)->AsObject.Type == LUA_TUPVAL, &((o)->AsUpValue))
#define LuaNullGCObject2UpValue(o) \
    LuaCheckExp((o) == nullptr || (o)->AsObject.Type == LUA_TUPVAL, &((o)->AsUpValue))
#define LuaGCObject2Thread(o)    LuaCheckExp((o)->AsObject.Type == LUA_TTHREAD, &((o)->AsThread))

/* macro to convert any Lua object into a Lua::GCObject */
#define LuaObject2GCObject(v)    (cast(Lua::GCObject *, (v)))

#endif

