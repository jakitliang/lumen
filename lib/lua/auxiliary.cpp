/*!
 * @brief Auxiliary functions for building new Lua C++ libraries
 * @author Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define LUA_LIB

#include "lua.hpp"

#include "lumen/memory.h"


#define FREELIST_REF    0    /* free list of references */

#define LuaToLumen(L) reinterpret_cast<Lua::CState *>(L)
#define LumenToLua(L) reinterpret_cast<Lua::State *>(L)

#ifndef LUA_COLIBNAME
#define LUA_COLIBNAME    "coroutine"
LUALIB_API int luaopen_base(Lua::CState *L);
#endif

#ifndef LUA_TABLIBNAME
#define LUA_TABLIBNAME    "table"
LUALIB_API int luaopen_table(Lua::CState *L);
#endif

#ifndef LUA_IOLIBNAME
#define LUA_IOLIBNAME    "io"
LUALIB_API int luaopen_io(Lua::CState *L);
#endif

#ifndef LUA_OSLIBNAME
#define LUA_OSLIBNAME    "os"
LUALIB_API int luaopen_os(Lua::CState *L);
#endif

#ifndef LUA_STRLIBNAME
#define LUA_STRLIBNAME    "string"
LUALIB_API int luaopen_string(Lua::CState *L);
#endif

#ifndef LUA_MATHLIBNAME
#define LUA_MATHLIBNAME    "math"
LUALIB_API int luaopen_math(Lua::CState *L);
#endif

#ifndef LUA_UTF8LIBNAME
#define LUA_UTF8LIBNAME    "utf8"
LUALIB_API int luaopen_utf8(Lua::CState *L);
#endif

#ifndef LUA_DBLIBNAME
#define LUA_DBLIBNAME    "debug"
LUALIB_API int luaopen_debug(Lua::CState *L);
#endif

#ifndef LUA_BITLIBNAME
#define LUA_BITLIBNAME    "bit"
LUALIB_API int luaopen_bit(Lua::CState *L);
#endif

#ifndef LUA_LOADLIBNAME
#define LUA_LOADLIBNAME    "package"
LUALIB_API int luaopen_package(Lua::CState *L);
#endif

static inline int infSize(const Lua::Interface *l) {
    int size = 0;
    for (; l->Name; l++) size++;
    return size;
}

static inline int infSize(const Lua::Registry *l) {
    int size = 0;
    for (; l->Name; l++) size++;
    return size;
}

void Lua::State::OpenLib(const char *name, const Lua::Interface *inf, int nUpValue) {
    if (name) {
        int size = infSize(inf);
        /* check whether lib already exists */
        FindTable(Lua::RegistryIndex, "_LOADED", 1);
        GetField(-1, name);  /* get _LOADED[name] */
        if (!IsTable(-1)) {  /* not found? */
            Pop(1);  /* remove previous result */
            /* try global variable (and create one if it does not exist) */
            if (FindTable(Lua::GlobalIndex, name, size) != nullptr)
                Error("name conflict for module " LUA_QS, name);
            PushValue(-1);
            SetField(-3, name);  /* _LOADED[name] = new table */
        }
        Remove(-2);  /* remove _LOADED table */
        Insert(-(nUpValue + 1));  /* move library table to below upvalues */
    }
    for (; inf->Name; inf++) {
        int i;
        for (i = 0; i < nUpValue; i++)  /* copy upvalues to the top */
            PushValue(-nUpValue);
        PushDelegate(inf->Invoke, nUpValue);
        SetField(-(nUpValue + 2), inf->Name);
    }
    Pop(nUpValue);  /* remove upvalues */
}

