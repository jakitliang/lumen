/*!
 * @brief Library for Table Manipulation
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstddef>

#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

#include "lumen.h"
#include "lumen/table.h"
#include "lumen/state.h"
#include "lumen/common.inl"

#define aux_getn(L, n)    (luaL_checktype(L, n, LUA_TTABLE), luaL_getn(L, n))


static int foreachi(lua_State *L) {
    int i;
    int n = aux_getn(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    for (i = 1; i <= n; i++) {
        lua_pushvalue(L, 2);  /* function */
        lua_pushinteger(L, i);  /* 1st argument */
        lua_rawgeti(L, 1, i);  /* 2nd argument */
        lua_call(L, 2, 1);
        if (!lua_isnil(L, -1))
            return 1;
        lua_pop(L, 1);  /* remove nil result */
    }
    return 0;
}


static int foreach(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushnil(L);  /* first key */
    while (lua_next(L, 1)) {
        lua_pushvalue(L, 2);  /* function */
        lua_pushvalue(L, -3);  /* key */
        lua_pushvalue(L, -3);  /* value */
        lua_call(L, 2, 1);
        if (!lua_isnil(L, -1))
            return 1;
        lua_pop(L, 2);  /* remove value and result */
    }
    return 0;
}


static int maxn(lua_State *L) {
    lua_Number max = 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);  /* first key */
    while (lua_next(L, 1)) {
        lua_pop(L, 1);  /* remove value */
        if (lua_type(L, -1) == LUA_TNUMBER) {
            lua_Number v = lua_tonumber(L, -1);
            if (v > max) max = v;
        }
    }
    lua_pushnumber(L, max);
    return 1;
}


static int getn(lua_State *L) {
    lua_pushinteger(L, aux_getn(L, 1));
    return 1;
}


static int setn(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
#ifndef luaL_setn
    luaL_setn(L, 1, luaL_checkint(L, 2));
#else
    luaL_error(L, LUA_QL("setn") " is obsolete");
#endif
    lua_pushvalue(L, 1);
    return 1;
}


static int tinsert(lua_State *L) {
    int e = aux_getn(L, 1) + 1;  /* first empty element */
    int pos;  /* where to insert new element */
    switch (lua_gettop(L)) {
        case 2: {  /* called with only 2 arguments */
            pos = e;  /* insert new element at the end */
            break;
        }
        case 3: {
            int i;
            pos = luaL_checkint(L, 2);  /* 2nd argument is the position */
            if (pos > e) e = pos;  /* `grow' array if necessary */
            for (i = e; i > pos; i--) {  /* move up elements */
                lua_rawgeti(L, 1, i - 1);
                lua_rawseti(L, 1, i);  /* t[i] = t[i-1] */
            }
            break;
        }
        default: {
            return luaL_error(L, "wrong number of arguments to " LUA_QL("insert"));
        }
    }
    luaL_setn(L, 1, e);  /* new size */
    lua_rawseti(L, 1, pos);  /* t[pos] = v */
    return 0;
}


static int tremove(lua_State *L) {
    int e = aux_getn(L, 1);
    int pos = luaL_optint(L, 2, e);
    if (!(1 <= pos && pos <= e))  /* position is outside bounds? */
        return 0;  /* nothing to remove */
    luaL_setn(L, 1, e - 1);  /* t.n = n-1 */
    lua_rawgeti(L, 1, pos);  /* result = t[pos] */
    for (; pos < e; pos++) {
        lua_rawgeti(L, 1, pos + 1);
        lua_rawseti(L, 1, pos);  /* t[pos] = t[pos+1] */
    }
    lua_pushnil(L);
    lua_rawseti(L, 1, e);  /* t[e] = nil */
    return 1;
}

/*
** Copy elements (1[f], ..., 1[e]) into (tt[t], tt[t+1], ...). Whenever
** possible, copy in increasing order, which is better for rehashing.
** "possible" means destination after original range, or smaller
** than origin, or copying to another table.
*/
static int tmove(lua_State *L) {
    lua_Integer f = luaL_checkinteger(L, 2);
    lua_Integer e = luaL_checkinteger(L, 3);
    lua_Integer t = luaL_checkinteger(L, 4);
    int tt = !lua_isnoneornil(L, 5) ? 5 : 1;  /* destination table */
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, tt, LUA_TTABLE);
    if (e >= f) {  /* otherwise, nothing to move */
        lua_Integer n, i;
        luaL_argcheck(L, f > 0 || e < INT_MAX + f, 3,
                      "too many elements to move");
        n = e - f + 1;  /* number of elements to move */
        luaL_argcheck(L, t <= INT_MAX - n + 1, 4,
                      "destination wrap around");
        if (t > e || t <= f || (tt != 1 && !lua_compare(L, 1, tt, LUA_OPEQ))) {
            for (i = 0; i < n; i++) {
                lua_rawgeti(L, 1, cast_int(f + i));
                lua_rawseti(L, tt, cast_int(t + i));
            }
        } else {
            for (i = n - 1; i >= 0; i--) {
                lua_rawgeti(L, 1, cast_int(f + i));
                lua_rawseti(L, tt, cast_int(t + i));
            }
        }
    }
    lua_pushvalue(L, tt);  /* return destination table */
    return 1;
}

