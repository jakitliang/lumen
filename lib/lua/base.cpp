/*!
 * @brief Basic library
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define LUA_LIB

#include "lumen.h"

#include "lumen/memory.h"

namespace Lua {
    struct Base {};
}

/*
** If your system does not support `stdout', you can just remove this function.
** If you need, you can define your own `print' function, following this
** model but changing `fputs' to put the strings at a proper place
** (a console window or a log file, for instance).
*/
static int luaB_print(Lumen::IState *L) {
    int n = L->GetTop();  /* number of arguments */
    int i;
    L->GetGlobal("tostring");
    for (i = 1; i <= n; i++) {
        const char *s;
        L->PushValue(-1);  /* function to be called */
        L->PushValue(i);   /* value to print */
        L->Call(1, 1);
        s = L->ToString(-1);  /* get result */
        if (s == nullptr)
            return L->Error(LUA_QL("tostring") " must return a string to "
                            LUA_QL("print"));
        if (i > 1) fputs("\t", stdout);
        fputs(s, stdout);
        L->Pop(1);  /* pop result */
    }
    fputs("\n", stdout);
    return 0;
}


static int luaB_tonumber(Lumen::IState *L) {
    int base = L->OptInt(2, 10);
    if (base == 10) {  /* standard conversion */
        L->CheckAny(1);
        if (L->IsNumber(1)) {
            L->PushNumber(L->ToNumber(1));
            return 1;
        }
    } else {
        const char *s1 = L->CheckString(1);
        char *s2;
        unsigned long n;
        L->ArgCheck(2 <= base && base <= 36, 2, "base out of range");
        n = strtoul(s1, &s2, base);
        if (s1 != s2) {  /* at least one valid digit? */
            while (isspace((unsigned char) (*s2))) s2++;  /* skip trailing spaces */
            if (*s2 == '\0') {  /* no invalid trailing characters? */
                L->PushNumber((Lumen::Number) n);
                return 1;
            }
        }
    }
    L->PushNil();  /* else not a number */
    return 1;
}


static int luaB_error(Lumen::IState *L) {
    int level = L->OptInt(2, 1);
    L->SetTop(1);
    if (L->IsString(1) && level > 0) {  /* add extra information? */
        L->Where(level);
        L->PushValue(1);
        L->Concat(2);
    }
    return L->Error();
}


static int luaB_getmetatable(Lumen::IState *L) {
    L->CheckAny(1);
    if (!L->GetMetatable(1)) {
        L->PushNil();
        return 1;  /* no metatable */
    }
    L->GetMetaField(1, "__metatable");
    return 1;  /* returns either __metatable field (if present) or metatable */
}


static int luaB_setmetatable(Lumen::IState *L) {
    int t = L->TypeId(2);
    L->CheckType(1, Lumen::TypeTable);
    L->ArgCheck(t == Lumen::TypeNil || t == Lumen::TypeTable, 2,
                "nil or table expected");
    if (L->GetMetaField(1, "__metatable"))
        L->Error("cannot change a protected metatable");
    L->SetTop(2);
    L->SetMetatable(1);
    return 1;
}


static void getfunc(Lumen::IState *L, int opt) {
    if (L->IsFunction(1)) L->PushValue(1);
    else {
        Lumen::DebugInfo ar; // NOLINT
        int level = opt ? L->OptInt(1, 1) : L->CheckInt(1);
        L->ArgCheck(level >= 0, 1, "level must be non-negative");
        if (L->GetStack(level, &ar) == 0)
            L->ArgError(1, "invalid level");
        L->GetInfo("f", &ar);
        if (L->IsNil(-1))
            L->Error("no function environment for tail call at level %d",
                     level);
    }
}


static int luaB_getfenv(Lumen::IState *L) {
    getfunc(L, 1);
    if (L->IsDelegate(-1))  /* is a C function? */
        L->PushValue(Lumen::GlobalIndex);  /* return the thread's global env. */
    else
        L->GetFEnv(-1);
    return 1;
}


static int luaB_setfenv(Lumen::IState *L) {
    L->CheckType(2, Lumen::TypeTable);
    getfunc(L, 0);
    L->PushValue(2);
    if (L->IsNumber(1) && L->ToNumber(1) == 0) {
        /* change environment of current thread */
        L->PushThread();
        L->Insert(-2);
        L->SetFEnv(-2);
        return 0;
    } else if (L->IsDelegate(-2) || L->SetFEnv(-2) == 0)
        L->Error(
            LUA_QL("setfenv") " cannot change environment of given object");
    return 1;
}


