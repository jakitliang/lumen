/*!
 * @brief Stack and Call structure of Lua
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_DO_H
#define LUA_DO_H


#include "lua/object.h"
#include "lua/state.h"
#include "lua/zio.h"


#define LuaDoCheckStack(L, n) do { \
    if ((char *)L->StackLast - (char *)L->Top <= (n)*(int)sizeof(Lua::Value)) \
        Lua::Do::GrowStack(L, n);      \
    else LuaCondHardStackTests(Lua::Do::ReAllocStack(L, L->StackCount - Lua::ExtraStack - 1)); \
} while(0)


#define LuaIncrTop(L) \
LuaDo(              \
    LuaDoCheckStack(L,1); \
    L->Top++;       \
)

#define LuaSaveStack(L, p)        ((char *)(p) - (char *)L->Stack)
#define LuaRestoreStack(L, n)    ((Lua::Value *)((char *)L->Stack + (n)))

#define LuaSaveCI(L, p)        ((char *)(p) - (char *)L->BaseCI)
#define LuaRestoreCI(L, n)        ((Lua::CallInfo *)((char *)L->BaseCI + (n)))

namespace Lua::Do {
    /* results from Lua::Do::PreCall */
    typedef int PCRet;
    enum {
        PCRetLua = 0,     /* initiated a call to a Lua function */
        PCRetC = 1,       /* did a call to a C function */
        PCRetYield = 2    /* C function yielded */
    };

    /* type of protected functions, to be run by `runProtected` */
    typedef void (*PFunc)(Lua::State *L, void *ud);

    int ProtectedParser(Lua::State *L, Lua::ZIO *z, const char *name);

    void CallHook(Lua::State *L, int event, int line);

    int PreCall(Lua::State *L, Lua::StkId func, int nResults);
    void Call(Lua::State *L, Lua::StkId func, int nResults);
    int PCall(Lua::State *L, Lua::Do::PFunc func, void *u,
                             ptrdiff_t oldtop, ptrdiff_t ef);
    int PosCall(Lua::State *L, Lua::StkId firstResult);
    void ReAllocCI(Lua::State *L, int newSize);
    void ReAllocStack(Lua::State *L, int newSize);
    void GrowStack(Lua::State *L, int n);

    void Throw(Lua::State *L, int errcode);
    int RawRunProtected(Lua::State *L, Lua::Do::PFunc f, void *ud);

    void SetErrorObject(Lua::State *L, int errcode, Lua::StkId oldTop);
}

#endif

