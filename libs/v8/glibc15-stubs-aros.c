/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: glibc compatibility bridge for GCC 15 libstdc++ statics linked
          into v8.library. Companion to external/electron/v8's
          glibc-stubs-aros.c, covering the newer runtime's surface:
          fortify _chk wrappers, per-locale _l variants (C locale only,
          matching v8_enable_i18n_support=false), gettext/iconv no-ops,
          and misc process introspection.
*/

/* V8_GLIBC15_STUBS
 *          locale _l family only ever sees the default locale.
 */

#include <aros/symbolsets.h>
#include <proto/exec.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

typedef void *locale_t_shim;
typedef unsigned long wint_shim;

/* ---- locale handles: one real-enough C locale ----
   gcc-15 libstdc++ (gnu locale model) dereferences the handle as a
   glibc __locale_struct: 13 category pointers then __ctype_b,
   __ctype_tolower, __ctype_toupper (boot 64: ctype<char> ctor read
   handle+0x78 -> CR2=0x79 with the old (void*)1 stub). Provide the
   struct with genuine C-locale tables, glibc bit layout. */
#define ISb_upper  0x0100
#define ISb_lower  0x0200
#define ISb_alpha  0x0400
#define ISb_digit  0x0800
#define ISb_xdigit 0x1000
#define ISb_space  0x2000
#define ISb_print  0x4000
#define ISb_graph  0x8000
#define ISb_blank  0x0001
#define ISb_cntrl  0x0002
#define ISb_punct  0x0004
#define ISb_alnum  0x0008

static unsigned short shim_ctype_b[384];
static int shim_ctype_lower[384];
static int shim_ctype_upper[384];
static struct {
    void *cats[13];
    const unsigned short *ctype_b;
    const int *ctype_tolower;
    const int *ctype_toupper;
    const char *names[13];
} shim_c_locale;

static void shim_locale_init(void)
{
    if (shim_c_locale.ctype_b) return;
    for (int i = 0; i < 384; i++) {
        int c = i - 128;          /* glibc tables span [-128, 255] */
        unsigned short f = 0;
        int lo = c, up = c;
        if (c >= 0 && c < 256) {
            unsigned ch = (unsigned)c;
            if (ch < 32 || ch == 127) f |= ISb_cntrl;
            if (ch == ' ' || (ch >= 9 && ch <= 13)) f |= ISb_space;
            if (ch == ' ' || ch == '\t') f |= ISb_blank;
            if (ch >= '0' && ch <= '9') f |= ISb_digit | ISb_alnum | ISb_xdigit;
            if (ch >= 'a' && ch <= 'z') { f |= ISb_lower | ISb_alpha | ISb_alnum; up = ch - 32; }
            if (ch >= 'A' && ch <= 'Z') { f |= ISb_upper | ISb_alpha | ISb_alnum; lo = ch + 32; }
            if ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) f |= ISb_xdigit;
            if (ch >= 33 && ch < 127) f |= ISb_graph;
            if (ch >= 32 && ch < 127) f |= ISb_print;
            if ((f & ISb_graph) && !(f & ISb_alnum)) f |= ISb_punct;
        }
        shim_ctype_b[i] = f;
        shim_ctype_lower[i] = lo;
        shim_ctype_upper[i] = up;
    }
    /* glibc convention: pointers aim at index 128 (c==0) */
    shim_c_locale.ctype_b = &shim_ctype_b[128];
    shim_c_locale.ctype_tolower = &shim_ctype_lower[128];
    shim_c_locale.ctype_toupper = &shim_ctype_upper[128];
    for (int i = 0; i < 13; i++) shim_c_locale.names[i] = "C";
}

/* Strong override of libstdc++'s locale::facet::_S_get_c_locale():
   the archive's implementation yields a bogus handle (literal 1) under
   the AROS loader (boots 64/65, CR2=0x79); the member's copy is
   weakened in our libstdc++-aros.a so this definition wins. */
void *__aros_get_c_locale(void) __asm__("_ZNSt6locale5facet15_S_get_c_localeEv");
void *__aros_get_c_locale(void)
{
    shim_locale_init();
    return &shim_c_locale;
}

