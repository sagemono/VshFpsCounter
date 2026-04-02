/*
 * cxx_stubs.c
 *
 * Minimal stubs for C++ ABI and libc symbols pulled in by libperf.a.
 *
 * libperf is internally C++ but we use it in counter only mode
 * so these are never called at runtime these just satisfy the linker.
 *
 * We provide these instead of linking libstdc++.a because that
 * library uses TLS variables (_Mbstate, _Wcstate, _Errno) which
 * are forbidden in PRX modules on PS3.
 *
 * printf/puts/putchar are replaced with implementations that use
 * sys_tty_write (syscall 403) to output to the DECR TM TTY,
 * avoiding libc's TLS-dependent implementations.
 */

#include <stdint.h>
#include <stdarg.h>
#include <sys/syscall.h>

__attribute__((noinline)) static int sys_tty_write(int ch, const char *buf, unsigned int len, unsigned int *written)
{
    system_call_4(403,
        (uint64_t)(uint32_t)ch,
        (uint64_t)(uint32_t)buf,
        (uint64_t)len,
        (uint64_t)(uint32_t)written);
    return_to_user_prog(int);
}

static void tty_write_str(const char *s)
{
    unsigned int len = 0;
    const char *p = s;
    while (*p++) len++;
    if (len > 0)
    {
        unsigned int written;
        sys_tty_write(0, s, len, &written);
    }
}

static void tty_write_char(char c)
{
    unsigned int written;
    sys_tty_write(0, &c, 1, &written);
}

static void tty_write_uint(unsigned int val)
{
    char buf[12];
    int i = 11;
    buf[11] = '\0';
    if (val == 0) { tty_write_char('0'); return; }
    while (val > 0 && i > 0) { buf[--i] = '0' + (val % 10); val /= 10; }
    tty_write_str(&buf[i]);
}

static void tty_write_int(int val)
{
    if (val < 0) { tty_write_char('-'); val = -val; }
    tty_write_uint((unsigned int)val);
}

static void tty_write_hex(unsigned int val, int width)
{
    static const char hex[] = "0123456789abcdef";
    char buf[9];
    int i;
    for (i = 7; i >= 0; i--) { buf[i] = hex[val & 0xf]; val >>= 4; }
    buf[8] = '\0';
    int start = 8 - width;
    if (start < 0) start = 0;
    if (width == 0) { start = 0; while (start < 7 && buf[start] == '0') start++; }
    tty_write_str(&buf[start]);
}

static void tty_write_ull(unsigned long long val)
{
    char buf[24];
    int i = 23;
    buf[23] = '\0';
    if (val == 0) { tty_write_char('0'); return; }
    while (val > 0 && i > 0) { buf[--i] = '0' + (char)(val % 10); val /= 10; }
    tty_write_str(&buf[i]);
}

static void tty_write_float(float val, int decimals)
{
    if (val < 0.0f) { tty_write_char('-'); val = -val; }
    unsigned int integer = (unsigned int)val;
    tty_write_uint(integer);
    if (decimals > 0)
    {
        tty_write_char('.');
        float frac = val - (float)integer;
        int d;
        for (d = 0; d < decimals; d++)
        {
            frac *= 10.0f;
            unsigned int digit = (unsigned int)frac;
            if (digit > 9) digit = 9;
            tty_write_char('0' + (char)digit);
            frac -= (float)digit;
        }
    }
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt)
    {
        if (*fmt != '%') { tty_write_char(*fmt++); continue; }
        fmt++;

        int zero_pad = 0;
        int width = 0;
        int precision = -1;
        int is_long_long = 0;

        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (*fmt == '.') { fmt++; precision = 0; while (*fmt >= '0' && *fmt <= '9') { precision = precision * 10 + (*fmt - '0'); fmt++; } }
        if (*fmt == 'l') { fmt++; if (*fmt == 'l') { is_long_long = 1; fmt++; } }

        switch (*fmt)
        {
        case 'd': tty_write_int(va_arg(ap, int)); break;
        case 'u':
            if (is_long_long) tty_write_ull(va_arg(ap, unsigned long long));
            else tty_write_uint(va_arg(ap, unsigned int));
            break;
        case 'x':
            tty_write_hex(va_arg(ap, unsigned int), zero_pad ? width : 0);
            break;
        case 's': { const char *s = va_arg(ap, const char*); tty_write_str(s ? s : "(null)"); break; }
        case 'f': tty_write_float((float)va_arg(ap, double), precision >= 0 ? precision : 1); break;
        case '%': tty_write_char('%'); break;
        case '\0': goto done;
        default: tty_write_char('%'); tty_write_char(*fmt); break;
        }
        fmt++;
    }
done:
    va_end(ap);
    return 0;
}

int putchar(int c) { tty_write_char((char)c); return c; }

int puts(const char *s)
{
    tty_write_str(s);
    tty_write_char('\n');
    return 0;
}

void _Assert(const char *file, int line, const char *expr)
{
    printf("[FpsSensor] ASSERT FAILED: %s:%d: %s\n", file, line, expr);
    while(1);
}

static char _heap_buf[8192];
static unsigned int _heap_pos = 0;

void *_Znwj(unsigned int size)
{
    unsigned int aligned = (size + 15) & ~15u;
    if (_heap_pos + aligned > sizeof(_heap_buf))
    return (void*)0;
    void *ptr = &_heap_buf[_heap_pos];
    _heap_pos += aligned;
    return ptr;
}

void _ZdlPv(void *ptr)
{
    if (ptr >= (void*)_heap_buf && ptr < (void*)(_heap_buf + sizeof(_heap_buf)))
    {
    unsigned int offset = (unsigned int)((char*)ptr - _heap_buf);
    if (offset < _heap_pos)
    _heap_pos = offset;
    }
}

void *__cxa_allocate_exception(unsigned int size) { (void)size; return (void*)0; }
void  __cxa_throw(void *obj, void *tinfo, void (*dest)(void*)) { (void)obj; (void)tinfo; (void)dest; while(1); }
void  __cxa_call_unexpected(void *exc) { (void)exc; while(1); }
void  __cxa_pure_virtual(void) { while(1); }
int __gxx_personality_v0(void) { return 0; }
uintptr_t _ZTVN10__cxxabiv117__class_type_infoE[3] = { 0, 0, 0 };
uintptr_t _ZTVN10__cxxabiv120__si_class_type_infoE[3] = { 0, 0, 0 };
void __exit_user_prx_modules(void) {}
void __fini(void) {}
void _exit(int status) { (void)status; while(1); }