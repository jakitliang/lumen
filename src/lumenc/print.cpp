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

#include "lua/debug.h"
#include "lua/object.h"
#include "lua/opcodes.h"
#include "lua/undump.h"

#define PrintFunction    Lua::Dumper::Print

#define Sizeof(x)    ((int)sizeof(x))
#define VOID(p)        ((const void*)(p))

static void PrintString(const Lua::String *ts) {
    const char *s = LuaStringCString(ts);
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

static void PrintConstant(const Lua::Proto *f, int i) {
    const Lua::Value *o = &f->K[i];
    switch (LuaTypeOf(o)) {
        case LUA_TNIL:
            printf("nil");
            break;
        case LUA_TBOOLEAN:
            printf(LuaBoolValue(o) ? "true" : "false");
            break;
        case LUA_TNUMBER:
            printf(LUA_NUMBER_FMT, LuaNumberValue(o));
            break;
        case LUA_TSTRING:
            PrintString(LuaStringValue(o));
            break;
        default:                /* cannot happen */
            printf("? type=%d", LuaTypeOf(o));
            break;
    }
}

static void PrintCode(const Lua::Proto *f) {
    const Lua::Instruction *code = f->Code;
    int pc, n = f->CodeCount;
    for (pc = 0; pc < n; pc++) {
        Lua::Instruction i = code[pc];
        Lua::OpCode o = LuaOpCodeGet(i);
        int a = LuaOpCodeGetArgA(i);
        int b = LuaOpCodeGetArgB(i);
        int c = LuaOpCodeGetArgC(i);
        int bx = LuaOpCodeGetArgBx(i);
        int sbx = LuaOpCodeGetArgsBx(i);
        int line = LuaDebugGetLine(f, pc);
        printf("\t%d\t", pc + 1);
        if (line > 0) printf("[%d]\t", line); else printf("[-]\t");
        printf("%-9s\t", Lua::OpNames[o]);
        switch (LuaGetOpMode(o)) {
            case Lua::OpModeIABC:
                printf("%d", a);
                if (LuaGetBMode(o) != Lua::OpArgN) printf(" %d", LuaOpCodeIsK(b) ? (-1 - LuaOpCodeIndexK(b)) : b);
                if (LuaGetCMode(o) != Lua::OpArgN) printf(" %d", LuaOpCodeIsK(c) ? (-1 - LuaOpCodeIndexK(c)) : c);
                break;
            case Lua::OpModeIABx:
                if (LuaGetBMode(o) == Lua::OpArgK) printf("%d %d", a, -1 - bx);
                else printf("%d %d", a, bx);
                break;
            case Lua::OpModeIAsBx:
                if (o == Lua::OpCodeJump) printf("%d", sbx);
                else printf("%d %d", a, sbx);
                break;
        }
        switch (o) {
            case Lua::OpCodeLoadK:
                printf("\t; ");
                PrintConstant(f, bx);
                break;
            case Lua::OpCodeGetUpVal:
            case Lua::OpCodeSetUpVal:
                printf("\t; %s", (f->UpValuesCount > 0) ? LuaStringCString(f->UpValues[b]) : "-");
                break;
            case Lua::OpCodeGetGlobal:
            case Lua::OpCodeSetGlobal:
                printf("\t; %s", LuaStringValue2CString(&f->K[bx]));
                break;
            case Lua::OpCodeGetTable:
            case Lua::OpCodeSelf:
                if (LuaOpCodeIsK(c)) {
                    printf("\t; ");
                    PrintConstant(f, LuaOpCodeIndexK(c));
                }
                break;
            case Lua::OpCodeSetTable:
            case Lua::OpCodeAdd:
            case Lua::OpCodeSub:
            case Lua::OpCodeMul:
            case Lua::OpCodeDiv:
            case Lua::OpCodePow:
            case Lua::OpCodeEQ:
            case Lua::OpCodeLT:
            case Lua::OpCodeLE:
                if (LuaOpCodeIsK(b) || LuaOpCodeIsK(c)) {
                    printf("\t; ");
                    if (LuaOpCodeIsK(b)) PrintConstant(f, LuaOpCodeIndexK(b)); else printf("-");
                    printf(" ");
                    if (LuaOpCodeIsK(c)) PrintConstant(f, LuaOpCodeIndexK(c)); else printf("-");
                }
                break;
            case Lua::OpCodeJump:
            case Lua::OpCodeForLoop:
            case Lua::OpCodeForPrep:
                printf("\t; to %d", sbx + pc + 2);
                break;
            case Lua::OpCodeClosure:
                printf("\t; %p", VOID(f->SubProto[bx]));
                break;
            case Lua::OpCodeSetList:
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

static void PrintHeader(const Lua::Proto *f) {
    const char *s = LuaStringCString(f->Source);
    if (*s == '@' || *s == '=')
        s++;
    else if (*s == LUA_SIGNATURE[0])
        s = "(bstring)";
    else
        s = "(string)";
    printf("\n%s <%s:%d,%d> (%d instruction%s, %d bytes at %p)\n",
           (f->LineDefined == 0) ? "main" : "function", s,
           f->LineDefined, f->LastLineDefined,
           S(f->CodeCount), f->CodeCount * Sizeof(Lua::Instruction), VOID(f));
    printf("%d%s param%s, %d slot%s, %d upvalue%s, ",
           f->NUmParams, f->IsVararg ? "+" : "", SS(f->NUmParams),
           S(f->MaxStackSize), S(f->NUpValues));
    printf("%d local%s, %d constant%s, %d function%s\n",
           S(f->LocalVarsCount), S(f->KCount), S(f->SubProtoCount));
}

static void PrintConstants(const Lua::Proto *f) {
    int i, n = f->KCount;
    printf("constants (%d) for %p:\n", n, VOID(f));
    for (i = 0; i < n; i++) {
        printf("\t%d\t", i + 1);
        PrintConstant(f, i);
        printf("\n");
    }
}

static void PrintLocals(const Lua::Proto *f) {
    int i, n = f->LocalVarsCount;
    printf("locals (%d) for %p:\n", n, VOID(f));
    for (i = 0; i < n; i++) {
        printf("\t%d\t%s\t%d\t%d\n",
               i, LuaStringCString(f->LocalVars[i].VarName), f->LocalVars[i].StartPC + 1, f->LocalVars[i].EndPC + 1);
    }
}

static void PrintUpvalues(const Lua::Proto *f) {
    int i, n = f->UpValuesCount;
    printf("upvalues (%d) for %p:\n", n, VOID(f));
    if (f->UpValues == nullptr) return;
    for (i = 0; i < n; i++) {
        printf("\t%d\t%s\n", i, LuaStringCString(f->UpValues[i]));
    }
}

void PrintFunction(const Lua::Proto *f, int full) {
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
