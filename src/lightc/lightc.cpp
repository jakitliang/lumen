/*!
 * @brief Lua compiler (saves bytecodes to files; also list bytecodes)
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define lumenc_c
#define LUA_CORE

#include "lumen/do.h"
#include "lumen/memory.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/opcodes.h"
#include "lumen/string.h"
#include "lumen/undump.h"

#include "lumen.h"

#define PROGRAM_NAME    "lumenc"        /* default program name */
#define OUTPUT      PROGRAM_NAME ".out"    /* default output file */

static int listing = 0;            /* list bytecodes? */
static int dumping = 1;            /* dump bytecodes? */
static int stripping = 0;            /* strip debug information? */
static char Output[] = {OUTPUT};    /* default output file name */
static const char *output = Output;    /* actual output file name */
static const char *programName = PROGRAM_NAME;    /* actual program name */

static void fatal(const char *message) {
    fprintf(stderr, "%s: %s\n", programName, message);
    exit(EXIT_FAILURE);
}

static void cannot(const char *what) {
    fprintf(stderr, "%s: cannot %s %s: %s\n", programName, what, output, strerror(errno));
    exit(EXIT_FAILURE);
}

static void usage(const char *message) {
    if (*message == '-')
        fprintf(stderr, "%s: unrecognized option " LUA_QS "\n", programName, message);
    else
        fprintf(stderr, "%s: %s\n", programName, message);
    fprintf(stderr,
            "usage: %s [options] [filenames].\n"
            "Available options are:\n"
            "  -        process stdin\n"
            "  -l       list\n"
            "  -o name  output to file " LUA_QL("name") " (default is \"%s\")\n"
            "  -p       parse only\n"
            "  -s       strip debug information\n"
            "  -v       show version information\n"
            "  --       stop handling options\n",
            programName, Output);
    exit(EXIT_FAILURE);
}

#define    IS(s)    (strcmp(argv[i],s)==0)

static int doargs(int argc, char *argv[]) {
    int i;
    int version = 0;
    if (argv[0] != nullptr && *argv[0] != 0) programName = argv[0];
    for (i = 1; i < argc; i++) {
        if (*argv[i] != '-')            /* end of options; keep it */
            break;
        else if (IS("--"))            /* end of options; skip it */
        {
            ++i;
            if (version) ++version;
            break;
        } else if (IS("-"))            /* end of options; use stdin */
            break;
        else if (IS("-l"))            /* list */
            ++listing;
        else if (IS("-o"))            /* output file */
        {
            output = argv[++i];
            if (output == nullptr || *output == 0) usage(LUA_QL("-o") " needs argument");
            if (IS("-")) output = nullptr;
        } else if (IS("-p"))            /* parse only */
            dumping = 0;
        else if (IS("-s"))            /* strip debug information */
            stripping = 1;
        else if (IS("-v"))            /* show version */
            ++version;
        else                    /* unknown option */
            usage(argv[i]);
    }
    if (i == argc && (listing || !dumping)) {
        dumping = 0;
        argv[--i] = Output;
    }
    if (version) {
        printf("%s  %s\n", LUMEN_RELEASE, LUMEN_COPYRIGHT);
        if (version == argc - 1) exit(EXIT_SUCCESS);
    }
    return i;
}

#define toProto(L, i) ((L->Top+(i))->GetClosure()->AsLua.Func)

// NOLINTNEXTLINE
#define ToLumen(L) reinterpret_cast<Lumen::State *>(L)
#define ToLumenIState(L) reinterpret_cast<Lumen::IState *>(L)

static const Lumen::Proto *combine(Lumen::State *L, int n) {
    if (n == 1)
        return toProto(L, -1);
    else {
        int i, pc;
        Lumen::Proto *f = Lumen::Proto::New(L);
        LumenSetProtoValue2S(L, L->Top, f);
        LumenIncrTop(L);
        f->Source = LumenStringNewLiteral(L, "=(" PROGRAM_NAME ")");
        f->MaxStackSize = 1;
        pc = 2 * n + 1;
        f->Code = LumenMemoryNewVector(L, pc, Lumen::Instruction);
        f->CodeCount = pc;
        f->SubProto = LumenMemoryNewVector(L, n, Lumen::Proto*);
        f->SubProtoCount = n;
        pc = 0;
        for (i = 0; i < n; i++) {
            f->SubProto[i] = toProto(L, i - n - 1);
            f->Code[pc++] = LumenOpCodeCreateABx(Lumen::OpCodeClosure, 0, i);
            f->Code[pc++] = LumenOpCodeCreateABC(Lumen::OpCodeCall, 0, 1, 1);
        }
        f->Code[pc++] = LumenOpCodeCreateABC(Lumen::OpCodeReturn, 0, 1, 0);
        return f;
    }
}

static int writer(Lumen::IState *, const void *p, size_t size, void *u) {
    return (fwrite(p, size, 1, (FILE *) u) != 1) && (size != 0);
}

struct MainArgs {
    int argc;
    char **argv;
};

static int pMain(Lumen::IState *L) {
    auto s = reinterpret_cast<MainArgs *>(L->ToUserdata(1));
    int argc = s->argc;
    char **argv = s->argv;
    const Lumen::Proto *f;
    int i;
    if (!L->CheckStack(argc)) fatal("too many input files");
    for (i = 0; i < argc; i++) {
        const char *filename = IS("-") ? nullptr : argv[i];
        if (L->LoadFile(filename) != 0) fatal(L->ToString(-1));
    }
    f = combine(ToLumen(L), argc);
    if (listing) Lumen::Dumper::Print(f, listing > 1);
    if (dumping) {
        FILE *D = (output == nullptr) ? stdout : fopen(output, "wb");
        if (D == nullptr) cannot("open");
        LumenLock(ToLumen(L));
        Lumen::Dumper::Dump(ToLumen(L), f, writer, D, stripping);
        LumenUnlock(ToLumen(L));
        if (ferror(D)) cannot("write");
        if (fclose(D)) cannot("close");
    }
    return 0;
}

int main(int argc, char *argv[]) {
    Lumen::IState *L;
    int i = doargs(argc, argv);
    argc -= i;
    argv += i;
    if (argc <= 0) usage("no input files given");
    L = Lumen::Open();
    if (L == nullptr) fatal("not enough memory for state");
    MainArgs s{argc, argv};
    if (L->TryCall(pMain, &s) != 0) fatal(L->ToString(-1));
    Lumen::Close(L);
    return EXIT_SUCCESS;
}
