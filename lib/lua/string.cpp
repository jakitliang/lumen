/*!
 * @brief Standard library for string operations and pattern-matching
 * @author Lua.org, PUC-Rio, Jakit (https://github.com/jakitliang/lumen)
 * @date 2025/5/13
 * @copyright
 * Copyright (c) 2025 Lua.org, PUC-Rio, Jakit. All rights reserved.
 * Licensed under the BSD 2-Clause License.
 */

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

#define LUA_LIB

#include "lumen.h"
#include "lumen/memory.h"
#include "lumen/string.h"
#include "lumen/common.inl"


/* macro to `unsigned` a character */
#define uchar(c) static_cast<unsigned char>(c)

#define ToLumenState(L) reinterpret_cast<Lumen::State *>(L)
#define ToLumenIState(L) reinterpret_cast<Lumen::IState *>(L)
#define ToLumenIString(s) reinterpret_cast<Lumen::IString *>(s)
#define ToLumenString(s) reinterpret_cast<Lumen::String *>(s)

struct Lumen::IString::Context {
    static int Byte(Lumen::IState *L);

    static int Char(Lumen::IState *L);

    static int Dump(Lumen::IState *L);

    static int Find(Lumen::IState *L);

    static int Format(Lumen::IState *L);

    static int GFindNodeF(Lumen::IState *L);

    static int GMatch(Lumen::IState *L);

    static int GSub(Lumen::IState *L);

    static int Length(Lumen::IState *L);

    static int Lower(Lumen::IState *L);

    static int Match(Lumen::IState *L);

    static int Rep(Lumen::IState *L);

    static int Reverse(Lumen::IState *L);

    static int Sub(Lumen::IState *L);

    static int Upper(Lumen::IState *L);

    static int Pack(Lumen::IState *L);

    static int PackSize(Lumen::IState *L);

    static int Unpack(Lumen::IState *L);
};

int Lumen::IString::Context::Length(Lumen::IState *L) {
    Lumen::UInteger length;
    L->CheckString(1, &length);
    L->PushInteger((Lumen::Integer) length);
    return 1;
}


static ptrdiff_t relStringPos(ptrdiff_t pos, size_t len) {
    /* relative string position: negative means back from end */
    if (pos < 0) pos += (ptrdiff_t) len + 1;
    return (pos >= 0) ? pos : 0;
}


int Lumen::IString::Context::Sub(Lumen::IState *L) {
    size_t l;
    const char *s = L->CheckString(1, &l);
    ptrdiff_t start = relStringPos(L->CheckInteger(2), l);
    ptrdiff_t end = relStringPos(L->OptInteger(3, -1), l);
    if (start < 1) start = 1;
    if (end > (ptrdiff_t) l) end = (ptrdiff_t) l;
    if (start <= end)
        L->PushString(s + start - 1, end - start + 1);
    else
        L->PushLiteral("");
    return 1;
}


int Lumen::IString::Context::Reverse(Lumen::IState *L) {
    size_t l;
    Lumen::Buffer b; // NOLINT
    const char *s = L->CheckString(1, &l);
    b.Init(L);
    while (l--) b.AddChar(s[l]);
    b.PushResult();
    return 1;
}


int Lumen::IString::Context::Lower(Lumen::IState *L) {
    size_t l;
    size_t i;
    Lumen::Buffer b; // NOLINT
    const char *s = L->CheckString(1, &l);
    b.Init(L);
    for (i = 0; i < l; i++)
        b.AddChar(tolower(uchar(s[i])));
    b.PushResult();
    return 1;
}

int Lumen::IString::Context::Upper(Lumen::IState *L) {
    size_t l;
    size_t i;
    Lumen::Buffer b; // NOLINT
    const char *s = L->CheckString(1, &l);
    b.Init(L);
    for (i = 0; i < l; i++)
        b.AddChar(toupper(uchar(s[i])));
    b.PushResult();
    return 1;
}

int Lumen::IString::Context::Rep(Lumen::IState *L) {
    size_t l;
    Lumen::Buffer b; // NOLINT
    const char *s = L->CheckString(1, &l);
    int n = L->CheckInt(2);
    b.Init(L);
    while (n-- > 0)
        b.AddString(s, l);
    b.PushResult();
    return 1;
}

int Lumen::IString::Context::Byte(Lumen::IState *L) {
    size_t l;
    const char *s = L->CheckString(1, &l);
    ptrdiff_t posI = relStringPos(L->OptInteger(2, 1), l);
    ptrdiff_t posE = relStringPos(L->OptInteger(3, posI), l);
    int n, i;
    if (posI <= 0) posI = 1;
    if ((size_t) posE > l) posE = l; // NOLINT
    if (posI > posE) return 0;  /* empty interval; return no values */
    n = (int) (posE - posI + 1);
    if (posI + n <= posE)  /* overflow? */
        L->Error("string slice too long");
    L->CheckStack(n, "string slice too long");
    for (i = 0; i < n; i++)
        L->PushInteger(uchar(s[posI + i - 1]));
    return n;
}


int Lumen::IString::Context::Char(Lumen::IState *L) {
    int n = L->GetTop();  /* number of arguments */
    int i;
    Lumen::Buffer b; // NOLINT
    b.Init(L);
    for (i = 1; i <= n; i++) {
        int c = L->CheckInt(i);
        L->ArgCheck(uchar(c) == c, i, "invalid value");
        b.AddChar(uchar(c));
    }
    b.PushResult();
    return 1;
}

// MARK: Pack & Unpack

union FType {
    float f;
    double d;
    Lumen::Number n;
    char buff[5 * sizeof(Lumen::Number)];  /* enough for any float type */
};

/*
** information to pack/unpack stuff
*/
struct Header {
    Lumen::IState *L;
    int islittle;
    int maxalign;
};

