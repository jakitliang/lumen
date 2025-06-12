/*!
 * @brief Stack and Call structure of Lua
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_DO_H
#define LUMEN_DO_H


#include "lumen/object.h"
#include "lumen/state.h"
#include "lumen/zio.h"


#define LumenDoCheckStack(L, n) do { \
    if ((char *)L->StackLast - (char *)L->Top <= (n)*(int)sizeof(Lumen::Object)) \
        Lumen::Do::GrowStack(L, n);      \
    else LumenCondHardStackTests(Lumen::Do::ReAllocStack(L, L->StackCount - Lumen::ExtraStack - 1)); \
} while(0)


#define LumenIncrTop(L) \
LumenDo(              \
    LumenDoCheckStack(L,1); \
    L->Top++;       \
)

#define LumenSaveStack(L, p)        ((char *)(p) - (char *)L->Stack)
#define LumenRestoreStack(L, n)    ((Lumen::Object *)((char *)L->Stack + (n)))

#define LumenSaveCI(L, p)        ((char *)(p) - (char *)L->BaseCI)
#define LumenRestoreCI(L, n)        ((Lumen::CallInfo *)((char *)L->BaseCI + (n)))

namespace Lumen::Do {
    /* results from Lumen::Do::PreCall */
    typedef int PCRet;
    enum {
        PCRetLua = 0,     /* initiated a call to a Lua function */
        PCRetC = 1,       /* did a call to a C function */
        PCRetYield = 2    /* C function yielded */
    };

    /* type of protected functions, to be run by `runProtected` */
    typedef void (*PFunc)(Lumen::State *L, void *ud);

    int ProtectedParser(Lumen::State *L, Lumen::ZIO *z, const char *name);

    void CallHook(Lumen::State *L, int event, int line);

    int PreCall(Lumen::State *L, Lumen::Value func, int nResults);
    void Call(Lumen::State *L, Lumen::Value func, int nResults);
    int PCall(Lumen::State *L, Lumen::Do::PFunc func, void *u,
                             Lumen::Integer oldtop, Lumen::Integer ef);
    int PosCall(Lumen::State *L, Lumen::Value firstResult);
    void ReAllocCI(Lumen::State *L, int newSize);
    void ReAllocStack(Lumen::State *L, int newSize);
    void GrowStack(Lumen::State *L, int n);

    void Throw(Lumen::State *L, int errcode);
    int RawRunProtected(Lumen::State *L, Lumen::Do::PFunc f, void *ud);

    void SetErrorObject(Lumen::State *L, int errcode, Lumen::Value oldTop);

    void Resume(Lumen::State *L, void *ud);

    int ResumeError(Lumen::State *L, const char *msg);
}

#endif

