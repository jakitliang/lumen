/*!
 * @brief Auxiliary functions for building Lua libraries
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>


/* This file uses only the official API of Lua.
** Any function declared here could be written as an application function.
*/

#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"

#include "lumen.h"
#include "lumen/memory.h"


#define FREELIST_REF    0    /* free list of references */

/* convert a stack index to positive */
#define absIndex(L, i)    ((i) > 0 || (i) <= LUA_REGISTRYINDEX ? (i) : lua_gettop(L) + (i) + 1)


/*
** {======================================================
** Error-report functions
** =======================================================
*/


int luaL_argerror(lua_State *L, int nArg, const char *extraMsg) {
    lua_Debug ar;
    if (!lua_getstack(L, 0, &ar))  /* no stack frame? */
        return luaL_error(L, "bad argument #%d (%s)", nArg, extraMsg);
    lua_getinfo(L, "n", &ar);
    if (strcmp(ar.namewhat, "method") == 0) {
        nArg--;  /* do not count `self' */
        if (nArg == 0)  /* error is in the self argument itself? */
            return luaL_error(L, "calling " LUA_QS " on bad self (%s)",
                              ar.name, extraMsg);
    }
    if (ar.name == nullptr)
        ar.name = "?";
    return luaL_error(L, "bad argument #%d to " LUA_QS " (%s)",
                      nArg, ar.name, extraMsg);
}


int luaL_typerror(lua_State *L, int nArg, const char *tName) {
    const char *msg = lua_pushfstring(L, "%s expected, got %s",
                                      tName, luaL_typename(L, nArg));
    return luaL_argerror(L, nArg, msg);
}


static void tag_error(lua_State *L, int nArg, int tag) {
    luaL_typerror(L, nArg, lua_typename(L, tag));
}


void luaL_where(lua_State *L, int level) {
    lua_Debug ar;
    if (lua_getstack(L, level, &ar)) {  /* check function at level */
        lua_getinfo(L, "Sl", &ar);  /* get info about it */
        if (ar.currentline > 0) {  /* is there info? */
            lua_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
            return;
        }
    }
    lua_pushliteral(L, "");  /* else, no information available... */
}


int luaL_error(lua_State *L, const char *fmt, ...) {
    va_list argP;
        va_start(argP, fmt);
    luaL_where(L, 1);
    lua_pushvfstring(L, fmt, argP);
        va_end(argP);
    lua_concat(L, 2);
    return lua_error(L);
}

/* }====================================================== */


int luaL_checkoption(lua_State *L, int nArg, const char *def,
                     const char *const lst[]) {
    const char *name = (def) ? luaL_optstring(L, nArg, def) :
                       luaL_checkstring(L, nArg);
    int i;
    for (i = 0; lst[i]; i++)
        if (strcmp(lst[i], name) == 0)
            return i;
    return luaL_argerror(L, nArg,
                         lua_pushfstring(L, "invalid option " LUA_QS, name));
}


int luaL_newmetatable(lua_State *L, const char *tName) {
    lua_getfield(L, LUA_REGISTRYINDEX, tName);  /* get registry.name */
    if (!lua_isnil(L, -1))  /* name already in use? */
        return 0;  /* leave previous value on top, but return 0 */
    lua_pop(L, 1);
    lua_newtable(L);  /* create metatable */
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, tName);  /* registry.name = metatable */
    return 1;
}

void *luaL_testudata(lua_State *L, int ud, const char *tName) {
    return reinterpret_cast<Lumen::IState *>(L)->TestUserdata(ud, tName);
}

void *luaL_checkudata(lua_State *L, int ud, const char *tName) {
    auto p = luaL_testudata(L, ud, tName);
    if (p == nullptr) luaL_typerror(L, ud, tName);  /* else error */
    return p;  /* to avoid warnings */
}

