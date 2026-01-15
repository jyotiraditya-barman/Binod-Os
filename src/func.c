/* functions.c - small freestanding helpers + printf for kernel modules
   Works without libc. Uses VGA text mode at 0xB8000.
*/

#include <stdarg.h>
#include <stdint.h>

/* ---------- small types for freestanding build ---------- */
typedef unsigned int uint;
typedef unsigned char uchar;

/* ---------- tiny memcpy/memset/strlen/strncpy/strncmp ---------- */
void *memcpy_small(void *dst, const void *src, uint n) {
    uchar *d = (uchar*)dst;
    const uchar *s = (const uchar*)src;
    for (uint i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memset_small(void *dst, int v, uint n) {
    uchar *d = (uchar*)dst;
    uchar val = (uchar)v;
    for (uint i = 0; i < n; i++) d[i] = val;
    return dst;
}

uint strlen_small(const char *s) {
    uint i = 0;
    while (s && s[i]) i++;
    return i;
}

/* strncpy_small behaves like strncpy but always NUL-terminates (if n>0) */
char *strncpy_small(char *dst, const char *src, uint n) {
    if (n == 0) return dst;
    uint i = 0;
    for (; i < n - 1 && src[i]; i++) dst[i] = src[i];
    /* fill rest with 0 */
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

/* strncmp standard behavior */
int strncmp_small(const char *a, const char *b, uint n) {
    for (uint i = 0; i < n; i++) {
        unsigned char ac = (unsigned char)a[i];
        unsigned char bc = (unsigned char)b[i];
        if (ac != bc) return (int)ac - (int)bc;
        if (ac == 0) return 0;
    }
    return 0;
}

/* Export names expected by other files */
int strncmp(const char *a, const char *b, unsigned int n) {
    return strncmp_small(a, b, n);
}
char *strncpy(char *dst, const char *src, unsigned int n) {
    return strncpy_small(dst, src, n);
}
unsigned int strlen(const char *s) {
    return strlen_small(s);
}

/* ---------- minimal VGA output (local cursor separate from kernel.c) ---------- */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
static volatile uint16_t *vga = (volatile uint16_t*)0xB8000;
static unsigned short fn_cursor_x = 0;
static unsigned short fn_cursor_y = 0;
static unsigned char fn_color = 0x07; /* light gray on black */

/* scroll up one line */
static void fn_scroll() {
    if (fn_cursor_y < VGA_HEIGHT) return;
    /* shift lines up */
    for (int row = 1; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            vga[(row - 1) * VGA_WIDTH + col] = vga[row * VGA_WIDTH + col];
        }
    }
    /* clear last line */
    uint16_t blank = ((uint16_t)fn_color << 8) | (uint8_t)' ';
    for (int col = 0; col < VGA_WIDTH; col++) vga[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = blank;
    fn_cursor_y = VGA_HEIGHT - 1;
}

/* put single character (handles newline) */
static void fn_putc(char ch) {
    if (ch == '\n') {
        fn_cursor_x = 0;
        fn_cursor_y++;
        fn_scroll();
        return;
    }
    uint16_t val = ((uint16_t)fn_color << 8) | (uint8_t)ch;
    vga[fn_cursor_y * VGA_WIDTH + fn_cursor_x] = val;
    fn_cursor_x++;
    if (fn_cursor_x >= VGA_WIDTH) {
        fn_cursor_x = 0;
        fn_cursor_y++;
        fn_scroll();
    }
}

/* write string */
static void fn_puts(const char *s) {
    for (uint i = 0; s && s[i]; i++) fn_putc(s[i]);
}

/* ---------- tiny integer -> string helpers ---------- */
static void utoa(unsigned int v, char *out, int base) {
    char buf[33];
    int p = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) {
        int d = v % base;
        buf[p++] = (d < 10) ? ('0' + d) : ('a' + (d - 10));
        v /= base;
    }
    /* reverse */
    int j = 0;
    while (p--) out[j++] = buf[p];
    out[j] = 0;
}

/* ---------- minimal vsnprintf + printf (global) ---------- */
static int fn_vsnprintf(char *buf, int bufsize, const char *fmt, va_list ap) {
    int p = 0;
    for (int i = 0; fmt[i] && p < bufsize - 1; i++) {
        if (fmt[i] == '%') {
            i++;
            char tmp[64];
            if (fmt[i] == 's') {
                char *s = va_arg(ap, char*);
                for (int k = 0; s && s[k] && p < bufsize - 1; k++) buf[p++] = s[k];
            } else if (fmt[i] == 'd' || fmt[i] == 'u') {
                int v = va_arg(ap, int);
                if (fmt[i] == 'd' && v < 0) {
                    buf[p++] = '-';
                    v = -v;
                }
                utoa((unsigned)v, tmp, 10);
                for (int k = 0; tmp[k] && p < bufsize - 1; k++) buf[p++] = tmp[k];
            } else if (fmt[i] == 'x') {
                unsigned v = va_arg(ap, unsigned);
                utoa(v, tmp, 16);
                for (int k = 0; tmp[k] && p < bufsize - 1; k++) buf[p++] = tmp[k];
            } else if (fmt[i] == 'c') {
                char c = (char)va_arg(ap, int);
                buf[p++] = c;
            } else {
                /* unknown, print literally */
                buf[p++] = '%';
                if (p < bufsize - 1) buf[p++] = fmt[i];
            }
        } else {
            buf[p++] = fmt[i];
        }
    }
    buf[p] = '\0';
    return p;
}

/* Global printf used by fs.c and others */
int printf(const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = fn_vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    /* output using fn_putc (handles newlines & scroll) */
    for (int i = 0; i < n; i++) fn_putc(tmp[i]);
    return n;
}