/* dummy union to get native endianness */
static const union {
    int dummy;
    char little;  /* true iff machine is little endian */
} NativeEndian = {1};

/*
** options for pack/unpack
*/
typedef enum KOption {
    Kint,        /* signed integers */
    Kuint,    /* unsigned integers */
    Kfloat,    /* floating-point numbers */
    Kchar,    /* fixed-length strings */
    Kstring,    /* strings with prefixed length */
    Kzstr,    /* zero-terminated strings */
    Kpadding,    /* padding */
    Kpaddalign,    /* padding for alignment */
    Knop        /* no-op (configuration or spaces) */
} KOption;

constexpr size_t MAX_SIZE = INT_MAX;

/* dummy structure to get native alignment requirements */
struct ValueD {
    char c;
    union {
        double d;
        void *p;
        LUA_INTEGER i;
        LUA_NUMBER n;
    } u;
};

#define MAX_ALIGN_V    (offsetof(struct ValueD, u))

/*
** Read an integer numeral from string 'fmt' or return 'df' if
** there is no numeral
*/
static int digit(int c) { return '0' <= c && c <= '9'; }

static int getnum(const char **fmt, int df) {
    if (!digit(**fmt))  /* no number? */
        return df;  /* return default value */
    else {
        int a = 0;
        do {
            a = a * 10 + (*((*fmt)++) - '0');
        } while (digit(**fmt) && a <= ((int) MAX_SIZE - 9) / 10);
        return a;
    }
}

/*
** Read an integer numeral and raises an error if it is larger
** than the maximum size for integers.
*/
static int getnumlimit(Header *h, const char **fmt, int df) {
    int sz = getnum(fmt, df);
    if (sz > MAX_SIZE || sz <= 0)
        return h->L->Error("integral size (%d) out of limits [1,%d]",
                           sz, MAX_SIZE);
    return sz;
}

/*
** Read and classify next option. 'size' is filled with option's size.
*/
static KOption getoption(Header *h, const char **fmt, int *size) {
    int opt = *((*fmt)++);
    *size = 0;  /* default */
    switch (opt) {
        case 'b':
            *size = sizeof(char);
            return Kint;
        case 'B':
            *size = sizeof(char);
            return Kuint;
        case 'h':
            *size = sizeof(short);
            return Kint;
        case 'H':
            *size = sizeof(short);
            return Kuint;
        case 'l':
            *size = sizeof(long);
            return Kint;
        case 'L':
            *size = sizeof(long);
            return Kuint;
        case 'j':
            *size = sizeof(LUA_INTEGER);
            return Kint;
        case 'J':
            *size = sizeof(LUA_INTEGER);
            return Kuint;
        case 'T':
            *size = sizeof(size_t);
            return Kuint;
        case 'f':
            *size = sizeof(float);
            return Kfloat;
        case 'd':
            *size = sizeof(double);
            return Kfloat;
        case 'n':
            *size = sizeof(LUA_NUMBER);
            return Kfloat;
        case 'i':
            *size = getnumlimit(h, fmt, sizeof(int));
            return Kint;
        case 'I':
            *size = getnumlimit(h, fmt, sizeof(int));
            return Kuint;
        case 's':
            *size = getnumlimit(h, fmt, sizeof(size_t));
            return Kstring;
        case 'c':
            *size = getnum(fmt, -1);
            if (*size == -1)
                h->L->Error("missing size for format option 'c'");
            return Kchar;
        case 'z':
            return Kzstr;
        case 'x':
            *size = 1;
            return Kpadding;
        case 'X':
            return Kpaddalign;
        case ' ':
            break;
        case '<':
            h->islittle = 1;
            break;
        case '>':
            h->islittle = 0;
            break;
        case '=':
            h->islittle = NativeEndian.little;
            break;
        case '!':
            h->maxalign = getnumlimit(h, fmt, MAX_ALIGN_V);
            break;
        default:
            h->L->Error("invalid format option '%c'", opt);
    }
    return Knop;
}

/*
** Read, classify, and fill other details about the next option.
** 'psize' is filled with option's size, 'notoalign' with its
** alignment requirements.
** Local variable 'size' gets the size to be aligned. (Kpadal option
** always gets its full alignment, other options are limited by
** the maximum alignment ('maxalign'). Kchar option needs no alignment
** despite its size.
*/
static KOption getdetails(Header *h, size_t totalsize,
                          const char **fmt, int *psize, int *ntoalign) {
    KOption opt = getoption(h, fmt, psize);
    int align = *psize;  /* usually, alignment follows size */
    if (opt == Kpaddalign) {  /* 'X' gets alignment from following option */
        if (**fmt == '\0' || getoption(h, fmt, &align) == Kchar || align == 0)
            h->L->ArgError(1, "invalid next option for option 'X'");
    }
    if (align <= 1 || opt == Kchar)  /* need no alignment? */
        *ntoalign = 0;
    else {
        if (align > h->maxalign)  /* enforce maximum alignment */
            align = h->maxalign;
        if ((align & (align - 1)) != 0)  /* is 'align' not a power of 2? */
            h->L->ArgError(1, "format asks for alignment not power of 2");
        *ntoalign = (align - (int) (totalsize & (align - 1))) & (align - 1);
    }
    return opt;
}

#define LUAL_PACKPADBYTE        0x00
/* size of a Lumen::Integer */
#define SZINT    ((int) sizeof(Lumen::Integer))

#define MC    ((1 << CHAR_BIT) - 1)

