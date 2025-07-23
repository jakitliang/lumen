/*!
 * @brief Meta Methods
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_TM_H
#define LUMEN_TM_H

#include "lumen/object.h"

namespace Lumen::MetaMethod {
    const Lumen::Object *Get(Lumen::Table *events, Lumen::MetaMethod::Name event, Lumen::String *name);

    const Lumen::Object *GetByObject(Lumen::State *L, const Lumen::Object *o,
                                     Lumen::MetaMethod::Name event);

    void Init(Lumen::State *L);

    LUAI_DATA const char *const TypeNames[];
}

#define LumenMetaMethodGetGlobalFast(g, et, e) ((et) == nullptr ? nullptr : \
    ((et)->Flags & (1u<<(e))) ? nullptr : Lumen::MetaMethod::Get(et, e, (g)->MetatableName[e]))

#define LumenMetaMethodGetFast(l, et, e)    LumenMetaMethodGetGlobalFast(LumenGlobalState(l), et, e)

/*
** function to be used with macro "LumenMetaMethodGetFast": optimized for absence of
** tag methods
*/
inline const Lumen::Object *Lumen::MetaMethod::Get(Lumen::Table *events, Lumen::MetaMethod::Name event, Lumen::String *name) {
    const Lumen::Object *tm = Lumen::Table::GetString(events, name);
    LumenAssert(event <= Lumen::MetaMethod::NameEQ);
    if (tm->IsNil()) {  /* no tag method? */
        events->Flags |= cast_byte(1u << event);  /* cache this fact */
        return nullptr;
    } else return tm;
}

#endif