locale_t_shim __newlocale(int mask, const char *name, locale_t_shim base)
{ (void)mask; (void)name; (void)base; shim_locale_init(); return (locale_t_shim)&shim_c_locale; }
locale_t_shim __duplocale(locale_t_shim l) { (void)l; shim_locale_init(); return (locale_t_shim)&shim_c_locale; }
void __freelocale(locale_t_shim l) { (void)l; }
locale_t_shim __uselocale(locale_t_shim l) { (void)l; shim_locale_init(); return (locale_t_shim)&shim_c_locale; }

/* ---- ctype/wctype _l variants: ASCII/C locale ---- */
unsigned long __wctype_l(const char *name, locale_t_shim l)
{
    (void)l;
    /* encode class as index+1 in the handle */
    static const char *classes[] = { "alnum","alpha","blank","cntrl","digit",
        "graph","lower","print","punct","space","upper","xdigit" };
    for (unsigned i = 0; i < sizeof(classes)/sizeof(classes[0]); i++)
        if (!strcmp(name, classes[i])) return i + 1;
    return 0;
}

static int shim_isw(unsigned long wc, unsigned long cls)
{
    if (wc > 127) return 0;
    int c = (int)wc;
    switch (cls) {
    case 1:  return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z');
    case 2:  return (c>='a'&&c<='z')||(c>='A'&&c<='Z');
    case 3:  return c==' '||c=='\t';
    case 4:  return c<32||c==127;
    case 5:  return c>='0'&&c<='9';
    case 6:  return c>32&&c<127;
    case 7:  return c>='a'&&c<='z';
    case 8:  return c>=32&&c<127;
    case 9:  return (c>32&&c<127)&&!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z'));
    case 10: return c==' '||(c>=9&&c<=13);
    case 11: return c>='A'&&c<='Z';
    case 12: return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
    }
    return 0;
}
int __iswctype_l(wint_shim wc, unsigned long cls, locale_t_shim l)
{ (void)l; return shim_isw(wc, cls); }
wint_shim __towlower_l(wint_shim wc, locale_t_shim l)
{ (void)l; return (wc < 128 && wc >= 'A' && wc <= 'Z') ? wc + 32 : wc; }
wint_shim __towupper_l(wint_shim wc, locale_t_shim l)
{ (void)l; return (wc < 128 && wc >= 'a' && wc <= 'z') ? wc - 32 : wc; }

/* ---- string/locale _l variants: C locale semantics ---- */
int __strcoll_l(const char *a, const char *b, locale_t_shim l)
{ (void)l; return strcmp(a, b); }
size_t __strxfrm_l(char *dst, const char *src, size_t n, locale_t_shim l)
{
    (void)l;
    size_t len = strlen(src);
    if (n) { strncpy(dst, src, n - 1); dst[n - 1] = 0; }
    return len;
}
double __strtod_l(const char *s, char **end, locale_t_shim l)
{ (void)l; return strtod(s, end); }
float __strtof_l(const char *s, char **end, locale_t_shim l)
{ (void)l; return strtof(s, end); }
size_t __strftime_l(char *s, size_t max, const char *fmt,
                    const struct tm *tm, locale_t_shim l)
{ (void)l; return strftime(s, max, fmt, tm); }
char *__nl_langinfo_l(int item, locale_t_shim l)
{ (void)l; (void)item; return (char *)"ASCII"; }

/* ---- wide-string helpers (wchar_t assumed 4 bytes, values copied) ---- */
typedef int wchar_shim;
int __wcscoll_l(const wchar_shim *a, const wchar_shim *b, locale_t_shim l)
{
    (void)l;
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}
size_t __wcsxfrm_l(wchar_shim *dst, const wchar_shim *src, size_t n, locale_t_shim l)
{
    (void)l;
    size_t len = 0; while (src[len]) len++;
    if (n) {
        size_t i;
        for (i = 0; i < n - 1 && src[i]; i++) dst[i] = src[i];
        dst[i] = 0;
    }
    return len;
}
size_t __wcsftime_l(wchar_shim *s, size_t max, const wchar_shim *fmt,
                    const struct tm *tm, locale_t_shim l)
{ (void)s; (void)max; (void)fmt; (void)tm; (void)l; return 0; }

