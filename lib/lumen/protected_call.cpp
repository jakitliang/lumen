/*!
 * @brief protected_call
 * @author Jakit
 * @date 2025/6/7
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#include "lumen/protected_call.h"
#include "lumen/do.h"
#include "lumen/gc.h"

#define apiIncrTop(L) \
LumenDo(                \
    LumenApiCheck(L, L->Top < L->CallInfo->Top); \
    L->Top++;         \
)

static Lumen::Table *getCurEnv(Lumen::State *L) {
    if (L->CallInfo == L->BaseCI)  /* no enclosing function? */
        return LumenTableValue(LumenGlobalTable(L));  /* use global table as environment */
    else {
        Lumen::Closure *func = LumenCurFunc(L);
        return func->AsC.Env;
    }
}

void Lumen::ProtectedCall::Call(Lumen::State *L, void *ud) {
    ProtectedCall *c = cast(ProtectedCall *, ud);
    Lumen::Do::Call(L, c->Func, c->NResults);
}

void Lumen::ProtectedCCall::Call(Lumen::State *L, void *ud) {
    ProtectedCCall *c = cast(ProtectedCCall *, ud);
    Lumen::Closure *cl;
    cl = Lumen::CClosure::New(L, 0, getCurEnv(L));
    cl->AsC.Func = c->Func;
    LumenSetClosureValue(L, L->Top, cl);  /* push function */
    apiIncrTop(L);
    LumenSetLUDataValue(L->Top, c->UData);  /* push only argument */
    apiIncrTop(L);
    Lumen::Do::Call(L, L->Top - 2, 0);
}