/*
** Pack integer 'n' with 'size' bytes and 'islittle' endianness.
** The final 'if' handles the case when 'size' is larger than
** the size of a Lua integer, correcting the extra sign-extension
** bytes if necessary (by default they would be zeros).
*/
static void packInt(std::vector<char> *b, Lumen::UInteger n,
                    int isLittle, int size, int neg) {
    auto oldLength = b->size();
    b->resize(b->size() + size);
    char *buff = b->data() + oldLength;
    int i;
    buff[isLittle ? 0 : size - 1] = (char) (n & MC);  /* first byte */
    for (i = 1; i < size; i++) {
        n >>= CHAR_BIT;
        buff[isLittle ? i : size - 1 - i] = (char) (n & MC);
    }
    if (neg && size > SZINT) {  /* negative number need sign extension? */
        for (i = SZINT; i < size; i++)  /* correct extra bytes */
            buff[isLittle ? i : size - 1 - i] = (char) MC;
    }
}

/*
** Copy 'size' bytes from 'src' to 'dest', correcting endianness if
** given 'islittle' is different from native endianness.
*/
static void copyWithEndian(volatile char *dest, volatile const char *src,
                           int size, int isLittle) {
    if (isLittle == NativeEndian.little) {
        while (size-- != 0)
            *(dest++) = *(src++);
    } else {
        dest += size - 1;
        while (size-- != 0)
            *(dest--) = *(src++);
    }
}

int Lumen::IString::Context::Pack(Lumen::IState *L) {
    std::vector<char> b;
    Header h{L, NativeEndian.little, 1};
    const char *fmt = L->CheckString(1);  /* format string */
    int arg = 1;  /* current argument to pack */
    size_t totalSize = 0;  /* accumulate total size of result */
    L->PushNil();  /* mark to separate arguments from string buffer */
    b.clear();
    while (*fmt != '\0') {
        int size, nToAlign;
        KOption opt = getdetails(&h, totalSize, &fmt, &size, &nToAlign);
        totalSize += nToAlign + size;
        while (nToAlign-- > 0)
            b.push_back((char) LUAL_PACKPADBYTE);  /* fill alignment */
        arg++;
        switch (opt) {
            case Kint: {  /* signed integers */
                Lumen::Integer n = L->CheckInteger(arg);
                if (size < SZINT) {  /* need overflow check? */
                    Lumen::Integer lim = (Lumen::Integer) 1 << ((size * CHAR_BIT) - 1);
                    L->ArgCheck(-lim <= n && n < lim, arg, "integer overflow");
                }
                packInt(&b, (Lumen::UInteger) n, h.islittle, size, (n < 0));
                break;
            }
            case Kuint: {  /* unsigned integers */
                Lumen::Integer n = L->CheckInteger(arg);
                if (size < SZINT)  /* need overflow check? */
                    L->ArgCheck((Lumen::UInteger) n < ((Lumen::UInteger) 1 << (size * CHAR_BIT)),
                                arg, "unsigned overflow");
                packInt(&b, (Lumen::UInteger) n, h.islittle, size, 0);
                break;
            }
            case Kfloat: {  /* floating-point options */
                volatile FType u;
                auto oldLength = b.size();
                b.resize(oldLength + size);
                char *buff = b.data() + oldLength;
                Lumen::Number n = L->CheckNumber(arg);  /* get argument */
                if (size == sizeof(u.f)) u.f = (float) n;  /* copy it into 'u' */
                else if (size == sizeof(u.d)) u.d = (double) n;
                else u.n = n;
                /* move 'u' to final result, correcting endianness if needed */
                copyWithEndian(buff, u.buff, size, h.islittle);
                break;
            }
            case Kchar: {  /* fixed-size string */
                size_t len;
                const char *s = L->CheckString(arg, &len);
                L->ArgCheck(len <= (size_t) size, arg,
                            "string longer than given size");
                b.insert(b.end(), s, s + len);
                while (len++ < (size_t) size)  /* pad extra space */
                    b.push_back((char) LUAL_PACKPADBYTE);
                break;
            }
            case Kstring: {  /* strings with length count */
                size_t len;
                const char *s = L->CheckString(arg, &len);
                L->ArgCheck(size >= (int) sizeof(size_t) ||
                            len < ((size_t) 1 << (size * CHAR_BIT)),
                            arg, "string length does not fit in given size");
                packInt(&b, (Lumen::UInteger) len, h.islittle, size, 0);  /* pack length */
                b.insert(b.end(), s, s + len);
                totalSize += len;
                break;
            }
            case Kzstr: {  /* zero-terminated string */
                size_t len;
                const char *s = L->CheckString(arg, &len);
                L->ArgCheck(strlen(s) == len, arg, "string contains zeros");
                b.insert(b.end(), s, s + len);
                b.push_back((char) '\0');
                totalSize += len + 1;
                break;
            }
            case Kpadding:
                b.push_back((char) LUAL_PACKPADBYTE); /* FALLTHROUGH */
            case Kpaddalign:
            case Knop:
                arg--;  /* undo increment */
                break;
        }
    }
    L->PushString(b.data(), b.size());
    return 1;
}

int Lumen::IString::Context::PackSize(Lumen::IState *L) {
    Header h{L, NativeEndian.little, 1};
    const char *fmt = L->CheckString(1);  /* format string */
    size_t totalSize = 0;  /* accumulate total size of result */
    while (*fmt != '\0') {
        int size, nToAlign;
        KOption opt = getdetails(&h, totalSize, &fmt, &size, &nToAlign);
        size += nToAlign;  /* total space used by option */
        L->ArgCheck(totalSize <= INT_MAX - size, 1,
                    "format result too large");
        totalSize += size;
        switch (opt) {
            case Kstring:  /* strings with length count */
            case Kzstr:    /* zero-terminated string */
                L->ArgError(1, "variable-length format");
                /* call never return, but to avoid warnings: *//* FALLTHROUGH */
            default:
                break;
        }
    }
    L->PushInteger((Lumen::Integer) totalSize);
    return 1;
}

