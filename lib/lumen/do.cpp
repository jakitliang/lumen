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

#include "lumen/debug.h"
#include "lumen/do.h"
#include "lumen/memory.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/opcodes.h"
#include "lumen/parser.h"
#include "lumen/state.h"
#include "lumen/string.h"
#include "lumen/tm.h"
#include "lumen/undump.h"
#include "lumen/vm.h"
#include "lumen/zio.h"

/*
** {======================================================
** Error-recovery functions
** =======================================================
*/


/* chain list of long jump buffers */
struct Lumen::LongJump {
    Lumen::LongJump *Previous;
    LUA_JUMP_BUFF b;
    volatile int Status;  /* error code */
};


void Lumen::Do::SetErrorObject(Lumen::State *L, int errcode, Lumen::Value oldTop) {
    switch (errcode) {
        case Lumen::RetErrMem: {
            LumenSetStringValue2S(L, oldTop, LumenStringNewLiteral(L, LUA_MEM_ERR_MSG));
            break;
        }
        case Lumen::RetErr: {
            LumenSetStringValue2S(L, oldTop, LumenStringNewLiteral(L, "error in error handling"));
            break;
        }
        case Lumen::RetErrSyntax:
        case Lumen::RetErrRun: {
            LumenSetObjectS2S(L, oldTop, L->Top - 1);  /* error message on current top */
            break;
        }
    }
    L->Top = oldTop + 1;
}


static inline void restoreStackLimit(Lumen::State *L) {
    LumenAssert(L->StackLast - L->Stack == L->StackCount - Lumen::ExtraStack - 1);
    if (L->BaseCICount > LUA_MAX_CALLS) {  /* there was an overflow? */
        int inuse = cast_int(L->CallInfo - L->BaseCI);
        if (inuse + 1 < LUA_MAX_CALLS)  /* can `undo' overflow? */
            Lumen::Do::ReAllocCI(L, LUA_MAX_CALLS);
    }
}


static void resetStack(Lumen::State *L, int status) {
    L->CallInfo = L->BaseCI;
    L->Base = L->CallInfo->Base;
    Lumen::UpValue::Close(L, L->Base);  /* close eventual pending closures */
    Lumen::Do::SetErrorObject(L, status, L->Base);
    L->NCCalls = L->BaseCCalls;
    L->AllowHook = 1;
    restoreStackLimit(L);
    L->ErrFunc = 0;
    L->ErrorJmp = nullptr;
}


void Lumen::Do::Throw(Lumen::State *L, int errcode) {
    if (L->ErrorJmp) {
        L->ErrorJmp->Status = errcode;
        LUA_THROW(L, L->ErrorJmp);
    } else {
        L->Status = cast_byte(errcode);
        if (LumenGlobalState(L)->Panic) {
            resetStack(L, errcode);
            LumenUnlock(L);
            LumenGlobalState(L)->Panic(reinterpret_cast<Lumen::IState *>(L));
        }
        exit(EXIT_FAILURE);
    }
}


int Lumen::Do::RawRunProtected(Lumen::State *L, Lumen::Do::PFunc f, void *ud) {
    Lumen::LongJump lj; // NOLINT
    lj.Status = 0;
    lj.Previous = L->ErrorJmp;  /* chain new error handler */
    L->ErrorJmp = &lj;
    LUA_TRY(L, &lj,
             (*f)(L, ud);
    )
    L->ErrorJmp = lj.Previous;  /* restore old error handler */
    return lj.Status;
}

/* }====================================================== */


static void correctStack(Lumen::State *L, Lumen::Object *oldStack) {
    Lumen::CallInfo *ci;
    Lumen::GCObject *up;
    L->Top = (L->Top - oldStack) + L->Stack;
    for (up = L->OpenedUpValue; up != nullptr; up = up->AsObject.GCNext)
        up->ToUpValue()->SelfValue = (up->ToUpValue()->SelfValue - oldStack) + L->Stack;
    for (ci = L->BaseCI; ci <= L->CallInfo; ci++) {
        ci->Top = (ci->Top - oldStack) + L->Stack;
        ci->Base = (ci->Base - oldStack) + L->Stack;
        ci->Func = (ci->Func - oldStack) + L->Stack;
    }
    L->Base = (L->Base - oldStack) + L->Stack;
}