void Lua::State::OpenLib(const char *name, const Lua::Registry *inf, int nUpValue) {
    if (name) {
        int size = infSize(inf);
        /* check whether lib already exists */
        FindTable(Lua::RegistryIndex, "_LOADED", 1);
        GetField(-1, name);  /* get _LOADED[name] */
        if (!IsTable(-1)) {  /* not found? */
            Pop(1);  /* remove previous result */
            /* try global variable (and create one if it does not exist) */
            if (FindTable(Lua::GlobalIndex, name, size) != nullptr)
                Error("name conflict for module " LUA_QS, name);
            PushValue(-1);
            SetField(-3, name);  /* _LOADED[name] = new table */
        }
        Remove(-2);  /* remove _LOADED table */
        Insert(-(nUpValue + 1));  /* move library table to below upvalues */
    }
    for (; inf->Name; inf++) {
        int i;
        for (i = 0; i < nUpValue; i++)  /* copy upvalues to the top */
            PushValue(-nUpValue);
        PushFunction(inf->Invoke, nUpValue);
        SetField(-(nUpValue + 2), inf->Name);
    }
    Pop(nUpValue);  /* remove upvalues */
}

void Lua::State::Register(const Lua::Interface *l, int nUpValue) {
    CheckStack(nUpValue, "too many upvalues");
    for (; l->Name != nullptr; l++) {  /* fill the table with given functions */
        int i;
        for (i = 0; i < nUpValue; i++)  /* copy upvalues to the top */
            PushValue(-nUpValue);
        PushDelegate(l->Invoke, nUpValue);  /* closure with those upvalues */
        SetField(-(nUpValue + 2), l->Name);
    }
    Pop(nUpValue);  /* remove upvalues */
}

void Lua::State::Register(const Lua::Registry *l, int nUpValue) {
    CheckStack(nUpValue, "too many upvalues");
    for (; l->Name != nullptr; l++) {  /* fill the table with given functions */
        int i;
        for (i = 0; i < nUpValue; i++)  /* copy upvalues to the top */
            PushValue(-nUpValue);
        PushFunction(l->Invoke, nUpValue);  /* closure with those upvalues */
        SetField(-(nUpValue + 2), l->Name);
    }
    Pop(nUpValue);  /* remove upvalues */
}

bool Lua::State::GetMetaField(int obj, const char *e) {
    if (!GetMetatable(obj))  /* no metatable? */
        return false;
    PushString(e);
    RawGet(-2);
    if (IsNil(-1)) {
        Pop(2);  /* remove metatable and metaField */
        return false;
    } else {
        Remove(-2);  /* remove only metatable */
        return true;
    }
}

bool Lua::State::CallMeta(int obj, const char *e) {
    obj = AbsIndex(obj);
    if (!GetMetaField(obj, e))  /* no metaField? */
        return false;
    PushValue(obj);
    Call(1, 1);
    return true;
}

int Lua::State::TypeError(int nArg, const char *tName) {
    const char *msg = PushFString("%s expected, got %s",
                                  tName, TypeName(nArg));
    return ArgError(nArg, msg);
}

int Lua::State::ArgError(int nArg, const char *extraMsg) {
    Lua::DebugInfo ar; // NOLINT
    if (!GetStack(0, &ar))  /* no stack frame? */
        return Error("bad argument #%d (%s)", nArg, extraMsg);
    GetInfo("n", &ar);
    if (strcmp(ar.NameSpace, "method") == 0) {
        nArg--;  /* do not count `self` */
        if (nArg == 0)  /* error is in the self argument itself? */
            return Error("calling " LUA_QS " on bad self (%s)",
                         ar.Name, extraMsg);
    }
    if (ar.Name == nullptr)
        ar.Name = "?";
    return Error("bad argument #%d to " LUA_QS " (%s)",
                 nArg, ar.Name, extraMsg);
}

static void tagError(Lua::State *L, int nArg, int tag) {
    L->TypeError(nArg, L->TypeId(tag));
}

const char *Lua::State::CheckString(int nArg, size_t *length) {
    const char *s = ToString(nArg, length);
    if (!s) tagError(this, nArg, Lua::TypeString);
    return s;
}

