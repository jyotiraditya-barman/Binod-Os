// === FILE: io.h ===
#ifndef IO_H
#define IO_H


#include <stdarg.h>
#include <stdint.h>


#define VGA_WIDTH 80
#define VGA_HEIGHT 50
#define VGA_ATTR 0x07


extern int cursor_x;
extern int cursor_y;


/* ATA PIO */
//void ata_init(void);
void ata_read_lba28(uint32_t lba, uint8_t sectors, void* buffer);
void ata_write_lba28(uint32_t lba, uint8_t sectors, const void* buffer);
int ata_identify(void);

/* VGA Text Mode */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define COLOR_BLACK 0
#define COLOR_BLUE 1
#define COLOR_GREEN 2
#define COLOR_CYAN 3
#define COLOR_RED 4
#define COLOR_MAGENTA 5
#define COLOR_BROWN 6
#define COLOR_LIGHT_GRAY 7
#define COLOR_DARK_GRAY 8
#define COLOR_LIGHT_BLUE 9
#define COLOR_LIGHT_GREEN 10
#define COLOR_LIGHT_CYAN 11
#define COLOR_LIGHT_RED 12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW 14
#define COLOR_WHITE 15

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void putc_k(char ch);
void puts_k(const char* s);
void printf_k(const char* fmt, ...);
void vga_scroll(int lines);
void vga_set_cursor(int x, int y);
void vga_get_cursor(int* x, int* y);

/* Keyboard */
void kbd_init(void);
char kbd_getchar(void);
int kbd_getscancode(void);
int kbd_iskeypressed(void);
int readline(char* buf, int bufsize);

/* Serial Port */
void serial_init(uint16_t port);
void serial_putc(uint16_t port, char c);
void serial_puts(uint16_t port, const char* s);
void serial_printf(uint16_t port, const char* fmt, ...);

/* Simple string functions (since we don't have libc) */
int strcmp(const char* s1, const char* s2);
char* strcpy(char* dest, const char* src);
uint32_t strlen(const char* s);
void* memcpy(void* dest, const void* src, uint32_t n);
void* memset(void* dest, uint8_t val, uint32_t n);

void clrscr(void);
int putchar_col(int c);
void puts_col(const char *s);
void printf_col(const char* fmt, ...);
//int printf_col(const char *fmt, ...);
int vsnprintf_col(char *buf, int bufsz, const char *fmt, va_list ap);



#endif // IO_H
