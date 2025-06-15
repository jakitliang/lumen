/*!
 * @brief Version of Lumen
 * @author Jakit
 * @date 2025/6/16
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD License.
 */

#define LUMEN_COPYRIGHT    "Copyright (C) 2025 Jakit Liang. https://github.com/jakitliang/lumen"
#define LUMEN_AUTHORS      "Jakit Liang"

#ifndef LUMEN_VERSION_H
#define LUMEN_VERSION_H

#define LUMEN_VERSION_MAJOR_N      1
#define LUMEN_VERSION_MINOR_N      1
#define LUMEN_VERSION_RELEASE_N    10

#define LUMEN_RELEASE  "Lumen " \
LUA_TO_STRING(LUMEN_VERSION_MAJOR_N) "." \
LUA_TO_STRING(LUMEN_VERSION_MINOR_N) "." \
LUA_TO_STRING(LUMEN_VERSION_RELEASE_N)

#endif //LUMEN_VERSION_H