void Lumen::Do::ReAllocStack(Lumen::State *L, int newSize) {
    Lumen::Object *oldStack = L->Stack;
    int realSize = newSize + 1 + (int) Lumen::ExtraStack;
    LumenAssert(L->StackLast - L->Stack == L->StackCount - Lumen::ExtraStack - 1);
    LumenMemoryReAllocVector(L, L->Stack, L->StackCount, realSize, Lumen::Object);
    L->StackCount = realSize;
    L->StackLast = L->Stack + newSize;
    correctStack(L, oldStack);
}


void Lumen::Do::ReAllocCI(Lumen::State *L, int newSize) {
    Lumen::CallInfo *oldCI = L->BaseCI;
    LumenMemoryReAllocVector(L, L->BaseCI, L->BaseCICount, newSize, Lumen::CallInfo);
    L->BaseCICount = newSize;
    L->CallInfo = (L->CallInfo - oldCI) + L->BaseCI;
    L->EndCI = L->BaseCI + L->BaseCICount - 1;
}


void Lumen::Do::GrowStack(Lumen::State *L, int n) {
    if (n <= L->StackCount)  /* double size is enough? */
        Lumen::Do::ReAllocStack(L, 2 * L->StackCount);
    else
        Lumen::Do::ReAllocStack(L, L->StackCount + n);
}


static Lumen::CallInfo *growCI(Lumen::State *L) {
    if (L->BaseCICount > LUA_MAX_CALLS)  /* overflow while handling overflow? */
        Lumen::Do::Throw(L, Lumen::RetErr);
    else {
        Lumen::Do::ReAllocCI(L, 2 * L->BaseCICount);
        if (L->BaseCICount > LUA_MAX_CALLS)
            Lumen::Debug::RunError(L, "stack overflow");
    }
    return ++L->CallInfo;
}


void Lumen::Do::CallHook(Lumen::State *L, int event, int line) {
    Lumen::Hook hook = L->Hook;
    if (hook && L->AllowHook) {
        Lumen::Integer top = LumenSaveStack(L, L->Top);
        Lumen::Integer ci_top = LumenSaveStack(L, L->CallInfo->Top);
        Lumen::DebugInfo ar;
        ar.Event = event;
        ar.CurrentLine = line;
        if (event == Lumen::HookTailRet)
            ar.CurrentCI = 0;  /* tail call; no debug information about it */
        else
            ar.CurrentCI = cast_int(L->CallInfo - L->BaseCI);
        LumenDoCheckStack(L, Lumen::MinStack);  /* ensure minimum stack size */
        L->CallInfo->Top = L->Top + Lumen::MinStack;
        LumenAssert(L->CallInfo->Top <= L->StackLast);
        L->AllowHook = 0;  /* cannot call hooks inside a hook */
        LumenUnlock(L);
        (*hook)(reinterpret_cast<Lumen::IState *>(L), &ar);
        LumenLock(L);
        LumenAssert(!L->AllowHook);
        L->AllowHook = 1;
        L->CallInfo->Top = LumenRestoreStack(L, ci_top);
        L->Top = LumenRestoreStack(L, top);
    }
}


