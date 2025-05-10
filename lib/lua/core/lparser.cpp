/*!
 * @brief Lua Parser
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstring>

#define lparser_c
#define LUA_CORE

#include "lua.h"

#include "lua/code.h"
#include "lua/debug.h"
#include "lua/do.h"
#include "lua/lex.h"
#include "lua/mem.h"
#include "lua/object.h"
#include "lua/opcodes.h"
#include "lua/parser.h"
#include "lua/state.h"
#include "lua/string.h"
#include "lua/table.h"


#define hasMulRet(k)        ((k) == Lua::ExpDesc::KindCall || (k) == Lua::ExpDesc::KindVararg)

#define getLocalVar(fs, i)    ((fs)->Func->LocalVars[(fs)->ActiveVars[i]])

#define LuaParserCheckLimit(fs, v, l, m)    if ((v)>(l)) errorLimit(fs,l,m)


/*
** nodes for block list (list of active blocks)
*/
struct Lua::BlockNode {
    Lua::BlockNode *Previous;  /* chain */
    int BreakList;  /* list of jumps out of this loop */
    Lua::Byte ActiveVarsCount;  /* # active locals outside the breakable structure */
    Lua::Byte IsUpValue;  /* true if some variable in the block is an upValue */
    Lua::Byte IsBreakable;  /* true if `block` is a loop */
};


/*
** prototypes for recursive non-terminal functions
*/
static void chunk(Lua::LexState *ls);

static void expr(Lua::LexState *ls, Lua::ExpDesc *v);


static void anchorToken(Lua::LexState *ls) {
    if (ls->CurToken.Kind == Lua::Token::SymbolName || ls->CurToken.Kind == Lua::Token::SymbolString) {
        Lua::String *ts = ls->CurToken.SemInfo.ts;
        Lua::LexState::NewString(ls, LuaStringCString(ts), ts->Length);
    }
}


static void errorExpected(Lua::LexState *ls, int token) {
    Lua::LexState::SyntaxError(ls,
                               Lua::PushFString(ls->L, LUA_QS " expected", Lua::LexState::Token2CString(ls, token)));
}


static void errorLimit(Lua::FuncState *fs, int limit, const char *what) {
    const char *msg = (fs->Func->LineDefined == 0) ?
                      Lua::PushFString(fs->L, "main function has more than %d %s", limit, what) :
                      Lua::PushFString(fs->L, "function at line %d has more than %d %s",
                                       fs->Func->LineDefined, limit, what);
    Lua::LexState::LexError(fs->Lexer, msg, 0);
}


static int testNext(Lua::LexState *ls, int c) {
    if (ls->CurToken.Kind == c) {
        Lua::LexState::Next(ls);
        return 1;
    } else return 0;
}


static void check(Lua::LexState *ls, int c) {
    if (ls->CurToken.Kind != c)
        errorExpected(ls, c);
}

static void checkNext(Lua::LexState *ls, int c) {
    check(ls, c);
    Lua::LexState::Next(ls);
}


#define checkCondition(ls, c, msg)    LuaDo( if (!(c)) Lua::LexState::SyntaxError(ls, msg); )


static void checkMatch(Lua::LexState *ls, int what, int who, int where) {
    if (!testNext(ls, what)) {
        if (where == ls->LineNumber)
            errorExpected(ls, what);
        else {
            Lua::LexState::SyntaxError(ls, Lua::PushFString(ls->L,
                                                            LUA_QS " expected (to close " LUA_QS " at line %d)",
                                                            Lua::LexState::Token2CString(ls, what),
                                                            Lua::LexState::Token2CString(ls, who), where));
        }
    }
}


static Lua::String *strCheckName(Lua::LexState *ls) {
    Lua::String *ts;
    check(ls, Lua::Token::SymbolName);
    ts = ls->CurToken.SemInfo.ts;
    Lua::LexState::Next(ls);
    return ts;
}


static inline void initExp(Lua::ExpDesc *e, Lua::ExpDesc::Kind k, int i) {
    e->f = e->t = NO_JUMP;
    e->k = k;
    e->Info = i;
}


static inline void codeString(Lua::LexState *ls, Lua::ExpDesc *e, Lua::String *s) {
    initExp(e, Lua::ExpDesc::KindK, Lua::FuncState::StringK(ls->fs, s));
}


static inline void checkName(Lua::LexState *ls, Lua::ExpDesc *e) {
    codeString(ls, e, strCheckName(ls));
}


static int registerLocalVar(Lua::LexState *ls, Lua::String *varname) {
    Lua::FuncState *fs = ls->fs;
    Lua::Proto *f = fs->Func;
    int oldSize = f->LocalVarsCount;
    LuaMemoryGrowVector(ls->L, f->LocalVars, fs->LocalVarsCount, f->LocalVarsCount,
                        Lua::LocalVar, SHRT_MAX, "too many local variables");
    while (oldSize < f->LocalVarsCount) f->LocalVars[oldSize++].VarName = nullptr;
    f->LocalVars[fs->LocalVarsCount].VarName = varname;
    LuaGCObjectBarrier(ls->L, f, varname);
    return fs->LocalVarsCount++;
}


#define newLocalVarLiteral(ls, v, n) \
    newLocalVar(ls, Lua::LexState::NewString(ls, "" v, (sizeof(v)/sizeof(char))-1), n)


static void newLocalVar(Lua::LexState *ls, Lua::String *name, int n) {
    Lua::FuncState *fs = ls->fs;
    LuaParserCheckLimit(fs, fs->ActiveVarsCount + n + 1, LUAI_MAXVARS, "local variables");
    fs->ActiveVars[fs->ActiveVarsCount + n] = cast(unsigned short, registerLocalVar(ls, name));
}


static void adjustLocalVars(Lua::LexState *ls, int nVars) {
    Lua::FuncState *fs = ls->fs;
    fs->ActiveVarsCount = cast_byte(fs->ActiveVarsCount + nVars);
    for (; nVars; nVars--) {
        getLocalVar(fs, fs->ActiveVarsCount - nVars).StartPC = fs->PC;
    }
}


static void removeVars(Lua::LexState *ls, int toLevel) {
    Lua::FuncState *fs = ls->fs;
    while (fs->ActiveVarsCount > toLevel)
        getLocalVar(fs, --fs->ActiveVarsCount).EndPC = fs->PC;
}


