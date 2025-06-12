/*!
 * @brief Auxiliary functions from Debug Interface module
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_DEBUG_H
#define LUMEN_DEBUG_H


#include "lumen/state.h"


#define LumenDebugPCRel(pc, p)         (cast(int, (pc) - (p)->Code) - 1)
#define LumenDebugGetLine(f, pc)       (((f)->LineInfo) ? (f)->LineInfo[pc] : 0)
#define LumenDebugResetHookCount(L)    (L->HookCount = L->BaseHookCount)

namespace Lumen::Debug {
    void TypeError(Lumen::State *L, const Lumen::Object *o,
                               const char *opname);

    void ConcatError(Lumen::State *L, Lumen::Value p1, Lumen::Value p2);

    void ArithError(Lumen::State *L, const Lumen::Object *p1,
                                const Lumen::Object *p2);

    int OrderError(Lumen::State *L, const Lumen::Object *p1,
                               const Lumen::Object *p2);

    void RunError(Lumen::State *L, const char *fmt, ...);

    void ErrorMessage(Lumen::State *L);

    int CheckCode(const Lumen::Proto *pt);

    int CheckOpenOP(Lumen::Instruction i);

    int GetInfo(Lumen::State *L, const char *what, Lumen::DebugInfo *ar,
                Lumen::Closure *f, Lumen::CallInfo *ci);

    void CollectValidLines(Lumen::State *L, Lumen::Closure *f);
}

#endif
