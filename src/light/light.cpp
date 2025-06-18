/*!
 * @brief Lua stand-alone interpreter
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define lumen_c

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

/*
 * lua_stdin_is_tty detects whether the standard input is a 'tty' (that
 * is, whether we're running lua interactively).
 * CHANGE it if you have a better definition for non-POSIX/non-Windows
 * systems.
 */
#if defined(LUA_USE_ISATTY)
LUA_C_BEGIN
#include <unistd.h>
LUA_C_END
#define lua_stdin_is_tty()	isatty(0)
#elif defined(LUA_WIN)
#include <io.h>
#define lua_stdin_is_tty()    _isatty(_fileno(stdin))
#else
#define lua_stdin_is_tty()	1  /* assume stdin is a tty */
#endif

/*
 * lua_readline defines how to show a prompt and then read a line from
 * the standard input.
 * lua_saveline defines how to "save" a read line in a "history".
 * lua_freeline defines how to free a line read by lua_readline.
 * CHANGE them if you want to improve this functionality (e.g., by using
 * GNU readline and history facilities).
 */
#if defined(LUA_USE_READLINE)
LUA_C_BEGIN
#include <readline/readline.h>
#include <readline/history.h>
LUA_C_END
#define lua_readline(L,b,p)	((void)L, ((b)=readline(p)) != nullptr)
#define lua_saveline(L,idx) \
    if (lua_strlen(L,idx) > 0)  /* non-empty line? */ \
      add_history(lua_tostring(L, idx));  /* add it to history */
#define lua_freeline(L,b)	((void)L, free(b))
#else
#define lua_readline(L, b, p)    \
    ((void)L, fputs(p, stdout), fflush(stdout),  /* show prompt */ \
    fgets(b, LUA_MAXINPUT, stdin) != nullptr)  /* get line */
#define lua_saveline(L, idx)    do { (void)L; (void)idx; } while (0)
#define lua_freeline(L, b)    do { (void)L; (void)b; } while (0)
#endif

static lua_State *globalL = nullptr;

static const char *programName = LUA_PROGNAME;


static void luaStop(lua_State *L, lua_Debug *ar) {
    (void) ar;  /* unused arg. */
    lua_sethook(L, nullptr, 0, 0);
    luaL_error(L, "interrupted!");
}


static void luaAction(int i) {
    signal(i, SIG_DFL); /* if another SIGINT happens before luaStop,
                              terminate process (default action) */
    lua_sethook(globalL, luaStop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}


static void printUsage() {
    fprintf(stderr,
            "usage: %s [options] [script [args]].\n"
            "Available options are:\n"
            "  -e stat  execute string " LUA_QL("stat") "\n"
            "  -l name  require library " LUA_QL("name") "\n"
            "  -i       enter interactive mode after executing " LUA_QL("script") "\n"
            "  -v       show version information\n"
            "  --       stop handling options\n"
            "  -        execute stdin and stop handling options\n",
            programName);
    fflush(stderr);
}


static void luaMessage(const char *pName, const char *msg) {
    if (pName) fprintf(stderr, "%s: ", pName);
    fprintf(stderr, "%s\n", msg);
    fflush(stderr);
}


static int report(lua_State *L, int status) {
    if (status && !lua_isnil(L, -1)) {
        const char *msg = lua_tostring(L, -1);
        if (msg == nullptr) msg = "(error object is not a string)";
        luaMessage(programName, msg);
        lua_pop(L, 1);
    }
    return status;
}


static int traceback(lua_State *L) {
    if (!lua_isstring(L, 1))  /* 'message' not a string? */
        return 1;  /* keep it intact */
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }
    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return 1;
    }
    lua_pushvalue(L, 1);  /* pass error message */
    lua_pushinteger(L, 2);  /* skip this function and traceback */
    lua_call(L, 2, 1);  /* call debug.traceback */
    return 1;
}


static int doCall(lua_State *L, int nArg, int clear) {
    int status;
    int base = lua_gettop(L) - nArg;  /* function index */
    lua_pushcfunction(L, traceback);  /* push traceback function */
    lua_insert(L, base);  /* put it under chunk and args */
    signal(SIGINT, luaAction);
    status = lua_pcall(L, nArg, (clear ? 0 : LUA_MULTRET), base);
    signal(SIGINT, SIG_DFL);
    lua_remove(L, base);  /* remove traceback function */
    /* force a complete garbage collection in case of errors */
    if (status != 0) lua_gc(L, LUA_GCCOLLECT, 0);
    return status;
}