static int indexUpValue(Lua::FuncState *fs, Lua::String *name, Lua::ExpDesc *v) {
    int i;
    Lua::Proto *f = fs->Func;
    int oldSize = f->UpValuesCount;
    for (i = 0; i < f->NUpValues; i++) {
        if (fs->UpValues[i].k == v->k && fs->UpValues[i].Info == v->Info) {
            lua_assert(f->UpValues[i] == name);
            return i;
        }
    }
    /* new one */
    LuaParserCheckLimit(fs, f->NUpValues + 1, LUAI_MAXUPVALUES, "upvalues");
    LuaMemoryGrowVector(fs->L, f->UpValues, f->NUpValues, f->UpValuesCount,
                        Lua::String *, Lua::MaxInt, "");
    while (oldSize < f->UpValuesCount) f->UpValues[oldSize++] = nullptr;
    f->UpValues[f->NUpValues] = name;
    LuaGCObjectBarrier(fs->L, f, name);
    lua_assert(v->k == Lua::ExpDesc::KindLocal || v->k == Lua::ExpDesc::KindUpValue);
    fs->UpValues[f->NUpValues].k = cast_byte(v->k);
    fs->UpValues[f->NUpValues].Info = cast_byte(v->Info);
    return f->NUpValues++;
}


static int searchVar(Lua::FuncState *fs, Lua::String *n) {
    int i;
    for (i = fs->ActiveVarsCount - 1; i >= 0; i--) {
        if (n == getLocalVar(fs, i).VarName)
            return i;
    }
    return -1;  /* not found */
}


static void markUpValue(Lua::FuncState *fs, int level) {
    Lua::BlockNode *bl = fs->Blocks;
    while (bl && bl->ActiveVarsCount > level) bl = bl->Previous;
    if (bl) bl->IsUpValue = 1;
}


static int singleVarAux(Lua::FuncState *fs, Lua::String *n, Lua::ExpDesc *var, int base) {
    if (fs == nullptr) {  /* no more levels? */
        initExp(var, Lua::ExpDesc::KindGlobal, NO_REG);  /* default is global variable */
        return Lua::ExpDesc::KindGlobal;
    } else {
        int v = searchVar(fs, n);  /* look up at current level */
        if (v >= 0) {
            initExp(var, Lua::ExpDesc::KindLocal, v);
            if (!base)
                markUpValue(fs, v);  /* local will be used as an upVal */
            return Lua::ExpDesc::KindLocal;
        } else {  /* not found at current level; try upper one */
            if (singleVarAux(fs->Prev, n, var, 0) == Lua::ExpDesc::KindGlobal)
                return Lua::ExpDesc::KindGlobal;
            var->Info = indexUpValue(fs, n, var);  /* else was LOCAL or UPVAL */
            var->k = Lua::ExpDesc::KindUpValue;  /* upValue in this level */
            return Lua::ExpDesc::KindUpValue;
        }
    }
}


static void singleVar(Lua::LexState *ls, Lua::ExpDesc *var) {
    Lua::String *varname = strCheckName(ls);
    Lua::FuncState *fs = ls->fs;
    if (singleVarAux(fs, varname, var, 1) == Lua::ExpDesc::KindGlobal)
        var->Info = Lua::FuncState::StringK(fs, varname);  /* info points to global name */
}


static void adjustAssign(Lua::LexState *ls, int nVars, int nExps, Lua::ExpDesc *e) {
    Lua::FuncState *fs = ls->fs;
    int extra = nVars - nExps;
    if (hasMulRet(e->k)) {
        extra++;  /* includes call itself */
        if (extra < 0) extra = 0;
        Lua::FuncState::SetReturns(fs, e, extra);  /* last exp. provides the difference */
        if (extra > 1) Lua::FuncState::ReserveRegs(fs, extra - 1);
    } else {
        if (e->k != Lua::ExpDesc::KindVoid) Lua::FuncState::Exp2NextReg(fs, e);  /* close last expression */
        if (extra > 0) {
            int reg = fs->FreeReg;
            Lua::FuncState::ReserveRegs(fs, extra);
            Lua::FuncState::Nil(fs, reg, extra);
        }
    }
}


static void enterLevel(Lua::LexState *ls) {
    if (++ls->L->NCCalls > LUAI_MAXCCALLS)
        Lua::LexState::LexError(ls, "chunk has too many syntax levels", 0);
}


#define leaveLevel(ls)    ((ls)->L->NCCalls--)


static void enterBlock(Lua::FuncState *fs, Lua::BlockNode *bl, Lua::Byte isBreakable) {
    bl->BreakList = NO_JUMP;
    bl->IsBreakable = isBreakable;
    bl->ActiveVarsCount = fs->ActiveVarsCount;
    bl->IsUpValue = 0;
    bl->Previous = fs->Blocks;
    fs->Blocks = bl;
    lua_assert(fs->FreeReg == fs->ActiveVarsCount);
}


static void leaveBlock(Lua::FuncState *fs) {
    Lua::BlockNode *bl = fs->Blocks;
    fs->Blocks = bl->Previous;
    removeVars(fs->Lexer, bl->ActiveVarsCount);
    if (bl->IsUpValue)
        Lua::FuncState::CodeABC(fs, Lua::OpCodeClose, bl->ActiveVarsCount, 0, 0);
    /* a block either controls scope or breaks (never both) */
    lua_assert(!bl->IsBreakable || !bl->IsUpValue);
    lua_assert(bl->ActiveVarsCount == fs->ActiveVarsCount);
    fs->FreeReg = fs->ActiveVarsCount;  /* free registers */
    Lua::FuncState::PatchToHere(fs, bl->BreakList);
}


static void pushClosure(Lua::LexState *ls, Lua::FuncState *func, Lua::ExpDesc *v) {
    Lua::FuncState *fs = ls->fs;
    Lua::Proto *f = fs->Func;
    int oldSize = f->SubProtoCount;
    int i;
    LuaMemoryGrowVector(ls->L, f->SubProto, fs->ProtoCount, f->SubProtoCount, Lua::Proto *,
                        LUA_CODE_MAX_ARG_Bx, "constant table overflow");
    while (oldSize < f->SubProtoCount) f->SubProto[oldSize++] = nullptr;
    f->SubProto[fs->ProtoCount++] = func->Func;
    LuaGCObjectBarrier(ls->L, f, func->Func);
    initExp(v, Lua::ExpDesc::KindRelocatable, Lua::FuncState::CodeABx(fs, Lua::OpCodeClosure, 0, fs->ProtoCount - 1));
    for (i = 0; i < func->Func->NUpValues; i++) {
        Lua::OpCode o = (func->UpValues[i].k == Lua::ExpDesc::KindLocal) ? Lua::OpCodeMove : Lua::OpCodeGetUpVal;
        Lua::FuncState::CodeABC(fs, o, 0, func->UpValues[i].Info, 0);
    }
}