/*
** Unpack an integer with 'size' bytes and 'isLittle' endianness.
** If size is smaller than the size of a Lua integer and integer
** is signed, must do sign extension (propagating the sign to the
** higher bits); if size is larger than the size of a Lua integer,
** it must check the unread bytes to see whether they do not cause an
** overflow.
*/
static Lumen::Integer unPackInt(Lumen::IState *L, const char *str,
                                int isLittle, int size, int isSigned) {
    Lumen::UInteger res = 0;
    int i;
    int limit = (size <= SZINT) ? size : SZINT;
    for (i = limit - 1; i >= 0; i--) {
        int index = isLittle ? i : size - 1 - i;
        res |= ((Lumen::UInteger) (unsigned char) str[index]) << (i * CHAR_BIT);
    }
    if (size < SZINT) {  /* real size smaller than lua_Integer? */
        if (isSigned) {  /* needs sign extension? */
            Lumen::UInteger mask = (Lumen::UInteger) 1 << (size * CHAR_BIT - 1);
            res = ((res ^ mask) - mask);  /* do sign extension */
        }
    } else if (size > SZINT) {  /* must check unread bytes */
        int mask = (!isSigned || (Lumen::Integer) res >= 0) ? 0 : MC;
        for (i = limit; i < size; i++) {
            if ((unsigned char) str[isLittle ? i : size - 1 - i] != mask)
                L->Error("%d-byte integer does not fit into Lua Integer", size);
        }
    }
    return (Lumen::Integer) res;
}

/* translate a relative string position: negative means back from end */
static Lumen::Integer posRel(Lumen::Integer pos, size_t len) {
    if (pos >= 0) return pos;
    else if (0u - (size_t) pos > len) return 0;
    else return (Lumen::Integer) len + pos + 1;
}

int Lumen::IString::Context::Unpack(Lumen::IState *L) {
    Header h{L, NativeEndian.little, 1};
    const char *fmt = L->CheckString(1);
    size_t ld;
    const char *data = L->CheckString(2, &ld);
    size_t pos = (size_t) posRel(L->OptInteger(3, 1), ld) - 1;
    int n = 0;  /* number of results */
    L->ArgCheck(pos <= ld, 3, "initial position out of string");
    while (*fmt != '\0') {
        int size, nToAlign;
        KOption opt = getdetails(&h, pos, &fmt, &size, &nToAlign);
        if ((size_t) nToAlign + size > ~pos || pos + nToAlign + size > ld)
            L->ArgError(2, "data string too short");
        pos += nToAlign;  /* skip alignment */
        /* stack space for item + next position */
        L->CheckStack(2, "too many results");
        n++;
        switch (opt) {
            case Kint:
            case Kuint: {
                Lumen::Integer res = unPackInt(L, data + pos, h.islittle, size,
                                               (opt == Kint));
                L->PushInteger(res);
                break;
            }
            case Kfloat: {
                volatile FType u;
                Lumen::Number num;
                copyWithEndian(u.buff, data + pos, size, h.islittle);
                if (size == sizeof(u.f)) num = (Lumen::Number) u.f;
                else if (size == sizeof(u.d)) num = (Lumen::Number) u.d;
                else num = u.n;
                L->PushNumber(num);
                break;
            }
            case Kchar: {
                L->PushString(data + pos, size);
                break;
            }
            case Kstring: {
                size_t len = (size_t) unPackInt(L, data + pos, h.islittle, size, 0);
                L->ArgCheck(pos + len + size <= ld, 2, "data string too short");
                L->PushString(data + pos + size, len);
                pos += len;  /* skip string */
                break;
            }
            case Kzstr: {
                size_t len = (int) strlen(data + pos);
                L->PushString(data + pos, len);
                pos += len + 1;  /* skip string plus final '\0' */
                break;
            }
            case Kpaddalign:
            case Kpadding:
            case Knop:
                n--;  /* undo increment */
                break;
        }
        pos += size;
    }
    L->PushInteger((Lumen::Integer) pos + 1);  /* next position */
    return n + 1;
}

static int dumpWriter(Lumen::IState *, const void *b, size_t size, void *B) {
    auto buffer = reinterpret_cast<Lumen::Buffer *>(B);
    buffer->AddString((const char *) b, size);
    return 0;
}

int Lumen::IString::Context::Dump(Lumen::IState *L) {
    Lumen::Buffer b; // NOLINT
    L->CheckType(1, Lumen::TypeFunction);
    L->SetTop(1);
    b.Init(L);
    if (L->Dump(dumpWriter, &b) != 0)
        L->Error("unable to dump given function");
    b.PushResult();
    return 1;
}


/*
** {======================================================
** MARK: PATTERN MATCHING
** =======================================================
*/


#define CAP_UNFINISHED    (-1)
#define CAP_POSITION    (-2)

struct MatchState {
    const char *SrcInit;  /* init of source string */
    const char *SrcEnd;  /* end (`\0') of source string */
    Lumen::IState *L;
    int Level;  /* total number of captures (finished or unfinished) */
    struct {
        const char *Init;
        ptrdiff_t Length;
    } Capture[LUA_MAX_CAPTURES];
};


#define L_ESC        '%'
#define SPECIALS    "^$*+?.([%-"


static int checkCapture(MatchState *ms, int l) {
    l -= '1';
    if (l < 0 || l >= ms->Level || ms->Capture[l].Length == CAP_UNFINISHED)
        return ms->L->Error("invalid capture index");
    return l;
}


static inline int captureToClose(MatchState *ms) {
    int level = ms->Level;
    for (level--; level >= 0; level--)
        if (ms->Capture[level].Length == CAP_UNFINISHED) return level;
    return ms->L->Error("invalid pattern capture");
}