static void printVersion() {
    luaMessage(nullptr, LUMEN_RELEASE " -- " LUMEN_COPYRIGHT);
}


static int getArgs(lua_State *L, char **argv, int n) {
    int nArg;
    int i;
    int argc = 0;
    while (argv[argc]) argc++;  /* count total number of arguments */
    nArg = argc - (n + 1);  /* number of arguments to the script */
    luaL_checkstack(L, nArg + 3, "too many arguments to script");
    for (i = n + 1; i < argc; i++)
        lua_pushstring(L, argv[i]);
    lua_createtable(L, nArg, n + 1);
    for (i = 0; i < argc; i++) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - n);
    }
    return nArg;
}


static int doFile(lua_State *L, const char *name) {
    int status = luaL_loadfile(L, name) || doCall(L, 0, 1);
    return report(L, status);
}


static int doString(lua_State *L, const char *s, const char *name) {
    int status = luaL_loadbuffer(L, s, strlen(s), name) || doCall(L, 0, 1);
    return report(L, status);
}


static int doLibrary(lua_State *L, const char *name) {
    lua_getglobal(L, "require");
    lua_pushstring(L, name);
    return report(L, doCall(L, 1, 1));
}


static const char *getPrompt(lua_State *L, int firstLine) {
    const char *p;
    lua_getfield(L, LUA_GLOBALSINDEX, firstLine ? "_PROMPT" : "_PROMPT2");
    p = lua_tostring(L, -1);
    if (p == nullptr) p = (firstLine ? LUA_PROMPT : LUA_PROMPT2);
    lua_pop(L, 1);  /* remove global */
    return p;
}


static int inComplete(lua_State *L, int status) {
    if (status == LUA_ERRSYNTAX) {
        size_t msgLength;
        const char *msg = lua_tolstring(L, -1, &msgLength);
        const char *tp = msg + msgLength - (sizeof(LUA_QL("<eof>")) - 1);
        if (strstr(msg, LUA_QL("<eof>")) == tp) {
            lua_pop(L, 1);
            return 1;
        }
    }
    return 0;  /* else... */
}


static int pushLine(lua_State *L, int firstLine) {
    char buffer[LUA_MAXINPUT];
    char *b = buffer;
    size_t l;
    const char *prompt = getPrompt(L, firstLine);
    if (lua_readline(L, b, prompt) == 0)
        return 0;  /* no input */
    l = strlen(b);
    if (l > 0 && b[l - 1] == '\n')  /* line ends with newline? */
        b[l - 1] = '\0';  /* remove it */
    if (firstLine && b[0] == '=')  /* first line starts with `=` ? */
        lua_pushfstring(L, "return %s", b + 1);  /* change it to `return` */
    else
        lua_pushstring(L, b);
    lua_freeline(L, b);
    return 1;
}


static int loadLine(lua_State *L) {
    int status;
    lua_settop(L, 0);
    if (!pushLine(L, 1))
        return -1;  /* no input */
    for (;;) {  /* repeat until gets a complete line */
        status = luaL_loadbuffer(L, lua_tostring(L, 1), lua_strlen(L, 1), "=stdin");
        if (!inComplete(L, status)) break;  /* cannot try to add lines? */
        if (!pushLine(L, 0))  /* no more input? */
            return -1;
        lua_pushliteral(L, "\n");  /* add a new line... */
        lua_insert(L, -2);  /* ...between the two lines */
        lua_concat(L, 3);  /* join them */
    }
    lua_saveline(L, 1);
    lua_remove(L, 1);  /* remove line */
    return status;
}


static void dotty(lua_State *L) {
    int status;
    const char *oldProgramName = programName;
    programName = nullptr;
    while ((status = loadLine(L)) != -1) {
        if (status == 0) status = doCall(L, 0, 0);
        report(L, status);
        if (status == 0 && lua_gettop(L) > 0) {  /* any result to print? */
            lua_getglobal(L, "print");
            lua_insert(L, 1);
            if (lua_pcall(L, lua_gettop(L) - 1, 0, 0) != 0)
                luaMessage(programName, lua_pushfstring(L,
                                                        "error calling " LUA_QL("print") " (%s)",
                                                        lua_tostring(L, -1)));
        }
    }
    lua_settop(L, 0);  /* clear stack */
    fputs("\n", stdout);
    fflush(stdout);
    programName = oldProgramName;
}