static Lumen::Value adjustVarargs(Lumen::State *L, Lumen::Proto *p, int actual) {
    int i;
    int nFixArgs = p->NUmParams;
    Lumen::Table *hashTable = nullptr;
    Lumen::Value base, fixed;
    for (; actual < nFixArgs; ++actual)
        (L->Top++)->SetNil();
#if defined(LUA_COMPAT_VARARG)
    if (p->IsVararg & Lumen::Proto::VarargIsNeedsArg) { /* compat. with old-style vararg? */
        int nVar = actual - nFixArgs;  /* number of extra arguments */
        LumenAssert(p->IsVararg & Lumen::Proto::VarargHasArg);
        L->CheckGC();
        LumenDoCheckStack(L, p->MaxStackSize);
        hashTable = Lumen::Table::New(L, nVar, 1);  /* create `arg` table */
        for (i = 0; i < nVar; i++)  /* put extra arguments into `arg` table */
            LumenSetObject2N (L, Lumen::Table::SetNum(L, hashTable, i + 1), L->Top - nVar + i);
        /* store counter in field `n' */
        Lumen::Table::SetString(L, hashTable, LumenStringNewLiteral(L, "n"))->SetNumber(cast_num(nVar));
    }
#endif
    /* move fixed parameters to final position */
    fixed = L->Top - actual;  /* first fixed argument */
    base = L->Top;  /* final position of first argument */
    for (i = 0; i < nFixArgs; i++) {
        LumenSetObjectS2S(L, L->Top++, fixed + i);
        (fixed + i)->SetNil();
    }
    /* add `arg' parameter */
    if (hashTable) {
        (L->Top++)->SetTable(L, hashTable);
        LumenAssert(LumenObject2GCObject(hashTable)->IsWhite());
    }
    return base;
}


static Lumen::Value tryFuncTM(Lumen::State *L, Lumen::Value func) {
    const Lumen::Object *tm = Lumen::MetaMethod::GetByObject(L, func, Lumen::MetaMethod::NameCall);
    Lumen::Value p;
    Lumen::Integer funcR = LumenSaveStack(L, func);
    if (!tm->IsFunction())
        Lumen::Debug::TypeError(L, func, "call");
    /* Open a hole inside the stack at `func' */
    for (p = L->Top; p > func; p--) LumenSetObjectS2S (L, p, p - 1);
    LumenIncrTop(L);
    func = LumenRestoreStack(L, funcR);  /* previous call may change stack */
    LumenSetObject2S(L, func, tm);  /* tag method is the new function to be called */
    return func;
}


#define incrCI(L) \
  ((L->CallInfo == L->EndCI) ? growCI(L) : \
   (LumenCondHardStackTests(Lumen::Do::ReAllocCI(L, L->BaseCICount)), ++L->CallInfo))


int Lumen::Do::PreCall(Lumen::State *L, Lumen::Value func, int nResults) {
    Lumen::LClosure *cl;
    Lumen::Integer funcR;
    if (!func->IsFunction()) /* `func` is not a function? */
        func = tryFuncTM(L, func);  /* check the `function` tag method */
    funcR = LumenSaveStack(L, func);
    cl = &func->GetClosure()->AsLua;
    L->CallInfo->SavedPC = L->SavedPC;
    if (!cl->IsC) {  /* Lua function? prepare its call */
        Lumen::CallInfo *ci;
        Lumen::Value st, base;
        Lumen::Proto *p = cl->Func;
//        LumenDoCheckStack(L, p->MaxStackSize);
        LumenDoCheckStack(L, p->MaxStackSize + p->NUmParams);
        func = LumenRestoreStack(L, funcR);
        if (!p->IsVararg) {  /* no varargs? */
            base = func + 1;
            if (L->Top > base + p->NUmParams)
                L->Top = base + p->NUmParams;
        } else {  /* vararg function */
            int nargs = cast_int(L->Top - func) - 1;
            base = adjustVarargs(L, p, nargs);
            func = LumenRestoreStack(L, funcR);  /* previous call may change the stack */
        }
        ci = incrCI(L);  /* now `enter` new function */
        ci->Func = func;
        L->Base = ci->Base = base;
        ci->Top = L->Base + p->MaxStackSize;
        LumenAssert(ci->Top <= L->StackLast);
        L->SavedPC = p->Code;  /* starting point */
        ci->NTailCalls = 0;
        ci->NResults = nResults;
        for (st = L->Top; st < ci->Top; st++)
            st->SetNil();
        L->Top = ci->Top;
        if (L->HookMask & Lumen::HookMaskCall) {
            L->SavedPC++;  /* hooks assume 'pc' is already incremented */
            Lumen::Do::CallHook(L, Lumen::HookCall, -1);
            L->SavedPC--;  /* correct 'pc' */
        }
        return Lumen::Do::PCRetLua;
    } else {  /* if is a C function, call it */
        Lumen::CallInfo *ci;
        int n;
        LumenDoCheckStack(L, Lumen::MinStack);  /* ensure minimum stack size */
        ci = incrCI(L);  /* now `enter` new function */
        ci->Func = LumenRestoreStack(L, funcR);
        L->Base = ci->Base = ci->Func + 1;
        ci->Top = L->Top + Lumen::MinStack;
        LumenAssert(ci->Top <= L->StackLast);
        ci->NResults = nResults;
        if (L->HookMask & Lumen::HookMaskCall)
            Lumen::Do::CallHook(L, Lumen::HookCall, -1);
        LumenUnlock(L);
        n = (*L->GetCurrentFunction()->AsC.Func)(reinterpret_cast<Lumen::IState *>(L));  /* do the actual call */
        LumenLock(L);
        if (n < 0)  /* yielding? */
            return Lumen::Do::PCRetYield;
        else {
            Lumen::Do::PosCall(L, L->Top - n);
            return Lumen::Do::PCRetC;
        }
    }
}

