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

    const Lumen::Object *Get(Lumen::Table *events, Lumen::TM::Name event, Lumen::String *name);

    const Lumen::Object *GetByObject(Lumen::State *L, const Lumen::Object *o,
                              Lumen::TM::Name event);

    void Init(Lumen::State *L);

    LUAI_DATA const char *const TypeNames[];
}

#define LumenTMGetGlobalFast(g, et, e) ((et) == NULL ? NULL : \
    ((et)->Flags & (1u<<(e))) ? NULL : Lumen::TM::Get(et, e, (g)->MetatableName[e]))

#define LumenTMGetFast(l, et, e)    LumenTMGetGlobalFast(LumenGlobalState(l), et, e)

/*
** function to be used with macro "LumenTMGetFast": optimized for absence of
** tag methods
*/
inline const Lumen::Object *Lumen::TM::Get(Lumen::Table *events, Lumen::TM::Name event, Lumen::String *name) {
    const Lumen::Object *tm = Lumen::Table::GetString(events, name);
    LumenAssert(event <= Lumen::TM::NameEQ);
    if (LumenTypeIsNil(tm)) {  /* no tag method? */
        events->Flags |= cast_byte(1u << event);  /* cache this fact */
        return nullptr;
    } else return tm;
}

#endif
