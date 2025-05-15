/*!
 * @brief Stack and Call structure of Lua
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <csetjmp>
#include <cstdlib>
#include <cstring>

#define LUA_CORE

#include "lua.h"

#include "lua/debug.h"
#include "lua/do.h"
#include "lua/gc.h"
#include "lua/mem.h"
#include "lua/object.h"
#include "lua/opcodes.h"
#include "lua/parser.h"
#include "lua/state.h"
#include "lua/string.h"
#include "lua/table.h"
#include "lua/tm.h"
#include "lua/undump.h"
#include "lua/vm.h"
#include "lua/zio.h"


/*
** {======================================================
** Error-recovery functions
** =======================================================
*/


/* chain list of long jump buffers */
struct Lua::LongJump {
    Lua::LongJump *Previous;
    luai_jmpbuf b;
    volatile int Status;  /* error code */
};


void Lua::Do::SetErrorObject(Lua::State *L, int errcode, Lua::StkId oldTop) {
    switch (errcode) {
        case LUA_ERRMEM: {
            LuaSetStringValue2S(L, oldTop, LuaStringNewLiteral(L, LUA_MEM_ERR_MSG));
            break;
        }
        case LUA_ERRERR: {
            LuaSetStringValue2S(L, oldTop, LuaStringNewLiteral(L, "error in error handling"));
            break;
        }
        case LUA_ERRSYNTAX:
        case LUA_ERRRUN: {
            LuaSetObjectS2S(L, oldTop, L->Top - 1);  /* error message on current top */
            break;
        }
    }
    L->Top = oldTop + 1;
}


static void restoreStackLimit(Lua::State *L) {
    lua_assert(L->StackLast - L->Stack == L->StackCount - Lua::ExtraStack - 1);
    if (L->BaseCICount > LUAI_MAXCALLS) {  /* there was an overflow? */
        int inuse = cast_int(L->CallInfo - L->BaseCI);
        if (inuse + 1 < LUAI_MAXCALLS)  /* can `undo' overflow? */
            Lua::Do::ReAllocCI(L, LUAI_MAXCALLS);
    }
}


static void resetStack(Lua::State *L, int status) {
    L->CallInfo = L->BaseCI;
    L->Base = L->CallInfo->Base;
    Lua::UpValue::Close(L, L->Base);  /* close eventual pending closures */
    Lua::Do::SetErrorObject(L, status, L->Base);
    L->NCCalls = L->BaseCCalls;
    L->AllowHook = 1;
    restoreStackLimit(L);
    L->ErrFunc = 0;
    L->ErrorJmp = nullptr;
}


void Lua::Do::Throw(Lua::State *L, int errcode) {
    if (L->ErrorJmp) {
        L->ErrorJmp->Status = errcode;
        LUAI_THROW(L, L->ErrorJmp);
    } else {
        L->Status = cast_byte(errcode);
        if (LuaGlobal(L)->Panic) {
            resetStack(L, errcode);
            LuaUnlock(L);
            LuaGlobal(L)->Panic(L);
        }
        exit(EXIT_FAILURE);
    }
}


int Lua::Do::RawRunProtected(Lua::State *L, Lua::Do::PFunc f, void *ud) {
    Lua::LongJump lj; // NOLINT
    lj.Status = 0;
    lj.Previous = L->ErrorJmp;  /* chain new error handler */
    L->ErrorJmp = &lj;
    LUAI_TRY(L, &lj,
             (*f)(L, ud);
    )
    L->ErrorJmp = lj.Previous;  /* restore old error handler */
    return lj.Status;
}

/* }====================================================== */


static void correctStack(Lua::State *L, Lua::Value *oldStack) {
    Lua::CallInfo *ci;
    Lua::GCObject *up;
    L->Top = (L->Top - oldStack) + L->Stack;
    for (up = L->OpenedUpValue; up != nullptr; up = up->AsObject.GCNext)
        LuaGCObject2UpValue(up)->SelfValue = (LuaGCObject2UpValue(up)->SelfValue - oldStack) + L->Stack;
    for (ci = L->BaseCI; ci <= L->CallInfo; ci++) {
        ci->Top = (ci->Top - oldStack) + L->Stack;
        ci->Base = (ci->Base - oldStack) + L->Stack;
        ci->Func = (ci->Func - oldStack) + L->Stack;
    }
    L->Base = (L->Base - oldStack) + L->Stack;
}


