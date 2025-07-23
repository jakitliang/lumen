/*!
 * @brief Configuration file for Lua
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 1994-2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#ifndef lconfig_h
#define lconfig_h

#ifdef __cplusplus
#define LUA_CPP
#define LUA_C extern "C"
#define LUA_C_BEGIN LUA_C {
#define LUA_C_END };
#else
#define LUA_C extern
#define LUA_C_BEGIN
#define LUA_C_END
#endif

#ifdef LUA_CPP
#include <climits>
#include <cstddef>
#else
#include <limits.h>
#include <stddef.h>
#endif

#define LUA_ENUM(_type, _name) _type _name; enum

/*
** ==================================================================
** Search for "@@" to find all configurable definitions.
** ===================================================================
*/

// MARK: Platform configuration

/*
@@ LUA_ANSI controls the use of non-ansi features.
** CHANGE it (define it) if you want Lua to avoid the use of any
** non-ansi feature or library.
*/
#if defined(__STRICT_ANSI__)
#define LUA_ANSI
#endif


#if !defined(LUA_ANSI) && defined(_WIN32)
#define LUA_WIN
#endif

#if defined(LUA_USE_LINUX)
#define LUA_USE_POSIX
#define LUA_USE_DLOPEN		/* needs an extra library: -ldl */
#define LUA_USE_READLINE	/* needs some extra libraries */
#endif

#if defined(LUA_USE_MACOSX)
#define LUA_USE_POSIX
#define LUA_DL_DYLD		/* does not need extra library */
#endif



/*
@@ LUA_USE_POSIX includes all functionallity listed as X/Open System
@* Interfaces Extension (XSI).
** CHANGE it (define it) if your system is XSI compatible.
*/
#if defined(LUA_USE_POSIX)
#define LUA_USE_MKSTEMP
#define LUA_USE_ISATTY
#define LUA_USE_POPEN
#define LUA_USE_ULONGJMP
#endif


/*
@@ LUA_PATH and LUA_CPATH are the names of the environment variables that
@* Lua check to set its paths.
@@ LUA_INIT is the name of the environment variable that Lua
@* checks for initialization code.
** CHANGE them if you want different names.
*/
#define LUA_PATH     "LUA_PATH"
#define LUA_CPATH    "LUA_CPATH"
#define LUA_INIT     "LUA_INIT"


/*
@@ LUA_PATH_DEFAULT is the default path that Lua uses to look for
@* Lua libraries.
@@ LUA_CPATH_DEFAULT is the default path that Lua uses to look for
@* C libraries.
** CHANGE them if your machine has a non-conventional directory
** hierarchy or if you want to install your libraries in
** non-conventional directories.
*/
#if defined(_WIN32)
/*
** In Windows, any exclamation mark ('!') in the path is replaced by the
** path of the directory of the executable file of the current process.
*/
#define LUA_LDIR    "!\\lua\\"
#define LUA_CDIR    "!\\"
#define LUA_PATH_DEFAULT  \
        ".\\?.lua;"  LUA_LDIR"?.lua;"  LUA_LDIR"?\\init.lua;" \
        ".\\?.luac;" LUA_LDIR"?.luac;" LUA_LDIR"?\\init.luac;" \
                     LUA_CDIR"?.lua;"  LUA_CDIR"?\\init.lua" \
                     LUA_CDIR"?.luac;"  LUA_CDIR"?\\init.luac"
#define LUA_CPATH_DEFAULT \
    ".\\?.dll;"  LUA_CDIR"?.dll;" LUA_CDIR"loadall.dll"

#else
#define LUA_ROOT	"/usr/local/"
#define LUA_LDIR	LUA_ROOT "share/lua/5.1/"
#define LUA_CDIR	LUA_ROOT "lib/lua/5.1/"
#define LUA_PATH_DEFAULT  \
        "./?.lua;"  LUA_LDIR"?.lua;"  LUA_LDIR"?/init.lua;" \
        "./?.luac;" LUA_LDIR"?.luac;"  LUA_LDIR"?/init.luac;" \
                    LUA_CDIR"?.lua;"  LUA_CDIR"?/init.lua" \
                    LUA_CDIR"?.luac;"  LUA_CDIR"?/init.luac"