static int handleScript(lua_State *L, char **argv, int n) {
    int status;
    const char *fileName;
    int nArg = getArgs(L, argv, n);  /* collect arguments */
    lua_setglobal(L, "arg");
    fileName = argv[n];
    if (strcmp(fileName, "-") == 0 && strcmp(argv[n - 1], "--") != 0)
        fileName = nullptr;  /* stdin */
    status = luaL_loadfile(L, fileName);
    lua_insert(L, -(nArg + 1));
    if (status == 0)
        status = doCall(L, nArg, 0);
    else
        lua_pop(L, nArg);
    return report(L, status);
}


/* check that argument has no extra characters at the end */
#define noTail(x)   do { if ((x)[2] != '\0') return -1; } while (0)


static int collectArgs(char **argv, int *pi, int *pv, int *pe) {
    int i;
    for (i = 1; argv[i] != nullptr; i++) {
        if (argv[i][0] != '-')  /* not an option? */
            return i;
        switch (argv[i][1]) {  /* option */
            case '-':
                noTail(argv[i]);
                return (argv[i + 1] != nullptr ? i + 1 : 0);
            case '\0':
                return i;
            case 'i':
                noTail(argv[i]);
                *pi = 1;  /* go through */
            case 'v':
                noTail(argv[i]);
                *pv = 1;
                break;
            case 'e':
                *pe = 1;  /* go through */
            case 'l':
                if (argv[i][2] == '\0') {
                    i++;
                    if (argv[i] == nullptr) return -1;
                }
                break;
            default:
                return -1;  /* invalid option */
        }
    }
    return 0;
}


static int runArgs(lua_State *L, char **argv, int n) {
    int i;
    for (i = 1; i < n; i++) {
        if (argv[i] == nullptr) continue;
        lua_assert(argv[i][0] == '-');
        switch (argv[i][1]) {  /* option */
            case 'e': {
                const char *chunk = argv[i] + 2;
                if (*chunk == '\0') chunk = argv[++i];
                lua_assert(chunk != nullptr);
                if (doString(L, chunk, "=(command line)") != 0)
                    return 1;
                break;
            }
            case 'l': {
                const char *filename = argv[i] + 2;
                if (*filename == '\0') filename = argv[++i];
                lua_assert(filename != nullptr);
                if (doLibrary(L, filename))
                    return 1;  /* stop if file fails */
                break;
            }
            default:
                break;
        }
    }
    return 0;
}


static int handleLuaInit(lua_State *L) {
    const char *init = getenv(LUA_INIT);
    if (init == nullptr) return 0;  /* status OK */
    else if (init[0] == '@')
        return doFile(L, init + 1);
    else
        return doString(L, init, "=" LUA_INIT);
}


struct MainArgs {
    int argc;
    char **argv;
    int status;
};


static int pMain(lua_State *L) {
    auto s = reinterpret_cast<MainArgs *>(lua_touserdata(L, 1));
    char **argv = s->argv;
    int script;
    int has_i = 0, has_v = 0, has_e = 0;
    globalL = L;
    if (argv[0] && argv[0][0]) programName = argv[0];
    lua_gc(L, LUA_GCSTOP, 0);  /* stop collector during initialization */
    luaL_openlibs(L);  /* open libraries */
    lua_gc(L, LUA_GCRESTART, 0);
    s->status = handleLuaInit(L);
    if (s->status != 0) return 0;
    script = collectArgs(argv, &has_i, &has_v, &has_e);
    if (script < 0) {  /* invalid args? */
        printUsage();
        s->status = 1;
        return 0;
    }
    if (has_v) printVersion();
    s->status = runArgs(L, argv, (script > 0) ? script : s->argc);
    if (s->status != 0) return 0;
    if (script)
        s->status = handleScript(L, argv, script);
    if (s->status != 0) return 0;
    if (has_i)
        dotty(L);
    else if (script == 0 && !has_e && !has_v) {
        if (lua_stdin_is_tty()) {
            printVersion();
            dotty(L);
        } else doFile(L, nullptr);  /* executes stdin as a file */
    }
    return 0;
}


int main(int argc, char **argv) {
    int status;
    lua_State *L = lua_open();  /* create state */
    if (L == nullptr) {
        luaMessage(argv[0], "cannot create state: not enough memory");
        return EXIT_FAILURE;
    }
    MainArgs s{argc, argv, 0};
    status = lua_cpcall(L, &pMain, &s);
    report(L, status);
    lua_close(L);
    return (status || s.status) ? EXIT_FAILURE : EXIT_SUCCESS;
}