void Lua::Do::ReAllocStack(Lua::State *L, int newSize) {
    Lua::Value *oldStack = L->Stack;
    int realSize = newSize + 1 + (int) Lua::ExtraStack;
    lua_assert(L->StackLast - L->Stack == L->StackCount - Lua::ExtraStack - 1);
    LuaMemoryReAllocVector(L, L->Stack, L->StackCount, realSize, Lua::Value);
    L->StackCount = realSize;
    L->StackLast = L->Stack + newSize;
    correctStack(L, oldStack);
}


void Lua::Do::ReAllocCI(Lua::State *L, int newSize) {
    Lua::CallInfo *oldCI = L->BaseCI;
    LuaMemoryReAllocVector(L, L->BaseCI, L->BaseCICount, newSize, Lua::CallInfo);
    L->BaseCICount = newSize;
    L->CallInfo = (L->CallInfo - oldCI) + L->BaseCI;
    L->EndCI = L->BaseCI + L->BaseCICount - 1;
}


void Lua::Do::GrowStack(Lua::State *L, int n) {
    if (n <= L->StackCount)  /* double size is enough? */
        Lua::Do::ReAllocStack(L, 2 * L->StackCount);
    else
        Lua::Do::ReAllocStack(L, L->StackCount + n);
}


static Lua::CallInfo *growCI(Lua::State *L) {
    if (L->BaseCICount > LUAI_MAXCALLS)  /* overflow while handling overflow? */
        Lua::Do::Throw(L, LUA_ERRERR);
    else {
        Lua::Do::ReAllocCI(L, 2 * L->BaseCICount);
        if (L->BaseCICount > LUAI_MAXCALLS)
            Lua::Debug::RunError(L, "stack overflow");
    }
    return ++L->CallInfo;
}


void Lua::Do::CallHook(Lua::State *L, int event, int line) {
    lua_Hook hook = L->Hook;
    if (hook && L->AllowHook) {
        ptrdiff_t top = LuaSaveStack(L, L->Top);
        ptrdiff_t ci_top = LuaSaveStack(L, L->CallInfo->Top);
        lua_Debug ar;
        ar.event = event;
        ar.currentline = line;
        if (event == LUA_HOOKTAILRET)
            ar.i_ci = 0;  /* tail call; no debug information about it */
        else
            ar.i_ci = cast_int(L->CallInfo - L->BaseCI);
        LuaDoCheckStack(L, Lua::MinStack);  /* ensure minimum stack size */
        L->CallInfo->Top = L->Top + Lua::MinStack;
        lua_assert(L->CallInfo->Top <= L->StackLast);
        L->AllowHook = 0;  /* cannot call hooks inside a hook */
        LuaUnlock(L);
        (*hook)(L, &ar);
        LuaLock(L);
        lua_assert(!L->AllowHook);
        L->AllowHook = 1;
        L->CallInfo->Top = LuaRestoreStack(L, ci_top);
        L->Top = LuaRestoreStack(L, top);
    }
}


static Lua::StkId adjustVarargs(Lua::State *L, Lua::Proto *p, int actual) {
    int i;
    int nFixArgs = p->NUmParams;
    Lua::Table *hashTable = nullptr;
    Lua::StkId base, fixed;
    for (; actual < nFixArgs; ++actual)
        LuaSetNilValue(L->Top++);
#if defined(LUA_COMPAT_VARARG)
    if (p->IsVararg & Lua::Proto::VarargIsNeedsArg) { /* compat. with old-style vararg? */
        int nVar = actual - nFixArgs;  /* number of extra arguments */
        lua_assert(p->IsVararg & Lua::Proto::VarargHasArg);
        LuaGCCheckGC(L);
        LuaDoCheckStack(L, p->MaxStackSize);
        hashTable = Lua::Table::New(L, nVar, 1);  /* create `arg' table */
        for (i = 0; i < nVar; i++)  /* put extra arguments into `arg' table */
            LuaSetObject2N (L, Lua::Table::SetNum(L, hashTable, i + 1), L->Top - nVar + i);
        /* store counter in field `n' */
        LuaSetNumberValue(Lua::Table::SetString(L, hashTable, LuaStringNewLiteral(L, "n")), cast_num(nVar));
    }
#endif
    /* move fixed parameters to final position */
    fixed = L->Top - actual;  /* first fixed argument */
    base = L->Top;  /* final position of first argument */
    for (i = 0; i < nFixArgs; i++) {
        LuaSetObjectS2S(L, L->Top++, fixed + i);
        LuaSetNilValue(fixed + i);
    }
    /* add `arg' parameter */
    if (hashTable) {
        LuaSetTableValue(L, L->Top++, hashTable);
        lua_assert(LuaGCIsWhite(LuaObject2GCObject(hashTable)));
    }
    return base;
}