#define LUA_CPATH_DEFAULT \
    "./?.so;"  LUA_CDIR"?.so;" LUA_CDIR"loadall.so"
#endif


/*
@@ LUA_DIRSEP is the directory separator (for submodules).
** CHANGE it if your machine does not use "/" as the directory separator
** and is not Windows. (On Windows Lua automatically uses "\".)
*/
#if defined(_WIN32)
#define LUA_DIRSEP    "\\"
#else
#define LUA_DIRSEP	"/"
#endif


/*
@@ LUA_PATHSEP is the character that separates templates in a path.
@@ LUA_PATH_MARK is the string that marks the substitution points in a
@* template.
@@ LUA_EXECDIR in a Windows path is replaced by the executable's
@* directory.
@@ LUA_IGMARK is a mark to ignore all before it when bulding the
@* luaopen_ function name.
** CHANGE them if for some reason your system cannot use those
** characters. (E.g., if one of those characters is a common character
** in file/directory names.) Probably you do not need to change them.
*/
#define LUA_PATHSEP    ";"
#define LUA_PATH_MARK    "?"
#define LUA_EXECDIR    "!"
#define LUA_IGMARK    "-"

// MARK: Linkage configuration

// Detect Compiler and Platform
#if defined(_WIN32) || defined(_WIN64)

// On Windows
#define LUA_EXPORT   __declspec(dllexport)
#define LUA_IMPORT   __declspec(dllimport)
#define LUA_HIDDEN   // Windows doesn't support hidden via attribute

#else

// On Linux / macOS with GCC/Clang
#define LUA_EXPORT   __attribute__((visibility("default")))
#define LUA_IMPORT   // Nothing needed
#define LUA_HIDDEN   __attribute__((visibility("hidden")))

#endif

/*
@@ LUA_API is a mark for all core API functions.
@@ LUALIB_API is a mark for all standard library functions.
** CHANGE them if you need to define those functions in some special way.
** For instance, if you want to create one Windows DLL with the core and
** the libraries, you may want to use the following definition (define
** LUA_BUILD_AS_DLL to get it).
*/
#if defined(LUA_BUILD_AS_DLL)

#if defined(LUA_CORE) || defined(LUA_LIB)
#define LUA_API LUA_C LUA_EXPORT
#define LPP_API LUA_EXPORT
#else
#define LUA_API LUA_C LUA_IMPORT
#define LPP_API LUA_IMPORT
#endif

#else

#define LUA_API		LUA_C
#define LPP_API

#endif

/* more often than not the libs go together with the core */
#define LUALIB_API    LUA_API


/*
@@ LUAI_FUNC is a mark for all extern functions that are not to be
@* exported to outside modules.
@@ LUAI_DATA is a mark for all extern (const) variables that are not to
@* be exported to outside modules.
** CHANGE them if you need to mark them in some special way. Elf/gcc
** (versions 3.2 and later) mark them as "hidden" to optimize access
** when Lua is compiled as a shared library.
*/
#define LUAI_FUNC    LUA_C LUA_HIDDEN
#define LUAI_DATA    LUA_C LUA_HIDDEN

/*
@@ LUA_QL describes how error messages quote program elements.
** CHANGE it if you want a different appearance.
*/
#define LUA_QL(x)    "'" x "'"
#define LUA_QS        LUA_QL("%s")


/*
@@ LUA_IDSIZE gives the maximum size for the description of the source
@* of a function in debug information.
** CHANGE it if you want a different size.
*/
#define LUA_IDSIZE    60


/*
** {==================================================================
** MARK: Stand-alone configuration
** ===================================================================
*/

#if defined(lumen_c)

/*
@@ LUA_PROMPT is the default prompt used by stand-alone Lua.
@@ LUA_PROMPT2 is the default continuation prompt used by stand-alone Lua.
** CHANGE them if you want different prompts. (You can also change the
** prompts dynamically, assigning to globals _PROMPT/_PROMPT2.)
*/
#define LUA_PROMPT        "> "
#define LUA_PROMPT2        ">> "