static void addfield(lua_State *L, luaL_Buffer *b, int i) {
    lua_rawgeti(L, 1, i);
    if (!lua_isstring(L, -1))
        luaL_error(L, "invalid value (%s) at index %d in table for "
                      LUA_QL("concat"), luaL_typename(L, -1), i);
    luaL_addvalue(b);
}


static int tconcat(lua_State *L) {
    luaL_Buffer b;
    size_t lsep;
    int i, last;
    const char *sep = luaL_optlstring(L, 2, "", &lsep);
    luaL_checktype(L, 1, LUA_TTABLE);
    i = luaL_optint(L, 3, 1);
    last = luaL_opt(L, luaL_checkint, 4, luaL_getn(L, 1));
    luaL_buffinit(L, &b);
    for (; i < last; i++) {
        addfield(L, &b, i);
        luaL_addlstring(&b, sep, lsep);
    }
    if (i == last)  /* add last value (if interval was not empty) */
        addfield(L, &b, i);
    luaL_pushresult(&b);
    return 1;
}


/*
** {======================================================
** Quicksort
** (based on `Algorithms in MODULA-3', Robert Sedgewick;
**  Addison-Wesley, 1993.)
*/


static void set2(lua_State *L, int i, int j) {
    lua_rawseti(L, 1, i);
    lua_rawseti(L, 1, j);
}

static int sort_comp(lua_State *L, int a, int b) {
    if (!lua_isnil(L, 2)) {  /* function? */
        int res;
        lua_pushvalue(L, 2);
        lua_pushvalue(L, a - 1);  /* -1 to compensate function */
        lua_pushvalue(L, b - 2);  /* -2 to compensate function and `a' */
        lua_call(L, 2, 1);
        res = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return res;
    } else  /* a < b? */
        return lua_lessthan(L, a, b);
}

static void auxsort(lua_State *L, int l, int u) {
    while (l < u) {  /* for tail recursion */
        int i, j;
        /* sort elements a[l], a[(l+u)/2] and a[u] */
        lua_rawgeti(L, 1, l);
        lua_rawgeti(L, 1, u);
        if (sort_comp(L, -1, -2))  /* a[u] < a[l]? */
            set2(L, l, u);  /* swap a[l] - a[u] */
        else
            lua_pop(L, 2);
        if (u - l == 1) break;  /* only 2 elements */
        i = (l + u) / 2;
        lua_rawgeti(L, 1, i);
        lua_rawgeti(L, 1, l);
        if (sort_comp(L, -2, -1))  /* a[i]<a[l]? */
            set2(L, i, l);
        else {
            lua_pop(L, 1);  /* remove a[l] */
            lua_rawgeti(L, 1, u);
            if (sort_comp(L, -1, -2))  /* a[u]<a[i]? */
                set2(L, i, u);
            else
                lua_pop(L, 2);
        }
        if (u - l == 2) break;  /* only 3 elements */
        lua_rawgeti(L, 1, i);  /* Pivot */
        lua_pushvalue(L, -1);
        lua_rawgeti(L, 1, u - 1);
        set2(L, i, u - 1);
        /* a[l] <= P == a[u-1] <= a[u], only need to sort from l+1 to u-2 */
        i = l;
        j = u - 1;
        for (;;) {  /* invariant: a[l..i] <= P <= a[j..u] */
            /* repeat ++i until a[i] >= P */
            while (lua_rawgeti(L, 1, ++i), sort_comp(L, -1, -2)) {
                if (i > u) luaL_error(L, "invalid order function for sorting");
                lua_pop(L, 1);  /* remove a[i] */
            }
            /* repeat --j until a[j] <= P */
            while (lua_rawgeti(L, 1, --j), sort_comp(L, -3, -1)) {
                if (j < l) luaL_error(L, "invalid order function for sorting");
                lua_pop(L, 1);  /* remove a[j] */
            }
            if (j < i) {
                lua_pop(L, 3);  /* pop pivot, a[i], a[j] */
                break;
            }
            set2(L, i, j);
        }
        lua_rawgeti(L, 1, u - 1);
        lua_rawgeti(L, 1, i);
        set2(L, u - 1, i);  /* swap pivot (a[u-1]) with a[i] */
        /* a[l..i-1] <= a[i] == P <= a[i+1..u] */
        /* adjust so that smaller half is in [j..i] and larger one in [l..u] */
        if (i - l < u - i) {
            j = l;
            i = i - 1;
            l = i + 2;
        } else {
            j = i + 1;
            i = u;
            u = j - 2;
        }
        auxsort(L, j, i);  /* call recursively the smaller one */
    }  /* repeat the routine for the larger one */
}