static void openFunc(Lua::LexState *ls, Lua::FuncState *fs) {
    Lua::State *L = ls->L;
    Lua::Proto *f = Lua::Proto::New(L);
    fs->Func = f;
    fs->Prev = ls->fs;  /* linked list of funcStates */
    fs->Lexer = ls;
    fs->L = L;
    ls->fs = fs;
    fs->PC = 0;
    fs->LastPC = -1;
    fs->JumpPC = NO_JUMP;
    fs->FreeReg = 0;
    fs->ConstantsCount = 0;
    fs->ProtoCount = 0;
    fs->LocalVarsCount = 0;
    fs->ActiveVarsCount = 0;
    fs->Blocks = nullptr;
    f->Source = ls->Source;
    f->MaxStackSize = 2;  /* registers 0/1 are always valid */
    fs->Constants = Lua::Table::New(L, 0, 0);
    /* anchor table of constants and prototype (to avoid being collected) */
    LuaSetTableValue2S(L, L->Top, fs->Constants);
    LuaIncrTop(L);
    LuaSetProtoValue2S(L, L->Top, f);
    LuaIncrTop(L);
}


static void closeFunc(Lua::LexState *ls) {
    Lua::State *L = ls->L;
    Lua::FuncState *fs = ls->fs;
    Lua::Proto *f = fs->Func;
    removeVars(ls, 0);
    Lua::FuncState::Ret(fs, 0, 0);  /* final return */
    LuaMemoryReAllocVector(L, f->Code, f->CodeCount, fs->PC, Lua::Instruction);
    f->CodeCount = fs->PC;
    LuaMemoryReAllocVector(L, f->LineInfo, f->LineInfoCount, fs->PC, int);
    f->LineInfoCount = fs->PC;
    LuaMemoryReAllocVector(L, f->K, f->KCount, fs->ConstantsCount, Lua::Value);
    f->KCount = fs->ConstantsCount;
    LuaMemoryReAllocVector(L, f->SubProto, f->SubProtoCount, fs->ProtoCount, Lua::Proto *);
    f->SubProtoCount = fs->ProtoCount;
    LuaMemoryReAllocVector(L, f->LocalVars, f->LocalVarsCount, fs->LocalVarsCount, Lua::LocalVar);
    f->LocalVarsCount = fs->LocalVarsCount;
    LuaMemoryReAllocVector(L, f->UpValues, f->UpValuesCount, f->NUpValues, Lua::String *);
    f->UpValuesCount = f->NUpValues;
    lua_assert(Lua::Debug::CheckCode(f));
    lua_assert(fs->Blocks == nullptr);
    ls->fs = fs->Prev;
    /* last token read was anchored in defunct function; must reAnchor it */
    if (fs) anchorToken(ls);
    L->Top -= 2;  /* remove table and prototype from the stack */
}


Lua::Proto *Lua::Parser::Parse(Lua::State *L, Lua::ZIO *z, Lua::ZBuffer *buff, const char *name) {
    Lua::LexState lexState;
    Lua::FuncState funcState;
    lexState.buff = buff;
    Lua::LexState::SetInput(L, &lexState, z, Lua::String::New(L, name));
    openFunc(&lexState, &funcState);
    funcState.Func->IsVararg = Lua::Proto::VarargIsVararg;  /* main func. is always vararg */
    Lua::LexState::Next(&lexState);  /* read first token */
    chunk(&lexState);
    check(&lexState, Lua::Token::SymbolEOS);
    closeFunc(&lexState);
    lua_assert(funcState.Prev == nullptr);
    lua_assert(funcState.Func->NUpValues == 0);
    lua_assert(lexState.fs == nullptr);
    return funcState.Func;
}



/*============================================================*/
/* GRAMMAR RULES */
/*============================================================*/


static void field(Lua::LexState *ls, Lua::ExpDesc *v) {
    /* field -> ['.' | ':'] NAME */
    Lua::FuncState *fs = ls->fs;
    Lua::ExpDesc key;
    Lua::FuncState::Exp2AnyReg(fs, v);
    Lua::LexState::Next(ls);  /* skip the dot or colon */
    checkName(ls, &key);
    Lua::FuncState::Indexed(fs, v, &key);
}


static void yIndex(Lua::LexState *ls, Lua::ExpDesc *v) {
    /* index -> '[' expr ']' */
    Lua::LexState::Next(ls);  /* skip the `[` */
    expr(ls, v);
    Lua::FuncState::Exp2Val(ls->fs, v);
    checkNext(ls, ']');
}


/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/


struct ConsControl {
    Lua::ExpDesc v;  /* last list item read */
    Lua::ExpDesc *t;  /* table descriptor */
    int nh;  /* total number of `record` elements */
    int na;  /* total number of array elements */
    int tostore;  /* number of array elements pending to be stored */
};


static void recField(Lua::LexState *ls, struct ConsControl *cc) {
    /* recField -> (NAME | `['exp1`]') = exp1 */
    Lua::FuncState *fs = ls->fs;
    int reg = ls->fs->FreeReg;
    Lua::ExpDesc key, val;
    int rkKey;
    if (ls->CurToken.Kind == Lua::Token::SymbolName) {
        LuaParserCheckLimit(fs, cc->nh, Lua::MaxInt, "items in a constructor");
        checkName(ls, &key);
    } else  /* ls->t.token == '[' */
        yIndex(ls, &key);
    cc->nh++;
    checkNext(ls, '=');
    rkKey = Lua::FuncState::Exp2RK(fs, &key);
    expr(ls, &val);
    Lua::FuncState::CodeABC(fs, Lua::OpCodeSetTable, cc->t->Info, rkKey, Lua::FuncState::Exp2RK(fs, &val));
    fs->FreeReg = reg;  /* free registers */
}


static void closeListField(Lua::FuncState *fs, struct ConsControl *cc) {
    if (cc->v.k == Lua::ExpDesc::KindVoid) return;  /* there is no list item */
    Lua::FuncState::Exp2NextReg(fs, &cc->v);
    cc->v.k = Lua::ExpDesc::KindVoid;
    if (cc->tostore == LUA_FIELDS_PER_FLUSH) {
        Lua::FuncState::SetList(fs, cc->t->Info, cc->na, cc->tostore);  /* flush */
        cc->tostore = 0;  /* no more items pending */
    }
}


static void lastListField(Lua::FuncState *fs, struct ConsControl *cc) {
    if (cc->tostore == 0) return;
    if (hasMulRet(cc->v.k)) {
        LuaFuncStateSetMulRet(fs, &cc->v);
        Lua::FuncState::SetList(fs, cc->t->Info, cc->na, LUA_MULTRET);
        cc->na--;  /* do not count last expression (unknown number of elements) */
    } else {
        if (cc->v.k != Lua::ExpDesc::KindVoid)
            Lua::FuncState::Exp2NextReg(fs, &cc->v);
        Lua::FuncState::SetList(fs, cc->t->Info, cc->na, cc->tostore);
    }
}