static Lua::StkId tryFuncTM(Lua::State *L, Lua::StkId func) {
    const Lua::Value *tm = Lua::TM::GetByObject(L, func, Lua::TM::NameCall);
    Lua::StkId p;
    ptrdiff_t funcR = LuaSaveStack(L, func);
    if (!LuaTypeIsFunction(tm))
        Lua::Debug::TypeError(L, func, "call");
    /* Open a hole inside the stack at `func' */
    for (p = L->Top; p > func; p--) LuaSetObjectS2S (L, p, p - 1);
    LuaIncrTop(L);
    func = LuaRestoreStack(L, funcR);  /* previous call may change stack */
    LuaSetObject2S(L, func, tm);  /* tag method is the new function to be called */
    return func;
}


#define incrCI(L) \
  ((L->CallInfo == L->EndCI) ? growCI(L) : \
   (LuaCondHardStackTests(Lua::Do::ReAllocCI(L, L->BaseCICount)), ++L->CallInfo))


int Lua::Do::PreCall(Lua::State *L, Lua::StkId func, int nResults) {
    Lua::LClosure *cl;
    ptrdiff_t funcR;
    if (!LuaTypeIsFunction(func)) /* `func' is not a function? */
        func = tryFuncTM(L, func);  /* check the `function' tag method */
    funcR = LuaSaveStack(L, func);
    cl = &LuaClosureValue(func)->AsLua;
    L->CallInfo->SavedPC = L->SavedPC;
    if (!cl->IsC) {  /* Lua function? prepare its call */
        Lua::CallInfo *ci;
        Lua::StkId st, base;
        Lua::Proto *p = cl->Func;
        LuaDoCheckStack(L, p->MaxStackSize);
        func = LuaRestoreStack(L, funcR);
        if (!p->IsVararg) {  /* no varargs? */
            base = func + 1;
            if (L->Top > base + p->NUmParams)
                L->Top = base + p->NUmParams;
        } else {  /* vararg function */
            int nargs = cast_int(L->Top - func) - 1;
            base = adjustVarargs(L, p, nargs);
            func = LuaRestoreStack(L, funcR);  /* previous call may change the stack */
        }
        ci = incrCI(L);  /* now `enter' new function */
        ci->Func = func;
        L->Base = ci->Base = base;
        ci->Top = L->Base + p->MaxStackSize;
        lua_assert(ci->Top <= L->StackLast);
        L->SavedPC = p->Code;  /* starting point */
        ci->NTailCalls = 0;
        ci->NResults = nResults;
        for (st = L->Top; st < ci->Top; st++)
            LuaSetNilValue(st);
        L->Top = ci->Top;
        if (L->HookMask & LUA_MASKCALL) {
            L->SavedPC++;  /* hooks assume 'pc' is already incremented */
            Lua::Do::CallHook(L, LUA_HOOKCALL, -1);
            L->SavedPC--;  /* correct 'pc' */
        }
        return Lua::Do::PCRetLua;
    } else {  /* if is a C function, call it */
        Lua::CallInfo *ci;
        int n;
        LuaDoCheckStack(L, Lua::MinStack);  /* ensure minimum stack size */
        ci = incrCI(L);  /* now `enter' new function */
        ci->Func = LuaRestoreStack(L, funcR);
        L->Base = ci->Base = ci->Func + 1;
        ci->Top = L->Top + Lua::MinStack;
        lua_assert(ci->Top <= L->StackLast);
        ci->NResults = nResults;
        if (L->HookMask & LUA_MASKCALL)
            Lua::Do::CallHook(L, LUA_HOOKCALL, -1);
        LuaUnlock(L);
        n = (*LuaCurFunc(L)->AsC.Func)(L);  /* do the actual call */
        LuaLock(L);
        if (n < 0)  /* yielding? */
            return Lua::Do::PCRetYield;
        else {
            Lua::Do::PosCall(L, L->Top - n);
            return Lua::Do::PCRetC;
        }
    }
}

