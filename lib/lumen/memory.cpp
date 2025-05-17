/*!
 * @brief Memory Utility
 * @author Jakit
 * @date 2025/5/16
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#include "lumen/memory.h"
#include <cstring>

const char *Lumen::Memory::Find(const char *cStr1, size_t len1, const char *cStr2, size_t len2) {
    if (len2 == 0) return cStr1;  /* empty strings are everywhere */
    else if (len2 > len1) return nullptr;  /* avoids a negative `len1` */
    else {
        const char *found;  /* to search for a `*s2' inside `cStr1` */
        len2--;  /* 1st char will be checked by `memchr` */
        len1 = len1 - len2;  /* `s2` cannot be found after that */
        while (len1 > 0 && (found = (const char *) memchr(cStr1, *cStr2, len1)) != nullptr) {
            found++;   /* 1st char is already checked */
            if (memcmp(found, cStr2 + 1, len2) == 0)
                return found - 1;
            else {  /* correct `len1` and `cStr1` to try again */
                len1 -= found - cStr1;
                cStr1 = found;
            }
        }
        return nullptr;  /* not found */
    }
}
