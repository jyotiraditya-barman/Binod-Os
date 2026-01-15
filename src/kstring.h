
// === FILE: kstring.h ===
#ifndef KSTRING_H
#define KSTRING_H

#include <stddef.h>

size_t kstrlen(const char *s);
int kstrcmp(const char *a, const char *b);
int kstrncmp(const char *a, const char *b, size_t n);
char *kstrcpy(char *dst, const char *src);
char *kstrncpy(char *dst, const char *src, size_t n);
void *kmemset(void *s, int c, size_t n);
void *kmemcpy(void *dest, const void *src, size_t n);
int kmemcmp(const void *a, const void *b, size_t n);

#endif // KSTRING_H