static void listField(Lua::LexState *ls, struct ConsControl *cc) {
    expr(ls, &cc->v);
    LuaParserCheckLimit(ls->fs, cc->na, Lua::MaxInt, "items in a constructor");
    cc->na++;
    cc->tostore++;
}


static void constructor(Lua::LexState *ls, Lua::ExpDesc *t) {
    /* constructor -> ?? */
    Lua::FuncState *fs = ls->fs;
    int line = ls->LineNumber;
    int pc = Lua::FuncState::CodeABC(fs, Lua::OpCodeNewTable, 0, 0, 0);
    ConsControl cc;
    cc.na = cc.nh = cc.tostore = 0;
    cc.t = t;
    initExp(t, Lua::ExpDesc::KindRelocatable, pc);
    initExp(&cc.v, Lua::ExpDesc::KindVoid, 0);  /* no value (yet) */
    Lua::FuncState::Exp2NextReg(ls->fs, t);  /* fix it at stack top (for gc) */
    checkNext(ls, '{');
    do {
        lua_assert(cc.v.k == Lua::ExpDesc::KindVoid || cc.tostore > 0);
        if (ls->CurToken.Kind == '}') break;
        closeListField(fs, &cc);
        switch (ls->CurToken.Kind) {
            case Lua::Token::SymbolName: {  /* may be listFields or recFields */
                Lua::LexState::LookAhead(ls);
                if (ls->Ahead.Kind != '=')  /* expression? */
                    listField(ls, &cc);
                else
                    recField(ls, &cc);
                break;
            }
            case '[': {  /* constructor_item -> recField */
                recField(ls, &cc);
                break;
            }
            default: {  /* constructor_part -> listField */
                listField(ls, &cc);
                break;
            }
        }
    } while (testNext(ls, ',') || testNext(ls, ';'));
    checkMatch(ls, '}', '{', line);
    lastListField(fs, &cc);
    LuaOpCodeSetArgB(fs->Func->Code[pc], Lua::Int2FB(cc.na)); /* set initial array size */
    LuaOpCodeSetArgC(fs->Func->Code[pc], Lua::Int2FB(cc.nh));  /* set initial table size */
}

/* }====================================================================== */



static void parList(Lua::LexState *ls) {
    /* parList -> [ param { `,' param } ] */
    Lua::FuncState *fs = ls->fs;
    Lua::Proto *f = fs->Func;
    int nParams = 0;
    f->IsVararg = 0;
    if (ls->CurToken.Kind != ')') {  /* is `parList` not empty? */
        do {
            switch (ls->CurToken.Kind) {
                case Lua::Token::SymbolName: {  /* param -> NAME */
                    newLocalVar(ls, strCheckName(ls), nParams++);
                    break;
                }
                case Lua::Token::SymbolDots: {  /* param -> `...' */
                    Lua::LexState::Next(ls);
#if defined(LUA_COMPAT_VARARG)
                    /* use `arg` as default name */
                    newLocalVarLiteral(ls, "arg", nParams++);
                    f->IsVararg = Lua::Proto::VarargHasArg | Lua::Proto::VarargIsNeedsArg;
#endif
                    f->IsVararg |= Lua::Proto::VarargIsVararg;
                    break;
                }
                default:
                    Lua::LexState::SyntaxError(ls, "<name> or " LUA_QL("...") " expected");
            }
        } while (!f->IsVararg && testNext(ls, ','));
    }
    adjustLocalVars(ls, nParams);
    f->NUmParams = cast_byte(fs->ActiveVarsCount - (f->IsVararg & Lua::Proto::VarargHasArg));
    Lua::FuncState::ReserveRegs(fs, fs->ActiveVarsCount);  /* reserve register for parameters */
}


static void body(Lua::LexState *ls, Lua::ExpDesc *e, int needSelf, int line) {
    /* body ->  `(' parList `)' chunk END */
    Lua::FuncState new_fs;
    openFunc(ls, &new_fs);
    new_fs.Func->LineDefined = line;
    checkNext(ls, '(');
    if (needSelf) {
        newLocalVarLiteral(ls, "self", 0);
        adjustLocalVars(ls, 1);
    }
    parList(ls);
    checkNext(ls, ')');
    chunk(ls);
    new_fs.Func->LastLineDefined = ls->LineNumber;
    checkMatch(ls, Lua::Token::SymbolEnd, Lua::Token::SymbolFunction, line);
    closeFunc(ls);
    pushClosure(ls, &new_fs, e);
}


static int expList1(Lua::LexState *ls, Lua::ExpDesc *v) {
    /* expList1 -> expr { `,' expr } */
    int n = 1;  /* at least one expression */
    expr(ls, v);
    while (testNext(ls, ',')) {
        Lua::FuncState::Exp2NextReg(ls->fs, v);
        expr(ls, v);
        n++;
    }
    return n;
}


static void funcArgs(Lua::LexState *ls, Lua::ExpDesc *f) {
    Lua::FuncState *fs = ls->fs;
    Lua::ExpDesc args;
    int base, nParams;
    int line = ls->LineNumber;
    switch (ls->CurToken.Kind) {
        case '(': {  /* funcArgs -> `(' [ expList1 ] `)' */
            if (line != ls->LastLine)
                Lua::LexState::SyntaxError(ls, "ambiguous syntax (function call x new statement)");
            Lua::LexState::Next(ls);
            if (ls->CurToken.Kind == ')')  /* arg list is empty? */
                args.k = Lua::ExpDesc::KindVoid;
            else {
                expList1(ls, &args);
                LuaFuncStateSetMulRet(fs, &args);
            }
            checkMatch(ls, ')', '(', line);
            break;
        }
        case '{': {  /* funcArgs -> constructor */
            constructor(ls, &args);
            break;
        }
        case Lua::Token::SymbolString: {  /* funcArgs -> STRING */
            codeString(ls, &args, ls->CurToken.SemInfo.ts);
            Lua::LexState::Next(ls);  /* must use `semInfo' before `next' */
            break;
        }
        default: {
            Lua::LexState::SyntaxError(ls, "function arguments expected");
            return;
        }
    }
    lua_assert(f->k == Lua::ExpDesc::KindNonRelocatable);
    base = f->Info;  /* base register for call */
    if (hasMulRet(args.k))
        nParams = LUA_MULTRET;  /* open call */
    else {
        if (args.k != Lua::ExpDesc::KindVoid)
            Lua::FuncState::Exp2NextReg(fs, &args);  /* close last argument */
        nParams = fs->FreeReg - (base + 1);
    }
    initExp(f, Lua::ExpDesc::KindCall, Lua::FuncState::CodeABC(fs, Lua::OpCodeCall, base, nParams + 1, 2));
    Lua::FuncState::FixLine(fs, line);
    fs->FreeReg = base + 1;  /* call remove function and arguments and leaves
                            (unless changed) one result */
}


