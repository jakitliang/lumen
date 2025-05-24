/*!
 * @brief Opcodes for Lua virtual machine
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#define LUA_CORE

#include "lumen/opcodes.h"


/* ORDER OP */

const char *const Lumen::OpNames[Lumen::OpCodeCount + 1] = {
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

const Lumen::Byte Lumen::OpModes[Lumen::OpCodeCount] = {
        /*     T  A    B             C            mode		               opcode	*/
        opMode(0, 1, Lumen::OpArgR, Lumen::OpArgN, Lumen::OpModeIABC)        /* Lumen::OpCodeMove */
        , opMode(0, 1, Lumen::OpArgK, Lumen::OpArgN, Lumen::OpModeIABx)        /* Lumen::OpCodeLoadK */
        , opMode(0, 1, Lumen::OpArgU, Lumen::OpArgU, Lumen::OpModeIABC)        /* Lumen::OpCodeLoadBool */
        , opMode(0, 1, Lumen::OpArgR, Lumen::OpArgN, Lumen::OpModeIABC)        /* Lumen::OpCodeLoadNil */
        , opMode(0, 1, Lumen::OpArgU, Lumen::OpArgN, Lumen::OpModeIABC)        /* Lumen::OpCodeGetUpVal */
        , opMode(0, 1, Lumen::OpArgK, Lumen::OpArgN, Lumen::OpModeIABx)        /* Lumen::OpCodeGetGlobal */
        , opMode(0, 1, Lumen::OpArgR, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeGetTable */
        , opMode(0, 0, Lumen::OpArgK, Lumen::OpArgN, Lumen::OpModeIABx)        /* Lumen::OpCodeSetGlobal */
        , opMode(0, 0, Lumen::OpArgU, Lumen::OpArgN, Lumen::OpModeIABC)        /* Lumen::OpCodeSetUpVal */
        , opMode(0, 0, Lumen::OpArgK, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeSetTable */
        , opMode(0, 1, Lumen::OpArgU, Lumen::OpArgU, Lumen::OpModeIABC)        /* Lumen::OpCodeNewTable */
        , opMode(0, 1, Lumen::OpArgR, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeSelf */
        , opMode(0, 1, Lumen::OpArgK, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeAdd */
        , opMode(0, 1, Lumen::OpArgK, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeSub */
        , opMode(0, 1, Lumen::OpArgK, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeMul */
        , opMode(0, 1, Lumen::OpArgK, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeDiv */
        , opMode(0, 1, Lumen::OpArgK, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeMod */
        , opMode(0, 1, Lumen::OpArgK, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodePow */
        , opMode(0, 1, Lumen::OpArgR, Lumen::OpArgN, Lumen::OpModeIABC)        /* Lumen::OpCodeUnm */
        , opMode(0, 1, Lumen::OpArgR, Lumen::OpArgN, Lumen::OpModeIABC)        /* Lumen::OpCodeNot */
        , opMode(0, 1, Lumen::OpArgR, Lumen::OpArgN, Lumen::OpModeIABC)        /* Lumen::OpCodeLen */
        , opMode(0, 1, Lumen::OpArgR, Lumen::OpArgR, Lumen::OpModeIABC)        /* Lumen::OpCodeConcat */
        , opMode(0, 0, Lumen::OpArgR, Lumen::OpArgN, Lumen::OpModeIAsBx)        /* Lumen::OpCodeJump */
        , opMode(1, 0, Lumen::OpArgK, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeEQ */
        , opMode(1, 0, Lumen::OpArgK, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeLT */
        , opMode(1, 0, Lumen::OpArgK, Lumen::OpArgK, Lumen::OpModeIABC)        /* Lumen::OpCodeLE */
        , opMode(1, 1, Lumen::OpArgR, Lumen::OpArgU, Lumen::OpModeIABC)        /* Lumen::OpCodeTest */
        , opMode(1, 1, Lumen::OpArgR, Lumen::OpArgU, Lumen::OpModeIABC)        /* Lumen::OpCodeTestTest */
        , opMode(0, 1, Lumen::OpArgU, Lumen::OpArgU, Lumen::OpModeIABC)        /* Lumen::OpCodeCall */
        , opMode(0, 1, Lumen::OpArgU, Lumen::OpArgU, Lumen::OpModeIABC)        /* Lumen::OpCodeTailCall */
        , opMode(0, 0, Lumen::OpArgU, Lumen::OpArgN, Lumen::OpModeIABC)        /* Lumen::OpCodeReturn */
        , opMode(0, 1, Lumen::OpArgR, Lumen::OpArgN, Lumen::OpModeIAsBx)        /* Lumen::OpCodeForLoop */
        , opMode(0, 1, Lumen::OpArgR, Lumen::OpArgN, Lumen::OpModeIAsBx)        /* Lumen::OpCodeForPrep */
        , opMode(1, 0, Lumen::OpArgN, Lumen::OpArgU, Lumen::OpModeIABC)        /* Lumen::OpCodeTForLoop */
        , opMode(0, 0, Lumen::OpArgU, Lumen::OpArgU, Lumen::OpModeIABC)        /* Lumen::OpCodeSetList */
        , opMode(0, 0, Lumen::OpArgN, Lumen::OpArgN, Lumen::OpModeIABC)        /* Lumen::OpCodeClose */
        , opMode(0, 1, Lumen::OpArgU, Lumen::OpArgN, Lumen::OpModeIABx)        /* Lumen::OpCodeClosure */
        , opMode(0, 1, Lumen::OpArgU, Lumen::OpArgN, Lumen::OpModeIABC)        /* Lumen::OpCodeVararg */
};