static const char *classEnd(MatchState *ms, const char *p) {
    switch (*p++) {
        case L_ESC: {
            if (*p == '\0')
                ms->L->Error("malformed pattern (ends with " LUA_QL("%%") ")");
            return p + 1;
        }
        case '[': {
            if (*p == '^') p++;
            do {  /* look for a `]` */
                if (*p == '\0')
                    ms->L->Error("malformed pattern (missing " LUA_QL("]") ")");
                if (*(p++) == L_ESC && *p != '\0')
                    p++;  /* skip escapes (e.g. `%]`) */
            } while (*p != ']');
            return p + 1;
        }
        default: {
            return p;
        }
    }
}


static int matchClass(int c, int cl) {
    int res;
    switch (tolower(cl)) {
        case 'a' :
            res = isalpha(c);
            break;
        case 'c' :
            res = iscntrl(c);
            break;
        case 'd' :
            res = isdigit(c);
            break;
        case 'l' :
            res = islower(c);
            break;
        case 'p' :
            res = ispunct(c);
            break;
        case 's' :
            res = isspace(c);
            break;
        case 'u' :
            res = isupper(c);
            break;
        case 'w' :
            res = isalnum(c);
            break;
        case 'x' :
            res = isxdigit(c);
            break;
        case 'z' :
            res = (c == 0);
            break;
        default:
            return (cl == c);
    }
    return (islower(cl) ? res : !res); // NOLINT
}


static int matchBracketClass(int c, const char *p, const char *ec) {
    int sig = 1;
    if (*(p + 1) == '^') {
        sig = 0;
        p++;  /* skip the `^' */
    }
    while (++p < ec) {
        if (*p == L_ESC) {
            p++;
            if (matchClass(c, uchar(*p)))
                return sig;
        } else if ((*(p + 1) == '-') && (p + 2 < ec)) {
            p += 2;
            if (uchar(*(p - 2)) <= c && c <= uchar(*p))
                return sig;
        } else if (uchar(*p) == c) return sig;
    }
    return !sig;
}


static int singleMatch(int c, const char *p, const char *ep) {
    switch (*p) {
        case '.':
            return 1;  /* matches any char */
        case L_ESC:
            return matchClass(c, uchar(*(p + 1)));
        case '[':
            return matchBracketClass(c, p, ep - 1);
        default:
            return (uchar(*p) == c);
    }
}


static const char *match(MatchState *ms, const char *s, const char *p);


static const char *matchBalance(MatchState *ms, const char *s,
                                const char *p) {
    if (*p == 0 || *(p + 1) == 0)
        ms->L->Error("unbalanced pattern");
    if (*s != *p) return nullptr;
    else {
        int b = *p; // NOLINT
        int e = *(p + 1); // NOLINT
        int cont = 1;
        while (++s < ms->SrcEnd) {
            if (*s == e) {
                if (--cont == 0) return s + 1;
            } else if (*s == b) cont++;
        }
    }
    return nullptr;  /* string ends out of balance */
}


static const char *maxExpand(MatchState *ms, const char *s,
                             const char *p, const char *ep) {
    ptrdiff_t i = 0;  /* counts maximum expand for item */
    while ((s + i) < ms->SrcEnd && singleMatch(uchar(*(s + i)), p, ep))
        i++;
    /* keeps trying to match with the maximum repetitions */
    while (i >= 0) {
        const char *res = match(ms, (s + i), ep + 1);
        if (res) return res;
        i--;  /* else didn't match; reduce 1 repetition to try again */
    }
    return nullptr;
}


static const char *minExpand(MatchState *ms, const char *s,
                             const char *p, const char *ep) {
    for (;;) {
        const char *res = match(ms, s, ep + 1);
        if (res != nullptr)
            return res;
        else if (s < ms->SrcEnd && singleMatch(uchar(*s), p, ep))
            s++;  /* try with one more repetition */
        else return nullptr;
    }
}


static const char *startCapture(MatchState *ms, const char *s,
                                const char *p, int what) {
    const char *res;
    int level = ms->Level;
    if (level >= LUA_MAX_CAPTURES) ms->L->Error("too many captures");
    ms->Capture[level].Init = s;
    ms->Capture[level].Length = what;
    ms->Level = level + 1;
    if ((res = match(ms, s, p)) == nullptr)  /* match failed? */
        ms->Level--;  /* undo capture */
    return res;
}


static const char *endCapture(MatchState *ms, const char *s,
                              const char *p) {
    int l = captureToClose(ms);
    const char *res;
    ms->Capture[l].Length = s - ms->Capture[l].Init;  /* close capture */
    if ((res = match(ms, s, p)) == nullptr)  /* match failed? */
        ms->Capture[l].Length = CAP_UNFINISHED;  /* undo capture */
    return res;
}


static const char *matchCapture(MatchState *ms, const char *s, int l) {
    size_t len;
    l = checkCapture(ms, l);
    len = ms->Capture[l].Length;
    if ((size_t) (ms->SrcEnd - s) >= len &&
        memcmp(ms->Capture[l].Init, s, len) == 0)
        return s + len;
    else return nullptr;
}


