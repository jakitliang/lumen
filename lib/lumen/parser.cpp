/*!
 * @brief Lua Parser
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */


#include <cstring>

#define LUA_CORE

#include "lumen/code.h"
#include "lumen/do.h"
#include "lumen/lex.h"
#include "lumen/memory.h"
#include "lumen/object.h"
#include "lumen/common.inl"
#include "lumen/opcodes.h"
#include "lumen/parser.h"
#include "lumen/state.h"
#include "lumen/debug.h"


#define hasMulRet(k)        ((k) == Lumen::ExpDesc::KindCall || (k) == Lumen::ExpDesc::KindVararg)

#define getLocalVar(fs, i)    ((fs)->Func->LocalVars[(fs)->ActiveVars[i]])

#define LumenParserCheckLimit(fs, v, l, m)    if ((v)>(l)) errorLimit(fs,l,m)


/*
** nodes for block list (list of active blocks)
*/
struct Lumen::BlockNode {
    Lumen::BlockNode *Previous;  /* chain */
    int BreakList;  /* list of jumps out of this loop */
    Lumen::Byte ActiveVarsCount;  /* # active locals outside the breakable structure */
    Lumen::Byte IsUpValue;  /* true if some variable in the block is an upValue */
    Lumen::Byte IsBreakable;  /* true if `block` is a loop */
};


/*
** prototypes for recursive non-terminal functions
*/
static void chunk(Lumen::LexState *ls);

static void expr(Lumen::LexState *ls, Lumen::ExpDesc *v);


static void anchorToken(Lumen::LexState *ls) {
    if (ls->CurToken.Kind == Lumen::Token::SymbolName || ls->CurToken.Kind == Lumen::Token::SymbolString) {
        Lumen::String *ts = ls->CurToken.SemInfo.ts;
        Lumen::LexState::NewString(ls, ts->CString(), ts->Length);
    }
}


static void errorExpected(Lumen::LexState *ls, int token) {
    Lumen::LexState::SyntaxError(ls,
                                 Lumen::PushFString(ls->L, LUA_QS " expected",
                                                    Lumen::LexState::Token2CString(ls, token)));
}


static void errorLimit(Lumen::FuncState *fs, int limit, const char *what) {
    const char *msg = (fs->Func->LineDefined == 0) ?
                      Lumen::PushFString(fs->L, "main function has more than %d %s", limit, what) :
                      Lumen::PushFString(fs->L, "function at line %d has more than %d %s",
                                         fs->Func->LineDefined, limit, what);
    Lumen::LexState::LexError(fs->Lexer, msg, 0);
}


static int testNext(Lumen::LexState *ls, int c) {
    if (ls->CurToken.Kind == c) {
        Lumen::LexState::Next(ls);
        return 1;
    } else return 0;
}


static void check(Lumen::LexState *ls, int c) {
    if (ls->CurToken.Kind != c)
        errorExpected(ls, c);
}

static void checkNext(Lumen::LexState *ls, int c) {
    check(ls, c);
    Lumen::LexState::Next(ls);
}


#define checkCondition(ls, c, msg)    LumenDo( if (!(c)) Lumen::LexState::SyntaxError(ls, msg); )


static void checkMatch(Lumen::LexState *ls, int what, int who, int where) {
    if (!testNext(ls, what)) {
        if (where == ls->LineNumber)
            errorExpected(ls, what);
        else {
            Lumen::LexState::SyntaxError(ls, Lumen::PushFString(ls->L,
                                                                LUA_QS " expected (to close " LUA_QS " at line %d)",
                                                                Lumen::LexState::Token2CString(ls, what),
                                                                Lumen::LexState::Token2CString(ls, who), where));
        }
    }
}


static Lumen::String *strCheckName(Lumen::LexState *ls) {
    Lumen::String *ts;
    check(ls, Lumen::Token::SymbolName);
    ts = ls->CurToken.SemInfo.ts;
    Lumen::LexState::Next(ls);
    return ts;
}


static inline void initExp(Lumen::ExpDesc *e, Lumen::ExpDesc::Kind k, int i) {
    e->f = e->t = NO_JUMP;
    e->k = k;
    e->Info = i;
}


static inline void codeString(Lumen::LexState *ls, Lumen::ExpDesc *e, Lumen::String *s) {
    initExp(e, Lumen::ExpDesc::KindK, Lumen::FuncState::StringK(ls->fs, s));
}


static inline void checkName(Lumen::LexState *ls, Lumen::ExpDesc *e) {
    codeString(ls, e, strCheckName(ls));
}


static int registerLocalVar(Lumen::LexState *ls, Lumen::String *varname) {
    Lumen::FuncState *fs = ls->fs;
    Lumen::Proto *f = fs->Func;
    int oldSize = f->LocalVarsCount;
    LumenMemoryGrowVector(ls->L, f->LocalVars, fs->LocalVarsCount, f->LocalVarsCount,
                          Lumen::LocalVar, SHRT_MAX, "too many local variables");
    while (oldSize < f->LocalVarsCount) f->LocalVars[oldSize++].VarName = nullptr;
    f->LocalVars[fs->LocalVarsCount].VarName = varname;
    ls->L->BarrierGCObject(f, varname);
    return fs->LocalVarsCount++;
}


#define newLocalVarLiteral(ls, v, n) \
    newLocalVar(ls, Lumen::LexState::NewString(ls, "" v, (sizeof(v)/sizeof(char))-1), n)


static void newLocalVar(Lumen::LexState *ls, Lumen::String *name, int n) {
    Lumen::FuncState *fs = ls->fs;
    LumenParserCheckLimit(fs, fs->ActiveVarsCount + n + 1, LUA_MAX_VARS, "local variables");
    fs->ActiveVars[fs->ActiveVarsCount + n] = cast(unsigned short, registerLocalVar(ls, name));
}


static void adjustLocalVars(Lumen::LexState *ls, int nVars) {
    Lumen::FuncState *fs = ls->fs;
    fs->ActiveVarsCount = cast_byte(fs->ActiveVarsCount + nVars);
    for (; nVars; nVars--) {
        getLocalVar(fs, fs->ActiveVarsCount - nVars).StartPC = fs->PC;
    }
}


static void removeVars(Lumen::LexState *ls, int toLevel) {
    Lumen::FuncState *fs = ls->fs;
    while (fs->ActiveVarsCount > toLevel)
        getLocalVar(fs, --fs->ActiveVarsCount).EndPC = fs->PC;
}