void luaL_checkstack(lua_State *L, int space, const char *msg) {
    /* keep some extra space to run error routines, if needed */
    const int extra = LUA_MIN_STACK;
    if (!lua_checkstack(L, space + extra)) {
        if (msg)
            luaL_error(L, "stack overflow (%s)", msg);
        else
            luaL_error(L, "stack overflow");
    }
}


void luaL_checktype(lua_State *L, int nArg, int t) {
    if (lua_type(L, nArg) != t)
        tag_error(L, nArg, t);
}


void luaL_checkany(lua_State *L, int nArg) {
    if (lua_type(L, nArg) == LUA_TNONE)
        luaL_argerror(L, nArg, "value expected");
}


const char *luaL_checklstring(lua_State *L, int nArg, size_t *len) {
    const char *s = lua_tolstring(L, nArg, len);
    if (!s) tag_error(L, nArg, LUA_TSTRING);
    return s;
}


const char *luaL_optlstring(lua_State *L, int nArg,
                            const char *def, size_t *len) {
    if (lua_isnoneornil(L, nArg)) {
        if (len)
            *len = (def ? strlen(def) : 0);
        return def;
    } else return luaL_checklstring(L, nArg, len);
}


lua_Number luaL_checknumber(lua_State *L, int nArg) {
    lua_Number d = lua_tonumber(L, nArg);
    if (d == 0 && !lua_isnumber(L, nArg))  /* avoid extra test when d is not 0 */
        tag_error(L, nArg, LUA_TNUMBER);
    return d;
}


lua_Number luaL_optnumber(lua_State *L, int nArg, lua_Number def) {
    return luaL_opt(L, luaL_checknumber, nArg, def);
}


lua_Integer luaL_checkinteger(lua_State *L, int nArg) {
    lua_Integer d = lua_tointeger(L, nArg);
    if (d == 0 && !lua_isnumber(L, nArg))  /* avoid extra test when d is not 0 */
        tag_error(L, nArg, LUA_TNUMBER);
    return d;
}


lua_Integer luaL_optinteger(lua_State *L, int nArg,
                            lua_Integer def) {
    return luaL_opt(L, luaL_checkinteger, nArg, def);
}


int luaL_getmetafield(lua_State *L, int obj, const char *event) {
    if (!lua_getmetatable(L, obj))  /* no metatable? */
        return 0;
    lua_pushstring(L, event);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);  /* remove metatable and metafield */
        return 0;
    } else {
        lua_remove(L, -2);  /* remove only metatable */
        return 1;
    }
}


int luaL_callmeta(lua_State *L, int obj, const char *event) {
    obj = absIndex(L, obj);
    if (!luaL_getmetafield(L, obj, event))  /* no metafield? */
        return 0;
    lua_pushvalue(L, obj);
    lua_call(L, 1, 1);
    return 1;
}


void (luaL_register)(lua_State *L, const char *libname,
                     const luaL_Reg *l) {
    luaI_openlib(L, libname, l, 0);
}


static int libsize(const luaL_Reg *l) {
    int size = 0;
    for (; l->name; l++) size++;
    return size;
}


void luaI_openlib(lua_State *L, const char *libname,
                  const luaL_Reg *l, int nup) {
    if (libname) {
        int size = libsize(l);
        /* check whether lib already exists */
        luaL_findtable(L, LUA_REGISTRYINDEX, Lumen::RegKeyLoaded, 1);
        lua_getfield(L, -1, libname);  /* get _LOADED[libname] */
        if (!lua_istable(L, -1)) {  /* not found? */
            lua_pop(L, 1);  /* remove previous result */
            /* try global variable (and create one if it does not exist) */
            if (luaL_findtable(L, LUA_GLOBALSINDEX, libname, size) != nullptr)
                luaL_error(L, "name conflict for module " LUA_QS, libname);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, libname);  /* _LOADED[libname] = new table */
        }
        lua_remove(L, -2);  /* remove _LOADED table */
        lua_insert(L, -(nup + 1));  /* move library table to below upvalues */
    }
    for (; l->name; l++) {
        int i;
        for (i = 0; i < nup; i++)  /* copy upvalues to the top */
            lua_pushvalue(L, -nup);
        lua_pushcclosure(L, l->func, nup);
        lua_setfield(L, -(nup + 2), l->name);
    }
    lua_pop(L, nup);  /* remove upvalues */
}