static Lumen::Value callRetHooks(Lumen::State *L, Lumen::Value firstResult) {
    Lumen::Integer fr = LumenSaveStack(L, firstResult);  /* next call may change stack */
    Lumen::Do::CallHook(L, Lumen::HookRet, -1);
    if (L->CallInfo->IsLuaFunction()) {  /* Lua function? */
        while ((L->HookMask & Lumen::HookMaskRet) && L->CallInfo->NTailCalls--) /* tail calls */
            Lumen::Do::CallHook(L, Lumen::HookTailRet, -1);
    }
    return LumenRestoreStack(L, fr);
}

int Lumen::Do::PosCall(Lumen::State *L, Lumen::Value firstResult) {
    Lumen::Value res;
    int wanted, i;
    Lumen::CallInfo *ci;
    if (L->HookMask & Lumen::HookMaskRet)
        firstResult = callRetHooks(L, firstResult);
    ci = L->CallInfo--;
    res = ci->Func;  /* res == final position of 1st result */
    wanted = ci->NResults;
    L->Base = (ci - 1)->Base;  /* restore base */
    L->SavedPC = (ci - 1)->SavedPC;  /* restore SavedPC */
    /* move results to correct place */
    for (i = wanted; i != 0 && firstResult < L->Top; i--)
        LumenSetObjectS2S (L, res++, firstResult++);
    while (i-- > 0)
        (res++)->SetNil();
    L->Top = res;
    return (wanted - Lumen::RetMul);  /* 0 iff wanted == Lumen::RetMul */
}

/*
** Call a function (C or Lua). The function to be called is at *func.
** The arguments are on the stack, right after the function.
** When returns, all the results are on the stack, starting at the original
** function position.
*/
void Lumen::Do::Call(Lumen::State *L, Lumen::Value func, int nResults) {
    if (++L->NCCalls >= LUA_MAX_C_CALLS) {
        if (L->NCCalls == LUA_MAX_C_CALLS)
            Lumen::Debug::RunError(L, "C stack overflow");
        else if (L->NCCalls >= (LUA_MAX_C_CALLS + (LUA_MAX_C_CALLS >> 3)))
            Lumen::Do::Throw(L, Lumen::RetErr);  /* error while handing stack error */
    }
    if (Lumen::Do::PreCall(L, func, nResults) == Lumen::Do::PCRetLua)  /* is a Lua function? */
        Lumen::VM::Execute(L, 1);  /* call it */
    L->NCCalls--;
    L->CheckGC();
}