static int indexUpValue(Lumen::FuncState *fs, Lumen::String *name, Lumen::ExpDesc *v) {
    int i;
    Lumen::Proto *f = fs->Func;
    int oldSize = f->UpValuesCount;
    for (i = 0; i < f->NUpValues; i++) {
        if (fs->UpValues[i].k == v->k && fs->UpValues[i].Info == v->Info) {
            LumenAssert(f->UpValues[i] == name);
            return i;
        }
    }
    /* new one */
    LumenParserCheckLimit(fs, f->NUpValues + 1, LUA_MAX_UP_VALUES, "upvalues");
    LumenMemoryGrowVector(fs->L, f->UpValues, f->NUpValues, f->UpValuesCount,
                          Lumen::String *, Lumen::MaxInt, "");
    while (oldSize < f->UpValuesCount) f->UpValues[oldSize++] = nullptr;
    f->UpValues[f->NUpValues] = name;
    fs->L->BarrierGCObject(f, name);
    LumenAssert(v->k == Lumen::ExpDesc::KindLocal || v->k == Lumen::ExpDesc::KindUpValue);
    fs->UpValues[f->NUpValues].k = cast_byte(v->k);
    fs->UpValues[f->NUpValues].Info = cast_byte(v->Info);
    return f->NUpValues++;
}


static int searchVar(Lumen::FuncState *fs, Lumen::String *n) {
    int i;
    for (i = fs->ActiveVarsCount - 1; i >= 0; i--) {
        if (n == getLocalVar(fs, i).VarName)
            return i;
    }
    return -1;  /* not found */
}


static void markUpValue(Lumen::FuncState *fs, int level) {
    Lumen::BlockNode *bl = fs->Blocks;
    while (bl && bl->ActiveVarsCount > level) bl = bl->Previous;
    if (bl) bl->IsUpValue = 1;
}


static int singleVarAux(Lumen::FuncState *fs, Lumen::String *n, Lumen::ExpDesc *var, int base) {
    if (fs == nullptr) {  /* no more levels? */
        initExp(var, Lumen::ExpDesc::KindGlobal, NO_REG);  /* default is global variable */
        return Lumen::ExpDesc::KindGlobal;
    } else {
        int v = searchVar(fs, n);  /* look up at current level */
        if (v >= 0) {
            initExp(var, Lumen::ExpDesc::KindLocal, v);
            if (!base)
                markUpValue(fs, v);  /* local will be used as an upVal */
            return Lumen::ExpDesc::KindLocal;
        } else {  /* not found at current level; try upper one */
            if (singleVarAux(fs->Prev, n, var, 0) == Lumen::ExpDesc::KindGlobal)
                return Lumen::ExpDesc::KindGlobal;
            var->Info = indexUpValue(fs, n, var);  /* else was LOCAL or UPVAL */
            var->k = Lumen::ExpDesc::KindUpValue;  /* upValue in this level */
            return Lumen::ExpDesc::KindUpValue;
        }
    }
}


static void singleVar(Lumen::LexState *ls, Lumen::ExpDesc *var) {
    Lumen::String *varname = strCheckName(ls);
    Lumen::FuncState *fs = ls->fs;
    if (singleVarAux(fs, varname, var, 1) == Lumen::ExpDesc::KindGlobal)
        var->Info = Lumen::FuncState::StringK(fs, varname);  /* info points to global name */
}


static void adjustAssign(Lumen::LexState *ls, int nVars, int nExps, Lumen::ExpDesc *e) {
    Lumen::FuncState *fs = ls->fs;
    int extra = nVars - nExps;
    if (hasMulRet(e->k)) {
        extra++;  /* includes call itself */
        if (extra < 0) extra = 0;
        Lumen::FuncState::SetReturns(fs, e, extra);  /* last exp. provides the difference */
        if (extra > 1) Lumen::FuncState::ReserveRegs(fs, extra - 1);
    } else {
        if (e->k != Lumen::ExpDesc::KindVoid) Lumen::FuncState::Exp2NextReg(fs, e);  /* close last expression */
        if (extra > 0) {
            int reg = fs->FreeReg;
            Lumen::FuncState::ReserveRegs(fs, extra);
            Lumen::FuncState::Nil(fs, reg, extra);
        }
    }
}


static void enterLevel(Lumen::LexState *ls) {
    if (++ls->L->NCCalls > LUA_MAX_C_CALLS)
        Lumen::LexState::LexError(ls, "chunk has too many syntax levels", 0);
}


#define leaveLevel(ls)    ((ls)->L->NCCalls--)


static void enterBlock(Lumen::FuncState *fs, Lumen::BlockNode *bl, Lumen::Byte isBreakable) {
    bl->BreakList = NO_JUMP;
    bl->IsBreakable = isBreakable;
    bl->ActiveVarsCount = fs->ActiveVarsCount;
    bl->IsUpValue = 0;
    bl->Previous = fs->Blocks;
    fs->Blocks = bl;
    LumenAssert(fs->FreeReg == fs->ActiveVarsCount);
}


static void leaveBlock(Lumen::FuncState *fs) {
    Lumen::BlockNode *bl = fs->Blocks;
    fs->Blocks = bl->Previous;
    removeVars(fs->Lexer, bl->ActiveVarsCount);
    if (bl->IsUpValue)
        Lumen::FuncState::CodeABC(fs, Lumen::OpCodeClose, bl->ActiveVarsCount, 0, 0);
    /* a block either controls scope or breaks (never both) */
    LumenAssert(!bl->IsBreakable || !bl->IsUpValue);
    LumenAssert(bl->ActiveVarsCount == fs->ActiveVarsCount);
    fs->FreeReg = fs->ActiveVarsCount;  /* free registers */
    Lumen::FuncState::PatchToHere(fs, bl->BreakList);
}


