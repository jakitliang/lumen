cmake_minimum_required(VERSION 3.15)
project(lumen
    VERSION 1.0.0
    DESCRIPTION "Light the way of Lua.")

set(CMAKE_CXX_STANDARD 17)

# ----- User Definition -----

option(LUA_BUILD_STATIC "Build shared library of Lua" OFF)
option(LUA_SKIP_INSTALL "Build shared library of Lua" OFF)
option(LUA_USE_API_CHECK "Build with develop mode" OFF)
option(LUA_USE_CORE_DEBUG "Build with develop mode" OFF)
option(LUA_USE_INTERPRETER "Build with interpreter" ON)
option(LUA_USE_COMPILER "Build with compiler" ON)
option(LUA_USE_TEST "Build with test" OFF)
option(LUA_USE_COMPAT "Build with Lua 5.1 compat" OFF)
option(LUA_USE_HP_HASH "Build with High performance Hash" ON)

# ----- Lumen -----

set(LUMEN_BASE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LUMEN_BASE_BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(LUA_PRODUCT_NUMBER 1)
set(LUA_PRODUCT_VERSION 3)
set(LUA_PRODUCT_PATCH 2)
set(LUA_PRODUCT_BUILD 1)
set(LUA_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/etc/lumen.ico")
set(LUA_PRODUCT_RESOURCE "")

if (NOT DEFINED LUA_BUILD_TYPE)
    if (LUA_BUILD_STATIC)
        set(LUA_BUILD_TYPE "STATIC")
    else ()
        set(LUA_BUILD_TYPE "SHARED")
    endif ()
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "aix")
    list(APPEND LUA_COMPILE_DEF LUA_USE_POSIX LUA_USE_DLOPEN)
    list(APPEND LUA_COMPILE_OPT "")
    list(APPEND LUA_LINK_LIBS dl)
elseif (CMAKE_SYSTEM_NAME MATCHES "freebsd")
    list(APPEND LUA_COMPILE_DEF LUA_USE_LINUX)
    list(APPEND LUA_COMPILE_OPT "")
    list(APPEND LUA_LINK_LIBS readline)
elseif (CMAKE_SYSTEM_NAME MATCHES "bsd")
    list(APPEND LUA_COMPILE_DEF LUA_USE_POSIX LUA_USE_DLOPEN)
    list(APPEND LUA_COMPILE_OPT "")
    list(APPEND LUA_LINK_LIBS "")
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
    list(APPEND LUA_COMPILE_DEF LUA_USE_LINUX)
    list(APPEND LUA_COMPILE_OPT "")
    list(APPEND LUA_LINK_LIBS m dl readline history ncurses)
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    list(APPEND LUA_COMPILE_DEF LUA_USE_LINUX)
    list(APPEND LUA_COMPILE_OPT "")
    list(APPEND LUA_LINK_LIBS m readline)
elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
    list(APPEND LUA_COMPILE_DEF _CRT_SECURE_NO_DEPRECATE)
    list(APPEND LUA_COMPILE_OPT "")
    list(APPEND LUA_LINK_LIBS "")
elseif (CMAKE_SYSTEM_NAME MATCHES "Emscripten")
    list(APPEND LUA_COMPILE_DEF LUA_USE_POSIX)
    list(APPEND LUA_COMPILE_OPT "")
    list(APPEND LUA_LINK_LIBS "")
    set(LUA_USE_HP_HASH OFF)
    set(LUA_BUILD_STATIC_ONLY ON)
endif ()

if (LUA_USE_HP_HASH)
    list(APPEND LUA_COMPILE_DEF LUA_USE_HP_HASH)
endif ()

if (LUA_USE_API_CHECK)
    list(APPEND LUA_COMPILE_DEF LUA_USE_APICHECK)
endif ()

if (LUA_USE_COMPAT)
    set(LUA_SHARED_LIB_NAME "lua51")
else ()
    set(LUA_SHARED_LIB_NAME "Lumen")
endif ()

# ----- Lumen include -----

include_directories(
    include
    internal
)

# ----- Lumen libs -----

add_subdirectory(lib)

# ----- Lumen executables -----

add_subdirectory(src)

# ----- Lumen test -----

if (LUA_USE_TEST)
    add_subdirectory(test)
endif ()