void Lumen::Do::Resume(Lumen::State *L, void *ud) {
    Lumen::Value firstArg = cast(Lumen::Value, ud);
    Lumen::CallInfo *ci = L->CallInfo;
    if (L->Status == 0) {  /* start coroutine? */
        LumenAssert(ci == L->BaseCI && firstArg > L->Base);
        if (Lumen::Do::PreCall(L, firstArg - 1, Lumen::RetMul) != Lumen::Do::PCRetLua)
            return;
    } else {  /* resuming from previous yield */
        LumenAssert(L->Status == Lumen::RetYield);
        L->Status = 0;
        if (!ci->IsLuaFunction()) {  /* `common' yield? */
            /* finish interrupted execution of `Lumen::OpCodeCall' */
            LumenAssert(LumenOpCodeGet(*((ci - 1)->SavedPC - 1)) == Lumen::OpCodeCall ||
                        LumenOpCodeGet(*((ci - 1)->SavedPC - 1)) == Lumen::OpCodeTailCall);
            if (Lumen::Do::PosCall(L, firstArg))  /* complete it... */
                L->Top = L->CallInfo->Top;  /* and correct top if not multiple results */
        } else  /* yielded inside a hook: just continue its execution */
            L->Base = L->CallInfo->Base;
    }
    Lumen::VM::Execute(L, cast_int(L->CallInfo - L->BaseCI));
}

int Lumen::Do::ResumeError(Lumen::State *L, const char *msg) {
    L->Top = L->CallInfo->Base;
    LumenSetStringValue2S(L, L->Top, Lumen::String::New(L, msg));
    LumenIncrTop(L);
    LumenUnlock(L);
    return Lumen::RetErrRun;
}

int Lumen::Do::PCall(Lumen::State *L, Lumen::Do::PFunc func, void *u,
                     Lumen::Integer old_top, Lumen::Integer ef) {
    int status;
    unsigned short oldNCCalls = L->NCCalls;
    Lumen::Integer old_ci = LumenSaveCI(L, L->CallInfo);
    Lumen::Byte old_allowHooks = L->AllowHook;
    Lumen::Integer old_errFunc = L->ErrFunc;
    L->ErrFunc = ef;
    status = Lumen::Do::RawRunProtected(L, func, u);
    if (status != 0) {  /* an error occurred? */
        Lumen::Value oldTop = LumenRestoreStack(L, old_top);
        Lumen::UpValue::Close(L, oldTop);  /* close eventual pending closures */
        Lumen::Do::SetErrorObject(L, status, oldTop);
        L->NCCalls = oldNCCalls;
        L->CallInfo = LumenRestoreCI(L, old_ci);
        L->Base = L->CallInfo->Base;
        L->SavedPC = L->CallInfo->SavedPC;
        L->AllowHook = old_allowHooks;
        restoreStackLimit(L);
    }
    L->ErrFunc = old_errFunc;
    return status;
}

static void funcParser(Lumen::State *L, void *ud) {
    int i;
    Lumen::Proto *tf;
    Lumen::Closure *cl;
    struct Lumen::Parser *p = cast(struct Lumen::Parser *, ud);
    int c = Lumen::ZIO::LookAhead(p->z);
    L->CheckGC();
    tf = ((c == LUA_SIGNATURE[0]) ? Lumen::Dumper::UnDump : Lumen::Parser::Parse)(L, p->z,
                                                                                  &p->buff, p->name);
    cl = Lumen::LClosure::New(L, tf->NUpValues, LumenGlobalTable(L)->GetTable());
    cl->AsLua.Func = tf;
    for (i = 0; i < tf->NUpValues; i++)  /* initialize eventual upValues */
        cl->AsLua.UpValues[i] = Lumen::UpValue::New(L);
    L->Top->SetClosure(L, cl);
    LumenIncrTop(L);
}

int Lumen::Do::ProtectedParser(Lumen::State *L, Lumen::ZIO *z, const char *name) {
    Lumen::Parser p; // NOLINT
    int status;
    p.z = z;
    p.name = name;
    LumenZBufferInit(L, &p.buff);
    status = Lumen::Do::PCall(L, funcParser, &p, LumenSaveStack(L, L->Top), L->ErrFunc);
    LumenZBufferFree(L, &p.buff);
    return status;
}