static int luaB_rawequal(Lumen::IState *L) {
    L->CheckAny(1);
    L->CheckAny(2);
    L->PushBoolean(L->RawEqual(1, 2));
    return 1;
}


static int luaB_rawget(Lumen::IState *L) {
    L->CheckType(1, Lumen::TypeTable);
    L->CheckAny(2);
    L->SetTop(2);
    L->RawGet(1);
    return 1;
}

static int luaB_rawset(Lumen::IState *L) {
    L->CheckType(1, Lumen::TypeTable);
    L->CheckAny(2);
    L->CheckAny(3);
    L->SetTop(3);
    L->RawSet(1);
    return 1;
}


static int luaB_gcinfo(Lumen::IState *L) {
    L->PushInteger(L->GetGCCount());
    return 1;
}


static int luaB_collectgarbage(Lumen::IState *L) {
    static const char *const opts[] = {"stop", "restart", "collect",
                                       "count", "step", "setpause", "setstepmul", nullptr};
    static const int optsnum[] = {Lumen::GCStop, Lumen::GCRestart, Lumen::GCCollect,
                                  Lumen::GCCount, Lumen::GCStep, Lumen::GCSetPause, Lumen::GCSetStepMul};
    int o = L->CheckOption(1, "collect", opts);
    int ex = L->OptInt(2, 0);
    int res = L->GC(optsnum[o], ex);
    switch (optsnum[o]) {
        case Lumen::GCCount: {
            int b = L->GC(Lumen::GCCountB, 0);
            L->PushNumber(res + ((Lumen::Number) b / 1024));
            return 1;
        }
        case Lumen::GCStep: {
            L->PushBoolean(res);
            return 1;
        }
        default: {
            L->PushNumber(res);
            return 1;
        }
    }
}


static int luaB_type(Lumen::IState *L) {
    L->CheckAny(1);
    L->PushString(L->TypeName(1));
    return 1;
}


static int luaB_next(Lumen::IState *L) {
    L->CheckType(1, Lumen::TypeTable);
    L->SetTop(2);  /* create a 2nd argument if there isn't one */
    if (L->Next(1))
        return 2;
    else {
        L->PushNil();
        return 1;
    }
}

static int pairsMeta(Lumen::IState *L, const char *method, int isZero,
                     Lumen::Delegate iter) {
    L->CheckAny(1);
    if (L->GetMetaField(1, method) == Lumen::TypeNil) {  /* no metamethod? */
        L->PushDelegate(iter);  /* will return generator, */
        L->PushValue(1);  /* state, */
        if (isZero) L->PushInteger(0);  /* and initial value */
        else L->PushNil();
    } else {
        L->PushValue(1);  /* argument 'self' to metamethod */
        L->Call(1, 3);  /* get 3 values from metamethod */
    }
    return 3;
}

static int luaB_pairs(Lumen::IState *L) {
#ifdef LUA_COMPAT_PAIRS
    return pairsMeta(L, "__pairs", 0, luaB_next);
#else
    L->CheckType(1, Lumen::TypeTable);
    L->PushValue(Lumen::UpValueIndex(1));  /* return generator, */
    L->PushValue(1);  /* state, */
    L->PushNil();  /* and initial value */
    return 3;
#endif
}


static int ipairsaux(Lumen::IState *L) {
    int i = L->CheckInt(2);
    L->CheckType(1, Lumen::TypeTable);
    i++;  /* next value */
    L->PushInteger(i);
    L->RawGetAt(1, i);
    return (L->IsNil(-1)) ? 0 : 2;
}


static int luaB_ipairs(Lumen::IState *L) {
#ifdef LUA_COMPAT_PAIRS
    return pairsMeta(L, "__ipairs", 1, ipairsaux);
#else
    L->CheckType(1, Lumen::TypeTable);
    L->PushValue(Lumen::UpValueIndex(1));  /* return generator, */
    L->PushValue(1);  /* state, */
    L->PushInteger(0);  /* and initial value */
    return 3;
#endif
}


static int load_aux(Lumen::IState *L, int status) {
    if (status == 0)  /* OK? */
        return 1;
    else {
        L->PushNil();
        L->Insert(-2);  /* put before error message */
        return 2;  /* return nil plus error message */
    }
}