/*
@@ LUA_PROGRAM_NAME is the default name for the stand-alone Lua program.
** CHANGE it if your stand-alone interpreter has a different name and
** your system is not able to detect that name automatically.
*/
#define LUA_PROGRAM_NAME        "lumen"


/*
@@ LUA_MAX_INPUT is the maximum length for an input line in the
@* stand-alone interpreter.
** CHANGE it if you need longer lines.
*/
#define LUA_MAX_INPUT    512

#endif

/* }================================================================== */

// MARK: GC configuration

/*
@@ LUA_GC_PAUSE defines the default pause between garbage-collector cycles
@* as a percentage.
** CHANGE it if you want the GC to run faster or slower (higher values
** mean larger pauses which mean slower collection.) You can also change
** this value dynamically.
*/
#define LUA_GC_PAUSE    200  /* 200% (wait memory to double before next GC) */


/*
@@ LUA_GC_MUL defines the default speed of garbage collection relative to
@* memory allocation as a percentage.
** CHANGE it if you want to change the granularity of the garbage
** collection. (Higher values mean coarser collections. 0 represents
** infinity, where each step performs a full collection.) You can also
** change this value dynamically.
*/
#define LUA_GC_MUL    200 /* GC runs 'twice the speed' of memory allocation */

// MARK: Compat configuration

/*
@@ LUA_COMPAT_GETN controls compatibility with old getn behavior.
** CHANGE it (define it) if you want exact compatibility with the
** behavior of setn/getn in Lua 5.0.
*/
#undef LUA_COMPAT_GETN

/*
@@ LUA_COMPAT_LOADLIB controls compatibility about global loadlib.
** CHANGE it to undefined as soon as you do not need a global 'loadlib'
** function (the function is still available as 'package.loadlib').
*/
#undef LUA_COMPAT_LOADLIB

/*
@@ LUA_COMPAT_VARARG controls compatibility with old vararg feature.
** CHANGE it to undefined as soon as your programs use only '...' to
** access vararg parameters (instead of the old 'arg' table).
*/
#define LUA_COMPAT_VARARG

/*
@@ LUA_COMPAT_MOD controls compatibility with old math.mod function.
** CHANGE it to undefined as soon as your programs use 'math.fmod' or
** the new '%' operator instead of 'math.mod'.
*/
#define LUA_COMPAT_MOD

/*
@@ LUA_COMPAT_LSTR controls compatibility with old long string nesting
@* facility.
** CHANGE it to 2 if you want the old behaviour, or undefine it to turn
** off the advisory error when nesting [[...]].
*/
#define LUA_COMPAT_LSTR        1

/*
@@ LUA_COMPAT_GFIND controls compatibility with old 'string.gfind' name.
** CHANGE it to undefined as soon as you rename 'string.gfind' to
** 'string.gmatch'.
*/
#define LUA_COMPAT_GFIND

/*
@@ LUA_COMPAT_OPENLIB controls compatibility with old 'luaL_openlib'
@* behavior.
** CHANGE it to undefined as soon as you replace to 'luaL_register'
** your uses of 'luaL_openlib'
*/
#define LUA_COMPAT_OPENLIB

/*
@@ LUA_COMPAT_PAIRS controls the effectiveness of the __ipairs and __pairs metamethod.
*/
#define LUA_COMPAT_PAIRS

/*
@@ LUA_USE_APICHECK turns on several consistency checks on the C API.
** Define it as a help when debugging C code.
*/
#ifdef LUA_USE_APICHECK
#ifdef LUA_CPP
#include <cassert>
#else
#include <assert.h>
#endif
#define luai_apicheck(L, e)    assert(e)
#define LumenApiCheck(L, e)    assert(e)
#else
#define luai_apicheck(L, e)    ((void) 0)
#define LumenApiCheck(L, e)    ((void) 0)
#endif

// MARK: Integer configuration

/*
@@ LUA_INTEGER is the integral type used by lua_pushinteger/lua_tointeger.
** CHANGE that if ptrdiff_t is not adequate on your machine. (On most
** machines, ptrdiff_t gives a good choice between int or long.)
*/
#define LUA_INTEGER    ptrdiff_t

#define LUA_UINTEGER   size_t