static const char *match(MatchState *ms, const char *s, const char *p) {
    init: /* using goto to optimize tail recursion */
    switch (*p) {
        case '(': {  /* start capture */
            if (*(p + 1) == ')')  /* position capture? */
                return startCapture(ms, s, p + 2, CAP_POSITION);
            else
                return startCapture(ms, s, p + 1, CAP_UNFINISHED);
        }
        case ')': {  /* end capture */
            return endCapture(ms, s, p + 1);
        }
        case L_ESC: {
            switch (*(p + 1)) {
                case 'b': {  /* balanced string? */
                    s = matchBalance(ms, s, p + 2);
                    if (s == nullptr) return nullptr;
                    p += 4;
                    goto init;  /* else return match(ms, s, p+4); */
                }
                case 'f': {  /* frontier? */
                    const char *ep;
                    char previous;
                    p += 2;
                    if (*p != '[')
                        ms->L->Error("missing " LUA_QL("[") " after "
                                     LUA_QL("%%f") " in pattern");
                    ep = classEnd(ms, p);  /* points to what is next */
                    previous = (s == ms->SrcInit) ? '\0' : *(s - 1);
                    if (matchBracketClass(uchar(previous), p, ep - 1) ||
                        !matchBracketClass(uchar(*s), p, ep - 1))
                        return nullptr;
                    p = ep;
                    goto init;  /* else return match(ms, s, ep); */
                }
                default: {
                    if (isdigit(uchar(*(p + 1)))) {  /* capture results (%0-%9)? */
                        s = matchCapture(ms, s, uchar(*(p + 1)));
                        if (s == nullptr) return nullptr;
                        p += 2;
                        goto init;  /* else return match(ms, s, p+2) */
                    }
                    goto DEFAULT_CASE;  /* case default */
                }
            }
        }
        case '\0': {  /* end of pattern */
            return s;  /* match succeeded */
        }
        case '$': {
            if (*(p + 1) == '\0')  /* is the `$` the last char in pattern? */
                return (s == ms->SrcEnd) ? s : nullptr;  /* check end of string */
            else goto DEFAULT_CASE;
        }
        default:
        DEFAULT_CASE:
        {  /* it is a pattern item */
            const char *ep = classEnd(ms, p);  /* points to what is next */
            int m = s < ms->SrcEnd && singleMatch(uchar(*s), p, ep);
            switch (*ep) {
                case '?': {  /* optional */
                    const char *res;
                    if (m && ((res = match(ms, s + 1, ep + 1)) != nullptr))
                        return res;
                    p = ep + 1;
                    goto init;  /* else return match(ms, s, ep+1); */
                }
                case '*': {  /* 0 or more repetitions */
                    return maxExpand(ms, s, p, ep);
                }
                case '+': {  /* 1 or more repetitions */
                    return (m ? maxExpand(ms, s + 1, p, ep) : nullptr);
                }
                case '-': {  /* 0 or more repetitions (minimum) */
                    return minExpand(ms, s, p, ep);
                }
                default: {
                    if (!m) return nullptr;
                    s++;
                    p = ep;
                    goto init;  /* else return match(ms, s+1, ep); */
                }
            }
        }
    }
}


static void pushOneCapture(MatchState *ms, int i, const char *s,
                           const char *e) {
    if (i >= ms->Level) {
        if (i == 0)  /* ms->level == 0, too */
            ms->L->PushString(s, e - s);  /* add whole match */
        else
            ms->L->Error("invalid capture index");
    } else {
        ptrdiff_t l = ms->Capture[i].Length;
        if (l == CAP_UNFINISHED) ms->L->Error("unfinished capture");
        if (l == CAP_POSITION)
            ms->L->PushInteger(ms->Capture[i].Init - ms->SrcInit + 1);
        else
            ms->L->PushString(ms->Capture[i].Init, l);
    }
}


static int pushCaptures(MatchState *ms, const char *s, const char *e) {
    int i;
    int nLevels = (ms->Level == 0 && s) ? 1 : ms->Level;
    ms->L->CheckStack(nLevels, "too many captures");
    for (i = 0; i < nLevels; i++)
        pushOneCapture(ms, i, s, e);
    return nLevels;  /* number of strings pushed */
}


static int strFindAux(Lumen::IState *L, int find) {
    size_t l1, l2;
    const char *s = L->CheckString(1, &l1);
    const char *p = L->CheckString(2, &l2);
    ptrdiff_t init = relStringPos(L->OptInteger(3, 1), l1) - 1;
    constexpr std::string_view specials(SPECIALS);
    std::string_view pView(p);
    if (init < 0) init = 0;
    else if ((size_t) (init) > l1) init = (ptrdiff_t) l1;
    if (find && (L->ToBoolean(4) ||  /* explicit request? */
                 pView.find_first_of(specials) == std::string_view::npos)) {  /* or no special characters? */
        /* do a plain search */
        const char *s2 = Lumen::Memory::Find(s + init, l1 - init, p, l2);
        if (s2) {
            L->PushInteger(s2 - s + 1);
            L->PushInteger(static_cast<Lumen::Integer>(s2 - s + l2));
            return 2;
        }
    } else {
        int anchor = (*p == '^') ? (p++, 1) : 0;
        const char *s1 = s + init;
        MatchState ms; // NOLINT
        ms.L = L;
        ms.SrcInit = s;
        ms.SrcEnd = s + l1;
        do {
            const char *res;
            ms.Level = 0;
            if ((res = match(&ms, s1, p)) != nullptr) {
                if (find) {
                    L->PushInteger(s1 - s + 1);  /* start */
                    L->PushInteger(res - s);   /* end */
                    return pushCaptures(&ms, nullptr, nullptr) + 2;
                } else
                    return pushCaptures(&ms, s1, res);
            }
        } while (s1++ < ms.SrcEnd && !anchor);
    }
    L->PushNil();  /* not found */
    return 1;
}


int Lumen::IString::Context::Find(Lumen::IState *L) {
    return strFindAux(L, 1);
}

int Lumen::IString::Context::Match(Lumen::IState *L) {
    return strFindAux(L, 0);
}

static int GMatchAux(Lumen::IState *L) {
    MatchState ms; // NOLINT
    size_t ls;
    const char *s = L->ToString(Lumen::UpValueIndex(1), &ls);
    const char *p = L->ToString(Lumen::UpValueIndex(2));
    const char *src;
    ms.L = L;
    ms.SrcInit = s;
    ms.SrcEnd = s + ls;
    for (src = s + (size_t) L->ToInteger(Lumen::UpValueIndex(3));
         src <= ms.SrcEnd;
         src++) {
        const char *e;
        ms.Level = 0;
        if ((e = match(&ms, src, p)) != nullptr) {
            Lumen::Integer newStart = e - s;
            if (e == src) newStart++;  /* empty match? go at least one position */
            L->PushInteger(newStart);
            L->Replace(Lumen::UpValueIndex(3));
            return pushCaptures(&ms, src, e);
        }
    }
    return 0;  /* not found */
}