static void pushClosure(Lumen::LexState *ls, Lumen::FuncState *func, Lumen::ExpDesc *v) {
    Lumen::FuncState *fs = ls->fs;
    Lumen::Proto *f = fs->Func;
    int oldSize = f->SubProtoCount;
    int i;
    LumenMemoryGrowVector(ls->L, f->SubProto, fs->ProtoCount, f->SubProtoCount, Lumen::Proto *,
                          Lumen::Code::BxMaxArg, "constant table overflow");
    while (oldSize < f->SubProtoCount) f->SubProto[oldSize++] = nullptr;
    f->SubProto[fs->ProtoCount++] = func->Func;
    ls->L->BarrierGCObject(f, func->Func);
    initExp(v, Lumen::ExpDesc::KindRelocatable,
            Lumen::FuncState::CodeABx(fs, Lumen::OpCodeClosure, 0, fs->ProtoCount - 1));
    for (i = 0; i < func->Func->NUpValues; i++) {
        Lumen::OpCode o = (func->UpValues[i].k == Lumen::ExpDesc::KindLocal) ? Lumen::OpCodeMove
                                                                             : Lumen::OpCodeGetUpVal;
        Lumen::FuncState::CodeABC(fs, o, 0, func->UpValues[i].Info, 0);
    }
}


static void openFunc(Lumen::LexState *ls, Lumen::FuncState *fs) {
    Lumen::State *L = ls->L;
    Lumen::Proto *f = Lumen::Proto::New(L);
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
    fs->Constants = Lumen::Table::New(L, 0, 0);
    /* anchor table of constants and prototype (to avoid being collected) */
    LumenSetTableValue2S(L, L->Top, fs->Constants);
    LumenIncrTop(L);
    LumenSetProtoValue2S(L, L->Top, f);
    LumenIncrTop(L);
}


static void closeFunc(Lumen::LexState *ls) {
    Lumen::State *L = ls->L;
    Lumen::FuncState *fs = ls->fs;
    Lumen::Proto *f = fs->Func;
    removeVars(ls, 0);
    Lumen::FuncState::Ret(fs, 0, 0);  /* final return */
    LumenMemoryReAllocVector(L, f->Code, f->CodeCount, fs->PC, Lumen::Instruction);
    f->CodeCount = fs->PC;
    LumenMemoryReAllocVector(L, f->LineInfo, f->LineInfoCount, fs->PC, int);
    f->LineInfoCount = fs->PC;
    LumenMemoryReAllocVector(L, f->K, f->KCount, fs->ConstantsCount, Lumen::Object);
    f->KCount = fs->ConstantsCount;
    LumenMemoryReAllocVector(L, f->SubProto, f->SubProtoCount, fs->ProtoCount, Lumen::Proto *);
    f->SubProtoCount = fs->ProtoCount;
    LumenMemoryReAllocVector(L, f->LocalVars, f->LocalVarsCount, fs->LocalVarsCount, Lumen::LocalVar);
    f->LocalVarsCount = fs->LocalVarsCount;
    LumenMemoryReAllocVector(L, f->UpValues, f->UpValuesCount, f->NUpValues, Lumen::String *);
    f->UpValuesCount = f->NUpValues;
    LumenAssert(Lumen::Debug::CheckCode(f));
    LumenAssert(fs->Blocks == nullptr);
    ls->fs = fs->Prev;
    /* last token read was anchored in defunct function; must reAnchor it */
    if (fs) anchorToken(ls);
    L->Top -= 2;  /* remove table and prototype from the stack */
}


Lumen::Proto *Lumen::Parser::Parse(Lumen::State *L, Lumen::ZIO *z, Lumen::ZBuffer *buff, const char *name) {
    Lumen::LexState lexState;
    Lumen::FuncState funcState;
    lexState.buff = buff;
    Lumen::LexState::SetInput(L, &lexState, z, Lumen::String::New(L, name));
    openFunc(&lexState, &funcState);
    funcState.Func->IsVararg = Lumen::Proto::VarargIsVararg;  /* main func. is always vararg */
    Lumen::LexState::Next(&lexState);  /* read first token */
    chunk(&lexState);
    check(&lexState, Lumen::Token::SymbolEOS);
    closeFunc(&lexState);
    LumenAssert(funcState.Prev == nullptr);
    LumenAssert(funcState.Func->NUpValues == 0);
    LumenAssert(lexState.fs == nullptr);
    return funcState.Func;
}



/*============================================================*/
/* GRAMMAR RULES */
/*============================================================*/


static void field(Lumen::LexState *ls, Lumen::ExpDesc *v) {
    /* field -> ['.' | ':'] NAME */
    Lumen::FuncState *fs = ls->fs;
    Lumen::ExpDesc key;
    Lumen::FuncState::Exp2AnyReg(fs, v);
    Lumen::LexState::Next(ls);  /* skip the dot or colon */
    checkName(ls, &key);
    Lumen::FuncState::Indexed(fs, v, &key);
}


static void yIndex(Lumen::LexState *ls, Lumen::ExpDesc *v) {
    /* index -> '[' expr ']' */
    Lumen::LexState::Next(ls);  /* skip the `[` */
    expr(ls, v);
    Lumen::FuncState::Exp2Val(ls->fs, v);
    checkNext(ls, ']');
}


/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/


struct ConsControl {
    Lumen::ExpDesc v;  /* last list item read */
    Lumen::ExpDesc *t;  /* table descriptor */
    int nh;  /* total number of `record` elements */
    int na;  /* total number of array elements */
    int tostore;  /* number of array elements pending to be stored */
};


static void recField(Lumen::LexState *ls, struct ConsControl *cc) {
    /* recField -> (NAME | `['exp1`]') = exp1 */
    Lumen::FuncState *fs = ls->fs;
    int reg = ls->fs->FreeReg;
    Lumen::ExpDesc key, val;
    int rkKey;
    if (ls->CurToken.Kind == Lumen::Token::SymbolName) {
        LumenParserCheckLimit(fs, cc->nh, Lumen::MaxInt, "items in a constructor");
        checkName(ls, &key);
    } else  /* ls->t.token == '[' */
        yIndex(ls, &key);
    cc->nh++;
    checkNext(ls, '=');
    rkKey = Lumen::FuncState::Exp2RK(fs, &key);
    expr(ls, &val);
    Lumen::FuncState::CodeABC(fs, Lumen::OpCodeSetTable, cc->t->Info, rkKey, Lumen::FuncState::Exp2RK(fs, &val));
    fs->FreeReg = reg;  /* free registers */
}