/* ---- fortify _chk wrappers ---- */
int __sprintf_chk(char *s, int flag, size_t slen, const char *fmt, ...)
{
    va_list ap; int r;
    (void)flag; (void)slen;
    va_start(ap, fmt);
    r = vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}
long __read_chk(int fd, void *buf, size_t n, size_t buflen)
{ (void)buflen; return read(fd, buf, n); }
size_t __mbsrtowcs_chk(wchar_shim *dst, const char **src, size_t len,
                       void *ps, size_t dstlen)
{
    (void)ps; (void)dstlen;
    /* C locale: bytes map 1:1 */
    const char *s = *src;
    size_t i = 0;
    if (!dst) return strlen(s);
    for (; i < len && s[i]; i++) dst[i] = (unsigned char)s[i];
    if (i < len) { dst[i] = 0; *src = NULL; } else *src = s + i;
    return i;
}
wchar_shim *__wmemcpy_chk(wchar_shim *d, const wchar_shim *s, size_t n, size_t dl)
{ (void)dl; for (size_t i = 0; i < n; i++) d[i] = s[i]; return d; }
wchar_shim *__wmemset_chk(wchar_shim *d, wchar_shim c, size_t n, size_t dl)
{ (void)dl; for (size_t i = 0; i < n; i++) d[i] = c; return d; }

/* ---- gettext family: identity ---- */
char *gettext(const char *msgid) { return (char *)msgid; }
char *dgettext(const char *domain, const char *msgid)
{ (void)domain; return (char *)msgid; }
char *bindtextdomain(const char *domain, const char *dirname)
{ (void)domain; return (char *)dirname; }
char *bind_textdomain_codeset(const char *domain, const char *codeset)
{ (void)domain; (void)codeset; return NULL; }

/* ---- iconv: unsupported, callers must handle failure ---- */
void *iconv_open(const char *to, const char *from)
{ (void)to; (void)from; errno = EINVAL; return (void *)-1; }
size_t iconv(void *cd, char **in, size_t *inn, char **out, size_t *outn)
{ (void)cd; (void)in; (void)inn; (void)out; (void)outn;
  errno = EINVAL; return (size_t)-1; }
int iconv_close(void *cd) { (void)cd; return 0; }

/* ---- misc ---- */
unsigned int arc4random(void)
{
    static unsigned long long st = 0x9e3779b97f4a7c15ULL;
    st ^= st << 13; st ^= st >> 7; st ^= st << 17;
    return (unsigned int)(st >> 16);
}
int get_nprocs(void) { return 4; }
char *secure_getenv(const char *name) { return getenv(name); }
char __libc_single_threaded = 0;
size_t __ctype_get_mb_cur_max(void) { return 1; }
unsigned long __isoc23_strtoul(const char *s, char **end, int base)
{ return strtoul(s, end, base); }

/* writev/poll: minimal forwarding */
struct iovec_shim { void *iov_base; size_t iov_len; };
long writev(int fd, const struct iovec_shim *iov, int cnt)
{
    long total = 0;
    for (int i = 0; i < cnt; i++) {
        long r = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}
int poll(void *fds, unsigned long nfds, int timeout)
{ (void)fds; (void)nfds; (void)timeout; errno = ENOSYS; return -1; }

/*
 * gcc-15 libstdc++'s own static initializers (eh_alloc's
 * GLIBCXX_TUNABLES getenv, boot 61 decode) run in the module CTORS set
 * before the linklib autoopen machinery has populated the C library
 * base globals. SysBase IS live by then (register capture confirmed),
 * so open the C runtime first, at the head of the INIT set, which
 * genmodule calls before CTORS.
 */
extern struct Library *StdCBase, *StdCIOBase, *PosixCBase;

static int v8_early_libc_open(void)
{
    if (!StdCBase)
        StdCBase = OpenLibrary("stdc.library", 0);
    if (!StdCIOBase)
        StdCIOBase = OpenLibrary("stdcio.library", 0);
    if (!PosixCBase)
        PosixCBase = OpenLibrary("posixc.library", 0);
    return StdCBase != NULL && StdCIOBase != NULL && PosixCBase != NULL;
}

ADD2INIT(v8_early_libc_open, -127);
