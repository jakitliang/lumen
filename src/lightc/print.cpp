/*!
 * @brief print bytecodes
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cctype>
#include <cstdio>

#define lumenc_c
#define LUA_CORE

#include "lumen/debug.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/opcodes.h"
#include "lumen/undump.h"

#define PrintFunction    Lumen::Dumper::Print

#define Sizeof(x)    ((int)sizeof(x))
#define VOID(p)        ((const void*)(p))

static void PrintString(const Lumen::String *ts) {
    const char *s = ts->CString();
    size_t i, n = ts->Length;
    putchar('"');
    for (i = 0; i < n; i++) {
        int c = s[i];
        switch (c) {
            case '"':
                printf("\\\"");
                break;
            case '\\':
                printf("\\\\");
                break;
            case '\a':
                printf("\\a");
                break;
            case '\b':
                printf("\\b");
                break;
            case '\f':
                printf("\\f");
                break;
            case '\n':
                printf("\\n");
                break;
            case '\r':
                printf("\\r");
                break;
            case '\t':
                printf("\\t");
                break;
            case '\v':
                printf("\\v");
                break;
            default:
                if (isprint((unsigned char) c))
                    putchar(c);
                else
                    printf("\\%03u", (unsigned char) c);
        }
    }
    putchar('"');
}

static void PrintConstant(const Lumen::Proto *f, int i) {
    const Lumen::Object *o = &f->K[i];
    switch (o->Type) {
        case Lumen::TypeNil:
            printf("nil");
            break;
        case Lumen::TypeBool:
            printf(o->GetBool() ? "true" : "false");
            break;
        case Lumen::TypeNumber:
            printf(LUA_NUMBER_FMT, o->GetNumber());
            break;
        case Lumen::TypeString:
            PrintString(o->GetString());
            break;
        default:                /* cannot happen */
            printf("? type=%d", o->Type);
            break;
    }
}

static void PrintCode(const Lumen::Proto *f) {
    const Lumen::Instruction *code = f->Code;
    int pc, n = f->CodeCount;
    for (pc = 0; pc < n; pc++) {
        Lumen::Instruction i = code[pc];
        Lumen::OpCode o = LumenOpCodeGet(i);
        int a = LumenOpCodeGetArgA(i);
        int b = LumenOpCodeGetArgB(i);
        int c = LumenOpCodeGetArgC(i);
        int bx = LumenOpCodeGetArgBx(i);
        int sbx = LumenOpCodeGetArgsBx(i);
        int line = LumenDebugGetLine(f, pc);
        printf("\t%d\t", pc + 1);
        if (line > 0) printf("[%d]\t", line); else printf("[-]\t");
        printf("%-9s\t", Lumen::OpNames[o]);
        switch (LumenGetOpMode(o)) {
            case Lumen::OpModeIABC:
                printf("%d", a);
                if (LumenGetBMode(o) != Lumen::OpArgN) printf(" %d", LumenOpCodeIsK(b) ? (-1 - LumenOpCodeIndexK(b)) : b);
                if (LumenGetCMode(o) != Lumen::OpArgN) printf(" %d", LumenOpCodeIsK(c) ? (-1 - LumenOpCodeIndexK(c)) : c);
                break;
            case Lumen::OpModeIABx:
                if (LumenGetBMode(o) == Lumen::OpArgK) printf("%d %d", a, -1 - bx);
                else printf("%d %d", a, bx);
                break;
            case Lumen::OpModeIAsBx:
                if (o == Lumen::OpCodeJump) printf("%d", sbx);
                else printf("%d %d", a, sbx);
                break;
        }
        switch (o) {
            case Lumen::OpCodeLoadK:
                printf("\t; ");
                PrintConstant(f, bx);
                break;
            case Lumen::OpCodeGetUpVal:
            case Lumen::OpCodeSetUpVal:
                printf("\t; %s", (f->UpValuesCount > 0) ? (f->UpValues[b])->CString() : "-");
                break;
            case Lumen::OpCodeGetGlobal:
            case Lumen::OpCodeSetGlobal:
                printf("\t; %s", (&f->K[bx])->ToCString());
                break;
            case Lumen::OpCodeGetTable:
            case Lumen::OpCodeSelf:
                if (LumenOpCodeIsK(c)) {
                    printf("\t; ");
                    PrintConstant(f, LumenOpCodeIndexK(c));
                }
                break;
            case Lumen::OpCodeSetTable:
            case Lumen::OpCodeAdd:
            case Lumen::OpCodeSub:
            case Lumen::OpCodeMul:
            case Lumen::OpCodeDiv:
            case Lumen::OpCodePow:
            case Lumen::OpCodeEQ:
            case Lumen::OpCodeLT:
            case Lumen::OpCodeLE:
                if (LumenOpCodeIsK(b) || LumenOpCodeIsK(c)) {
                    printf("\t; ");
                    if (LumenOpCodeIsK(b)) PrintConstant(f, LumenOpCodeIndexK(b)); else printf("-");
                    printf(" ");
                    if (LumenOpCodeIsK(c)) PrintConstant(f, LumenOpCodeIndexK(c)); else printf("-");
                }
                break;
            case Lumen::OpCodeJump:
            case Lumen::OpCodeForLoop:
            case Lumen::OpCodeForPrep:
                printf("\t; to %d", sbx + pc + 2);
                break;
            case Lumen::OpCodeClosure:
                printf("\t; %p", VOID(f->SubProto[bx]));
                break;
            case Lumen::OpCodeSetList:
                if (c == 0) printf("\t; %d", (int) code[++pc]);
                else printf("\t; %d", c);
                break;
            default:
                break;
        }
        printf("\n");
    }
}