const char *Lua::State::OptString(int nArg, const char *def, size_t *length) {
    if (IsNoneOrNil(nArg)) {
        if (length)
            *length = (def ? strlen(def) : 0);
        return def;
    }
    return CheckString(nArg, length);
}

Lua::Number Lua::State::CheckNumber(int nArg) {
    auto d = ToNumber(nArg);
    if (d == 0 && !IsNumber(nArg))  /* avoid extra test when d is not 0 */
        tagError(this, nArg, Lua::TypeNumber);
    return d;
}

Lua::Number Lua::State::OptNumber(int nArg, Lua::Number def) {
    return Opt(&Lua::State::CheckNumber, nArg, def);
}

Lua::Integer Lua::State::CheckInteger(int nArg) {
    auto d = ToInteger(nArg);
    if (d == 0 && !IsNumber(nArg))  /* avoid extra test when d is not 0 */
        tagError(this, nArg, Lua::TypeNumber);
    return d;
}

Lua::Integer Lua::State::OptInteger(int nArg, Lua::Integer def) {
    return Opt(&Lua::State::CheckInteger, nArg, def);
}

void Lua::State::CheckStack(int sz, const char *msg) {
    if (!CheckStack(sz)) {
        if (msg)
            Error("stack overflow (%s)", msg);
        else
            Error("stack overflow");
    }
}

void Lua::State::CheckType(int nArg, Lua::Type t) {
    if (Type(nArg) != t)
        tagError(this, nArg, t);
}

void Lua::State::CheckAny(int nArg) {
    if (Type(nArg) == Lua::TypeNone)
        ArgError(nArg, "value expected");
}

bool Lua::State::NewMetatable(const char *tName) {
    GetField(Lua::RegistryIndex, tName);  /* get registry.name */
    if (!IsNil(-1))  /* name already in use? */
        return false;  /* leave previous value on top, but return false */
    Pop(1);
    NewTable();  /* create metatable */
    PushValue(-1);
    SetField(Lua::RegistryIndex, tName);  /* registry.name = metatable */
    return true;
}

void *Lua::State::TestUserdata(int ud, const char *tName) {
    auto p = ToUserdata(ud);
    if (p != nullptr) {  /* value is a userdata? */
        if (GetMetatable(ud)) {  /* does it have a metatable? */
            GetMetatable(tName);  /* get correct metatable */
            if (!RawEqual(-1, -2)) {  /* not the same mt? */
                p = nullptr;
            }
            Pop(2);  /* remove both metatables */
            return p;
        }
    }
    return nullptr;
}

void *Lua::State::CheckUserdata(int ud, const char *tName) {
    auto p = TestUserdata(ud, tName);
    if (p == nullptr) TypeError(ud, tName);  /* else error */
    return p;  /* to avoid warnings */
}

void Lua::State::Where(int level) {
    Lua::DebugInfo ar; // NOLINT
    if (GetStack(level, &ar)) {  /* check function at level */
        GetInfo("Sl", &ar);  /* get info about it */
        if (ar.CurrentLine > 0) {  /* is there info? */
            PushFString("%s:%d: ", ar.SourceHint, ar.CurrentLine);
            return;
        }
    }
    PushLiteral("");  /* else, no information available... */
}

int Lua::State::Error(const char *fmt, ...) {
    va_list argP;
        va_start(argP, fmt);
    Where(1);
    PushVFString(fmt, argP);
        va_end(argP);
    Concat(2);
    return Error();
}

int Lua::State::CheckOption(int nArg, const char *def, const char *const *lst) {
    const char *name = (def) ? OptString(nArg, def) : CheckString(nArg);
    int i;
    for (i = 0; lst[i]; i++)
        if (strcmp(lst[i], name) == 0)
            return i;
    return ArgError(nArg, PushFString("invalid option " LUA_QS, name));
}