static int luaB_loadstring(Lumen::IState *L) {
    size_t l;
    const char *s = L->CheckString(1, &l);
    const char *chunkname = L->OptString(2, s);
    return load_aux(L, L->LoadBuffer(s, l, chunkname));
}


static int luaB_loadfile(Lumen::IState *L) {
    const char *fname = L->OptString(1, nullptr);
    return load_aux(L, L->LoadFile(fname));
}


/*
** Reader for generic `load' function: `lua_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
static const char *generic_reader(Lumen::IState *L, void *ud, size_t *size) {
    (void) ud;  /* to avoid warnings */
    L->CheckStack(2, "too many nested functions");
    L->PushValue(1);  /* get function */
    L->Call(0, 1);  /* call it */
    if (L->IsNil(-1)) {
        *size = 0;
        return nullptr;
    } else if (L->IsString(-1)) {
        L->Replace(3);  /* save string in a reserved stack slot */
        return L->ToString(3, size);
    } else L->Error("reader function must return a string");
    return nullptr;  /* to avoid warnings */
}


static int luaB_load(Lumen::IState *L) {
    int status;
    const char *cname = L->OptString(2, "=(load)");
    L->CheckType(1, Lumen::TypeFunction);
    L->SetTop(3);  /* function, eventual name, plus one reserved slot */
    status = L->Load(generic_reader, nullptr, cname);
    return load_aux(L, status);
}


static int luaB_dofile(Lumen::IState *L) {
    const char *fname = L->OptString(1, nullptr);
    int n = L->GetTop();
    if (L->LoadFile(fname) != 0) L->Error();
    L->Call(0, Lumen::RetMul);
    return L->GetTop() - n;
}


static int luaB_assert(Lumen::IState *L) {
    L->CheckAny(1);
    if (!L->ToBoolean(1))
        return L->Error("%s", L->OptString(2, "assertion failed!"));
    return L->GetTop();
}


static int luaB_unpack(Lumen::IState *L) {
    int i, e, n;
    L->CheckType(1, Lumen::TypeTable);
    i = L->OptInt(2, 1);
    e = L->Opt(&Lumen::IState::CheckInt, 3, L->GetN(1));
    if (i > e) return 0;  /* empty range */
    n = e - i + 1;  /* number of elements */
    if (n <= 0 || !L->CheckStack(n))  /* n <= 0 means arith. overflow */
        return L->Error("too many results to unpack");
    L->RawGetAt(1, i);  /* push arg[i] (avoiding overflow problems) */
    while (i++ < e)  /* push arg[i + 1...e] */
        L->RawGetAt(1, i);
    return n;
}


static int luaB_select(Lumen::IState *L) {
    int n = L->GetTop();
    if (L->TypeId(1) == Lumen::TypeString && *L->ToString(1) == '#') {
        L->PushInteger(n - 1);
        return 1;
    } else {
        int i = L->CheckInt(1);
        if (i < 0) i = n + i;
        else if (i > n) i = n;
        L->ArgCheck(1 <= i, 1, "index out of range");
        return n - i;
    }
}


static int luaB_pcall(Lumen::IState *L) {
    int status;
    L->CheckAny(1);
    status = L->TryCall(L->GetTop() - 1, Lumen::RetMul, 0);
    L->PushBoolean((status == 0));
    L->Insert(1);
    return L->GetTop();  /* return status + all results */
}


static int luaB_xpcall(Lumen::IState *L) {
    int status;
    L->CheckAny(2);
    L->SetTop(2);
    L->Insert(1);  /* put error function under function to be called */
    status = L->TryCall(0, Lumen::RetMul, 1);
    L->PushBoolean((status == 0));
    L->Replace(1);
    return L->GetTop();  /* return status + all results */
}


static int luaB_tostring(Lumen::IState *L) {
    L->CheckAny(1);
    if (L->CallMeta(1, "__tostring"))  /* is there a metafield? */
        return 1;  /* use its value */
    switch (L->TypeId(1)) {
        case Lumen::TypeNumber:
            L->PushString(L->ToString(1));
            break;
        case Lumen::TypeString:
            L->PushValue(1);
            break;
        case Lumen::TypeBool:
            L->PushString((L->ToBoolean(1) ? "true" : "false"));
            break;
        case Lumen::TypeNil:
            L->PushLiteral("nil");
            break;
        default:
            L->PushFString("%s: %p", L->TypeName(1), L->ToPointer(1));
            break;
    }
    return 1;
}


