/*!
 * @brief Dynamic library loader for Lua
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <stdlib.h>
#include <string.h>


#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


/* prefix for open functions in C libraries */
#define LUA_POF        "luaopen_"

/* separator for open functions in C libraries */
#define LUA_OFSEP    "_"


#define LIBPREFIX    "LOADLIB: "

#define POF        LUA_POF
#define LIB_FAIL    "open"


/* error codes for ll_loadfunc */
#define ERRLIB        1
#define ERRFUNC        2

#define setprogdir(L)        ((void)0)


LUAI_FUNC void ll_unloadlib(void *lib);

LUAI_FUNC void *ll_load(lua_State *L, const char *path);

LUAI_FUNC lua_CFunction ll_sym(lua_State *L, void *lib, const char *sym);


#if defined(LUA_DL_DLOPEN)
/*
** {========================================================================
** This is an implementation of loadlib based on the dlfcn interface.
** The dlfcn interface is available in Linux, SunOS, Solaris, IRIX, FreeBSD,
** NetBSD, AIX 4.2, HPUX 11, and  probably most other Unix flavors, at least
** as an emulation layer on top of native functions.
** =========================================================================
*/

#include <dlfcn.h>

LUAI_FUNC void ll_unloadlib (void *lib) {
  dlclose(lib);
}


LUAI_FUNC void *ll_load (lua_State *L, const char *path) {
  void *lib = dlopen(path, RTLD_NOW);
  if (lib == NULL) lua_pushstring(L, dlerror());
  return lib;
}


LUAI_FUNC lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
  lua_CFunction f = (lua_CFunction)dlsym(lib, sym);
  if (f == NULL) lua_pushstring(L, dlerror());
  return f;
}

/* }====================================================== */



#elif defined(LUA_DL_DLL)
/*
** {======================================================================
** This is an implementation of loadlib for Windows using native functions.
** =======================================================================
*/

#include <windows.h>


#undef setprogdir

static void setprogdir(lua_State *L) {
    char buff[MAX_PATH + 1];
    char *lb;
    DWORD nsize = sizeof(buff) / sizeof(char);
    DWORD n = GetModuleFileNameA(NULL, buff, nsize);
    if (n == 0 || n == nsize || (lb = strrchr(buff, '\\')) == NULL)
        luaL_error(L, "unable to get ModuleFileName");
    else {
        *lb = '\0';
        luaL_gsub(L, lua_tostring(L, -1), LUA_EXECDIR, buff);
        lua_remove(L, -2);  /* remove original string */
    }
}


static void pusherror(lua_State *L) {
    int error = GetLastError();
    char buffer[128];
    if (FormatMessageA(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
                       NULL, error, 0, buffer, sizeof(buffer), NULL))
        lua_pushstring(L, buffer);
    else
        lua_pushfstring(L, "system error %d\n", error);
}

LUAI_FUNC void ll_unloadlib(void *lib) {
    FreeLibrary((HINSTANCE) lib);
}


LUAI_FUNC void *ll_load(lua_State *L, const char *path) {
    HINSTANCE lib = LoadLibraryA(path);
    if (lib == NULL) pusherror(L);
    return lib;
}


LUAI_FUNC lua_CFunction ll_sym(lua_State *L, void *lib, const char *sym) {
    lua_CFunction f = (lua_CFunction) GetProcAddress((HINSTANCE) lib, sym);
    if (f == NULL) pusherror(L);
    return f;
}

/* }====================================================== */



#elif defined(LUA_DL_DYLD)
/*
** {======================================================================
** Native Mac OS X / Darwin Implementation
** =======================================================================
*/

#include <mach-o/dyld.h>


/* Mac appends a `_' before C function names */
#undef POF
#define POF	"_" LUA_POF


static void pusherror (lua_State *L) {
  const char *err_str;
  const char *err_file;
  NSLinkEditErrors err;
  int err_num;
  NSLinkEditError(&err, &err_num, &err_file, &err_str);
  lua_pushstring(L, err_str);
}


static const char *errorfromcode (NSObjectFileImageReturnCode ret) {
  switch (ret) {
    case NSObjectFileImageInappropriateFile:
      return "file is not a bundle";
    case NSObjectFileImageArch:
      return "library is for wrong CPU type";
    case NSObjectFileImageFormat:
      return "bad format";
    case NSObjectFileImageAccess:
      return "cannot access file";
    case NSObjectFileImageFailure:
    default:
      return "unable to load library";
  }
}


LUAI_FUNC void ll_unloadlib (void *lib) {
  NSUnLinkModule((NSModule)lib, NSUNLINKMODULE_OPTION_RESET_LAZY_REFERENCES);
}


LUAI_FUNC void *ll_load (lua_State *L, const char *path) {
  NSObjectFileImage img;
  NSObjectFileImageReturnCode ret;
  /* this would be a rare case, but prevents crashing if it happens */
  if(!_dyld_present()) {
    lua_pushliteral(L, "dyld not present");
    return NULL;
  }
  ret = NSCreateObjectFileImageFromFile(path, &img);
  if (ret == NSObjectFileImageSuccess) {
    NSModule mod = NSLinkModule(img, path, NSLINKMODULE_OPTION_PRIVATE |
                       NSLINKMODULE_OPTION_RETURN_ON_ERROR);
    NSDestroyObjectFileImage(img);
    if (mod == NULL) pusherror(L);
    return mod;
  }
  lua_pushstring(L, errorfromcode(ret));
  return NULL;
}


LUAI_FUNC lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
  NSSymbol nss = NSLookupSymbolInModule((NSModule)lib, sym);
  if (nss == NULL) {
    lua_pushfstring(L, "symbol " LUA_QS " not found", sym);
    return NULL;
  }
  return (lua_CFunction)NSAddressOfSymbol(nss);
}

/* }====================================================== */



#else
/*
** {======================================================
** Fallback for other systems
** =======================================================
*/

#undef LIB_FAIL
#define LIB_FAIL	"absent"


#define DLMSG	"dynamic libraries not enabled; check your Lua installation"


LUAI_FUNC void ll_unloadlib (void *lib) {
  (void)lib;  /* to avoid warnings */
}


LUAI_FUNC void *ll_load (lua_State *L, const char *path) {
  (void)path;  /* to avoid warnings */
  lua_pushliteral(L, DLMSG);
  return NULL;
}


LUAI_FUNC lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
  (void)lib; (void)sym;  /* to avoid warnings */
  lua_pushliteral(L, DLMSG);
  return NULL;
}

/* }====================================================== */
#endif