static void closeListField(Lumen::FuncState *fs, struct ConsControl *cc) {
    if (cc->v.k == Lumen::ExpDesc::KindVoid) return;  /* there is no list item */
    Lumen::FuncState::Exp2NextReg(fs, &cc->v);
    cc->v.k = Lumen::ExpDesc::KindVoid;
    if (cc->tostore == LUA_FIELDS_PER_FLUSH) {
        Lumen::FuncState::SetList(fs, cc->t->Info, cc->na, cc->tostore);  /* flush */
        cc->tostore = 0;  /* no more items pending */
    }
}


static void lastListField(Lumen::FuncState *fs, struct ConsControl *cc) {
    if (cc->tostore == 0) return;
    if (hasMulRet(cc->v.k)) {
        LumenFuncStateSetMulRet(fs, &cc->v);
        Lumen::FuncState::SetList(fs, cc->t->Info, cc->na, Lumen::RetMul);
        cc->na--;  /* do not count last expression (unknown number of elements) */
    } else {
        if (cc->v.k != Lumen::ExpDesc::KindVoid)
            Lumen::FuncState::Exp2NextReg(fs, &cc->v);
        Lumen::FuncState::SetList(fs, cc->t->Info, cc->na, cc->tostore);
    }
}


static void listField(Lumen::LexState *ls, struct ConsControl *cc) {
    expr(ls, &cc->v);
    LumenParserCheckLimit(ls->fs, cc->na, Lumen::MaxInt, "items in a constructor");
    cc->na++;
    cc->tostore++;
}


