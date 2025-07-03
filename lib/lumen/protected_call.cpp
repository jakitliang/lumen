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
#include "lumen/api.h"
#include "lumen/common.inl"

void Lumen::ProtectedCall::Call(Lumen::State *L, void *ud) {
    ProtectedCall *c = cast(ProtectedCall *, ud);
    Lumen::Do::Call(L, c->Func, c->NResults);
}

void Lumen::ProtectedCCall::Call(Lumen::State *L, void *ud) {
    ProtectedCCall *c = cast(ProtectedCCall *, ud);
    Lumen::Closure *cl;
    cl = Lumen::CClosure::New(L, 0, L->GetCurrentEnv());
    cl->AsC.Func = c->Func;
    L->Top->SetClosure(L, cl);  /* push function */
    LumenApiIncrTop(L);
    L->Top->SetLUData(c->UData);  /* push only argument */
    LumenApiIncrTop(L);
    Lumen::Do::Call(L, L->Top - 2, 0);
}