#define SS(x)    (x==1)?"":"s"
#define S(x)    x,SS(x)

static void PrintHeader(const Lumen::Proto *f) {
    const char *s = f->Source->CString();
    if (*s == '@' || *s == '=')
        s++;
    else if (*s == LUA_SIGNATURE[0])
        s = "(bstring)";
    else
        s = "(string)";
    printf("\n%s <%s:%d,%d> (%d instruction%s, %d bytes at %p)\n",
           (f->LineDefined == 0) ? "main" : "function", s,
           f->LineDefined, f->LastLineDefined,
           S(f->CodeCount), f->CodeCount * Sizeof(Lumen::Instruction), VOID(f));
    printf("%d%s param%s, %d slot%s, %d upvalue%s, ",
           f->NUmParams, f->IsVararg ? "+" : "", SS(f->NUmParams),
           S(f->MaxStackSize), S(f->NUpValues));
    printf("%d local%s, %d constant%s, %d function%s\n",
           S(f->LocalVarsCount), S(f->KCount), S(f->SubProtoCount));
}

static void PrintConstants(const Lumen::Proto *f) {
    int i, n = f->KCount;
    printf("constants (%d) for %p:\n", n, VOID(f));
    for (i = 0; i < n; i++) {
        printf("\t%d\t", i + 1);
        PrintConstant(f, i);
        printf("\n");
    }
}

static void PrintLocals(const Lumen::Proto *f) {
    int i, n = f->LocalVarsCount;
    printf("locals (%d) for %p:\n", n, VOID(f));
    for (i = 0; i < n; i++) {
        printf("\t%d\t%s\t%d\t%d\n",
               i, (f->LocalVars[i].VarName)->CString(), f->LocalVars[i].StartPC + 1, f->LocalVars[i].EndPC + 1);
    }
}

static void PrintUpvalues(const Lumen::Proto *f) {
    int i, n = f->UpValuesCount;
    printf("upvalues (%d) for %p:\n", n, VOID(f));
    if (f->UpValues == nullptr) return;
    for (i = 0; i < n; i++) {
        printf("\t%d\t%s\n", i, (f->UpValues[i])->CString());
    }
}

void PrintFunction(const Lumen::Proto *f, int full) {
    int i, n = f->SubProtoCount;
    PrintHeader(f);
    PrintCode(f);
    if (full) {
        PrintConstants(f);
        PrintLocals(f);
        PrintUpvalues(f);
    }
    for (i = 0; i < n; i++) PrintFunction(f->SubProto[i], full);
}