static int sort(lua_State *L) {
    int n = aux_getn(L, 1);
    luaL_checkstack(L, 40, "");  /* assume array is smaller than 2^40 */
    if (!lua_isnoneornil(L, 2))  /* is there a 2nd argument? */
        luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_settop(L, 2);  /* make sure there is two arguments */
    auxsort(L, 1, n);
    return 0;
}

/* }====================================================== */

Lumen::IObject *Lumen::ITable::operator[](int n) {
    auto t = reinterpret_cast<Lumen::Table *>(this);
    auto o = const_cast<Lumen::Object *>(Lumen::Table::GetNum(t, n));
    return reinterpret_cast<Lumen::IObject *>(o);
}

Lumen::IObject *Lumen::ITable::operator[](const Lumen::IObject *object) {
    auto t = reinterpret_cast<Lumen::Table *>(this);
    auto o = const_cast<Lumen::Object *>(Lumen::Table::Get(t, reinterpret_cast<const Lumen::Object *>(object)));
    return reinterpret_cast<Lumen::IObject *>(o);
}

Lumen::IObject *Lumen::ITable::operator[](Lumen::IString *str) {
    auto t = reinterpret_cast<Lumen::Table *>(this);
    auto o = const_cast<Lumen::Object *>(Lumen::Table::GetString(t, reinterpret_cast<Lumen::String *>(str)));
    return reinterpret_cast<Lumen::IObject *>(o);
}

void Lumen::ITable::Insert(Lumen::IState *l, int n, const Lumen::IObject *val) {
    auto t = reinterpret_cast<Lumen::Table *>(this);
    auto L = reinterpret_cast<Lumen::State *>(l);
    auto value = Lumen::Table::SetNum(L, t, n);
    LumenSetObject2T(L, value, reinterpret_cast<const Lumen::Object *>(val));
    L->BarrierTable(t, reinterpret_cast<const Lumen::Object *>(val));
}

void Lumen::ITable::Insert(Lumen::IState *l, Lumen::IString *key, const Lumen::IObject *val) {
    auto t = reinterpret_cast<Lumen::Table *>(this);
    auto L = reinterpret_cast<Lumen::State *>(l);
    auto value = Lumen::Table::SetString(L, t, reinterpret_cast<Lumen::String *>(key));
    LumenSetObject2T(L, value, reinterpret_cast<const Lumen::Object *>(val));
    L->BarrierTable(t, reinterpret_cast<const Lumen::Object *>(val));
}

void Lumen::ITable::Insert(Lumen::IState *l, const Lumen::IObject *key, const Lumen::IObject *val) {
    auto t = reinterpret_cast<Lumen::Table *>(this);
    auto L = reinterpret_cast<Lumen::State *>(l);
    auto value = Lumen::Table::Set(L, t, reinterpret_cast<const Lumen::Object *>(key));
    LumenSetObject2T(L, value, reinterpret_cast<const Lumen::Object *>(val));
    L->BarrierTable(t, reinterpret_cast<const Lumen::Object *>(val));
}

void Lumen::ITable::Insert(Lumen::IState *l, Lumen::IString *key, Lumen::Delegate delegate) {
    Lumen::Object val; // NOLINT
    auto t = reinterpret_cast<Lumen::Table *>(this);
    auto L = reinterpret_cast<Lumen::State *>(l);
    auto cl = Lumen::CClosure::New(L, 0, L->GetCurrentEnv());
    cl->AsC.Func = delegate;
    val.SetClosure(L, cl);
    auto value = Lumen::Table::SetString(L, t, reinterpret_cast<Lumen::String *>(key));
    LumenSetObject2T(L, value, &val);
    L->BarrierTable(t, &val);
}

Lumen::ITable *Lumen::ITable::GetMetatable() {
    auto t = reinterpret_cast<Lumen::Table *>(this);
    return reinterpret_cast<Lumen::ITable *>(t->Metatable);
}

Lumen::ITable *Lumen::ITable::Get(Lumen::IState *l, int idx) {
    auto L = reinterpret_cast<Lumen::State *>(l);
    Lumen::Value o = L->ToObject(idx);
    if (o->Type != Lumen::TypeTable) return nullptr;
    return reinterpret_cast<Lumen::ITable *>(o->GetTable());
}

Lumen::ITable *Lumen::ITable::New(Lumen::IState *L) {
    L->NewTable();
    return Get(L, -1);
}

static const luaL_Reg tab_funcs[] = {
    {"concat",   tconcat},
    {"foreach",  foreach},
    {"foreachi", foreachi},
    {"getn",     getn},
    {"maxn",     maxn},
    {"insert",   tinsert},
    {"remove",   tremove},
    {"setn",     setn},
    {"sort",     sort},
    {"move",     tmove},
    {nullptr,    nullptr}
};

template<>
LPP_API int Lumen::Open<Lumen::ITable>(Lumen::IState *L) {
    L->Register(LUA_TABLIBNAME, tab_funcs);
    return 1;
}

LUALIB_API int luaopen_table(lua_State *L) {
    luaL_register(L, LUA_TABLIBNAME, tab_funcs);
    return 1;
}