static Lua::StkId callRetHooks(Lua::State *L, Lua::StkId firstResult) {
    ptrdiff_t fr = LuaSaveStack(L, firstResult);  /* next call may change stack */
    Lua::Do::CallHook(L, LUA_HOOKRET, -1);
    if (LuaCIFuncIsLua(L->CallInfo)) {  /* Lua function? */
        while ((L->HookMask & LUA_MASKRET) && L->CallInfo->NTailCalls--) /* tail calls */
            Lua::Do::CallHook(L, LUA_HOOKTAILRET, -1);
    }
    return LuaRestoreStack(L, fr);
}

int Lua::Do::PosCall(Lua::State *L, Lua::StkId firstResult) {
    Lua::StkId res;
    int wanted, i;
    Lua::CallInfo *ci;
    if (L->HookMask & LUA_MASKRET)
        firstResult = callRetHooks(L, firstResult);
    ci = L->CallInfo--;
    res = ci->Func;  /* res == final position of 1st result */
    wanted = ci->NResults;
    L->Base = (ci - 1)->Base;  /* restore base */
    L->SavedPC = (ci - 1)->SavedPC;  /* restore SavedPC */
    /* move results to correct place */
    for (i = wanted; i != 0 && firstResult < L->Top; i--)
        LuaSetObjectS2S (L, res++, firstResult++);
    while (i-- > 0)
        LuaSetNilValue(res++);
    L->Top = res;
    return (wanted - LUA_MULTRET);  /* 0 iff wanted == LUA_MULTRET */
}

/*
** Call a function (C or Lua). The function to be called is at *func.
** The arguments are on the stack, right after the function.
** When returns, all the results are on the stack, starting at the original
** function position.
*/
void Lua::Do::Call(Lua::State *L, Lua::StkId func, int nResults) {
    if (++L->NCCalls >= LUAI_MAXCCALLS) {
        if (L->NCCalls == LUAI_MAXCCALLS)
            Lua::Debug::RunError(L, "C stack overflow");
        else if (L->NCCalls >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS >> 3)))
            Lua::Do::Throw(L, LUA_ERRERR);  /* error while handing stack error */
    }
    if (Lua::Do::PreCall(L, func, nResults) == Lua::Do::PCRetLua)  /* is a Lua function? */
        Lua::VM::Execute(L, 1);  /* call it */
    L->NCCalls--;
    LuaGCCheckGC(L);
}

static void resume(lua_State *L, void *ud) {
    Lua::StkId firstArg = cast(Lua::StkId, ud);
    Lua::CallInfo *ci = L->CallInfo;
    if (L->Status == 0) {  /* start coroutine? */
        lua_assert(ci == L->BaseCI && firstArg > L->Base);
        if (Lua::Do::PreCall(L, firstArg - 1, LUA_MULTRET) != Lua::Do::PCRetLua)
            return;
    } else {  /* resuming from previous yield */
        lua_assert(L->Status == LUA_YIELD);
        L->Status = 0;
        if (!LuaCIFuncIsLua(ci)) {  /* `common' yield? */
            /* finish interrupted execution of `Lua::OpCodeCall' */
            lua_assert(LuaOpCodeGet(*((ci - 1)->SavedPC - 1)) == Lua::OpCodeCall ||
                       LuaOpCodeGet(*((ci - 1)->SavedPC - 1)) == Lua::OpCodeTailCall);
            if (Lua::Do::PosCall(L, firstArg))  /* complete it... */
                L->Top = L->CallInfo->Top;  /* and correct top if not multiple results */
        } else  /* yielded inside a hook: just continue its execution */
            L->Base = L->CallInfo->Base;
    }
    Lua::VM::Execute(L, cast_int(L->CallInfo - L->BaseCI));
}

