/*!
 * @brief Tagged methods
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUA_TM_H
#define LUA_TM_H


#include "lua/object.h"

namespace Lua::TM {
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

    const Lua::Value *Get(Lua::Table *events, Lua::TM::Name event, Lua::String *ename);

    const Lua::Value *GetByObject(Lua::State *L, const Lua::Value *o,
                              Lua::TM::Name event);

    void Init(Lua::State *L);

    LUAI_DATA const char *const TypeNames[];
}

#define LuaTMGetGlobalFast(g, et, e) ((et) == NULL ? NULL : \
    ((et)->Flags & (1u<<(e))) ? NULL : Lua::TM::Get(et, e, (g)->MetatableName[e]))

#define LuaTMGetFast(l, et, e)    LuaTMGetGlobalFast(LuaGlobal(l), et, e)

#endif