/*
@@ LUA_BITS_INT defines the number of bits in an int.
** CHANGE here if Lua cannot automatically detect the number of bits of
** your machine. Probably you do not need to change this.
*/
#define LUA_BITS_INT    32


/*
@@ LUA_UINT32 is an unsigned integer with at least 32 bits.
@@ LUA_INT32 is an signed integer with at least 32 bits.
@@ LUA_UMEM is an unsigned integer big enough to count the total
@* memory used by Lua.
@@ LUA_MEM is a signed integer big enough to count the total memory
@* used by Lua.
** CHANGE here if for some weird reason the default definitions are not
** good enough for your machine. (The definitions in the 'else'
** part always works, but may waste space on machines with 64-bit
** longs.) Probably you do not need to change this.
*/
#define LUA_UINT32    unsigned int
#define LUA_INT32    int
#define LUA_MAX_INT32    INT_MAX
#define LUA_UMEM    size_t
#define LUA_MEM    ptrdiff_t

// MARK: VM configuration

/*
@@ LUA_MAX_CALLS limits the number of nested calls.
** CHANGE it if you need really deep recursive calls. This limit is
** arbitrary; its only purpose is to stop infinite recursion before
** exhausting memory.
*/
#define LUA_MAX_CALLS    20000


/*
@@ LUA_MAX_C_STACK limits the number of Lua stack slots that a C function
@* can use.
** CHANGE it if you need lots of (Lua) stack space for your C
** functions. This limit is arbitrary; its only purpose is to stop C
** functions to consume unlimited stack space. (must be smaller than
** -LUA_REGISTRYINDEX)
*/
#define LUA_MAX_C_STACK    8000

/* minimum Lua stack available to a C function */
#define LUA_MIN_STACK    20


/*
** {==================================================================
** CHANGE (to smaller values) the following definitions if your system
** has a small C stack. (Or you may want to change them to larger
** values if your system has a large C stack and these limits are
** too rigid for you.) Some of these constants control the size of
** stack-allocated arrays used by the compiler or the interpreter, while
** others limit the maximum number of recursive calls that the compiler
** or the interpreter can perform. Values too large may cause a C stack
** overflow for some forms of deep constructs.
** ===================================================================
*/


/*
@@ LUA_MAX_C_CALLS is the maximum depth for nested C calls (short) and
@* syntactical nested non-terminals in a program.
*/
#define LUA_MAX_C_CALLS        200


/*
@@ LUA_MAX_VARS is the maximum number of local variables per function
@* (must be smaller than 250).
*/
#define LUA_MAX_VARS        200


/*
@@ LUA_MAX_UP_VALUES is the maximum number of upvalues per function
@* (must be smaller than 250).
*/
#define LUA_MAX_UP_VALUES    60


/*
@@ LUAL_BUFFERSIZE is the buffer size used by the lauxlib buffer system.
*/
#define LUAL_BUFFERSIZE        BUFSIZ

/* }================================================================== */


// MARK: Numeric configuration

/*
** {==================================================================
@@ LUA_NUMBER is the type of numbers in Lua.
** CHANGE the following definitions only if you want to build Lua
** with a number type different from double. You may also need to
** change lua_number2int & lua_number2integer.
** ===================================================================
*/

#define LUA_NUMBER_DOUBLE
#define LUA_NUMBER    double

/*
@@ LUA_UAC_NUMBER is the result of an 'usual argument conversion'
@* over a number.
*/
#define LUA_UAC_NUMBER    double


/*
@@ LUA_NUMBER_SCAN is the format for reading numbers.
@@ LUA_NUMBER_FMT is the format for writing numbers.
@@ LumenNum2Str converts a number to a string.
@@ LUA_MAX_NUMBER2STR is maximum size of previous conversion.
@@ LumenStr2Num converts a string to a number.
*/
#define LUA_NUMBER_SCAN        "%lf"
#define LUA_NUMBER_FMT        "%.14g"
#define LumenNum2Str(s, n)    sprintf((s), LUA_NUMBER_FMT, (n))
#define lua_number2str(s, n)    sprintf((s), LUA_NUMBER_FMT, (n))
#define LUA_MAX_NUMBER2STR    32 /* 16 digits, sign, point, and \0 */
#define LumenStr2Num(s, p)    strtod((s), (p))
#define lua_str2number(s, p)    strtod((s), (p))