/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


static void prefixExp(Lua::LexState *ls, Lua::ExpDesc *v) {
    /* prefixExp -> NAME | '(' expr ')' */
    switch (ls->CurToken.Kind) {
        case '(': {
            int line = ls->LineNumber;
            Lua::LexState::Next(ls);
            expr(ls, v);
            checkMatch(ls, ')', '(', line);
            Lua::FuncState::DischargeVars(ls->fs, v);
            return;
        }
        case Lua::Token::SymbolName: {
            singleVar(ls, v);
            return;
        }
        default: {
            Lua::LexState::SyntaxError(ls, "unexpected symbol");
            return;
        }
    }
}


static void primaryExp(Lua::LexState *ls, Lua::ExpDesc *v) {
    /* primaryExp ->
          prefixExp { `.' NAME | `[' exp `]' | `:' NAME funcArgs | funcArgs } */
    Lua::FuncState *fs = ls->fs;
    prefixExp(ls, v);
    for (;;) {
        switch (ls->CurToken.Kind) {
            case '.': {  /* field */
                field(ls, v);
                break;
            }
            case '[': {  /* `[' exp1 `]' */
                Lua::ExpDesc key;
                Lua::FuncState::Exp2AnyReg(fs, v);
                yIndex(ls, &key);
                Lua::FuncState::Indexed(fs, v, &key);
                break;
            }
            case ':': {  /* `:' NAME funcArgs */
                Lua::ExpDesc key;
                Lua::LexState::Next(ls);
                checkName(ls, &key);
                Lua::FuncState::Self(fs, v, &key);
                funcArgs(ls, v);
                break;
            }
            case '(':
            case Lua::Token::SymbolString:
            case '{': {  /* funcArgs */
                Lua::FuncState::Exp2NextReg(fs, v);
                funcArgs(ls, v);
                break;
            }
            default:
                return;
        }
    }
}


static void simpleExp(Lua::LexState *ls, Lua::ExpDesc *v) {
    /* simpleExp -> NUMBER | STRING | NIL | true | false | ... |
                    constructor | FUNCTION body | primaryExp */
    switch (ls->CurToken.Kind) {
        case Lua::Token::SymbolNumber: {
            initExp(v, Lua::ExpDesc::KindKNum, 0);
            v->NumberValue = ls->CurToken.SemInfo.r;
            break;
        }
        case Lua::Token::SymbolString: {
            codeString(ls, v, ls->CurToken.SemInfo.ts);
            break;
        }
        case Lua::Token::SymbolNil: {
            initExp(v, Lua::ExpDesc::KindNil, 0);
            break;
        }
        case Lua::Token::SymbolTrue: {
            initExp(v, Lua::ExpDesc::KindTrue, 0);
            break;
        }
        case Lua::Token::SymbolFalse: {
            initExp(v, Lua::ExpDesc::KindFalse, 0);
            break;
        }
        case Lua::Token::SymbolDots: {  /* vararg */
            Lua::FuncState *fs = ls->fs;
            checkCondition(ls, fs->Func->IsVararg,
                           "cannot use " LUA_QL("...") " outside a vararg function");
            fs->Func->IsVararg &= ~Lua::Proto::VarargIsNeedsArg;  /* don't need 'arg' */
            initExp(v, Lua::ExpDesc::KindVararg, Lua::FuncState::CodeABC(fs, Lua::OpCodeVararg, 0, 1, 0));
            break;
        }
        case '{': {  /* constructor */
            constructor(ls, v);
            return;
        }
        case Lua::Token::SymbolFunction: {
            Lua::LexState::Next(ls);
            body(ls, v, 0, ls->LineNumber);
            return;
        }
        default: {
            primaryExp(ls, v);
            return;
        }
    }
    Lua::LexState::Next(ls);
}


static Lua::UnOpr getUnOpr(int op) {
    switch (op) {
        case Lua::Token::SymbolNot:
            return Lua::UnOprNot;
        case '-':
            return Lua::UnOprMinus;
        case '#':
            return Lua::UnOprLen;
        default:
            return Lua::UnOprNo;
    }
}


static Lua::BinOpr getBinOpr(int op) {
    switch (op) {
        case '+':
            return Lua::BinOprAdd;
        case '-':
            return Lua::BinOprSub;
        case '*':
            return Lua::BinOprMul;
        case '/':
            return Lua::BinOprDiv;
        case '%':
            return Lua::BinOprMod;
        case '^':
            return Lua::BinOprPow;
        case Lua::Token::SymbolConcat:
            return Lua::BinOprConcat;
        case Lua::Token::SymbolNE:
            return Lua::BinOprNE;
        case Lua::Token::SymbolEQ:
            return Lua::BinOprEQ;
        case '<':
            return Lua::BinOprLT;
        case Lua::Token::SymbolLE:
            return Lua::BinOprLE;
        case '>':
            return Lua::BinOprGT;
        case Lua::Token::SymbolGE:
            return Lua::BinOprGE;
        case Lua::Token::SymbolAnd:
            return Lua::BinOprAND;
        case Lua::Token::SymbolOr:
            return Lua::BinOprOR;
        default:
            return Lua::BinOprNo;
    }
}


static const struct {
    Lua::Byte left;  /* left priority for each binary operator */
    Lua::Byte right; /* right priority */
} priority[] = {  /* ORDER OPR */
        {6,  6},
        {6,  6},
        {7,  7},
        {7,  7},
        {7,  7},  /* `+' `-' `/' `%' */
        {10, 9},
        {5,  4},                 /* power and concat (right associative) */
        {3,  3},
        {3,  3},                  /* equality and inequality */
        {3,  3},
        {3,  3},
        {3,  3},
        {3,  3},  /* order */
        {2,  2},
        {1,  1}                   /* logical (and/or) */
};

#define UNARY_PRIORITY    8  /* priority for unary operators */


/*
** subExpr -> (simpleExp | UnOp subExpr) { BinOp subExpr }
** where `BinOp' is any binary operator with a priority higher than `limit'
*/
static Lua::BinOpr subExpr(Lua::LexState *ls, Lua::ExpDesc *v, unsigned int limit) {
    Lua::BinOpr op;
    Lua::UnOpr uop;
    enterLevel(ls);
    uop = getUnOpr(ls->CurToken.Kind);
    if (uop != Lua::UnOprNo) {
        Lua::LexState::Next(ls);
        subExpr(ls, v, UNARY_PRIORITY);
        Lua::FuncState::Prefix(ls->fs, uop, v);
    } else simpleExp(ls, v);
    /* expand while operators have priorities higher than `limit` */
    op = getBinOpr(ls->CurToken.Kind);
    while (op != Lua::BinOprNo && priority[op].left > limit) {
        Lua::ExpDesc v2;
        Lua::BinOpr nexTop;
        Lua::LexState::Next(ls);
        Lua::FuncState::InFix(ls->fs, op, v);
        /* read sub-expression with higher priority */
        nexTop = subExpr(ls, &v2, priority[op].right);
        Lua::FuncState::PosFix(ls->fs, op, v, &v2);
        op = nexTop;
    }
    leaveLevel(ls);
    return op;  /* return first untreated operator */
}


