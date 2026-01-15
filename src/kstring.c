

// === FILE: kstring.c ===
#include "kstring.h"

size_t kstrlen(const char *s) {
    const char *p = s; while (*p) p++; return (size_t)(p - s);
}

int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, size_t n) {
    size_t i = 0;
    for (; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

char *kstrcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *kstrncpy(char *dst, const char *src, size_t n) {
    size_t i=0;
    for (; i<n && src[i]; i++) dst[i] = src[i];
    for (; i<n; i++) dst[i]=0;
    return dst;
}

void *kmemset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *kmemcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest; const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

int kmemcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = a, *pb = b;
    for (size_t i=0;i<n;i++) if (pa[i] != pb[i]) return pa[i]-pb[i];
    return 0;
}

