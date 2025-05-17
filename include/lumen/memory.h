/*!
 * @brief Memory Utility
 * @author Jakit
 * @date 2025/5/16
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#ifndef LUMEN_MEMORY_H
#define LUMEN_MEMORY_H

#include <cstddef>

namespace Lumen::Memory {
    const char *Find(const char *cStr1, size_t len1, const char *cStr2, size_t len2);

    void *Alloc(void *userData, void *ptr, size_t originSize, size_t newSize);
}

#endif //LUMEN_MEMORY_H
