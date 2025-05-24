/*!
 * @brief Tagged methods
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_TM_H
#define LUMEN_TM_H


#include "lumen/object.h"

namespace Lumen::TM {
    /**
     * Tagged Method Name\n
     * WARNING: if you change the order of this enumeration,
     * grep "ORDER TM"
     */
    typedef int Name;
    enum {
        NameIndex,
        NameNewIndex,
        NameGC,
        NameMode,
        NameEQ,  /* last tag method with `fast' access */
        NameAdd,
        NameSub,
        NameMul,
        NameDiv,
        NameMod,
        NamePow,
        NameUnm,
        NameLen,
        NameLT,
        NameLE,
        NameConcat,
        NameCall,
        NameN        /* number of elements in the enum */
    };

    const Lumen::Value *Get(Lumen::Table *events, Lumen::TM::Name event, Lumen::String *ename);

    const Lumen::Value *GetByObject(Lumen::State *L, const Lumen::Value *o,
                              Lumen::TM::Name event);

    void Init(Lumen::State *L);

    LUAI_DATA const char *const TypeNames[];
}

#define LumenTMGetGlobalFast(g, et, e) ((et) == NULL ? NULL : \
    ((et)->Flags & (1u<<(e))) ? NULL : Lumen::TM::Get(et, e, (g)->MetatableName[e]))

#define LumenTMGetFast(l, et, e)    LumenTMGetGlobalFast(LumenGlobal(l), et, e)

#endif