/*
** {======================================================
** getn-setn: size for arrays
** =======================================================
*/

#if defined(LUA_COMPAT_GETN)

static int checkint (lua_State *L, int topop) {
  int n = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tointeger(L, -1) : -1;
  lua_pop(L, topop);
  return n;
}


static void getsizes (lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "LUA_SIZES");
  if (lua_isnil(L, -1)) {  /* no `size' table? */
    lua_pop(L, 1);  /* remove nil */
    lua_newtable(L);  /* create it */
    lua_pushvalue(L, -1);  /* `size' will be its own metatable */
    lua_setmetatable(L, -2);
    lua_pushliteral(L, "kv");
    lua_setfield(L, -2, "__mode");  /* metatable(N).__mode = "kv" */
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "LUA_SIZES");  /* store in register */
  }
}


void luaL_setn (lua_State *L, int t, int n) {
  t = abs_index(L, t);
  lua_pushliteral(L, "n");
  lua_rawget(L, t);
  if (checkint(L, 1) >= 0) {  /* is there a numeric field `n'? */
    lua_pushliteral(L, "n");  /* use it */
    lua_pushinteger(L, n);
    lua_rawset(L, t);
  }
  else {  /* use `sizes' */
    getsizes(L);
    lua_pushvalue(L, t);
    lua_pushinteger(L, n);
    lua_rawset(L, -3);  /* sizes[t] = n */
    lua_pop(L, 1);  /* remove `sizes' */
  }
}


int luaL_getn (lua_State *L, int t) {
  int n;
  t = abs_index(L, t);
  lua_pushliteral(L, "n");  /* try t.n */
  lua_rawget(L, t);
  if ((n = checkint(L, 1)) >= 0) return n;
  getsizes(L);  /* else try sizes[t] */
  lua_pushvalue(L, t);
  lua_rawget(L, -2);
  if ((n = checkint(L, 2)) >= 0) return n;
  return (int)lua_objlen(L, t);
}

#endif

/* }====================================================== */



const char *luaL_gsub(lua_State *L, const char *s, const char *p,
                      const char *r) {
    const char *wild;
    size_t l = strlen(p);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    while ((wild = strstr(s, p)) != nullptr) {
        luaL_addlstring(&b, s, wild - s);  /* push prefix */
        luaL_addstring(&b, r);  /* push replacement in place of pattern */
        s = wild + l;  /* continue after `p' */
    }
    luaL_addstring(&b, s);  /* push last suffix */
    luaL_pushresult(&b);
    return lua_tostring(L, -1);
}


const char *luaL_findtable(lua_State *L, int idx,
                           const char *name, int hintSize) {
    const char *e;
    lua_pushvalue(L, idx);
    do {
        e = strchr(name, '.');
        if (e == nullptr) e = name + strlen(name);
        lua_pushlstring(L, name, e - name);
        lua_rawget(L, -2);
        if (lua_isnil(L, -1)) {  /* no such field? */
            lua_pop(L, 1);  /* remove this nil */
            lua_createtable(L, 0, (*e == '.' ? 1 : hintSize)); /* new table for field */
            lua_pushlstring(L, name, e - name);
            lua_pushvalue(L, -2);
            lua_settable(L, -4);  /* set new table into field */
        } else if (!lua_istable(L, -1)) {  /* field has a non-table value? */
            lua_pop(L, 2);  /* remove table and value */
            return name;  /* return problematic part of the name */
        }
        lua_remove(L, -2);  /* remove previous table */
        name = e + 1;
    } while (*e == '.');
    return nullptr;
}



/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/