static void expr(Lua::LexState *ls, Lua::ExpDesc *v) {
    subExpr(ls, v, 0);
}

/* }==================================================================== */



/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/


static int blockFollow(int token) {
    switch (token) {
        case Lua::Token::SymbolElse:
        case Lua::Token::SymbolElseIf:
        case Lua::Token::SymbolEnd:
        case Lua::Token::SymbolUntil:
        case Lua::Token::SymbolEOS:
            return 1;
        default:
            return 0;
    }
}


static void block(Lua::LexState *ls) {
    /* block -> chunk */
    Lua::FuncState *fs = ls->fs;
    Lua::BlockNode bl;
    enterBlock(fs, &bl, 0);
    chunk(ls);
    lua_assert(bl.BreakList == NO_JUMP);
    leaveBlock(fs);
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
    LHS_assign *prev;
    Lua::ExpDesc v;  /* variable (global, local, upValue, or indexed) */
};


/*
** check whether, in an assignment to a local variable, the local variable
** is needed in a previous assignment (to a table). If so, save original
** local value in a safe place and use this safe copy in the previous
** assignment.
*/
static void checkConflict(Lua::LexState *ls, struct LHS_assign *lh, Lua::ExpDesc *v) {
    Lua::FuncState *fs = ls->fs;
    int extra = fs->FreeReg;  /* eventual position to save local variable */
    int conflict = 0;
    for (; lh; lh = lh->prev) {
        if (lh->v.k == Lua::ExpDesc::KindIndexed) {
            if (lh->v.Info == v->Info) {  /* conflict? */
                conflict = 1;
                lh->v.Info = extra;  /* previous assignment will use safe copy */
            }
            if (lh->v.Aux == v->Info) {  /* conflict? */
                conflict = 1;
                lh->v.Aux = extra;  /* previous assignment will use safe copy */
            }
        }
    }
    if (conflict) {
        Lua::FuncState::CodeABC(fs, Lua::OpCodeMove, fs->FreeReg, v->Info, 0);  /* make copy */
        Lua::FuncState::ReserveRegs(fs, 1);
    }
}


static void assignment(Lua::LexState *ls, struct LHS_assign *lh, int nVars) {
    Lua::ExpDesc e;
    checkCondition(ls, Lua::ExpDesc::KindLocal <= lh->v.k && lh->v.k <= Lua::ExpDesc::KindIndexed,
                   "syntax error");
    if (testNext(ls, ',')) {  /* assignment -> `,' primaryExp assignment */
        LHS_assign nv;
        nv.prev = lh;
        primaryExp(ls, &nv.v);
        if (nv.v.k == Lua::ExpDesc::KindLocal)
            checkConflict(ls, lh, &nv.v);
        LuaParserCheckLimit(ls->fs, nVars, LUAI_MAXCCALLS - ls->L->NCCalls,
                            "variables in assignment");
        assignment(ls, &nv, nVars + 1);
    } else {  /* assignment -> `=' expList1 */
        int nExps;
        checkNext(ls, '=');
        nExps = expList1(ls, &e);
        if (nExps != nVars) {
            adjustAssign(ls, nVars, nExps, &e);
            if (nExps > nVars)
                ls->fs->FreeReg -= nExps - nVars;  /* remove extra values */
        } else {
            Lua::FuncState::SetOneRet(ls->fs, &e);  /* close last expression */
            Lua::FuncState::StoreVar(ls->fs, &lh->v, &e);
            return;  /* avoid default */
        }
    }
    initExp(&e, Lua::ExpDesc::KindNonRelocatable, ls->fs->FreeReg - 1);  /* default assignment */
    Lua::FuncState::StoreVar(ls->fs, &lh->v, &e);
}


static int cond(Lua::LexState *ls) {
    /* cond -> exp */
    Lua::ExpDesc v;
    expr(ls, &v);  /* read condition */
    if (v.k == Lua::ExpDesc::KindNil) v.k = Lua::ExpDesc::KindFalse;  /* `false(s)` are all equal here */
    Lua::FuncState::GoIfTrue(ls->fs, &v);
    return v.f;
}


static void breakStat(Lua::LexState *ls) {
    Lua::FuncState *fs = ls->fs;
    Lua::BlockNode *bl = fs->Blocks;
    int upVal = 0;
    while (bl && !bl->IsBreakable) {
        upVal |= bl->IsUpValue;
        bl = bl->Previous;
    }
    if (!bl)
        Lua::LexState::SyntaxError(ls, "no loop to break");
    if (upVal)
        Lua::FuncState::CodeABC(fs, Lua::OpCodeClose, bl->ActiveVarsCount, 0, 0);
    Lua::FuncState::Concat(fs, &bl->BreakList, Lua::FuncState::Jump(fs));
}


static void whileStat(Lua::LexState *ls, int line) {
    /* whileStat -> WHILE cond DO block END */
    Lua::FuncState *fs = ls->fs;
    int whileInit;
    int condExit;
    Lua::BlockNode bl;
    Lua::LexState::Next(ls);  /* skip WHILE */
    whileInit = Lua::FuncState::GetLabel(fs);
    condExit = cond(ls);
    enterBlock(fs, &bl, 1);
    checkNext(ls, Lua::Token::SymbolDo);
    block(ls);
    Lua::FuncState::PatchList(fs, Lua::FuncState::Jump(fs), whileInit);
    checkMatch(ls, Lua::Token::SymbolEnd, Lua::Token::SymbolWhile, line);
    leaveBlock(fs);
    Lua::FuncState::PatchToHere(fs, condExit);  /* false conditions finish the loop */
}


