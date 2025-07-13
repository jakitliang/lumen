/*!
 * @brief Lumen common
 * @author Jakit
 * @date 2025/7/8
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#define LUA_CORE

#include "lumen/object.h"
#include "lumen/state.h"
#include "lumen/api.h"
#include "lumen/common.inl"

#include "lumen.h"

// NOLINTNEXTLINE
#define ToLumen(L) reinterpret_cast<Lumen::State *>(L)

Lumen::IState *Lumen::Open() {
    return Lumen::IState::New();
}

void Lumen::Close(Lumen::IState *&state) {
    auto L = ToLumen(state);
    if (L != nullptr) {
        Lumen::State::Close(L);
    }
    state = nullptr;
}

void Lumen::XMove(Lumen::IState *fromL, Lumen::IState *toL, int n) {
    auto from = ToLumen(fromL);
    auto to = ToLumen(toL);
    int i;
    if (from == to) return;
    LumenLock(to);
    LumenApiCheckElementCount(from, n);
    LumenApiCheck(from, LumenGlobalState(from) == LumenGlobalState(to));
    LumenApiCheck(from, to->CallInfo->Top - to->Top >= n);
    from->Top -= n;
    for (i = 0; i < n; i++) {
        LumenSetObject2S(to, to->Top++, from->Top + i);
    }
    LumenUnlock(to);
}

void Lumen::SetLevel(Lumen::IState *fromL, Lumen::IState *toL) {
    auto from = ToLumen(fromL);
    auto to = ToLumen(toL);
    to->NCCalls = from->NCCalls;
}