/*
@@ The luai_num* macros define the primitive operations over numbers.
*/
#if defined(LUA_CORE)
#include <cmath>
#define LumenNumAdd(a,b)	((a)+(b))
#define LumenNumSub(a,b)	((a)-(b))
#define LumenNumMul(a,b)	((a)*(b))
#define LumenNumDiv(a,b)	((a)/(b))
#define LumenNumMod(a,b)	((a) - floor((a)/(b))*(b))
#define LumenNumPow(a,b)	(pow(a,b))
#define LumenNumUnm(a)		(-(a))
#define LumenNumEQ(a,b)		((a)==(b))
#define LumenNumLT(a,b)		((a)<(b))
#define LumenNumLE(a,b)		((a)<=(b))
#define LumenNumIsNAN(a)	(!LumenNumEQ((a), (a)))
#endif


/*
@@ lua_number2int is a macro to convert lua_Number to int.
@@ lua_number2integer is a macro to convert lua_Number to lua_Integer.
** CHANGE them if you know a faster way to convert a lua_Number to
** int (with any rounding method and without throwing errors) in your
** system. In Pentium machines, a naive typecast from double to int
** in C is extremely slow, so any alternative is worth trying.
*/
#define lua_number2int(i, d)    ((i)=(int)(d))
#define lua_number2integer(i, d)    ((i)=(LUA_INTEGER)(d))

/* }================================================================== */


/*
@@ LUAI_USER_ALIGNMENT_T is a *deprecated* type that requires maximum alignment.
** CHANGE it if your system requires alignments larger than double. (For
** instance, if your system supports `long double` and they must be
** aligned in 16-byte boundaries, then you should add long double in the
** union.) Probably you do not need to change this.
*/
#define LUAI_USER_ALIGNMENT_T    union { double u; void *s; long l; }

// MARK: Exception configuration

/*
@@ LUA_THROW/LUA_TRY define how Lua does exception handling.
** CHANGE them if you prefer to use longjmp/setjmp even with C++
** or if want/don't to use _longjmp/_setjmp instead of regular
** longjmp/setjmp. By default, Lua handles errors with exceptions when
** compiling as C++ code, with _longjmp/_setjmp when asked to use them,
** and with longjmp/setjmp otherwise.
*/
#define LUA_THROW(L, c)     longjmp((c)->b, 1)
#define LUA_TRY(L, c, a)    if (setjmp((c)->b) == 0) { a }
#define LUA_JUMP_BUFF       jmp_buf


/*
@@ LUA_MAX_CAPTURES is the maximum number of captures that a pattern
@* can do during pattern-matching.
** CHANGE it if you need more captures. This limit is arbitrary.
*/
#define LUA_MAX_CAPTURES        32


// MARK: Library OS configuration


#if defined(loslib_c)
/*
@@ LUA_TMPNAMBUFSIZE is the maximum size of a name created by lua_tmpnam.
** CHANGE them if you have an alternative to tmpnam (which is considered
** insecure) or if you want the original tmpnam anyway.  By default, Lua
** uses tmpnam except when POSIX is available, where it uses mkstemp.
*/
#define LUA_TMPNAMBUFSIZE	L_tmpnam
#endif

// MARK: Library IO configuration

/*
@@ lua_popen spawns a new process connected to the current one through
@* the file streams.
** CHANGE it if you have a way to implement it in your system.
*/
#if defined(LUA_USE_POPEN)

#define lua_popen(L,c,m)	((void)L, fflush(NULL), popen(c,m))
#define lua_pclose(L,file)	((void)L, (pclose(file) != -1))

#elif defined(LUA_WIN)

#define lua_popen(L, c, m)    ((void)L, _popen(c,m))
#define lua_pclose(L, file)    ((void)L, (_pclose(file) != -1))

#else

#define lua_popen(L,c,m)	((void)((void)c, m),  \
        luaL_error(L, LUA_QL("popen") " not supported"), (FILE*)0)
#define lua_pclose(L,file)		((void)((void)L, file), 0)

#endif

// MARK: Library Load configuration