static void repeatStat(Lua::LexState *ls, int line) {
    /* repeatStat -> REPEAT block UNTIL cond */
    int condExit;
    Lua::FuncState *fs = ls->fs;
    int repeat_init = Lua::FuncState::GetLabel(fs);
    Lua::BlockNode bl1, bl2;
    enterBlock(fs, &bl1, 1);  /* loop block */
    enterBlock(fs, &bl2, 0);  /* scope block */
    Lua::LexState::Next(ls);  /* skip REPEAT */
    chunk(ls);
    checkMatch(ls, Lua::Token::SymbolUntil, Lua::Token::SymbolRepeat, line);
    condExit = cond(ls);  /* read condition (inside scope block) */
    if (!bl2.IsUpValue) {  /* no upValues? */
        leaveBlock(fs);  /* finish scope */
        Lua::FuncState::PatchList(ls->fs, condExit, repeat_init);  /* close the loop */
    } else {  /* complete semantics when there are upValues */
        breakStat(ls);  /* if condition then break */
        Lua::FuncState::PatchToHere(ls->fs, condExit);  /* else... */
        leaveBlock(fs);  /* finish scope... */
        Lua::FuncState::PatchList(ls->fs, Lua::FuncState::Jump(fs), repeat_init);  /* and repeat */
    }
    leaveBlock(fs);  /* finish loop */
}


static int exp1(Lua::LexState *ls) {
    Lua::ExpDesc e;
    int k;
    expr(ls, &e);
    k = e.k;
    Lua::FuncState::Exp2NextReg(ls->fs, &e);
    return k;
}


static void forBody(Lua::LexState *ls, int base, int line, int nVars, int isNum) {
    /* forBody -> DO block */
    Lua::BlockNode bl;
    Lua::FuncState *fs = ls->fs;
    int prep, endFor;
    adjustLocalVars(ls, 3);  /* control variables */
    checkNext(ls, Lua::Token::SymbolDo);
    prep = isNum ? LuaFuncStateCodeAsBx(fs, Lua::OpCodeForPrep, base, NO_JUMP) : Lua::FuncState::Jump(fs);
    enterBlock(fs, &bl, 0);  /* scope for declared variables */
    adjustLocalVars(ls, nVars);
    Lua::FuncState::ReserveRegs(fs, nVars);
    block(ls);
    leaveBlock(fs);  /* end of scope for declared variables */
    Lua::FuncState::PatchToHere(fs, prep);
    endFor = (isNum) ? LuaFuncStateCodeAsBx(fs, Lua::OpCodeForLoop, base, NO_JUMP) :
             Lua::FuncState::CodeABC(fs, Lua::OpCodeTForLoop, base, 0, nVars);
    Lua::FuncState::FixLine(fs, line);  /* pretend that `OP_FOR` starts the loop */
    Lua::FuncState::PatchList(fs, (isNum ? endFor : Lua::FuncState::Jump(fs)), prep + 1);
}


static void forNum(Lua::LexState *ls, Lua::String *varname, int line) {
    /* forNum -> NAME = exp1,exp1[,exp1] forBody */
    Lua::FuncState *fs = ls->fs;
    int base = fs->FreeReg;
    newLocalVarLiteral(ls, "(for index)", 0);
    newLocalVarLiteral(ls, "(for limit)", 1);
    newLocalVarLiteral(ls, "(for step)", 2);
    newLocalVar(ls, varname, 3);
    checkNext(ls, '=');
    exp1(ls);  /* initial value */
    checkNext(ls, ',');
    exp1(ls);  /* limit */
    if (testNext(ls, ','))
        exp1(ls);  /* optional step */
    else {  /* default step = 1 */
        Lua::FuncState::CodeABx(fs, Lua::OpCodeLoadK, fs->FreeReg, Lua::FuncState::NumberK(fs, 1));
        Lua::FuncState::ReserveRegs(fs, 1);
    }
    forBody(ls, base, line, 1, 1);
}


static void forList(Lua::LexState *ls, Lua::String *indexName) {
    /* forList -> NAME {,NAME} IN expList1 forBody */
    Lua::FuncState *fs = ls->fs;
    Lua::ExpDesc e;
    int nVars = 0;
    int line;
    int base = fs->FreeReg;
    /* create control variables */
    newLocalVarLiteral(ls, "(for generator)", nVars++);
    newLocalVarLiteral(ls, "(for state)", nVars++);
    newLocalVarLiteral(ls, "(for control)", nVars++);
    /* create declared variables */
    newLocalVar(ls, indexName, nVars++);
    while (testNext(ls, ','))
        newLocalVar(ls, strCheckName(ls), nVars++);
    checkNext(ls, Lua::Token::SymbolIn);
    line = ls->LineNumber;
    adjustAssign(ls, 3, expList1(ls, &e), &e);
    Lua::FuncState::CheckStack(fs, 3);  /* extra space to call generator */
    forBody(ls, base, line, nVars - 3, 0);
}


static void forStat(Lua::LexState *ls, int line) {
    /* forStat -> FOR (forNum | forList) END */
    Lua::FuncState *fs = ls->fs;
    Lua::String *varname;
    Lua::BlockNode bl;
    enterBlock(fs, &bl, 1);  /* scope for loop and control variables */
    Lua::LexState::Next(ls);  /* skip `for' */
    varname = strCheckName(ls);  /* first variable name */
    switch (ls->CurToken.Kind) {
        case '=':
            forNum(ls, varname, line);
            break;
        case ',':
        case Lua::Token::SymbolIn:
            forList(ls, varname);
            break;
        default:
            Lua::LexState::SyntaxError(ls, LUA_QL("=") " or " LUA_QL("in") " expected");
    }
    checkMatch(ls, Lua::Token::SymbolEnd, Lua::Token::SymbolFor, line);
    leaveBlock(fs);  /* loop scope (`break` jumps to this point) */
}


static int testThenBlock(Lua::LexState *ls) {
    /* test_then_block -> [IF | ELSEIF] cond THEN block */
    int condExit;
    Lua::LexState::Next(ls);  /* skip IF or ELSEIF */
    condExit = cond(ls);
    checkNext(ls, Lua::Token::SymbolThen);
    block(ls);  /* `then' part */
    return condExit;
}


static void ifStat(Lua::LexState *ls, int line) {
    /* ifStat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
    Lua::FuncState *fs = ls->fs;
    int fList;
    int escapeList = NO_JUMP;
    fList = testThenBlock(ls);  /* IF cond THEN block */
    while (ls->CurToken.Kind == Lua::Token::SymbolElseIf) {
        Lua::FuncState::Concat(fs, &escapeList, Lua::FuncState::Jump(fs));
        Lua::FuncState::PatchToHere(fs, fList);
        fList = testThenBlock(ls);  /* ELSEIF cond THEN block */
    }
    if (ls->CurToken.Kind == Lua::Token::SymbolElse) {
        Lua::FuncState::Concat(fs, &escapeList, Lua::FuncState::Jump(fs));
        Lua::FuncState::PatchToHere(fs, fList);
        Lua::LexState::Next(ls);  /* skip ELSE (after patch, for correct line info) */
        block(ls);  /* `else' part */
    } else
        Lua::FuncState::Concat(fs, &escapeList, fList);
    Lua::FuncState::PatchToHere(fs, escapeList);
    checkMatch(ls, Lua::Token::SymbolEnd, Lua::Token::SymbolIf, line);
}


