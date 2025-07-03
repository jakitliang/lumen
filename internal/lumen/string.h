/*!
 * @brief String table (keep all strings handled by Lua)
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_STRING_H
#define LUMEN_STRING_H

#include "lumen/gc.h"
#include "lumen/object.h"
#include "lumen/state.h"

#define LumenStringSize(s)    (sizeof(Lumen::String)+((s)->Length+1)*sizeof(char))

#define LumenUserdataSize(u)    (sizeof(Lumen::Userdata)+(u)->Length)

#define LumenStringNewLiteral(L, s) \
    (Lumen::String::New(L, "" s, (sizeof(s)/sizeof(char))-1))

#define LumenStringFix(s)    Lumen::GC::LSetBit((s)->Marked, Lumen::GC::MarkFixedBit)

#endif