#define buffLength(B)    ((B)->p - (B)->buffer)
#define buffFree(B)    ((size_t)(LUAL_BUFFERSIZE - buffLength(B)))

#define LIMIT    (LUA_MIN_STACK/2)


static int buffEmpty(luaL_Buffer *B) {
    size_t l = buffLength(B);
    if (l == 0) return 0;  /* put nothing on stack */
    else {
        lua_pushlstring(B->L, B->buffer, l);
        B->p = B->buffer;
        B->lvl++;
        return 1;
    }
}


static void adjustStack(luaL_Buffer *B) {
    if (B->lvl > 1) {
        lua_State *L = B->L;
        int toget = 1;  /* number of levels to concat */
        size_t toplen = lua_strlen(L, -1);
        do {
            size_t l = lua_strlen(L, -(toget + 1));
            if (B->lvl - toget + 1 >= LIMIT || toplen > l) {
                toplen += l;
                toget++;
            } else break;
        } while (toget < B->lvl);
        lua_concat(L, toget);
        B->lvl = B->lvl - toget + 1;
    }
}


char *luaL_prepbuffer(luaL_Buffer *B) {
    if (buffEmpty(B))
        adjustStack(B);
    return B->buffer;
}


void luaL_addlstring(luaL_Buffer *B, const char *s, size_t l) {
    while (l--)
        luaL_addchar(B, *s++);
}


void luaL_addstring(luaL_Buffer *B, const char *s) {
    luaL_addlstring(B, s, strlen(s));
}


void luaL_pushresult(luaL_Buffer *B) {
    buffEmpty(B);
    lua_concat(B->L, B->lvl);
    B->lvl = 1;
}


void luaL_addvalue(luaL_Buffer *B) {
    lua_State *L = B->L;
    size_t vl;
    const char *s = lua_tolstring(L, -1, &vl);
    if (vl <= buffFree(B)) {  /* fit into buffer? */
        memcpy(B->p, s, vl);  /* put it there */
        B->p += vl;
        lua_pop(L, 1);  /* remove from stack */
    } else {
        if (buffEmpty(B))
            lua_insert(L, -2);  /* put buffer before new value */
        B->lvl++;  /* add new value into B stack */
        adjustStack(B);
    }
}


void luaL_buffinit(lua_State *L, luaL_Buffer *B) {
    B->L = L;
    B->p = B->buffer;
    B->lvl = 0;
}

/* }====================================================== */


int luaL_ref(lua_State *L, int t) {
    int ref;
    t = absIndex(L, t);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);  /* remove from stack */
        return LUA_REFNIL;  /* `nil' has a unique fixed reference */
    }
    lua_rawgeti(L, t, FREELIST_REF);  /* get first free element */
    ref = (int) lua_tointeger(L, -1);  /* ref = t[FREELIST_REF] */
    lua_pop(L, 1);  /* remove it from stack */
    if (ref != 0) {  /* any free element? */
        lua_rawgeti(L, t, ref);  /* remove it from list */
        lua_rawseti(L, t, FREELIST_REF);  /* (t[FREELIST_REF] = t[ref]) */
    } else {  /* no free elements */
        ref = (int) lua_objlen(L, t);
        ref++;  /* create new reference */
    }
    lua_rawseti(L, t, ref);
    return ref;
}


void luaL_unref(lua_State *L, int t, int ref) {
    if (ref >= 0) {
        t = absIndex(L, t);
        lua_rawgeti(L, t, FREELIST_REF);
        lua_rawseti(L, t, ref);  /* t[ref] = t[FREELIST_REF] */
        lua_pushinteger(L, ref);
        lua_rawseti(L, t, FREELIST_REF);  /* t[FREELIST_REF] = ref */
    }
}


/*
** {======================================================
** Load functions
** =======================================================
*/

struct LoadFunc {
    int ExtraLine;
    FILE *f;
    char Buff[LUAL_BUFFERSIZE];
};