static void localFunc(Lua::LexState *ls) {
    Lua::ExpDesc v, b;
    Lua::FuncState *fs = ls->fs;
    newLocalVar(ls, strCheckName(ls), 0);
    initExp(&v, Lua::ExpDesc::KindLocal, fs->FreeReg);
    Lua::FuncState::ReserveRegs(fs, 1);
    adjustLocalVars(ls, 1);
    body(ls, &b, 0, ls->LineNumber);
    Lua::FuncState::StoreVar(fs, &v, &b);
    /* debug information will only see the variable after this point! */
    getLocalVar(fs, fs->ActiveVarsCount - 1).StartPC = fs->PC;
}


static void localStat(Lua::LexState *ls) {
    /* stat -> LOCAL NAME {`,' NAME} [`=' expList1] */
    int nVars = 0;
    int nExps;
    Lua::ExpDesc e;
    do {
        newLocalVar(ls, strCheckName(ls), nVars++);
    } while (testNext(ls, ','));
    if (testNext(ls, '='))
        nExps = expList1(ls, &e);
    else {
        e.k = Lua::ExpDesc::KindVoid;
        nExps = 0;
    }
    adjustAssign(ls, nVars, nExps, &e);
    adjustLocalVars(ls, nVars);
}


static int funcName(Lua::LexState *ls, Lua::ExpDesc *v) {
    /* funcName -> NAME {field} [`:' NAME] */
    int needSelf = 0;
    singleVar(ls, v);
    while (ls->CurToken.Kind == '.')
        field(ls, v);
    if (ls->CurToken.Kind == ':') {
        needSelf = 1;
        field(ls, v);
    }
    return needSelf;
}


static void funcStat(Lua::LexState *ls, int line) {
    /* funcStat -> FUNCTION funcName body */
    int needSelf;
    Lua::ExpDesc v, b;
    Lua::LexState::Next(ls);  /* skip FUNCTION */
    needSelf = funcName(ls, &v);
    body(ls, &b, needSelf, line);
    Lua::FuncState::StoreVar(ls->fs, &v, &b);
    Lua::FuncState::FixLine(ls->fs, line);  /* definition `happens` in the first line */
}


static void exprStat(Lua::LexState *ls) {
    /* stat -> func | assignment */
    Lua::FuncState *fs = ls->fs;
    LHS_assign v;
    primaryExp(ls, &v.v);
    if (v.v.k == Lua::ExpDesc::KindCall)  /* stat -> func */
        LuaOpCodeSetArgC(LuaFuncStateGetCode(fs, &v.v), 1);  /* call statement uses no results */
    else {  /* stat -> assignment */
        v.prev = nullptr;
        assignment(ls, &v, 1);
    }
}


static void retStat(Lua::LexState *ls) {
    /* stat -> RETURN expList */
    Lua::FuncState *fs = ls->fs;
    Lua::ExpDesc e;
    int first, nRet;  /* registers with returned values */
    Lua::LexState::Next(ls);  /* skip RETURN */
    if (blockFollow(ls->CurToken.Kind) || ls->CurToken.Kind == ';')
        first = nRet = 0;  /* return no values */
    else {
        nRet = expList1(ls, &e);  /* optional return values */
        if (hasMulRet(e.k)) {
            LuaFuncStateSetMulRet(fs, &e);
            if (e.k == Lua::ExpDesc::KindCall && nRet == 1) {  /* tail call? */
                LuaOpCodeSet(LuaFuncStateGetCode(fs, &e), Lua::OpCodeTailCall);
                lua_assert(LuaOpCodeGetArgA(LuaFuncStateGetCode(fs, &e)) == fs->ActiveVarsCount);
            }
            first = fs->ActiveVarsCount;
            nRet = LUA_MULTRET;  /* return all values */
        } else {
            if (nRet == 1)  /* only one single value? */
                first = Lua::FuncState::Exp2AnyReg(fs, &e);
            else {
                Lua::FuncState::Exp2NextReg(fs, &e);  /* values must go to the `stack` */
                first = fs->ActiveVarsCount;  /* return all `active' values */
                lua_assert(nRet == fs->FreeReg - first);
            }
        }
    }
    Lua::FuncState::Ret(fs, first, nRet);
}


static int statement(Lua::LexState *ls) {
    int line = ls->LineNumber;  /* may be needed for error messages */
    switch (ls->CurToken.Kind) {
        case Lua::Token::SymbolIf: {  /* stat -> ifStat */
            ifStat(ls, line);
            return 0;
        }
        case Lua::Token::SymbolWhile: {  /* stat -> whileStat */
            whileStat(ls, line);
            return 0;
        }
        case Lua::Token::SymbolDo: {  /* stat -> DO block END */
            Lua::LexState::Next(ls);  /* skip DO */
            block(ls);
            checkMatch(ls, Lua::Token::SymbolEnd, Lua::Token::SymbolDo, line);
            return 0;
        }
        case Lua::Token::SymbolFor: {  /* stat -> forStat */
            forStat(ls, line);
            return 0;
        }
        case Lua::Token::SymbolRepeat: {  /* stat -> repeatStat */
            repeatStat(ls, line);
            return 0;
        }
        case Lua::Token::SymbolFunction: {
            funcStat(ls, line);  /* stat -> funcStat */
            return 0;
        }
        case Lua::Token::SymbolLocal: {  /* stat -> localStat */
            Lua::LexState::Next(ls);  /* skip LOCAL */
            if (testNext(ls, Lua::Token::SymbolFunction))  /* local function? */
                localFunc(ls);
            else
                localStat(ls);
            return 0;
        }
        case Lua::Token::SymbolReturn: {  /* stat -> retStat */
            retStat(ls);
            return 1;  /* must be last statement */
        }
        case Lua::Token::SymbolBreak: {  /* stat -> breakStat */
            Lua::LexState::Next(ls);  /* skip BREAK */
            breakStat(ls);
            return 1;  /* must be last statement */
        }
        default: {
            exprStat(ls);
            return 0;  /* to avoid warnings */
        }
    }
}


static void chunk(Lua::LexState *ls) {
    /* chunk -> { stat [`;'] } */
    int islast = 0;
    enterLevel(ls);
    while (!islast && !blockFollow(ls->CurToken.Kind)) {
        islast = statement(ls);
        testNext(ls, ';');
        lua_assert(ls->fs->Func->MaxStackSize >= ls->fs->FreeReg &&
                   ls->fs->FreeReg >= ls->fs->ActiveVarsCount);
        ls->fs->FreeReg = ls->fs->ActiveVarsCount;  /* free registers */
    }
    leaveLevel(ls);
}

/* }====================================================================== */