static void constructor(Lumen::LexState *ls, Lumen::ExpDesc *t) {
    /* constructor -> ?? */
    Lumen::FuncState *fs = ls->fs;
    int line = ls->LineNumber;
    int pc = Lumen::FuncState::CodeABC(fs, Lumen::OpCodeNewTable, 0, 0, 0);
    ConsControl cc;
    cc.na = cc.nh = cc.tostore = 0;
    cc.t = t;
    initExp(t, Lumen::ExpDesc::KindRelocatable, pc);
    initExp(&cc.v, Lumen::ExpDesc::KindVoid, 0);  /* no value (yet) */
    Lumen::FuncState::Exp2NextReg(ls->fs, t);  /* fix it at stack top (for gc) */
    checkNext(ls, '{');
    do {
        LumenAssert(cc.v.k == Lumen::ExpDesc::KindVoid || cc.tostore > 0);
        if (ls->CurToken.Kind == '}') break;
        closeListField(fs, &cc);
        switch (ls->CurToken.Kind) {
            case Lumen::Token::SymbolName: {  /* may be listFields or recFields */
                Lumen::LexState::LookAhead(ls);
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
    LumenOpCodeSetArgB(fs->Func->Code[pc], Lumen::Int2FB(cc.na)); /* set initial array size */
    LumenOpCodeSetArgC(fs->Func->Code[pc], Lumen::Int2FB(cc.nh));  /* set initial table size */
}

/* }====================================================================== */



static void parList(Lumen::LexState *ls) {
    /* parList -> [ param { `,' param } ] */
    Lumen::FuncState *fs = ls->fs;
    Lumen::Proto *f = fs->Func;
    int nParams = 0;
    f->IsVararg = 0;
    if (ls->CurToken.Kind != ')') {  /* is `parList` not empty? */
        do {
            switch (ls->CurToken.Kind) {
                case Lumen::Token::SymbolName: {  /* param -> NAME */
                    newLocalVar(ls, strCheckName(ls), nParams++);
                    break;
                }
                case Lumen::Token::SymbolDots: {  /* param -> `...' */
                    Lumen::LexState::Next(ls);
#if defined(LUA_COMPAT_VARARG)
                    /* use `arg` as default name */
                    newLocalVarLiteral(ls, "arg", nParams++);
                    f->IsVararg = Lumen::Proto::VarargHasArg | Lumen::Proto::VarargIsNeedsArg;
#endif
                    f->IsVararg |= Lumen::Proto::VarargIsVararg;
                    break;
                }
                default:
                    Lumen::LexState::SyntaxError(ls, "<name> or " LUA_QL("...") " expected");
            }
        } while (!f->IsVararg && testNext(ls, ','));
    }
    adjustLocalVars(ls, nParams);
    f->NUmParams = cast_byte(fs->ActiveVarsCount - (f->IsVararg & Lumen::Proto::VarargHasArg));
    Lumen::FuncState::ReserveRegs(fs, fs->ActiveVarsCount);  /* reserve register for parameters */
}


static void body(Lumen::LexState *ls, Lumen::ExpDesc *e, int needSelf, int line) {
    /* body ->  `(' parList `)' chunk END */
    Lumen::FuncState new_fs;
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
    checkMatch(ls, Lumen::Token::SymbolEnd, Lumen::Token::SymbolFunction, line);
    closeFunc(ls);
    pushClosure(ls, &new_fs, e);
}


static int expList1(Lumen::LexState *ls, Lumen::ExpDesc *v) {
    /* expList1 -> expr { `,' expr } */
    int n = 1;  /* at least one expression */
    expr(ls, v);
    while (testNext(ls, ',')) {
        Lumen::FuncState::Exp2NextReg(ls->fs, v);
        expr(ls, v);
        n++;
    }
    return n;
}


static void funcArgs(Lumen::LexState *ls, Lumen::ExpDesc *f) {
    Lumen::FuncState *fs = ls->fs;
    Lumen::ExpDesc args;
    int base, nParams;
    int line = ls->LineNumber;
    switch (ls->CurToken.Kind) {
        case '(': {  /* funcArgs -> `(' [ expList1 ] `)' */
            if (line != ls->LastLine)
                Lumen::LexState::SyntaxError(ls, "ambiguous syntax (function call x new statement)");
            Lumen::LexState::Next(ls);
            if (ls->CurToken.Kind == ')')  /* arg list is empty? */
                args.k = Lumen::ExpDesc::KindVoid;
            else {
                expList1(ls, &args);
                LumenFuncStateSetMulRet(fs, &args);
            }
            checkMatch(ls, ')', '(', line);
            break;
        }
        case '{': {  /* funcArgs -> constructor */
            constructor(ls, &args);
            break;
        }
        case Lumen::Token::SymbolString: {  /* funcArgs -> STRING */
            codeString(ls, &args, ls->CurToken.SemInfo.ts);
            Lumen::LexState::Next(ls);  /* must use `semInfo' before `next' */
            break;
        }
        default: {
            Lumen::LexState::SyntaxError(ls, "function arguments expected");
            return;
        }
    }
    LumenAssert(f->k == Lumen::ExpDesc::KindNonRelocatable);
    base = f->Info;  /* base register for call */
    if (hasMulRet(args.k))
        nParams = Lumen::RetMul;  /* open call */
    else {
        if (args.k != Lumen::ExpDesc::KindVoid)
            Lumen::FuncState::Exp2NextReg(fs, &args);  /* close last argument */
        nParams = fs->FreeReg - (base + 1);
    }
    initExp(f, Lumen::ExpDesc::KindCall, Lumen::FuncState::CodeABC(fs, Lumen::OpCodeCall, base, nParams + 1, 2));
    Lumen::FuncState::FixLine(fs, line);
    fs->FreeReg = base + 1;  /* call remove function and arguments and leaves
                            (unless changed) one result */
}


/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


static void prefixExp(Lumen::LexState *ls, Lumen::ExpDesc *v) {
    /* prefixExp -> NAME | '(' expr ')' */
    switch (ls->CurToken.Kind) {
        case '(': {
            int line = ls->LineNumber;
            Lumen::LexState::Next(ls);
            expr(ls, v);
            checkMatch(ls, ')', '(', line);
            Lumen::FuncState::DischargeVars(ls->fs, v);
            return;
        }
        case Lumen::Token::SymbolName: {
            singleVar(ls, v);
            return;
        }
        default: {
            Lumen::LexState::SyntaxError(ls, "unexpected symbol");
            return;
        }
    }
}


static void primaryExp(Lumen::LexState *ls, Lumen::ExpDesc *v) {
    /* primaryExp ->
          prefixExp { `.' NAME | `[' exp `]' | `:' NAME funcArgs | funcArgs } */
    Lumen::FuncState *fs = ls->fs;
    prefixExp(ls, v);
    for (;;) {
        switch (ls->CurToken.Kind) {
            case '.': {  /* field */
                field(ls, v);
                break;
            }
            case '[': {  /* `[' exp1 `]' */
                Lumen::ExpDesc key;
                Lumen::FuncState::Exp2AnyReg(fs, v);
                yIndex(ls, &key);
                Lumen::FuncState::Indexed(fs, v, &key);
                break;
            }
            case ':': {  /* `:' NAME funcArgs */
                Lumen::ExpDesc key;
                Lumen::LexState::Next(ls);
                checkName(ls, &key);
                Lumen::FuncState::Self(fs, v, &key);
                funcArgs(ls, v);
                break;
            }
            case '(':
            case Lumen::Token::SymbolString:
            case '{': {  /* funcArgs */
                Lumen::FuncState::Exp2NextReg(fs, v);
                funcArgs(ls, v);
                break;
            }
            default:
                return;
        }
    }
}


static void simpleExp(Lumen::LexState *ls, Lumen::ExpDesc *v) {
    /* simpleExp -> NUMBER | STRING | NIL | true | false | ... |
                    constructor | FUNCTION body | primaryExp */
    switch (ls->CurToken.Kind) {
        case Lumen::Token::SymbolNumber: {
            initExp(v, Lumen::ExpDesc::KindKNum, 0);
            v->NumberValue = ls->CurToken.SemInfo.r;
            break;
        }
        case Lumen::Token::SymbolString: {
            codeString(ls, v, ls->CurToken.SemInfo.ts);
            break;
        }
        case Lumen::Token::SymbolNil: {
            initExp(v, Lumen::ExpDesc::KindNil, 0);
            break;
        }
        case Lumen::Token::SymbolTrue: {
            initExp(v, Lumen::ExpDesc::KindTrue, 0);
            break;
        }
        case Lumen::Token::SymbolFalse: {
            initExp(v, Lumen::ExpDesc::KindFalse, 0);
            break;
        }
        case Lumen::Token::SymbolDots: {  /* vararg */
            Lumen::FuncState *fs = ls->fs;
            checkCondition(ls, fs->Func->IsVararg,
                           "cannot use " LUA_QL("...") " outside a vararg function");
            fs->Func->IsVararg &= ~Lumen::Proto::VarargIsNeedsArg;  /* don't need 'arg' */
            initExp(v, Lumen::ExpDesc::KindVararg, Lumen::FuncState::CodeABC(fs, Lumen::OpCodeVararg, 0, 1, 0));
            break;
        }
        case '{': {  /* constructor */
            constructor(ls, v);
            return;
        }
        case Lumen::Token::SymbolFunction: {
            Lumen::LexState::Next(ls);
            body(ls, v, 0, ls->LineNumber);
            return;
        }
        default: {
            primaryExp(ls, v);
            return;
        }
    }
    Lumen::LexState::Next(ls);
}


static Lumen::UnOpr getUnOpr(int op) {
    switch (op) {
        case Lumen::Token::SymbolNot:
            return Lumen::UnOprNot;
        case '-':
            return Lumen::UnOprMinus;
        case '#':
            return Lumen::UnOprLen;
        default:
            return Lumen::UnOprNo;
    }
}


static Lumen::BinOpr getBinOpr(int op) {
    switch (op) {
        case '+':
            return Lumen::BinOprAdd;
        case '-':
            return Lumen::BinOprSub;
        case '*':
            return Lumen::BinOprMul;
        case '/':
            return Lumen::BinOprDiv;
        case '%':
            return Lumen::BinOprMod;
        case '^':
            return Lumen::BinOprPow;
        case Lumen::Token::SymbolConcat:
            return Lumen::BinOprConcat;
        case Lumen::Token::SymbolNE:
            return Lumen::BinOprNE;
        case Lumen::Token::SymbolEQ:
            return Lumen::BinOprEQ;
        case '<':
            return Lumen::BinOprLT;
        case Lumen::Token::SymbolLE:
            return Lumen::BinOprLE;
        case '>':
            return Lumen::BinOprGT;
        case Lumen::Token::SymbolGE:
            return Lumen::BinOprGE;
        case Lumen::Token::SymbolAnd:
            return Lumen::BinOprAND;
        case Lumen::Token::SymbolOr:
            return Lumen::BinOprOR;
        default:
            return Lumen::BinOprNo;
    }
}


static const struct {
    Lumen::Byte left;  /* left priority for each binary operator */
    Lumen::Byte right; /* right priority */
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
static Lumen::BinOpr subExpr(Lumen::LexState *ls, Lumen::ExpDesc *v, unsigned int limit) {
    Lumen::BinOpr op;
    Lumen::UnOpr uop;
    enterLevel(ls);
    uop = getUnOpr(ls->CurToken.Kind);
    if (uop != Lumen::UnOprNo) {
        Lumen::LexState::Next(ls);
        subExpr(ls, v, UNARY_PRIORITY);
        Lumen::FuncState::Prefix(ls->fs, uop, v);
    } else simpleExp(ls, v);
    /* expand while operators have priorities higher than `limit` */
    op = getBinOpr(ls->CurToken.Kind);
    while (op != Lumen::BinOprNo && priority[op].left > limit) {
        Lumen::ExpDesc v2;
        Lumen::BinOpr nexTop;
        Lumen::LexState::Next(ls);
        Lumen::FuncState::InFix(ls->fs, op, v);
        /* read sub-expression with higher priority */
        nexTop = subExpr(ls, &v2, priority[op].right);
        Lumen::FuncState::PosFix(ls->fs, op, v, &v2);
        op = nexTop;
    }
    leaveLevel(ls);
    return op;  /* return first untreated operator */
}


static void expr(Lumen::LexState *ls, Lumen::ExpDesc *v) {
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
        case Lumen::Token::SymbolElse:
        case Lumen::Token::SymbolElseIf:
        case Lumen::Token::SymbolEnd:
        case Lumen::Token::SymbolUntil:
        case Lumen::Token::SymbolEOS:
            return 1;
        default:
            return 0;
    }
}


static void block(Lumen::LexState *ls) {
    /* block -> chunk */
    Lumen::FuncState *fs = ls->fs;
    Lumen::BlockNode bl;
    enterBlock(fs, &bl, 0);
    chunk(ls);
    LumenAssert(bl.BreakList == NO_JUMP);
    leaveBlock(fs);
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
    LHS_assign *prev;
    Lumen::ExpDesc v;  /* variable (global, local, upValue, or indexed) */
};


/*
** check whether, in an assignment to a local variable, the local variable
** is needed in a previous assignment (to a table). If so, save original
** local value in a safe place and use this safe copy in the previous
** assignment.
*/
static void checkConflict(Lumen::LexState *ls, struct LHS_assign *lh, Lumen::ExpDesc *v) {
    Lumen::FuncState *fs = ls->fs;
    int extra = fs->FreeReg;  /* eventual position to save local variable */
    int conflict = 0;
    for (; lh; lh = lh->prev) {
        if (lh->v.k == Lumen::ExpDesc::KindIndexed) {
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
        Lumen::FuncState::CodeABC(fs, Lumen::OpCodeMove, fs->FreeReg, v->Info, 0);  /* make copy */
        Lumen::FuncState::ReserveRegs(fs, 1);
    }
}


static void assignment(Lumen::LexState *ls, struct LHS_assign *lh, int nVars) {
    Lumen::ExpDesc e;
    checkCondition(ls, Lumen::ExpDesc::KindLocal <= lh->v.k && lh->v.k <= Lumen::ExpDesc::KindIndexed,
                   "syntax error");
    if (testNext(ls, ',')) {  /* assignment -> `,' primaryExp assignment */
        LHS_assign nv;
        nv.prev = lh;
        primaryExp(ls, &nv.v);
        if (nv.v.k == Lumen::ExpDesc::KindLocal)
            checkConflict(ls, lh, &nv.v);
        LumenParserCheckLimit(ls->fs, nVars, LUA_MAX_C_CALLS - ls->L->NCCalls,
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
            Lumen::FuncState::SetOneRet(ls->fs, &e);  /* close last expression */
            Lumen::FuncState::StoreVar(ls->fs, &lh->v, &e);
            return;  /* avoid default */
        }
    }
    initExp(&e, Lumen::ExpDesc::KindNonRelocatable, ls->fs->FreeReg - 1);  /* default assignment */
    Lumen::FuncState::StoreVar(ls->fs, &lh->v, &e);
}


static int cond(Lumen::LexState *ls) {
    /* cond -> exp */
    Lumen::ExpDesc v;
    expr(ls, &v);  /* read condition */
    if (v.k == Lumen::ExpDesc::KindNil) v.k = Lumen::ExpDesc::KindFalse;  /* `false(s)` are all equal here */
    Lumen::FuncState::GoIfTrue(ls->fs, &v);
    return v.f;
}


static void breakStat(Lumen::LexState *ls) {
    Lumen::FuncState *fs = ls->fs;
    Lumen::BlockNode *bl = fs->Blocks;
    int upVal = 0;
    while (bl && !bl->IsBreakable) {
        upVal |= bl->IsUpValue;
        bl = bl->Previous;
    }
    if (!bl)
        Lumen::LexState::SyntaxError(ls, "no loop to break");
    if (upVal)
        Lumen::FuncState::CodeABC(fs, Lumen::OpCodeClose, bl->ActiveVarsCount, 0, 0);
    Lumen::FuncState::Concat(fs, &bl->BreakList, Lumen::FuncState::Jump(fs));
}


static void whileStat(Lumen::LexState *ls, int line) {
    /* whileStat -> WHILE cond DO block END */
    Lumen::FuncState *fs = ls->fs;
    int whileInit;
    int condExit;
    Lumen::BlockNode bl;
    Lumen::LexState::Next(ls);  /* skip WHILE */
    whileInit = Lumen::FuncState::GetLabel(fs);
    condExit = cond(ls);
    enterBlock(fs, &bl, 1);
    checkNext(ls, Lumen::Token::SymbolDo);
    block(ls);
    Lumen::FuncState::PatchList(fs, Lumen::FuncState::Jump(fs), whileInit);
    checkMatch(ls, Lumen::Token::SymbolEnd, Lumen::Token::SymbolWhile, line);
    leaveBlock(fs);
    Lumen::FuncState::PatchToHere(fs, condExit);  /* false conditions finish the loop */
}


static void repeatStat(Lumen::LexState *ls, int line) {
    /* repeatStat -> REPEAT block UNTIL cond */
    int condExit;
    Lumen::FuncState *fs = ls->fs;
    int repeat_init = Lumen::FuncState::GetLabel(fs);
    Lumen::BlockNode bl1, bl2;
    enterBlock(fs, &bl1, 1);  /* loop block */
    enterBlock(fs, &bl2, 0);  /* scope block */
    Lumen::LexState::Next(ls);  /* skip REPEAT */
    chunk(ls);
    checkMatch(ls, Lumen::Token::SymbolUntil, Lumen::Token::SymbolRepeat, line);
    condExit = cond(ls);  /* read condition (inside scope block) */
    if (!bl2.IsUpValue) {  /* no upValues? */
        leaveBlock(fs);  /* finish scope */
        Lumen::FuncState::PatchList(ls->fs, condExit, repeat_init);  /* close the loop */
    } else {  /* complete semantics when there are upValues */
        breakStat(ls);  /* if condition then break */
        Lumen::FuncState::PatchToHere(ls->fs, condExit);  /* else... */
        leaveBlock(fs);  /* finish scope... */
        Lumen::FuncState::PatchList(ls->fs, Lumen::FuncState::Jump(fs), repeat_init);  /* and repeat */
    }
    leaveBlock(fs);  /* finish loop */
}


static int exp1(Lumen::LexState *ls) {
    Lumen::ExpDesc e;
    int k;
    expr(ls, &e);
    k = e.k;
    Lumen::FuncState::Exp2NextReg(ls->fs, &e);
    return k;
}


static void forBody(Lumen::LexState *ls, int base, int line, int nVars, int isNum) {
    /* forBody -> DO block */
    Lumen::BlockNode bl;
    Lumen::FuncState *fs = ls->fs;
    int prep, endFor;
    adjustLocalVars(ls, 3);  /* control variables */
    checkNext(ls, Lumen::Token::SymbolDo);
    prep = isNum ? LumenFuncStateCodeAsBx(fs, Lumen::OpCodeForPrep, base, NO_JUMP) : Lumen::FuncState::Jump(fs);
    enterBlock(fs, &bl, 0);  /* scope for declared variables */
    adjustLocalVars(ls, nVars);
    Lumen::FuncState::ReserveRegs(fs, nVars);
    block(ls);
    leaveBlock(fs);  /* end of scope for declared variables */
    Lumen::FuncState::PatchToHere(fs, prep);
    endFor = (isNum) ? LumenFuncStateCodeAsBx(fs, Lumen::OpCodeForLoop, base, NO_JUMP) :
             Lumen::FuncState::CodeABC(fs, Lumen::OpCodeTForLoop, base, 0, nVars);
    Lumen::FuncState::FixLine(fs, line);  /* pretend that `OP_FOR` starts the loop */
    Lumen::FuncState::PatchList(fs, (isNum ? endFor : Lumen::FuncState::Jump(fs)), prep + 1);
}


static void forNum(Lumen::LexState *ls, Lumen::String *varname, int line) {
    /* forNum -> NAME = exp1,exp1[,exp1] forBody */
    Lumen::FuncState *fs = ls->fs;
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
        Lumen::FuncState::CodeABx(fs, Lumen::OpCodeLoadK, fs->FreeReg, Lumen::FuncState::NumberK(fs, 1));
        Lumen::FuncState::ReserveRegs(fs, 1);
    }
    forBody(ls, base, line, 1, 1);
}


static void forList(Lumen::LexState *ls, Lumen::String *indexName) {
    /* forList -> NAME {,NAME} IN expList1 forBody */
    Lumen::FuncState *fs = ls->fs;
    Lumen::ExpDesc e;
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
    checkNext(ls, Lumen::Token::SymbolIn);
    line = ls->LineNumber;
    adjustAssign(ls, 3, expList1(ls, &e), &e);
    Lumen::FuncState::CheckStack(fs, 3);  /* extra space to call generator */
    forBody(ls, base, line, nVars - 3, 0);
}


static void forStat(Lumen::LexState *ls, int line) {
    /* forStat -> FOR (forNum | forList) END */
    Lumen::FuncState *fs = ls->fs;
    Lumen::String *varname;
    Lumen::BlockNode bl;
    enterBlock(fs, &bl, 1);  /* scope for loop and control variables */
    Lumen::LexState::Next(ls);  /* skip `for' */
    varname = strCheckName(ls);  /* first variable name */
    switch (ls->CurToken.Kind) {
        case '=':
            forNum(ls, varname, line);
            break;
        case ',':
        case Lumen::Token::SymbolIn:
            forList(ls, varname);
            break;
        default:
            Lumen::LexState::SyntaxError(ls, LUA_QL("=") " or " LUA_QL("in") " expected");
    }
    checkMatch(ls, Lumen::Token::SymbolEnd, Lumen::Token::SymbolFor, line);
    leaveBlock(fs);  /* loop scope (`break` jumps to this point) */
}


static int testThenBlock(Lumen::LexState *ls) {
    /* test_then_block -> [IF | ELSEIF] cond THEN block */
    int condExit;
    Lumen::LexState::Next(ls);  /* skip IF or ELSEIF */
    condExit = cond(ls);
    checkNext(ls, Lumen::Token::SymbolThen);
    block(ls);  /* `then' part */
    return condExit;
}


static void ifStat(Lumen::LexState *ls, int line) {
    /* ifStat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
    Lumen::FuncState *fs = ls->fs;
    int fList;
    int escapeList = NO_JUMP;
    fList = testThenBlock(ls);  /* IF cond THEN block */
    while (ls->CurToken.Kind == Lumen::Token::SymbolElseIf) {
        Lumen::FuncState::Concat(fs, &escapeList, Lumen::FuncState::Jump(fs));
        Lumen::FuncState::PatchToHere(fs, fList);
        fList = testThenBlock(ls);  /* ELSEIF cond THEN block */
    }
    if (ls->CurToken.Kind == Lumen::Token::SymbolElse) {
        Lumen::FuncState::Concat(fs, &escapeList, Lumen::FuncState::Jump(fs));
        Lumen::FuncState::PatchToHere(fs, fList);
        Lumen::LexState::Next(ls);  /* skip ELSE (after patch, for correct line info) */
        block(ls);  /* `else' part */
    } else
        Lumen::FuncState::Concat(fs, &escapeList, fList);
    Lumen::FuncState::PatchToHere(fs, escapeList);
    checkMatch(ls, Lumen::Token::SymbolEnd, Lumen::Token::SymbolIf, line);
}


static void localFunc(Lumen::LexState *ls) {
    Lumen::ExpDesc v, b;
    Lumen::FuncState *fs = ls->fs;
    newLocalVar(ls, strCheckName(ls), 0);
    initExp(&v, Lumen::ExpDesc::KindLocal, fs->FreeReg);
    Lumen::FuncState::ReserveRegs(fs, 1);
    adjustLocalVars(ls, 1);
    body(ls, &b, 0, ls->LineNumber);
    Lumen::FuncState::StoreVar(fs, &v, &b);
    /* debug information will only see the variable after this point! */
    getLocalVar(fs, fs->ActiveVarsCount - 1).StartPC = fs->PC;
}


static void localStat(Lumen::LexState *ls) {
    /* stat -> LOCAL NAME {`,' NAME} [`=' expList1] */
    int nVars = 0;
    int nExps;
    Lumen::ExpDesc e;
    do {
        newLocalVar(ls, strCheckName(ls), nVars++);
    } while (testNext(ls, ','));
    if (testNext(ls, '='))
        nExps = expList1(ls, &e);
    else {
        e.k = Lumen::ExpDesc::KindVoid;
        nExps = 0;
    }
    adjustAssign(ls, nVars, nExps, &e);
    adjustLocalVars(ls, nVars);
}


static int funcName(Lumen::LexState *ls, Lumen::ExpDesc *v) {
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


static void funcStat(Lumen::LexState *ls, int line) {
    /* funcStat -> FUNCTION funcName body */
    int needSelf;
    Lumen::ExpDesc v, b;
    Lumen::LexState::Next(ls);  /* skip FUNCTION */
    needSelf = funcName(ls, &v);
    body(ls, &b, needSelf, line);
    Lumen::FuncState::StoreVar(ls->fs, &v, &b);
    Lumen::FuncState::FixLine(ls->fs, line);  /* definition `happens` in the first line */
}


static void exprStat(Lumen::LexState *ls) {
    /* stat -> func | assignment */
    Lumen::FuncState *fs = ls->fs;
    LHS_assign v;
    primaryExp(ls, &v.v);
    if (v.v.k == Lumen::ExpDesc::KindCall)  /* stat -> func */
        LumenOpCodeSetArgC(LumenFuncStateGetCode(fs, &v.v), 1);  /* call statement uses no results */
    else {  /* stat -> assignment */
        v.prev = nullptr;
        assignment(ls, &v, 1);
    }
}


static void retStat(Lumen::LexState *ls) {
    /* stat -> RETURN expList */
    Lumen::FuncState *fs = ls->fs;
    Lumen::ExpDesc e;
    int first, nRet;  /* registers with returned values */
    Lumen::LexState::Next(ls);  /* skip RETURN */
    if (blockFollow(ls->CurToken.Kind) || ls->CurToken.Kind == ';')
        first = nRet = 0;  /* return no values */
    else {
        nRet = expList1(ls, &e);  /* optional return values */
        if (hasMulRet(e.k)) {
            LumenFuncStateSetMulRet(fs, &e);
            if (e.k == Lumen::ExpDesc::KindCall && nRet == 1) {  /* tail call? */
                LumenOpCodeSet(LumenFuncStateGetCode(fs, &e), Lumen::OpCodeTailCall);
                LumenAssert(LumenOpCodeGetArgA(LumenFuncStateGetCode(fs, &e)) == fs->ActiveVarsCount);
            }
            first = fs->ActiveVarsCount;
            nRet = Lumen::RetMul;  /* return all values */
        } else {
            if (nRet == 1)  /* only one single value? */
                first = Lumen::FuncState::Exp2AnyReg(fs, &e);
            else {
                Lumen::FuncState::Exp2NextReg(fs, &e);  /* values must go to the `stack` */
                first = fs->ActiveVarsCount;  /* return all `active' values */
                LumenAssert(nRet == fs->FreeReg - first);
            }
        }
    }
    Lumen::FuncState::Ret(fs, first, nRet);
}


static int statement(Lumen::LexState *ls) {
    int line = ls->LineNumber;  /* may be needed for error messages */
    switch (ls->CurToken.Kind) {
        case Lumen::Token::SymbolIf: {  /* stat -> ifStat */
            ifStat(ls, line);
            return 0;
        }
        case Lumen::Token::SymbolWhile: {  /* stat -> whileStat */
            whileStat(ls, line);
            return 0;
        }
        case Lumen::Token::SymbolDo: {  /* stat -> DO block END */
            Lumen::LexState::Next(ls);  /* skip DO */
            block(ls);
            checkMatch(ls, Lumen::Token::SymbolEnd, Lumen::Token::SymbolDo, line);
            return 0;
        }
        case Lumen::Token::SymbolFor: {  /* stat -> forStat */
            forStat(ls, line);
            return 0;
        }
        case Lumen::Token::SymbolRepeat: {  /* stat -> repeatStat */
            repeatStat(ls, line);
            return 0;
        }
        case Lumen::Token::SymbolFunction: {
            funcStat(ls, line);  /* stat -> funcStat */
            return 0;
        }
        case Lumen::Token::SymbolLocal: {  /* stat -> localStat */
            Lumen::LexState::Next(ls);  /* skip LOCAL */
            if (testNext(ls, Lumen::Token::SymbolFunction))  /* local function? */
                localFunc(ls);
            else
                localStat(ls);
            return 0;
        }
        case Lumen::Token::SymbolReturn: {  /* stat -> retStat */
            retStat(ls);
            return 1;  /* must be last statement */
        }
        case Lumen::Token::SymbolBreak: {  /* stat -> breakStat */
            Lumen::LexState::Next(ls);  /* skip BREAK */
            breakStat(ls);
            return 1;  /* must be last statement */
        }
        default: {
            exprStat(ls);
            return 0;  /* to avoid warnings */
        }
    }
}


static void chunk(Lumen::LexState *ls) {
    /* chunk -> { stat [`;'] } */
    int islast = 0;
    enterLevel(ls);
    while (!islast && !blockFollow(ls->CurToken.Kind)) {
        islast = statement(ls);
        testNext(ls, ';');
        LumenAssert(ls->fs->Func->MaxStackSize >= ls->fs->FreeReg &&
                    ls->fs->FreeReg >= ls->fs->ActiveVarsCount);
        ls->fs->FreeReg = ls->fs->ActiveVarsCount;  /* free registers */
    }
    leaveLevel(ls);
}

/* }====================================================================== */
