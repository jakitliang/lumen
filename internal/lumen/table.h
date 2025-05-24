/*!
 * @brief Lua tables (hash table)
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef LUMEN_TABLE_H
#define LUMEN_TABLE_H

#include "lumen/object.h"

#define LumenTableGetNode(t,i)       (&(t)->Nodes[i])
#define LumenTableGetKey(n)          (&(n)->Key.KeyNext)
#define LumenTableGetValue(n)        (&(n)->Value)
#define LumenTableGetNext(n)         ((n)->Key.KeyNext.Next)
#define LumenTableKey2KeyValue(n)    (&(n)->Key.KeyValue)

#endif