int Lumen::IString::Context::GMatch(Lumen::IState *L) {
    L->CheckString(1);
    L->CheckString(2);
    L->SetTop(2);
    L->PushInteger(0);
    L->PushDelegate(GMatchAux, 3);
    return 1;
}


int Lumen::IString::Context::GFindNodeF(Lumen::IState *L) {
    return L->Error(LUA_QL("string.gfind") " was renamed to "
                    LUA_QL("string.gmatch"));
}


static void addCString(MatchState *ms, Lumen::Buffer *b, const char *s,
                       const char *e) {
    size_t l, i;
    const char *news = ms->L->ToString(3, &l);
    for (i = 0; i < l; i++) {
        if (news[i] != L_ESC)
            b->AddChar(news[i]);
        else {
            i++;  /* skip ESC */
            if (!isdigit(uchar(news[i])))
                b->AddChar(news[i]);
            else if (news[i] == '0')
                b->AddString(s, e - s);
            else {
                pushOneCapture(ms, news[i] - '1', s, e);
                b->AddValue();
            }
        }
    }
}


static void addValue(MatchState *ms, Lumen::Buffer *b, const char *s,
                     const char *e) {
    Lumen::IState *L = ms->L;
    switch (L->TypeId(3)) {
        case Lumen::TypeNumber:
        case Lumen::TypeString: {
            addCString(ms, b, s, e);
            return;
        }
        case Lumen::TypeFunction: {
            int n;
            L->PushValue(3);
            n = pushCaptures(ms, s, e);
            L->Call(n, 1);
            break;
        }
        case Lumen::TypeTable: {
            pushOneCapture(ms, 0, s, e);
            L->GetTable(3);
            break;
        }
    }
    if (!L->ToBoolean(-1)) {  /* nil or false? */
        L->Pop(1);
        L->PushString(s, e - s);  /* keep original text */
    } else if (!L->IsString(-1))
        L->Error("invalid replacement value (a %s)", L->TypeName(-1));
    b->AddValue(); /* add result to accumulator */
}


int Lumen::IString::Context::GSub(Lumen::IState *L) {
    size_t srcl;
    const char *src = L->CheckString(1, &srcl);
    const char *p = L->CheckString(2);
    auto tr = L->TypeId(3);
    int max_s = L->OptInt(4, (int) srcl + 1);
    int anchor = (*p == '^') ? (p++, 1) : 0;
    int n = 0;
    MatchState ms; // NOLINT
    Lumen::Buffer b; // NOLINT
    L->ArgCheck(tr == Lumen::TypeNumber || tr == Lumen::TypeString ||
                tr == Lumen::TypeFunction || tr == Lumen::TypeTable, 3,
                "string/function/table expected");
    b.Init(L);
    ms.L = L;
    ms.SrcInit = src;
    ms.SrcEnd = src + srcl;
    while (n < max_s) {
        const char *e;
        ms.Level = 0;
        e = match(&ms, src, p);
        if (e) {
            n++;
            addValue(&ms, &b, src, e);
        }
        if (e && e > src) /* non empty match? */
            src = e;  /* skip it */
        else if (src < ms.SrcEnd)
            b.AddChar(*src++);
        else break;
        if (anchor) break;
    }
    b.AddString(src, ms.SrcEnd - src);
    b.PushResult();
    L->PushInteger(n);  /* number of substitutions */
    return 2;
}

/* }====================================================== */


/* maximum size of each formatted item (> len(format('%99.99f', -1e308))) */
#define MAX_ITEM    512
/* valid flags in a format specification */
#define FLAGS    "-+ #0"
/*
** maximum size of each format specification (such as '%-099.99d')
** (+10 accounts for %99.99x plus margin of error)
*/
#define MAX_FORMAT    (sizeof(FLAGS) + sizeof(LUA_INT_FMT_LEN) + 10)


static void addQuoted(Lumen::IState *L, Lumen::Buffer *b, int arg) {
    size_t l;
    const char *s = L->CheckString(arg, &l);
    b->AddChar('"');
    while (l--) {
        switch (*s) {
            case '"':
            case '\\':
            case '\n': {
                b->AddChar('\\');
                b->AddChar(*s);
                break;
            }
            case '\r': {
                b->AddString("\\r", 2);
                break;
            }
            case '\0': {
                b->AddString("\\000", 4);
                break;
            }
            default: {
                b->AddChar(*s);
                break;
            }
        }
        s++;
    }
    b->AddChar('"');
}

static const char *scanFormat(Lumen::IState *L, const char *strFormat, char *form) {
    const char *p = strFormat;
    while (*p != '\0' && strchr(FLAGS, *p) != nullptr) p++;  /* skip flags */
    if ((size_t) (p - strFormat) >= sizeof(FLAGS))
        L->Error("invalid format (repeated flags)");
    if (isdigit(uchar(*p))) p++;  /* skip width */
    if (isdigit(uchar(*p))) p++;  /* (2 digits at most) */
    if (*p == '.') {
        p++;
        if (isdigit(uchar(*p))) p++;  /* skip precision */
        if (isdigit(uchar(*p))) p++;  /* (2 digits at most) */
    }
    if (isdigit(uchar(*p)))
        L->Error("invalid format (width or precision too long)");
    *(form++) = '%';
    strncpy(form, strFormat, p - strFormat + 1);
    form += p - strFormat + 1;
    *form = '\0';
    return p;
}


