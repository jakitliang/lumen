/*!
 * @brief Opcodes for Lua virtual machine
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#define lopcodes_c
#define LUA_CORE


#include "lua/opcodes.h"


/* ORDER OP */

const char *const Lua::OpNames[Lua::OpCodeCount + 1] = {
        "MOVE",
        "LOADK",
        "LOADBOOL",
        "LOADNIL",
        "GETUPVAL",
        "GETGLOBAL",
        "GETTABLE",
        "SETGLOBAL",
        "SETUPVAL",
        "SETTABLE",
        "NEWTABLE",
        "SELF",
        "ADD",
        "SUB",
        "MUL",
        "DIV",
        "MOD",
        "POW",
        "UNM",
        "NOT",
        "LEN",
        "CONCAT",
        "JMP",
        "EQ",
        "LT",
        "LE",
        "TEST",
        "TESTSET",
        "CALL",
        "TAILCALL",
        "RETURN",
        "FORLOOP",
        "FORPREP",
        "TFORLOOP",
        "SETLIST",
        "CLOSE",
        "CLOSURE",
        "VARARG",
        nullptr
};


#define opMode(t, a, b, c, m) (((t)<<7) | ((a)<<6) | ((b)<<4) | ((c)<<2) | (m))

const Lua::Byte Lua::OpModes[Lua::OpCodeCount] = {
        /*     T  A    B             C            mode		               opcode	*/
        opMode(0, 1, Lua::OpArgR, Lua::OpArgN, Lua::OpModeIABC)        /* Lua::OpCodeMove */
        , opMode(0, 1, Lua::OpArgK, Lua::OpArgN, Lua::OpModeIABx)        /* Lua::OpCodeLoadK */
        , opMode(0, 1, Lua::OpArgU, Lua::OpArgU, Lua::OpModeIABC)        /* Lua::OpCodeLoadBool */
        , opMode(0, 1, Lua::OpArgR, Lua::OpArgN, Lua::OpModeIABC)        /* Lua::OpCodeLoadNil */
        , opMode(0, 1, Lua::OpArgU, Lua::OpArgN, Lua::OpModeIABC)        /* Lua::OpCodeGetUpVal */
        , opMode(0, 1, Lua::OpArgK, Lua::OpArgN, Lua::OpModeIABx)        /* Lua::OpCodeGetGlobal */
        , opMode(0, 1, Lua::OpArgR, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeGetTable */
        , opMode(0, 0, Lua::OpArgK, Lua::OpArgN, Lua::OpModeIABx)        /* Lua::OpCodeSetGlobal */
        , opMode(0, 0, Lua::OpArgU, Lua::OpArgN, Lua::OpModeIABC)        /* Lua::OpCodeSetUpVal */
        , opMode(0, 0, Lua::OpArgK, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeSetTable */
        , opMode(0, 1, Lua::OpArgU, Lua::OpArgU, Lua::OpModeIABC)        /* Lua::OpCodeNewTable */
        , opMode(0, 1, Lua::OpArgR, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeSelf */
        , opMode(0, 1, Lua::OpArgK, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeAdd */
        , opMode(0, 1, Lua::OpArgK, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeSub */
        , opMode(0, 1, Lua::OpArgK, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeMul */
        , opMode(0, 1, Lua::OpArgK, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeDiv */
        , opMode(0, 1, Lua::OpArgK, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeMod */
        , opMode(0, 1, Lua::OpArgK, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodePow */
        , opMode(0, 1, Lua::OpArgR, Lua::OpArgN, Lua::OpModeIABC)        /* Lua::OpCodeUnm */
        , opMode(0, 1, Lua::OpArgR, Lua::OpArgN, Lua::OpModeIABC)        /* Lua::OpCodeNot */
        , opMode(0, 1, Lua::OpArgR, Lua::OpArgN, Lua::OpModeIABC)        /* Lua::OpCodeLen */
        , opMode(0, 1, Lua::OpArgR, Lua::OpArgR, Lua::OpModeIABC)        /* Lua::OpCodeConcat */
        , opMode(0, 0, Lua::OpArgR, Lua::OpArgN, Lua::OpModeIAsBx)        /* Lua::OpCodeJump */
        , opMode(1, 0, Lua::OpArgK, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeEQ */
        , opMode(1, 0, Lua::OpArgK, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeLT */
        , opMode(1, 0, Lua::OpArgK, Lua::OpArgK, Lua::OpModeIABC)        /* Lua::OpCodeLE */
        , opMode(1, 1, Lua::OpArgR, Lua::OpArgU, Lua::OpModeIABC)        /* Lua::OpCodeTest */
        , opMode(1, 1, Lua::OpArgR, Lua::OpArgU, Lua::OpModeIABC)        /* Lua::OpCodeTestTest */
        , opMode(0, 1, Lua::OpArgU, Lua::OpArgU, Lua::OpModeIABC)        /* Lua::OpCodeCall */
        , opMode(0, 1, Lua::OpArgU, Lua::OpArgU, Lua::OpModeIABC)        /* Lua::OpCodeTailCall */
        , opMode(0, 0, Lua::OpArgU, Lua::OpArgN, Lua::OpModeIABC)        /* Lua::OpCodeReturn */
        , opMode(0, 1, Lua::OpArgR, Lua::OpArgN, Lua::OpModeIAsBx)        /* Lua::OpCodeForLoop */
        , opMode(0, 1, Lua::OpArgR, Lua::OpArgN, Lua::OpModeIAsBx)        /* Lua::OpCodeForPrep */
        , opMode(1, 0, Lua::OpArgN, Lua::OpArgU, Lua::OpModeIABC)        /* Lua::OpCodeTForLoop */
        , opMode(0, 0, Lua::OpArgU, Lua::OpArgU, Lua::OpModeIABC)        /* Lua::OpCodeSetList */
        , opMode(0, 0, Lua::OpArgN, Lua::OpArgN, Lua::OpModeIABC)        /* Lua::OpCodeClose */
        , opMode(0, 1, Lua::OpArgU, Lua::OpArgN, Lua::OpModeIABx)        /* Lua::OpCodeClosure */
        , opMode(0, 1, Lua::OpArgU, Lua::OpArgN, Lua::OpModeIABC)        /* Lua::OpCodeVararg */
};