static int resumeError(lua_State *L, const char *msg) {
    L->Top = L->CallInfo->Base;
    LuaSetStringValue2S(L, L->Top, Lua::String::New(L, msg));
    LuaIncrTop(L);
    LuaUnlock(L);
    return LUA_ERRRUN;
}

LUA_API int lua_resume(lua_State *L, int nargs) {
    int status;
    LuaLock(L);
    if (L->Status != LUA_YIELD && (L->Status != 0 || L->CallInfo != L->BaseCI))
        return resumeError(L, "cannot resume non-suspended coroutine");
    if (L->NCCalls >= LUAI_MAXCCALLS)
        return resumeError(L, "C stack overflow");
    luai_userstateresume(L, nargs);
    lua_assert(L->ErrFunc == 0);
    L->BaseCCalls = ++L->NCCalls;
    status = Lua::Do::RawRunProtected(L, resume, L->Top - nargs);
    if (status != 0) {  /* error? */
        L->Status = cast_byte(status);  /* mark thread as `dead' */
        Lua::Do::SetErrorObject(L, status, L->Top);
        L->CallInfo->Top = L->Top;
    } else {
        lua_assert(L->NCCalls == L->BaseCCalls);
        status = L->Status;
    }
    --L->NCCalls;
    LuaUnlock(L);
    return status;
}

LUA_API int lua_yield(lua_State *L, int nResults) {
    luai_userstateyield(L, nResults);
    LuaLock(L);
    if (L->NCCalls > L->BaseCCalls)
        Lua::Debug::RunError(L, "attempt to yield across metaMethod/C-call boundary");
    L->Base = L->Top - nResults;  /* protect stack slots below */
    L->Status = LUA_YIELD;
    LuaUnlock(L);
    return -1;
}

int Lua::Do::PCall(lua_State *L, Lua::Do::PFunc func, void *u,
                   ptrdiff_t old_top, ptrdiff_t ef) {
    int status;
    unsigned short oldNCCalls = L->NCCalls;
    ptrdiff_t old_ci = LuaSaveCI(L, L->CallInfo);
    Lua::Byte old_allowHooks = L->AllowHook;
    ptrdiff_t old_errFunc = L->ErrFunc;
    L->ErrFunc = ef;
    status = Lua::Do::RawRunProtected(L, func, u);
    if (status != 0) {  /* an error occurred? */
        Lua::StkId oldTop = LuaRestoreStack(L, old_top);
        Lua::UpValue::Close(L, oldTop);  /* close eventual pending closures */
        Lua::Do::SetErrorObject(L, status, oldTop);
        L->NCCalls = oldNCCalls;
        L->CallInfo = LuaRestoreCI(L, old_ci);
        L->Base = L->CallInfo->Base;
        L->SavedPC = L->CallInfo->SavedPC;
        L->AllowHook = old_allowHooks;
        restoreStackLimit(L);
    }
    L->ErrFunc = old_errFunc;
    return status;
}

static void funcParser(lua_State *L, void *ud) {
    int i;
    Lua::Proto *tf;
    Lua::Closure *cl;
    struct Lua::Parser *p = cast(struct Lua::Parser *, ud);
    int c = Lua::ZIO::LookAhead(p->z);
    LuaGCCheckGC(L);
    tf = ((c == LUA_SIGNATURE[0]) ? Lua::Dumper::UnDump : Lua::Parser::Parse)(L, p->z,
                                                                              &p->buff, p->name);
    cl = Lua::LClosure::New(L, tf->NUpValues, LuaTableValue(LuaGlobalTable(L)));
    cl->AsLua.Func = tf;
    for (i = 0; i < tf->NUpValues; i++)  /* initialize eventual upValues */
        cl->AsLua.UpValues[i] = Lua::UpValue::New(L);
    LuaSetClosureValue(L, L->Top, cl);
    LuaIncrTop(L);
}

int Lua::Do::ProtectedParser(lua_State *L, Lua::ZIO *z, const char *name) {
    Lua::Parser p; // NOLINT
    int status;
    p.z = z;
    p.name = name;
    LuaZBufferInit(L, &p.buff);
    status = Lua::Do::PCall(L, funcParser, &p, LuaSaveStack(L, L->Top), L->ErrFunc);
    LuaZBufferFree(L, &p.buff);
    return status;
}