static void addIntLength(char *form) {
    size_t l = std::string_view(form).length();
    char spec = form[l - 1];
    strcpy(form + l - 1, LUA_INT_FMT_LEN);
    form[l + sizeof(LUA_INT_FMT_LEN) - 2] = spec;
    form[l + sizeof(LUA_INT_FMT_LEN) - 1] = '\0';
}


int Lumen::IString::Context::Format(Lumen::IState *L) {
    int top = L->GetTop();
    int arg = 1;
    size_t sfl;
    const char *strfrmt = L->CheckString(arg, &sfl);
    const char *strfrmt_end = strfrmt + sfl;
    Lumen::Buffer b; // NOLINT
    b.Init(L);
    while (strfrmt < strfrmt_end) {
        if (*strfrmt != L_ESC)
            b.AddChar(*strfrmt++);
        else if (*++strfrmt == L_ESC)
            b.AddChar(*strfrmt++);  /* %% */
        else { /* format item */
            char form[MAX_FORMAT];  /* to store the format (`%...') */
            char buff[MAX_ITEM];  /* to store the formatted item */
            if (++arg > top)
                L->ArgError(arg, "no value");
            strfrmt = scanFormat(L, strfrmt, form);
            switch (*strfrmt++) {
                case 'c': {
                    sprintf(buff, form, (int) L->CheckNumber(arg));
                    break;
                }
                case 'd':
                case 'i': {
                    addIntLength(form);
                    sprintf(buff, form, (LUA_INT_FMT) L->CheckNumber(arg));
                    break;
                }
                case 'o':
                case 'u':
                case 'x':
                case 'X': {
                    addIntLength(form);
                    sprintf(buff, form, (unsigned LUA_INT_FMT) L->CheckNumber(arg));
                    break;
                }
                case 'e':
                case 'E':
                case 'f':
                case 'g':
                case 'G': {
                    sprintf(buff, form, (double) L->CheckNumber(arg));
                    break;
                }
                case 'q': {
                    addQuoted(L, &b, arg);
                    continue;  /* skip the 'addsize' at the end */
                }
                case 's': {
                    size_t l;
                    const char *s = L->CheckString(arg, &l);
                    if (!strchr(form, '.') && l >= 100) {
                        /* no precision and string is too long to be formatted;
                           keep original string */
                        L->PushValue(arg);
                        b.AddValue();
                        continue;  /* skip the `addsize' at the end */
                    } else {
                        sprintf(buff, form, s);
                        break;
                    }
                }
                default: {  /* also treat cases `pnLlh' */
                    return L->Error("invalid option " LUA_QL("%%%c") " to "
                                    LUA_QL("format"), *(strfrmt - 1));
                }
            }
            b.AddString(buff, strlen(buff));
        }
    }
    b.PushResult();
    return 1;
}


static const Lumen::Interface strLib[] = {
    {"byte",     Lumen::IString::Context::Byte},
    {"char",     Lumen::IString::Context::Char},
    {"dump",     Lumen::IString::Context::Dump},
    {"find",     Lumen::IString::Context::Find},
    {"format",   Lumen::IString::Context::Format},
    {"gfind",    Lumen::IString::Context::GFindNodeF},
    {"gmatch",   Lumen::IString::Context::GMatch},
    {"gsub",     Lumen::IString::Context::GSub},
    {"len",      Lumen::IString::Context::Length},
    {"lower",    Lumen::IString::Context::Lower},
    {"match",    Lumen::IString::Context::Match},
    {"rep",      Lumen::IString::Context::Rep},
    {"reverse",  Lumen::IString::Context::Reverse},
    {"sub",      Lumen::IString::Context::Sub},
    {"upper",    Lumen::IString::Context::Upper},
    {"pack",     Lumen::IString::Context::Pack},
    {"packsize", Lumen::IString::Context::PackSize},
    {"unpack",   Lumen::IString::Context::Unpack},
    {nullptr,    nullptr}
};


static void createMetatable(Lumen::IState *L) {
    L->CreateTable(0, 1);  /* create metatable for strings */
    L->PushLiteral("");  /* dummy string */
    L->PushValue(-2);
    L->SetMetatable(-2);  /* set string metatable */
    L->Pop(1);  /* pop dummy string */
    L->PushValue(-2);  /* string library... */
    L->SetField(-2, "__index");  /* ...is the __index metamethod */
    L->Pop(1);  /* pop metatable */
}

const char *Lumen::IString::CString() {
    return ToLumenString(this)->CString();
}

Lumen::UInteger Lumen::IString::Length() {
    return ToLumenString(this)->Length;
}

Lumen::IString *Lumen::IString::Get(Lumen::IState *l, int idx) {
    auto L = reinterpret_cast<Lumen::State *>(l);
    auto o = L->ToObject(idx);
    if (!o->IsString()) return nullptr;
    return ToLumenIString(o->GetString());
}

Lumen::IString *Lumen::IString::New(Lumen::IState *L, const char *cStr) {
    L->PushString(cStr);
    return Get(L, -1);
}

Lumen::IString *Lumen::IString::New(Lumen::IState *L, const char *cStr, Lumen::UInteger length) {
    L->PushString(cStr, length);
    return Get(L, -1);
}

#define LUA_STRLIBNAME "string"

template<>
LPP_API int Lumen::Open<Lumen::IString>(Lumen::IState *L) {
    L->Register(LUA_STRLIBNAME, strLib);
#if defined(LUA_COMPAT_GFIND)
    L->GetField(-1, "gmatch");
    L->SetField(-2, "gfind");
#endif
    createMetatable(L);
    return 1;
}

/*
** Open string library
*/
LUALIB_API int luaopen_string(Lumen::IState *L) {
    L->Register(LUA_STRLIBNAME, strLib);
#if defined(LUA_COMPAT_GFIND)
    L->GetField(-1, "gmatch");
    L->SetField(-2, "gfind");
#endif
    createMetatable(L);
    return 1;
}

