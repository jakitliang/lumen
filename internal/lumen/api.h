/*!
 * @brief Lumen API Helper
 * @author Jakit
 * @date 2025/6/14
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#ifndef LUMEN_API_H
#define LUMEN_API_H

#include "lumen/object.h"

/* limit for table tag-method chains (to avoid loops) */
#define LumenMaxTagLoop    100

#define LumenApiCheckElementCount(L, n)    LumenApiCheck(L, (n) <= (L->Top - L->Base))

#define LumenApiCheckValidIndex(L, i)    LumenApiCheck(L, (i) != Lumen::NilObject)

#define LumenApiIncrTop(L) \
LumenDo(                \
    LumenApiCheck(L, L->Top < L->CallInfo->Top); \
    L->Top++;         \
)

#define LumenApiAdjustResults(L, nRes) \
LumenDo(                       \
    if (nRes == Lumen::RetMul && L->Top >= L->CallInfo->Top) \
        L->CallInfo->Top = L->Top;                           \
)

#define LumenApiCheckResults(L, na, nr) \
     LumenApiCheck(L, (nr) == Lumen::RetMul || (L->CallInfo->Top - L->Top >= (nr) - (na)))

/* corresponding test */
#define LumenApiIsValid(o)    ((o) != Lumen::NilObject)

/* test for pseudo index */
#define LumenApiIsPseudo(i)      ((i) <= Lumen::RegistryIndex)

/* test for upvalue */
#define LumenApiIsUpValue(i)     ((i) < Lumen::RegistryIndex)

#define LumenApiEqualObj(L, o1, o2)  (ttisequal(o1, o2) && luaV_equalobj_(L, o1, o2))

#endif //LUMEN_API_H
