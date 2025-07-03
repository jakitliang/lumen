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

#include "lua.hpp"

#define lua_assert(c) ((void) 0)

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
    fgets(b, LUA_MAX_INPUT, stdin) != nullptr)  /* get line */
#define lua_saveline(L, idx)    do { (void)L; (void)idx; } while (0)
#define lua_freeline(L, b)    do { (void)L; (void)b; } while (0)
#endif

static Lua::State *globalL = nullptr;

static const char *programName = LUA_PROGRAM_NAME;


static void luaStop(Lua::State *L, Lua::DebugInfo *) {
//    (void) ar;  /* unused arg. */
    L->SetHook(nullptr, 0, 0);
    L->Error("interrupted!");
}


static void luaAction(int i) {
    signal(i, SIG_DFL); /* if another SIGINT happens before luaStop,
                              terminate process (default action) */
    globalL->SetHook(luaStop, Lua::HookMaskCall | Lua::HookMaskRet | Lua::HookMaskCount, 1);
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


static int report(Lua::State *L, int status) {
    if (status && !L->IsNil(-1)) {
        const char *msg = L->ToString(-1);
        if (msg == nullptr) msg = "(error object is not a string)";
        luaMessage(programName, msg);
        L->Pop(1);
    }
    return status;
}


static int traceback(Lua::State *L) {
    if (!L->IsString(1))  /* 'message' not a string? */
        return 1;  /* keep it intact */
    L->GetField(Lua::GlobalIndex, "debug");
    if (!L->IsTable(-1)) {
        L->Pop(1);
        return 1;
    }
    L->GetField(-1, "traceback");
    if (!L->IsFunction(-1)) {
        L->Pop(2);
        return 1;
    }
    L->PushValue(1);  /* pass error message */
    L->PushInteger(2);  /* skip this function and traceback */
    L->Call(2, 1);  /* call debug.traceback */
    return 1;
}


static int doCall(Lua::State *L, int nArg, int clear) {
    int status;
    int base = L->GetTop() - nArg;  /* function index */
    L->PushDelegate(traceback);  /* push traceback function */
    L->Insert(base);  /* put it under chunk and args */
    signal(SIGINT, luaAction);
    status = L->TryCall(nArg, (clear ? 0 : Lua::RetMul), base);
    signal(SIGINT, SIG_DFL);
    L->Remove(base);  /* remove traceback function */
    /* force a complete garbage collection in case of errors */
    if (status != 0) L->GC(Lua::GCCollect, 0);
    return status;
}


static void printVersion() {
    luaMessage(nullptr, LUMEN_RELEASE " -- " LUMEN_COPYRIGHT);
}


static int getArgs(Lua::State *L, char **argv, int n) {
    int nArg;
    int i;
    int argc = 0;
    while (argv[argc]) argc++;  /* count total number of arguments */
    nArg = argc - (n + 1);  /* number of arguments to the script */
    L->CheckStack(nArg + 3, "too many arguments to script");
    for (i = n + 1; i < argc; i++)
        L->PushString(argv[i]);
    L->CreateTable(nArg, n + 1);
    for (i = 0; i < argc; i++) {
        L->PushString(argv[i]);
        L->RawSetAt(-2, i - n);
    }
    return nArg;
}


static int doFile(Lua::State *L, const char *name) {
    int status = L->LoadFile(name) || doCall(L, 0, 1);
    return report(L, status);
}


static int doString(Lua::State *L, const char *s, const char *name) {
    int status = L->LoadBuffer(s, strlen(s), name) || doCall(L, 0, 1);
    return report(L, status);
}


static int doLibrary(Lua::State *L, const char *name) {
    L->GetGlobal("require");
    L->PushString(name);
    return report(L, doCall(L, 1, 1));
}


static const char *getPrompt(Lua::State *L, int firstLine) {
    const char *p;
    L->GetField(Lua::GlobalIndex, firstLine ? "_PROMPT" : "_PROMPT2");
    p = L->ToString(-1);
    if (p == nullptr) p = (firstLine ? LUA_PROMPT : LUA_PROMPT2);
    L->Pop(1);  /* remove global */
    return p;
}


static int inComplete(Lua::State *L, int status) {
    if (status == Lua::RetErrSyntax) {
        size_t msgLength;
        const char *msg = L->ToString(-1, &msgLength);
        const char *tp = msg + msgLength - (sizeof(LUA_QL("<eof>")) - 1);
        if (strstr(msg, LUA_QL("<eof>")) == tp) {
            L->Pop(1);
            return 1;
        }
    }
    return 0;  /* else... */
}


static int pushLine(Lua::State *L, int firstLine) {
    char buffer[LUA_MAX_INPUT];
    char *b = buffer;
    size_t l;
    const char *prompt = getPrompt(L, firstLine);
    if (lua_readline(L, b, prompt) == 0)
        return 0;  /* no input */
    l = strlen(b);
    if (l > 0 && b[l - 1] == '\n')  /* line ends with newline? */
        b[l - 1] = '\0';  /* remove it */
    if (firstLine && b[0] == '=')  /* first line starts with `=` ? */
        L->PushFString("return %s", b + 1);  /* change it to `return` */
    else
        L->PushString(b);
    lua_freeline(L, b);
    return 1;
}


static int loadLine(Lua::State *L) {
    int status;
    L->SetTop(0);
    if (!pushLine(L, 1))
        return -1;  /* no input */
    for (;;) {  /* repeat until gets a complete line */
        status = L->LoadBuffer(L->ToString(1), L->StringLength(1), "=stdin");
        if (!inComplete(L, status)) break;  /* cannot try to add lines? */
        if (!pushLine(L, 0))  /* no more input? */
            return -1;
        L->PushLiteral("\n");  /* add a new line... */
        L->Insert(-2);  /* ...between the two lines */
        L->Concat(3);  /* join them */
    }
    lua_saveline(L, 1);
    L->Remove(1);  /* remove line */
    return status;
}


static void dotty(Lua::State *L) {
    int status;
    const char *oldProgramName = programName;
    programName = nullptr;
    while ((status = loadLine(L)) != -1) {
        if (status == 0) status = doCall(L, 0, 0);
        report(L, status);
        if (status == 0 && L->GetTop() > 0) {  /* any result to print? */
            L->GetGlobal("print");
            L->Insert(1);
            if (L->TryCall(L->GetTop() - 1, 0, 0) != 0)
                luaMessage(programName, L->PushFString("error calling " LUA_QL("print") " (%s)",
                                                       L->ToString(-1)));
        }
    }
    L->SetTop(0);  /* clear stack */
    fputs("\n", stdout);
    fflush(stdout);
    programName = oldProgramName;
}


static int handleScript(Lua::State *L, char **argv, int n) {
    int status;
    const char *fileName;
    int nArg = getArgs(L, argv, n);  /* collect arguments */
    L->SetGlobal("arg");
    fileName = argv[n];
    if (strcmp(fileName, "-") == 0 && strcmp(argv[n - 1], "--") != 0)
        fileName = nullptr;  /* stdin */
    status = L->LoadFile(fileName);
    L->Insert(-(nArg + 1));
    if (status == 0)
        status = doCall(L, nArg, 0);
    else
        L->Pop(nArg);
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


static int runArgs(Lua::State *L, char **argv, int n) {
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


static int handleLuaInit(Lua::State *L) {
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


static int pMain(Lua::State *L) {
    auto s = reinterpret_cast<MainArgs *>(L->ToUserdata(1));
    char **argv = s->argv;
    int script;
    int has_i = 0, has_v = 0, has_e = 0;
    globalL = L;
    if (argv[0] && argv[0][0]) programName = argv[0];
    L->GC(Lua::GCStop, 0);  /* stop collector during initialization */
    L->OpenLibs();  /* open libraries */
    L->GC(Lua::GCRestart, 0);
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
    Lua::State *L = Lua::Open();  /* create state */
    if (L == nullptr) {
        luaMessage(argv[0], "cannot create state: not enough memory");
        return EXIT_FAILURE;
    }
    MainArgs s{argc, argv, 0};
    status = L->TryCall(&pMain, &s);
    report(L, status);
    Lua::Close(L);
    return (status || s.status) ? EXIT_FAILURE : EXIT_SUCCESS;
}