Lua::Ref Lua::State::Ref(int t) {
    int ref;
    t = AbsIndex(t);
    if (IsNil(-1)) {
        Pop(1);  /* remove from stack */
        return Lua::RefNil;  /* `nil' has a unique fixed reference */
    }
    RawGetAt(t, FREELIST_REF);  /* get first free element */
    ref = (int) ToInteger(-1);  /* ref = t[FREELIST_REF] */
    Pop(1);  /* remove it from stack */
    if (ref != 0) {  /* any free element? */
        RawGetAt(t, ref);  /* remove it from list */
        RawSetAt(t, FREELIST_REF);  /* (t[FREELIST_REF] = t[ref]) */
    } else {  /* no free elements */
        ref = (int) ObjectLength(t);
        ref++;  /* create new reference */
    }
    RawSetAt(t, ref);
    return ref;
}

void Lua::State::Unref(int t, Lua::Ref ref) {
    if (ref >= 0) {
        t = AbsIndex(t);
        RawGetAt(t, FREELIST_REF);
        RawSetAt(t, ref);  /* t[ref] = t[FREELIST_REF] */
        PushInteger(ref);
        RawSetAt(t, FREELIST_REF);  /* t[FREELIST_REF] = ref */
    }
}

struct LoadFunc {
    int ExtraLine;
    FILE *f;
    char Buff[LUAL_BUFFERSIZE];
};

static const char *getF(Lua::State *, void *ud, size_t *size) {
    auto lf = (LoadFunc *) ud;
    if (lf->ExtraLine) {
        lf->ExtraLine = 0;
        *size = 1;
        return "\n";
    }
    if (feof(lf->f)) return nullptr;
    *size = fread(lf->Buff, 1, sizeof(lf->Buff), lf->f);
    return (*size > 0) ? lf->Buff : nullptr;
}

static int fileErr(Lua::State *L, const char *what, int fileNameIdx) {
    const char *strErr = strerror(errno);
    const char *filename = L->ToString(fileNameIdx) + 1;
    L->PushFString("cannot %s %s: %s", what, filename, strErr);
    L->Remove(fileNameIdx);
    return Lua::RetErrFile;
}

Lua::Ret Lua::State::LoadFile(const char *filename) {
    LoadFunc lf; // NOLINT
    int status, readStatus;
    int c;
    int fileNameIndex = GetTop() + 1;  /* index of filename on the stack */
    lf.ExtraLine = 0;
    if (filename == nullptr) {
        PushLiteral("=stdin");
        lf.f = stdin;
    } else {
        PushFString("@%s", filename);
        lf.f = fopen(filename, "r");
        if (lf.f == nullptr) return fileErr(this, "open", fileNameIndex);
    }
    c = getc(lf.f);
    if (c == '#') {  /* Unix exec. file? */
        lf.ExtraLine = 1;
        while ((c = getc(lf.f)) != EOF && c != '\n');  /* skip first line */
        if (c == '\n') c = getc(lf.f);
    }
    if (c == LUA_SIGNATURE[0] && filename) {  /* binary file? */
        lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
        if (lf.f == nullptr) return fileErr(this, "reopen", fileNameIndex);
        /* skip eventual `#!...' */
        while ((c = getc(lf.f)) != EOF && c != LUA_SIGNATURE[0]);
        lf.ExtraLine = 0;
    }
    ungetc(c, lf.f);
    status = Load(reinterpret_cast<Lua::Reader>(getF), &lf, ToString(-1));
    readStatus = ferror(lf.f);
    if (filename) fclose(lf.f);  /* close file (even in case of errors) */
    if (readStatus) {
        SetTop(fileNameIndex);  /* ignore results from `lua_load' */
        return fileErr(this, "read", fileNameIndex);
    }
    Remove(fileNameIndex);
    return status;
}

struct LoadState {
    const char *s;
    size_t size;
};

