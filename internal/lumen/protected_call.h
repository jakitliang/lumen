/*!
 * @brief Protected Call
 * @author Jakit
 * @date 2025/6/7
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#ifndef LUMEN_PROTECTED_CALL_H
#define LUMEN_PROTECTED_CALL_H

#include "lumen/object.h"

namespace Lumen {
    /**
     * Execute a protected call.
     */
    struct ProtectedCall {
        Lumen::Value Func;
        int NResults;

        static void Call(Lumen::State *L, void *ud);
    };

    /**
     * Execute a protected C call.
     */
    struct ProtectedCCall {
        Lumen::Delegate Func;
        void *UData;

        static void Call(Lumen::State *L, void *ud);
    };
}


#endif //LUMEN_PROTECTED_CALL_H