static const char *getF(lua_State *L, void *ud, size_t *size) {
    auto lf = (LoadFunc *) ud;
    (void) L;
    if (lf->ExtraLine) {
        lf->ExtraLine = 0;
        *size = 1;
        return "\n";
    }
    if (feof(lf->f)) return nullptr;
    *size = fread(lf->Buff, 1, sizeof(lf->Buff), lf->f);
    return (*size > 0) ? lf->Buff : nullptr;
}


static int errFile(lua_State *L, const char *what, int fileNameIdx) {
    const char *strErr = strerror(errno);
    const char *filename = lua_tostring(L, fileNameIdx) + 1;
    lua_pushfstring(L, "cannot %s %s: %s", what, filename, strErr);
    lua_remove(L, fileNameIdx);
    return LUA_ERRFILE;
}


int luaL_loadfile(lua_State *L, const char *filename) {
    LoadFunc lf;
    int status, readStatus;
    int c;
    int fileNameIndex = lua_gettop(L) + 1;  /* index of filename on the stack */
    lf.ExtraLine = 0;
    if (filename == nullptr) {
        lua_pushliteral(L, "=stdin");
        lf.f = stdin;
    } else {
        lua_pushfstring(L, "@%s", filename);
        lf.f = fopen(filename, "r");
        if (lf.f == nullptr) return errFile(L, "open", fileNameIndex);
    }
    c = getc(lf.f);
    if (c == '#') {  /* Unix exec. file? */
        lf.ExtraLine = 1;
        while ((c = getc(lf.f)) != EOF && c != '\n');  /* skip first line */
        if (c == '\n') c = getc(lf.f);
    }
    if (c == LUA_SIGNATURE[0] && filename) {  /* binary file? */
        lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
        if (lf.f == nullptr) return errFile(L, "reopen", fileNameIndex);
        /* skip eventual `#!...' */
        while ((c = getc(lf.f)) != EOF && c != LUA_SIGNATURE[0]);
        lf.ExtraLine = 0;
    }
    ungetc(c, lf.f);
    status = lua_load(L, getF, &lf, lua_tostring(L, -1));
    readStatus = ferror(lf.f);
    if (filename) fclose(lf.f);  /* close file (even in case of errors) */
    if (readStatus) {
        lua_settop(L, fileNameIndex);  /* ignore results from `lua_load' */
        return errFile(L, "read", fileNameIndex);
    }
    lua_remove(L, fileNameIndex);
    return status;
}


struct LoadState {
    const char *s;
    size_t size;
};


static const char *getS(lua_State *L, void *ud, size_t *size) {
    auto ls = (LoadState *) ud;
    (void) L;
    if (ls->size == 0) return nullptr;
    *size = ls->size;
    ls->size = 0;
    return ls->s;
}


int luaL_loadbuffer(lua_State *L, const char *buff, size_t size,
                    const char *name) {
    LoadState ls;
    ls.s = buff;
    ls.size = size;
    return lua_load(L, getS, &ls, name);
}


int (luaL_loadstring)(lua_State *L, const char *s) {
    return luaL_loadbuffer(L, s, strlen(s), s);
}


/* }====================================================== */


static int panic(lua_State *L) {
    (void) L;  /* to avoid warnings */
    fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n",
            lua_tostring(L, -1));
    return 0;
}


lua_State *luaL_newstate(void) {
    lua_State *L = lua_newstate(&Lumen::Memory::Alloc, nullptr);
    if (L) lua_atpanic(L, &panic);
    return L;
}

// MARK: From Lua 5.2. */

int luaL_fileresult(lua_State *L, int stat, const char *fileName) {
    int en = errno;  /* calls to Lua API may change this value */
    if (stat) {
        lua_pushboolean(L, 1);
        return 1;
    } else {
        lua_pushnil(L);
        if (fileName)
            lua_pushfstring(L, "%s: %s", fileName, strerror(en));
        else
            lua_pushstring(L, strerror(en));
        lua_pushnumber(L, (lua_Number) en);
        return 3;
    }
}