static const char *getS(Lua::State *, void *ud, size_t *size) {
    auto ls = (LoadState *) ud;
    if (ls->size == 0) return nullptr;
    *size = ls->size;
    ls->size = 0;
    return ls->s;
}

Lua::Ret Lua::State::LoadBuffer(const char *buff, size_t size, const char *name) {
    LoadState ls{buff, size};
    return Load(reinterpret_cast<Lua::Reader>(getS), &ls, name);
}

Lua::Ret Lua::State::LoadString(const char *s) {
    return LoadBuffer(s, strlen(s), s);
}

static int panic(Lua::State *L) {
    (void) L;  /* to avoid warnings */
    fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n",
            L->ToString(-1));
    return false;
}

Lua::State *Lua::State::New() {
    auto L = New(&Lumen::Memory::Alloc, nullptr);
    if (L) L->AtPanic(reinterpret_cast<Lua::Delegate>(panic));
    return L;
}

const char *Lua::State::GSub(const char *s, const char *p, const char *r) {
    std::string result;
    size_t l = strlen(p);
    if (l == 0) {
        PushString(s);
        return ToString(-1);
    }

    const char *wild;
    while ((wild = strstr(s, p)) != nullptr) {
        result.append(s, wild);
        result.append(r);
        s = wild + l;
    }

    result.append(s);
    PushString(result.c_str());
    return ToString(-1);
}

const char *Lua::State::FindTable(int idx, const char *name, int hintSize) {
    const char *e;
    PushValue(idx);
    do {
        e = strchr(name, '.');
        if (e == nullptr) e = name + strlen(name);
        PushString(name, e - name);
        RawGet(-2);
        if (IsNil(-1)) {  /* no such field? */
            Pop(1);  /* remove this nil */
            CreateTable(0, (*e == '.' ? 1 : hintSize)); /* new table for field */
            PushString(name, e - name);
            PushValue(-2);
            SetTable(-4);  /* set new table into field */
        } else if (!IsTable(-1)) {  /* field has a non-table value? */
            Pop(2);  /* remove table and value */
            return name;  /* return problematic part of the name */
        }
        Remove(-2);  /* remove previous table */
        name = e + 1;
    } while (*e == '.');
    return nullptr;
}

int Lua::State::OpenBase() {
    return luaopen_base(LuaToLumen(this));
}

int Lua::State::OpenTable() {
    return luaopen_table(LuaToLumen(this));
}

int Lua::State::OpenIO() {
    return luaopen_io(LuaToLumen(this));
}

int Lua::State::OpenOS() {
    return luaopen_os(LuaToLumen(this));
}

int Lua::State::OpenString() {
    return luaopen_string(LuaToLumen(this));
}

int Lua::State::OpenMath() {
    return luaopen_math(LuaToLumen(this));
}

int Lua::State::OpenDebug() {
    return luaopen_debug(LuaToLumen(this));
}

int Lua::State::OpenBit() {
    return luaopen_bit(LuaToLumen(this));
}

int Lua::State::OpenPackage() {
    return luaopen_package(LuaToLumen(this));
}

static const Lua::Registry luaLibs[] = {
    {"",              luaopen_base},
    {LUA_LOADLIBNAME, luaopen_package},
    {LUA_TABLIBNAME,  luaopen_table},
    {LUA_IOLIBNAME,   luaopen_io},
    {LUA_OSLIBNAME,   luaopen_os},
    {LUA_STRLIBNAME,  luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_UTF8LIBNAME, luaopen_utf8},
    {LUA_BITLIBNAME,  luaopen_bit},
    {LUA_DBLIBNAME,   luaopen_debug},
    {nullptr,         nullptr}
};

void Lua::State::OpenLibs() {
    auto lib = luaLibs;
    for (; lib->Invoke; lib++) {
        PushFunction(lib->Invoke);
        PushString(lib->Name);
        Call(1, 0);
    }
}