/* mark for precompiled code (`<esc>Lua') */
#define LUA_SIGNATURE    "\033Lua"

/*
@@ LUA_DL_* define which dynamic-library system Lua should use.
** CHANGE here if Lua has problems choosing the appropriate
** dynamic-library system for your platform (either Windows' DLL, Mac's
** dyld, or Unix's dlopen). If your system is some kind of Unix, there
** is a good chance that it has dlopen, so LUA_DL_DLOPEN will work for
** it.  To use dlopen you also need to adapt the src/Makefile (probably
** adding -ldl to the linker options), so Lua does not select it
** automatically.  (When you change the makefile to add -ldl, you must
** also add -DLUA_USE_DLOPEN.)
** If you do not want any kind of dynamic library, undefine all these
** options.
** By default, _WIN32 gets LUA_DL_DLL and MAC OS X gets LUA_DL_DYLD.
*/
#if defined(LUA_USE_DLOPEN)
#define LUA_DL_DLOPEN
#endif

#if defined(LUA_WIN)
#define LUA_DL_DLL
#endif

// MARK: State configuration

struct lua_State;

struct luaL_Reg;

/*
@@ LUA_EXTRA_SPACE allows you to add user-specific data in a lua_State
@* (the data goes just *before* the lua_State pointer).
** CHANGE (define) this if you really need that. This value must be
** a multiple of the maximum alignment required for your machine.
*/
#define LUA_EXTRA_SPACE        0

/*
@@ luai_userstate* allow user-specific actions on threads.
** CHANGE them if you defined LUA_EXTRA_SPACE and need to do something
** extra when a thread is created/deleted/resumed/yielded.
*/
#define luai_userstateopen(L)        ((void)L)
#define luai_userstateclose(L)        ((void)L)
#define luai_userstatethread(L, L1)    ((void)L)
#define luai_userstatefree(L)        ((void)L)
#define luai_userstateresume(L, n)    ((void)L)
#define luai_userstateyield(L, n)    ((void)L)

// MARK: Library String configuration

/*
@@ LUA_INT_FMT_LEN is the length modifier for integer conversions
@* in 'string.format'.
@@ LUA_INT_FMT is the integer type corresponding to the previous length
@* modifier.
** CHANGE them if your system supports `long long` or does not support long.
*/

#if defined(LUA_USELONGLONG)

#define LUA_INT_FMT_LEN		"ll"
#define LUA_INT_FMT		long long

#else

#define LUA_INT_FMT_LEN        "l"
#define LUA_INT_FMT        long

#endif

#define LUA_TO_STRING_HELPER(x)    #x
#define LUA_TO_STRING(x)           LUA_TO_STRING_HELPER(x)

// MARK: Version Info

#define LUA_VERSION_MAJOR_N      5
#define LUA_VERSION_MINOR_N      1
#define LUA_VERSION_RELEASE_N    0
#define LUA_VERSION_NUM    (LUA_VERSION_MAJOR_N * 100 + LUA_VERSION_MINOR_N)
#define LUA_VERSION_RELEASE_NUM  (LUA_VERSION_NUM * 100 + LUA_VERSION_RELEASE_N)

#define LUMEN_COPYRIGHT    "Copyright (C) 2025 Jakit Liang. https://github.com/jakitliang/lumen"
#define LUMEN_AUTHORS      "Jakit Liang"

#define LUMEN_VERSION_MAJOR_N      1
#define LUMEN_VERSION_MINOR_N      3
#define LUMEN_VERSION_RELEASE_N    2
#define LUMEN_VERSION_NUM    (LUMEN_VERSION_MAJOR_N * 100 + LUMEN_VERSION_MINOR_N)

#define LUMEN_RELEASE  "Lumen " \
LUA_TO_STRING(LUMEN_VERSION_MAJOR_N) "." \
LUA_TO_STRING(LUMEN_VERSION_MINOR_N) "." \
LUA_TO_STRING(LUMEN_VERSION_RELEASE_N)

#define LUA_VERSION    "Lua 5.1"
#define LUA_RELEASE    "Lua 5.1.5"

/*
** Local configuration. You can use this space to add your redefinitions
** without modifying the main part of the file.
*/

#endif