static int luaB_newproxy(Lumen::IState *L) {
    L->SetTop(1);
    L->NewUserdata(0);  /* create proxy */
    if (L->ToBoolean(1) == 0)
        return 1;  /* no metatable */
    else if (L->IsBoolean(1)) {
        L->NewTable();  /* create a new metatable `m' ... */
        L->PushValue(-1);  /* ... and mark `m' as a valid metatable */
        L->PushBoolean(1);
        L->RawSet(Lumen::UpValueIndex(1));  /* weaktable[m] = true */
    } else {
        int validproxy = 0;  /* to check if weaktable[metatable(u)] == true */
        if (L->GetMetatable(1)) {
            L->RawGet(Lumen::UpValueIndex(1));
            validproxy = L->ToBoolean(-1);
            L->Pop(1);  /* remove value */
        }
        L->ArgCheck(validproxy, 1, "boolean or proxy expected");
        L->GetMetatable(1);  /* metatable is valid; get it */
    }
    L->SetMetatable(2);
    return 1;
}


static const Lumen::Interface base_funcs[] = {
    {"assert",         luaB_assert},
    {"collectgarbage", luaB_collectgarbage},
    {"dofile",         luaB_dofile},
    {"error",          luaB_error},
    {"gcinfo",         luaB_gcinfo},
    {"getfenv",        luaB_getfenv},
    {"getmetatable",   luaB_getmetatable},
    {"loadfile",       luaB_loadfile},
    {"load",           luaB_load},
    {"loadstring",     luaB_loadstring},
    {"next",           luaB_next},
    {"pcall",          luaB_pcall},
    {"print",          luaB_print},
    {"rawequal",       luaB_rawequal},
    {"rawget",         luaB_rawget},
    {"rawset",         luaB_rawset},
    {"select",         luaB_select},
    {"setfenv",        luaB_setfenv},
    {"setmetatable",   luaB_setmetatable},
    {"tonumber",       luaB_tonumber},
    {"tostring",       luaB_tostring},
    {"type",           luaB_type},
    {"unpack",         luaB_unpack},
    {"xpcall",         luaB_xpcall},
    {nullptr,          nullptr}
};


/*
** {======================================================
** Coroutine library
** =======================================================
*/

#define CO_RUN    0    /* running */
#define CO_SUS    1    /* suspended */
#define CO_NOR    2    /* 'normal' (it resumed another coroutine) */
#define CO_DEAD    3

static const char *const statnames[] =
    {"running", "suspended", "normal", "dead"};

static int costatus(Lumen::IState *L, Lumen::IState *co) {
    if (L == co) return CO_RUN;
    switch (co->Status()) {
        case Lumen::RetYield:
            return CO_SUS;
        case 0: {
            Lumen::DebugInfo ar; // NOLINT
            if (co->GetStack(0, &ar))  /* does it have frames? */
                return CO_NOR;  /* it is running */
            else if (co->GetTop() == 0)
                return CO_DEAD;
            else
                return CO_SUS;  /* initial state */
        }
        default:  /* some error occured */
            return CO_DEAD;
    }
}


static int luaB_costatus(Lumen::IState *L) {
    Lumen::IState *co = L->ToThread(1);
    L->ArgCheck(co, 1, "coroutine expected");
    L->PushString(statnames[costatus(L, co)]);
    return 1;
}


static int auxresume(Lumen::IState *L, Lumen::IState *co, int narg) {
    int status = costatus(L, co);
    if (!co->CheckStack(narg))
        L->Error("too many arguments to resume");
    if (status != CO_SUS) {
        L->PushFString("cannot resume %s coroutine", statnames[status]);
        return -1;  /* error flag */
    }
    Lumen::XMove(L, co, narg);
    Lumen::SetLevel(L, co);
    status = co->Resume(narg);
    if (status == 0 || status == Lumen::RetYield) {
        int nres = co->GetTop();
        if (!L->CheckStack(nres + 1))
            L->Error("too many results to resume");
        Lumen::XMove(co, L, nres);  /* move yielded values */
        return nres;
    } else {
        Lumen::XMove(co, L, 1);  /* move error message */
        return -1;  /* error flag */
    }
}