#ifdef _WIN32
#define doInspectStat(stat, what) ((void)0)
#else
LUA_C_BEGIN
#include <unistd.h>
#include <sys/wait.h>
LUA_C_END
#define doInspectStat(stat, what) do { \
    if (WIFEXITED(stat)) {             \
        stat = WEXITSTATUS(stat);      \
    } \
    else if (WIFSIGNALED(stat)) {      \
        stat = WTERMSIG(stat); what = "signal"; \
    } \
} while(0)
#endif

int luaL_execresult(lua_State *L, int stat) {
    const char *what = "exit";
    if (stat == -1) return luaL_fileresult(L, 0, nullptr);
    doInspectStat(stat, what);
    if (*what == 'e' && stat == 0)
        lua_pushboolean(L, 1);
    else
        lua_pushnil(L);
    lua_pushstring(L, what);
    lua_pushnumber(L, (lua_Number) stat);
    return 3;
}

int luaL_loadfilex(lua_State *L, const char *filename,
                   const char *mode) {
    return luaL_loadfile(L, filename);
}

int luaL_loadbufferx(lua_State *L, const char *buff, size_t sz,
                     const char *name, const char *mode) {
    return luaL_loadbuffer(L, buff, sz, name);
}

#define TRACEBACK_LEVELS1 12  /* size of the first part of the stack */
#define TRACEBACK_LEVELS2 10  /* size of the first part of the stack */

static int countLevels(lua_State *L) {
    lua_Debug ar;
    int li = 1, le = 1;
    /* find an upper bound */
    while (lua_getstack(L, le, &ar)) {
        li = le;
        le *= 2;
    }
    /* do a binary search */
    while (li < le) {
        int m = (li + le) / 2;
        if (lua_getstack(L, m, &ar)) li = m + 1;
        else le = m;
    }
    return le - 1;
}

static int findField(lua_State *L, int objIdx, int level) {
    if (level == 0 || !lua_istable(L, -1))
        return 0;  /* not found */
    lua_pushnil(L);  /* start 'next' loop */
    while (lua_next(L, -2)) {  /* for each pair in table */
        if (lua_type(L, -2) == LUA_TSTRING) {  /* ignore non-string keys */
            if (lua_rawequal(L, objIdx, -1)) {  /* found object? */
                lua_pop(L, 1);  /* remove value (but keep name) */
                return 1;
            } else if (findField(L, objIdx, level - 1)) {  /* try recursively */
                lua_remove(L, -2);  /* remove table (but keep name) */
                lua_pushliteral(L, ".");
                lua_insert(L, -2);  /* place '.' between the two names */
                lua_concat(L, 3);
                return 1;
            }
        }
        lua_pop(L, 1);  /* remove value */
    }
    return 0;  /* not found */
}

static int pushGlobalFuncName(lua_State *L, lua_Debug *ar) {
    int top = lua_gettop(L);
    lua_getinfo(L, "f", ar);  /* push function */
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    if (findField(L, top + 1, 2)) {
        lua_copy(L, -1, top + 1);  /* move name to proper place */
        lua_pop(L, 2);  /* remove pushed values */
        return 1;
    } else {
        lua_settop(L, top);  /* remove function and global table */
        return 0;
    }
}

static void pushFuncName(lua_State *L, lua_Debug *ar) {
    if (*ar->namewhat != '\0')  /* is there a name? */
        lua_pushfstring(L, "function " LUA_QS, ar->name);
    else if (*ar->what == 'm')  /* main? */
        lua_pushliteral(L, "main chunk");
    else if (*ar->what == 'C') {
        if (pushGlobalFuncName(L, ar)) {
            lua_pushfstring(L, "function " LUA_QS, lua_tostring(L, -1));
            lua_remove(L, -2);  /* remove name */
        } else
            lua_pushliteral(L, "?");
    } else
        lua_pushfstring(L, "function <%s:%d>", ar->short_src, ar->linedefined);
}