static int luaB_coresume(Lumen::IState *L) {
    Lumen::IState *co = L->ToThread(1);
    int r;
    L->ArgCheck(co, 1, "coroutine expected");
    r = auxresume(L, co, L->GetTop() - 1);
    if (r < 0) {
        L->PushBoolean(0);
        L->Insert(-2);
        return 2;  /* return false + error message */
    } else {
        L->PushBoolean(1);
        L->Insert(-(r + 1));
        return r + 1;  /* return true + `resume' returns */
    }
}


static int luaB_auxwrap(Lumen::IState *L) {
    Lumen::IState *co = L->ToThread(Lumen::UpValueIndex(1));
    int r = auxresume(L, co, L->GetTop());
    if (r < 0) {
        if (L->IsString(-1)) {  /* error object is a string? */
            L->Where(1);  /* add extra info */
            L->Insert(-2);
            L->Concat(2);
        }
        L->Error();  /* propagate error */
    }
    return r;
}


static int luaB_cocreate(Lumen::IState *L) {
    Lumen::IState *NL = L->NewThread();
    L->ArgCheck(L->IsFunction(1) && !L->IsDelegate(1), 1,
                "Lua function expected");
    L->PushValue(1);  /* move function to top */
    Lumen::XMove(L, NL, 1);  /* move function from L to NL */
    return 1;
}


static int luaB_cowrap(Lumen::IState *L) {
    luaB_cocreate(L);
    L->PushDelegate(luaB_auxwrap, 1);
    return 1;
}


static int luaB_yield(Lumen::IState *L) {
    return L->Yield(L->GetTop());
}


static int luaB_corunning(Lumen::IState *L) {
    if (L->PushThread())
        L->PushNil();  /* main thread is not a coroutine */
    return 1;
}


static const Lumen::Interface co_funcs[] = {
    {"create",  luaB_cocreate},
    {"resume",  luaB_coresume},
    {"running", luaB_corunning},
    {"status",  luaB_costatus},
    {"wrap",    luaB_cowrap},
    {"yield",   luaB_yield},
    {nullptr,   nullptr}
};

/* }====================================================== */


static void auxopen(Lumen::IState *L, const char *name,
                    Lumen::Delegate f, Lumen::Delegate u) {
    L->PushDelegate(u);
    L->PushDelegate(f, 1);
    L->SetField(-2, name);
}

#ifndef LUA_VERSION
#define LUA_VERSION    "Lua 5.1"
#endif

static void base_open(Lumen::IState *L) {
    /* set global _G */
    L->PushValue(Lumen::GlobalIndex);
    L->SetGlobal("_G");
    /* open lib into global table */
    L->Register("_G", base_funcs);
    L->PushLiteral(LUA_VERSION);
    L->SetGlobal("_VERSION");  /* set global _VERSION */
    /* `ipairs' and `pairs' need auxiliary functions as upvalues */
    auxopen(L, "ipairs", luaB_ipairs, ipairsaux);
    auxopen(L, "pairs", luaB_pairs, luaB_next);
    /* `newproxy' needs a weaktable as upvalue */
    L->CreateTable(0, 1);  /* new table `w' */
    L->PushValue(-1);  /* `w' will be its own metatable */
    L->SetMetatable(-2);
    L->PushLiteral("kv");
    L->SetField(-2, "__mode");  /* metatable(w).__mode = "kv" */
    L->PushDelegate(luaB_newproxy, 1);
    L->SetGlobal("newproxy");  /* set global `newproxy' */
}

#ifndef LUA_COLIBNAME
#define LUA_COLIBNAME    "coroutine"
#endif

template<>
LPP_API int Lumen::Open<Lumen::ICoroutine>(Lumen::IState *L) {
    L->Register(LUA_COLIBNAME, co_funcs);
    return 1;
}

LUALIB_API int luaopen_coroutine(struct lua_State *l) {
    auto L = reinterpret_cast<Lumen::IState *>(l);
    L->Register(LUA_COLIBNAME, co_funcs);
    return 1;
}

template<>
LPP_API int Lumen::Open<Lumen::IBase>(Lumen::IState *L) {
    base_open(L);
    return 1;
}

LUALIB_API int luaopen_base(struct lua_State *l) {
    auto L = reinterpret_cast<Lumen::IState *>(l);
    base_open(L);
    L->Register(LUA_COLIBNAME, co_funcs);
    return 2;
}