void luaL_traceback(lua_State *L, lua_State *L1, const char *msg,
                    int level) {
    lua_Debug ar;
    int top = lua_gettop(L);
    int numLevels = countLevels(L1);
    int mark = (numLevels > TRACEBACK_LEVELS1 + TRACEBACK_LEVELS2) ? TRACEBACK_LEVELS1 : 0;
    if (msg) lua_pushfstring(L, "%s\n", msg);
    lua_pushliteral(L, "stack traceback:");
    while (lua_getstack(L1, level++, &ar)) {
        if (level == mark) {  /* too many levels? */
            lua_pushliteral(L, "\n\t...");  /* add a '...' */
            level = numLevels - TRACEBACK_LEVELS2;  /* and skip to last ones */
        } else {
            lua_getinfo(L1, "Slnt", &ar);
            lua_pushfstring(L, "\n\t%s:", ar.short_src);
            if (ar.currentline > 0)
                lua_pushfstring(L, "%d:", ar.currentline);
            lua_pushliteral(L, " in ");
            pushFuncName(L, &ar);
            lua_concat(L, lua_gettop(L) - top);
        }
    }
    lua_concat(L, lua_gettop(L) - top);
}

void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup) {
    luaL_checkstack(L, nup, "too many upvalues");
    for (; l->name != nullptr; l++) {  /* fill the table with given functions */
        int i;
        for (i = 0; i < nup; i++)  /* copy upvalues to the top */
            lua_pushvalue(L, -nup);
        lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
        lua_setfield(L, -(nup + 2), l->name);
    }
    lua_pop(L, nup);  /* remove upvalues */
}

int luaL_getsubtable(lua_State *L, int idx, const char *name) {
    if (lua_getfield(L, idx, name) == LUA_TTABLE)
        return 1;  /* table already there */
    else {
        lua_pop(L, 1);  /* remove previous result */
        idx = lua_absindex(L, idx);
        lua_newtable(L);
        lua_pushvalue(L, -1);  /* copy to be left at top */
        lua_setfield(L, idx, name);  /* assign new table to field */
        return 0;  /* false, because did not find table there */
    }
}

void luaL_requiref(lua_State *L, const char *modname,
                   lua_CFunction openF, int glb) {
    luaL_getsubtable(L, LUA_REGISTRYINDEX, Lumen::RegKeyLoaded);
    lua_getfield(L, -1, modname);  /* LOADED[modname] */
    if (!lua_toboolean(L, -1)) {  /* package not already loaded? */
        lua_pop(L, 1);  /* remove field */
        lua_pushcfunction(L, openF);
        lua_pushstring(L, modname);  /* argument to open function */
        lua_call(L, 1, 1);  /* call 'openF' to open module */
        lua_pushvalue(L, -1);  /* make copy of module (call result) */
        lua_setfield(L, -3, modname);  /* LOADED[modname] = module */
    }
    lua_remove(L, -2);  /* remove LOADED table */
    if (glb) {
        lua_pushvalue(L, -1);  /* copy of module */
        lua_setglobal(L, modname);  /* _G[modname] = module */
    }
}

void luaL_pushmodule(lua_State *L, const char *modname, int hintSize) {
    luaL_findtable(L, LUA_REGISTRYINDEX, Lumen::RegKeyLoaded, 16);
    lua_getfield(L, -1, modname);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        if (luaL_findtable(L, LUA_GLOBALSINDEX, modname, hintSize) != nullptr)
            luaL_error(L, "bad module name '%s'", modname);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, modname);  /* _LOADED[modname] = new table. */
    }
    lua_remove(L, -2);  /* Remove _LOADED table. */
}

void luaL_setmetatable(lua_State *L, const char *tName) {
    luaL_checkstack(L, 1, "not enough stack slots");
    luaL_getmetatable(L, tName);
    lua_setmetatable(L, -2);
}

