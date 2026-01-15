// === FILE: io.c ===
#include "io.h"
#include "kstring.h" /* custom string helpers */
#include "framebuffer.h"
volatile uint16_t *vga = (volatile uint16_t*)0xB8000;
int cursor_x = 0, cursor_y = 0;
static uint8_t vga_attr = VGA_ATTR;
/* Scrollback by full-screen pages (simple implementation)
 * Each page stores the full 80x25 text buffer so the user can page up/down.
 */
/* Line-by-line scrollback buffer */
#define SCROLL_LINES 1024
static uint16_t scroll_lines[SCROLL_LINES][VGA_WIDTH];
static int scroll_next_write = 0; /* next slot to write */
static int scroll_count_lines = 0; /* total stored (capped at SCROLL_LINES) */
static int scroll_viewing = 0; /* whether we're currently viewing history */
static int scroll_view_top = 0; /* index in buffer of line rendered at top of screen */
static uint16_t saved_live[VGA_WIDTH * VGA_HEIGHT]; /* saved live screen when entering view */
/* Command-line history */
#define HISTORY_SIZE 64
#define HISTORY_LEN 256
static char history[HISTORY_SIZE][HISTORY_LEN];
static int history_count = 0;
static int history_next = 0; /* next slot to write */
/* Keyboard/readline coordination */
static int readline_active = 0;
#define KEY_UP ((char)0xFD)
#define KEY_DOWN ((char)0xFE)
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static void update_hardware_cursor(void) {
    unsigned short pos = (unsigned short)(cursor_y * VGA_WIDTH + cursor_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}
/* push a completed line (row) into the scrollback ring */
static void scroll_push_line_from_row(int row) {
    if (row < 0 || row >= VGA_HEIGHT) return;
    for (int c = 0; c < VGA_WIDTH; c++) scroll_lines[scroll_next_write][c] = vga[row * VGA_WIDTH + c];
    scroll_next_write = (scroll_next_write + 1) % SCROLL_LINES;
    if (scroll_count_lines < SCROLL_LINES) scroll_count_lines++;
}
/* push the top-most line being discarded during automatic scroll */
static void scroll_push_top_line(void) {
    for (int c = 0; c < VGA_WIDTH; c++) scroll_lines[scroll_next_write][c] = vga[c];
    scroll_next_write = (scroll_next_write + 1) % SCROLL_LINES;
    if (scroll_count_lines < SCROLL_LINES) scroll_count_lines++;
}
/* render scroll buffer starting at `top_idx` into the screen lines 0..VGA_HEIGHT-1 */
static void render_scroll_from_index(int top_idx) {
    int idx = top_idx;
    for (int r = 0; r < VGA_HEIGHT; r++) {
        for (int c = 0; c < VGA_WIDTH; c++) {
            vga[r * VGA_WIDTH + c] = scroll_lines[idx][c];
        }
        idx = (idx + 1) % SCROLL_LINES;
    }
}
/* compute index of most recent line in buffer (the newest stored) */
static int scroll_index_newest(void) {
    if (scroll_count_lines == 0) return 0;
    int idx = (scroll_next_write - 1 + SCROLL_LINES) % SCROLL_LINES;
    return idx;
}
/* Save live screen to saved_live buffer */
static void save_live_screen(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) saved_live[i] = vga[i];
}
/* Restore live screen from saved_live */
static void restore_live_screen(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) vga[i] = saved_live[i];
}
void clrscr(void) {
    uint16_t blank = ((uint16_t)vga_attr << 8) | ' ';
    /* push each existing line into scrollback */
    for (int r = 0; r < VGA_HEIGHT; r++) scroll_push_line_from_row(r);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) vga[i] = blank;
    cursor_x = 0; cursor_y = 0;
    update_hardware_cursor();
}
static void scroll_if_needed(void) {
    if (cursor_y < VGA_HEIGHT) return;
    /* before we overwrite the top line, push it into the scrollback */
    scroll_push_top_line();
    for (int r = 1; r < VGA_HEIGHT; r++) {
        for (int c = 0; c < VGA_WIDTH; c++) {
            vga[(r-1)*VGA_WIDTH + c] = vga[r*VGA_WIDTH + c];
        }
    }
    /* clear last line */
    uint16_t blank = ((uint16_t)vga_attr << 8) | ' ';
    for (int c = 0; c < VGA_WIDTH; c++) vga[(VGA_HEIGHT-1)*VGA_WIDTH + c] = blank;
    cursor_y = VGA_HEIGHT - 1;
}
int putchar_col(int c) {
    if (c == '\r') { cursor_x = 0; update_hardware_cursor(); return c; }
    if (c == '\n') { cursor_x = 0; cursor_y++; scroll_if_needed(); update_hardware_cursor(); return c; }
    if (c == '\t') { int spaces = 4 - (cursor_x % 4); while (spaces--) putchar_col(' '); return c; }
    if (c == '\b') {
        if (cursor_x == 0 && cursor_y == 0) return c;
        if (cursor_x == 0) { cursor_y--; cursor_x = VGA_WIDTH - 1; }
        else cursor_x--;
        vga[cursor_y * VGA_WIDTH + cursor_x] = ((uint16_t)vga_attr << 8) | ' ';
        update_hardware_cursor();
        return c;
    }
    uint16_t entry = ((uint16_t)vga_attr << 8) | (uint8_t)c;
    vga[cursor_y * VGA_WIDTH + cursor_x] = entry;
    cursor_x++;
    if (cursor_x >= VGA_WIDTH) { cursor_x = 0; cursor_y++; }
    scroll_if_needed();
    update_hardware_cursor();
    return c;
}
void puts_col(const char *s) {
    for (int i = 0; s && s[i]; i++) putchar_col((int)(uint8_t)s[i]);
}
/* tiny uitoa reused here */
static int uitoa(unsigned int v, char *out, int base) {
    char tmp[32]; int pos=0;
    if (v==0) { out[0]='0'; out[1]=0; return 1; }
    while (v) {
        int d = v % base; tmp[pos++] = (d < 10) ? ('0'+d) : ('a'+d-10);
        v /= base;
    }
    for (int i=0;i<pos;i++) out[i]=tmp[pos-1-i];
    out[pos]=0; return pos;
}
int vsnprintf_col(char *buf, int bufsz, const char *fmt, va_list ap) {
    int p=0;
    for (int i=0; fmt[i] && p < bufsz-1; i++) {
        if (fmt[i]=='%') {
            i++;
            if (fmt[i]=='s') {
                char *s = va_arg(ap, char*);
                if (!s) s = "(null)";
                for (int k=0; s[k] && p<bufsz-1; k++) buf[p++]=s[k];
            } else if (fmt[i]=='d' || fmt[i]=='u') {
                int v = va_arg(ap,int);
                if (fmt[i]=='d' && v<0) { buf[p++]='-'; v=-v; }
                char t[32]; uitoa((unsigned)v,t,10);
                for (int k=0;t[k] && p<bufsz-1;k++) buf[p++]=t[k];
            } else if (fmt[i]=='x') {
                unsigned v = va_arg(ap,unsigned);
                char t[32]; uitoa(v,t,16);
                for (int k=0;t[k] && p<bufsz-1;k++) buf[p++]=t[k];
            } else if (fmt[i]=='c') {
                char c = (char)va_arg(ap,int);
                buf[p++]=c;
            } else if (fmt[i]=='%') {
                buf[p++]='%';
            } else {
                /* unknown, print it verbatim */
                buf[p++]='%'; if (fmt[i]) buf[p++]=fmt[i];
            }
        } else buf[p++]=fmt[i];
    }
    buf[p]=0; return p;
}
/* keyboard via BIOS port (polling) - translate basic scancodes */
void io_init(void) {
    clrscr();
}
#include "io.h"
#include "stdarg.h"
/* ===================== Port I/O ===================== */
void outsw(uint16_t port, const void* addr, uint32_t count) {
    __asm__ volatile ("rep outsw" : : "S"(addr), "c"(count), "d"(port));
}
void insw(uint16_t port, void* addr, uint32_t count) {
    __asm__ volatile ("rep insw" : : "D"(addr), "c"(count), "d"(port) : "memory");
}
/* ===================== String Functions ===================== */
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}
char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}
uint32_t strlen(const char* s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}
void* memcpy(void* dest, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
   
    for (uint32_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}
void* memset(void* dest, uint8_t val, uint32_t n) {
    uint8_t* d = (uint8_t*)dest;
   
    for (uint32_t i = 0; i < n; i++) {
        d[i] = val;
    }
    return dest;
}
/* ===================== ATA PIO ===================== */
static void ata_wait_bsy(void) {
    while (inb(0x1F7) & 0x80) ;
}
static void ata_wait_drq(void) {
    while (!(inb(0x1F7) & 0x08)) ;
}
/*
void ata_init(void) {
    // Select master drive
    outb(0x1F6, 0xE0);
    // Software reset
    outb(0x3F6, 0x04);
    outb(0x3F6, 0x00);
    ata_wait_bsy();
}
*/
int ata_identify(void) {
    ata_wait_bsy();
    outb(0x1F6, 0xA0); // Master drive
    outb(0x1F7, 0xEC); // IDENTIFY command
   
    if (inb(0x1F7) == 0) return 0;
   
    ata_wait_drq();
   
    uint16_t buffer[256];
    for (int i = 0; i < 256; i++) {
        buffer[i] = inb(0x1F0) | (inb(0x1F0) << 8);
    }
   
    // Check if it's an ATA device
    return (buffer[0] & 0x8000) == 0;
}
void ata_read_lba28(uint32_t lba, uint8_t sectors, void* buffer) {
    if (sectors == 0) return;
   
    ata_wait_bsy();
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, sectors);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    outb(0x1F7, 0x20); // READ SECTORS
   
    uint8_t* buf = (uint8_t*)buffer;
    for (int s = 0; s < sectors; s++) {
        ata_wait_drq();
        insw(0x1F0, buf + s * 512, 256);
    }
}
void ata_write_lba28(uint32_t lba, uint8_t sectors, const void* buffer) {
    if (sectors == 0) return;
   
    ata_wait_bsy();
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, sectors);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    outb(0x1F7, 0x30); // WRITE SECTORS
   
    const uint8_t* buf = (const uint8_t*)buffer;
    for (int s = 0; s < sectors; s++) {
        ata_wait_drq();
        outsw(0x1F0, buf + s * 512, 256);
    }
   
    // Flush cache
    outb(0x1F7, 0xE7);
    ata_wait_bsy();
}
/* ===================== VGA Text Mode ===================== */
/*volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
static int cursor_x = 0, cursor_y = 0;
*/static uint8_t current_color = 0x0F;
void vga_init(void) {
    outb(0x3D4, 0x09);
    int8_t val = inb(0x3d5);
    val |= 0x00;
    outb(0x3d5, val);
    vga_clear();
    vga_set_color(COLOR_LIGHT_GRAY, COLOR_BLACK);
}
void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = (current_color << 8) | ' ';
    }
    cursor_x = 0;
    cursor_y = 0;
}
void vga_set_color(uint8_t fg, uint8_t bg) {
    current_color = (bg << 4) | fg;
}
void vga_set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_x >= VGA_WIDTH) cursor_x = VGA_WIDTH - 1;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_y >= VGA_HEIGHT) cursor_y = VGA_HEIGHT - 1;
}
void vga_get_cursor(int* x, int* y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}
/* ===================== Simple framebuffer shim (compiled into io.c) =====================
 * These are lightweight implementations to satisfy the kernel linkage and to
 * provide a safe, heuristic framebuffer interface. Full VBE/Multiboot parsing
 * is still in `framebuffer.c` but to avoid requiring changes to the outer
 * Makefile we compile the implementations here so symbols are present.
 */
static volatile uint8_t *shim_fb_ptr = 0;
static uint32_t shim_fb_w = 0, shim_fb_h = 0, shim_fb_pitch = 0, shim_fb_bpp = 0;
static int shim_fb_ok = 0;
void fb_init(uint32_t magic, uint32_t addr) {
    shim_fb_ok = 0;
    shim_fb_ptr = 0; shim_fb_w = shim_fb_h = shim_fb_pitch = shim_fb_bpp = 0;
    if (magic != 0x2BADB002 || addr == 0) return;
    /* Very small, safe multiboot v1 heuristic: read the 'vbe_mode' and
     * 'vbe_control_info' fields from the multiboot info structure if present.
     * If we find a plausible physical framebuffer pointer we keep it.
     */
    uint32_t *mb = (uint32_t*)(uintptr_t)addr;
    /* mb[16] = vbe_control_info, mb[17] = vbe_mode (per our earlier typedef) */
    uint32_t vbe_ctrl = mb[16];
    uint32_t vbe_mode = mb[17];
    if (vbe_ctrl == 0) return;
    uint8_t *ctrl = (uint8_t*)(uintptr_t)vbe_ctrl;
    uint32_t phys = 0;
    /* try common offset 0x0C for PhysBasePtr */
    phys = *(uint32_t*)(ctrl + 0x0C);
    if (phys == 0) phys = *(uint32_t*)(ctrl + 0x10);
    if (phys == 0) return;
    shim_fb_ptr = (volatile uint8_t*)(uintptr_t)phys;
    /* Heuristic read of width/height/pitch/bpp */
    uint16_t w = *(uint16_t*)(ctrl + 0x12);
    uint16_t h = *(uint16_t*)(ctrl + 0x14);
    uint16_t pitch = *(uint16_t*)(ctrl + 0x10);
    uint16_t bpp = *(uint16_t*)(ctrl + 0x1C);
    if (w >= 320 && h >= 200) {
        shim_fb_w = w; shim_fb_h = h; shim_fb_pitch = pitch; shim_fb_bpp = bpp;
        if (bpp == 32 || bpp == 24) shim_fb_ok = 1;
    }
}
int fb_available(void) { return shim_fb_ok; }
uint32_t fb_width(void) { return shim_fb_w; }
uint32_t fb_height(void) { return shim_fb_h; }
uint32_t fb_pitch(void) { return shim_fb_pitch; }
uint32_t fb_bpp(void) { return shim_fb_bpp; }
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!shim_fb_ok) return;
    if (x >= shim_fb_w || y >= shim_fb_h) return;
    uint8_t *p = (uint8_t*)shim_fb_ptr + (uint64_t)y * shim_fb_pitch + x * (shim_fb_bpp/8);
    if (shim_fb_bpp == 32) {
        *((uint32_t*)p) = (color & 0x00FFFFFF);
    } else if (shim_fb_bpp == 24) {
        p[0] = (uint8_t)(color & 0xFF);
        p[1] = (uint8_t)((color >> 8) & 0xFF);
        p[2] = (uint8_t)((color >> 16) & 0xFF);
    }
}
void fb_clear(uint32_t color) {
    if (!shim_fb_ok) return;
    for (uint32_t y = 0; y < shim_fb_h; y++) {
        for (uint32_t x = 0; x < shim_fb_w; x++) fb_putpixel(x, y, color);
    }
}
void fb_status(char *buf, int buflen) {
    if (!buf || buflen <= 0) return;
    if (shim_fb_ok) {
        /* snprintf-like: W x H x BPP */
        int n = 0;
        const char *t = "FB: available ";
        while (*t && n < buflen-1) buf[n++] = *t++;
        /* append width */
        uint32_t v = shim_fb_w;
        char tmp[32]; int m = 0;
        if (v == 0) tmp[m++] = '0'; else { char rev[16]; int r=0; while (v>0 && r<16) { rev[r++]= '0' + (v%10); v/=10; } while (r-->0) tmp[m++] = rev[r]; }
        tmp[m++] = 'x';
        v = shim_fb_h;
        if (v == 0) tmp[m++] = '0'; else { char rev[16]; int r=0; while (v>0 && r<16) { rev[r++]= '0' + (v%10); v/=10; } while (r-->0) tmp[m++] = rev[r]; }
        tmp[m++] = 'x';
        v = shim_fb_bpp;
        if (v == 0) tmp[m++] = '0'; else { char rev[8]; int r=0; while (v>0 && r<8) { rev[r++]= '0' + (v%10); v/=10; } while (r-->0) tmp[m++] = rev[r]; }
        tmp[m] = '\0';
        int i = 0; while (tmp[i] && n < buflen-1) buf[n++] = tmp[i++]; buf[n] = '\0';
    } else {
        const char *t = "FB: unavailable (using mode13)";
        int i = 0; while (t[i] && i < buflen-1) { buf[i] = t[i]; i++; } buf[i] = '\0';
    }
}
static void vga_scroll_if_needed(void) {
    if (cursor_y < VGA_HEIGHT) return;
    /* before shifting, push the top line being discarded into scrollback */
    scroll_push_top_line();
    // Scroll up by one line
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga[y * VGA_WIDTH + x] = vga[(y + 1) * VGA_WIDTH + x];
        }
    }
    // Clear bottom line
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (current_color << 8) | ' ';
    }
    cursor_y = VGA_HEIGHT - 1;
}
void vga_scroll(int lines) {
    if (lines <= 0) return;
   
    for (int i = 0; i < lines; i++) {
        // Move all lines up by one
        for (int y = 0; y < VGA_HEIGHT - 1; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                vga[y * VGA_WIDTH + x] = vga[(y + 1) * VGA_WIDTH + x];
            }
        }
       
        // Clear bottom line
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (current_color << 8) | ' ';
        }
    }
   
    if (cursor_y >= lines) {
        cursor_y -= lines;
    } else {
        cursor_y = 0;
    }
}
void putc_k(char ch) {
    switch (ch) {
        case '\n':
            /* push the just-completed line into scrollback, then advance */
            scroll_push_line_from_row(cursor_y);
            cursor_x = 0;
            cursor_y++;
            vga_scroll_if_needed();
            break;
        case '\r':
            cursor_x = 0;
            break;
        case '\t':
            cursor_x = (cursor_x + 8) & ~7;
            if (cursor_x >= VGA_WIDTH) {
                cursor_x = 0;
                cursor_y++;
                vga_scroll_if_needed();
            }
            break;
        case '\b':
            if (cursor_x > 0) {
                cursor_x--;
                vga[cursor_y * VGA_WIDTH + cursor_x] = (current_color << 8) | ' ';
            }
            break;
        default:
            vga[cursor_y * VGA_WIDTH + cursor_x] = (current_color << 8) | ch;
            cursor_x++;
            if (cursor_x >= VGA_WIDTH) {
                cursor_x = 0;
                cursor_y++;
                vga_scroll_if_needed();
                /* pushing page will be handled by scroll_if_needed when needed */
            }
            break;
    }
}
void puts_k(const char* s) {
    while (*s) putc_k(*s++);
}
/* Simple printf implementation without stdarg.h */
static void print_int(int val) {
    if (val == 0) {
        putc_k('0');
        return;
    }
   
    if (val < 0) {
        putc_k('-');
        val = -val;
    }
   
    char buf[12];
    int pos = 0;
   
    while (val) {
        buf[pos++] = '0' + (val % 10);
        val /= 10;
    }
   
    while (pos--) {
        putc_k(buf[pos]);
    }
}
static void print_hex(uint32_t val) {
    char hex[] = "0123456789ABCDEF";
   
    putc_k('0');
    putc_k('x');
   
    int started = 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
        int nibble = (val >> shift) & 0xF;
        if (nibble || started) {
            putc_k(hex[nibble]);
            started = 1;
        }
    }
   
    if (!started) {
        putc_k('0');
    }
}
static void print_uint(uint32_t val) {
    if (val == 0) {
        putc_k('0');
        return;
    }
   
    char buf[12];
    int pos = 0;
   
    while (val) {
        buf[pos++] = '0' + (val % 10);
        val /= 10;
    }
   
    while (pos--) {
        putc_k(buf[pos]);
    }
}
void printf_k(const char* fmt, ...) {
    va_list args;
    __builtin_va_start(args, fmt);
   
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            putc_k(fmt[i]);
            continue;
        }
       
        i++;
        switch (fmt[i]) {
            case 's': {
                char* s = __builtin_va_arg(args, char*);
                puts_k(s);
                break;
            }
            case 'c': {
                char c = (char)__builtin_va_arg(args, int);
                putc_k(c);
                break;
            }
            case 'd': {
                int val = __builtin_va_arg(args, int);
                print_int(val);
                break;
            }
            case 'x': {
                uint32_t val = __builtin_va_arg(args, uint32_t);
                print_hex(val);
                break;
            }
            case 'u': {
                uint32_t val = __builtin_va_arg(args, uint32_t);
                print_uint(val);
                break;
            }
            default:
                putc_k(fmt[i]);
                break;
        }
    }
   
    __builtin_va_end(args);
}
/* ===================== Keyboard ===================== */
static const char keymap_normal[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};
static const char keymap_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};
void kbd_init(void) {
    // Nothing to initialize for polling mode
}
char kbd_getchar(void) {
    static int shift_pressed = 0;
    static int ctrl_pressed = 0;
    static int alt_pressed = 0;
    while (1) {
        if (inb(0x64) & 1) {
            uint8_t scancode = inb(0x60);
            /* handle extended scancode prefix 0xE0 for arrow keys */
            if (scancode == 0xE0) {
                /* wait for next scancode */
                while (!(inb(0x64) & 1));
                uint8_t sc2 = inb(0x60);
                /* ignore releases (have 0x80 bit) */
                if (sc2 & 0x80) continue;
                /* Up arrow = 0x48, Down arrow = 0x50 */
                if (sc2 == 0x48) {
                    /* If readline is active, return special code for history navigation */
                    if (readline_active) return KEY_UP;
                    /* Enter or advance scrollback view */
                    if (scroll_count_lines == 0) continue;
                    int S = SCROLL_LINES;
                    if (!scroll_viewing) {
                        save_live_screen();
                        scroll_viewing = 1;
                        /* show last VGA_HEIGHT lines */
                        int show_lines = (scroll_count_lines < VGA_HEIGHT ? scroll_count_lines : VGA_HEIGHT);
                        scroll_view_top = (scroll_next_write - show_lines + S) % S;
                    } else {
                        /* scroll up by one line, clamped */
                        int current_forward = (scroll_next_write - scroll_view_top + S) % S;
                        int dist_to_top = scroll_count_lines - current_forward;
                        if (dist_to_top > 0) {
                            scroll_view_top = (scroll_view_top - 1 + S) % S;
                        }
                    }
                    render_scroll_from_index(scroll_view_top);
                    /* draw HISTORY indicator at top-right */
                    const char *hint = "HISTORY";
                    int pos = VGA_WIDTH - 7;
                    for (int i = 0; i < 7; i++) vga[pos + i] = ((current_color << 8) | hint[i]);
                    continue;
                } else if (sc2 == 0x50) {
                    if (readline_active) return KEY_DOWN;
                    if (!scroll_viewing) continue;
                    /* page down by one line */
                    scroll_view_top = (scroll_view_top + 1) % SCROLL_LINES;
                    /* if we've reached the newest (next write index), restore live */
                    if (scroll_view_top == scroll_next_write) {
                        restore_live_screen();
                        scroll_viewing = 0;
                    } else {
                        render_scroll_from_index(scroll_view_top);
                        const char *hint = "HISTORY";
                        int pos = VGA_WIDTH - 7;
                        for (int i = 0; i < 7; i++) vga[pos + i] = ((current_color << 8) | hint[i]);
                    }
                    continue;
                } else if (sc2 == 0x49) { /* Page Up */
                    if (readline_active) continue;
                    if (scroll_count_lines == 0) continue;
                    int S = SCROLL_LINES;
                    if (!scroll_viewing) {
                        save_live_screen();
                        scroll_viewing = 1;
                        int show_lines = (scroll_count_lines < VGA_HEIGHT ? scroll_count_lines : VGA_HEIGHT);
                        scroll_view_top = (scroll_next_write - show_lines + S) % S;
                    } else {
                        int current_forward = (scroll_next_write - scroll_view_top + S) % S;
                        int move_amount = (VGA_HEIGHT < (scroll_count_lines - current_forward) ? VGA_HEIGHT : (scroll_count_lines - current_forward));
                        if (move_amount > 0) {
                            scroll_view_top = (scroll_view_top - move_amount + S) % S;
                        }
                    }
                    render_scroll_from_index(scroll_view_top);
                    const char *hint = "HISTORY";
                    int pos = VGA_WIDTH - 7;
                    for (int i = 0; i < 7; i++) vga[pos + i] = ((current_color << 8) | hint[i]);
                    continue;
                } else if (sc2 == 0x51) { /* Page Down */
                    if (readline_active) continue;
                    if (!scroll_viewing) continue;
                    int S = SCROLL_LINES;
                    int current_forward = (scroll_next_write - scroll_view_top + S) % S;
                    if (current_forward <= VGA_HEIGHT) {
                        restore_live_screen();
                        scroll_viewing = 0;
                    } else {
                        int move_amount = (VGA_HEIGHT < (current_forward - VGA_HEIGHT) ? VGA_HEIGHT : (current_forward - VGA_HEIGHT));
                        scroll_view_top = (scroll_view_top + move_amount) % S;
                        render_scroll_from_index(scroll_view_top);
                        const char *hint = "HISTORY";
                        int pos = VGA_WIDTH - 7;
                        for (int i = 0; i < 7; i++) vga[pos + i] = ((current_color << 8) | hint[i]);
                    }
                    continue;
                } else {
                    continue;
                }
            }
            // Check for key release
            if (scancode & 0x80) {
                uint8_t key = scancode & 0x7F;
                if (key == 0x2A || key == 0x36) shift_pressed = 0;
                if (key == 0x1D) ctrl_pressed = 0;
                if (key == 0x38) alt_pressed = 0;
                continue;
            }
            // Key press
            if (scancode == 0x2A || scancode == 0x36) {
                shift_pressed = 1;
                continue;
            }
            if (scancode == 0x1D) {
                ctrl_pressed = 1;
                continue;
            }
            if (scancode == 0x38) {
                alt_pressed = 1;
                continue;
            }
            if (scancode < 128) {
                /* if we are viewing history and this is a normal key, restore live */
                if (scroll_viewing) {
                    restore_live_screen();
                    scroll_viewing = 0;
                }
                if (ctrl_pressed && scancode == 'l' - 'a' + 1) {
                    vga_clear();
                    cursor_x = cursor_y = 0;
                    return 0;
                }
                if (shift_pressed) {
                    return keymap_shift[scancode];
                } else {
                    return keymap_normal[scancode];
                }
            }
        }
    }
}
int kbd_getscancode(void) {
    if (inb(0x64) & 1) {
        return inb(0x60);
    }
    return -1;
}
int kbd_iskeypressed(void) {
    return (inb(0x64) & 1) != 0;
}
int readline(char* buf, int bufsize) {
    int pos = 0;
    int history_pos = -1; /* local selection index for history (offset from newest) */
    int prev_len = 0; /* previous displayed input length for clearing */
    /* record cursor start position so we can overwrite the input line when
     * navigating history */
    int start_x = cursor_x;
    readline_active = 1;
    while (1) {
        char ch = kbd_getchar();
        if (ch == KEY_UP) {
            /* navigate up in history */
            if (history_count == 0) continue;
            if (history_pos < history_count - 1) history_pos++; /* older */
            else history_pos = history_count - 1;
            int idx = (history_next - 1 - history_pos + HISTORY_SIZE) % HISTORY_SIZE;
            /* copy into buf */
            int i = 0; while (i < bufsize - 1 && history[idx][i]) { buf[i] = history[idx][i]; i++; }
            buf[i] = 0; pos = i;
            /* visually replace current input */
            /* clear previous */
            vga_set_cursor(start_x, cursor_y);
            for (int k = 0; k < prev_len; k++) { vga[cursor_y * VGA_WIDTH + start_x + k] = (current_color << 8) | ' '; }
            /* write new */
            vga_set_cursor(start_x, cursor_y);
            for (int k = 0; k < pos; k++) { vga[cursor_y * VGA_WIDTH + start_x + k] = (current_color << 8) | buf[k]; }
            cursor_x = start_x + pos;
            update_hardware_cursor();
            prev_len = pos;
            continue;
        }
        if (ch == KEY_DOWN) {
            if (history_count == 0) continue;
            if (history_pos <= 0) {
                /* clear input */
                history_pos = -1;
                for (int k = 0; k < prev_len; k++) vga[cursor_y * VGA_WIDTH + start_x + k] = (current_color << 8) | ' ';
                cursor_x = start_x;
                update_hardware_cursor();
                pos = 0; prev_len = 0; buf[0] = 0;
            } else {
                history_pos--;
                int idx = (history_next - 1 - history_pos + HISTORY_SIZE) % HISTORY_SIZE;
                int i = 0; while (i < bufsize - 1 && history[idx][i]) { buf[i] = history[idx][i]; i++; }
                buf[i] = 0; pos = i;
                /* replace visually */
                vga_set_cursor(start_x, cursor_y);
                for (int k = 0; k < prev_len; k++) vga[cursor_y * VGA_WIDTH + start_x + k] = (current_color << 8) | ' ';
                vga_set_cursor(start_x, cursor_y);
                for (int k = 0; k < pos; k++) vga[cursor_y * VGA_WIDTH + start_x + k] = (current_color << 8) | buf[k];
                cursor_x = start_x + pos;
                update_hardware_cursor();
                prev_len = pos;
            }
            continue;
        }
        if (ch == '\n') {
            putc_k('\n');
            buf[pos] = 0;
            /* save into history if non-empty */
            if (pos > 0) {
                int hidx = history_next % HISTORY_SIZE;
                int i = 0; while (i < HISTORY_LEN - 1 && i < pos) { history[hidx][i] = buf[i]; i++; }
                history[hidx][i] = 0;
                history_next = (history_next + 1) % HISTORY_SIZE;
                if (history_count < HISTORY_SIZE) history_count++;
            }
            readline_active = 0;
            return pos;
        }
        if (ch == '\b') {
            if (pos > 0) {
                pos--;
                /* Erase character visually */
                if (cursor_x == 0) {
                    if (cursor_y > 0) {
                        cursor_y--;
                        cursor_x = VGA_WIDTH - 1;
                    }
                } else {
                    cursor_x--;
                }
                vga[cursor_y * VGA_WIDTH + cursor_x] = (current_color << 8) | ' ';
                prev_len = pos;
            }
            continue;
        }
        if (ch == 0) continue;
        // Ctrl+U - clear line
        if (ch == 21) { // Ctrl+U
            while (pos > 0) {
                pos--;
                if (cursor_x == 0) {
                    if (cursor_y > 0) {
                        cursor_y--;
                        cursor_x = VGA_WIDTH - 1;
                    }
                } else {
                    cursor_x--;
                }
                vga[cursor_y * VGA_WIDTH + cursor_x] = (current_color << 8) | ' ';
            }
            prev_len = 0;
            continue;
        }
        if (pos < bufsize - 1) {
            buf[pos++] = ch;
            /* print char visually */
            vga[cursor_y * VGA_WIDTH + cursor_x] = (current_color << 8) | ch;
            cursor_x++;
            update_hardware_cursor();
            prev_len = pos;
        }
    }
}
/* ===================== Serial Port ===================== */
void serial_init(uint16_t port) {
     (port + 1, 0x00); // Disable interrupts
    outb(port + 3, 0x80); // Enable DLAB
    outb(port + 0, 0x03); // 38400 baud
    outb(port + 1, 0x00);
    outb(port + 3, 0x03); // 8 bits, no parity, one stop bit
    outb(port + 2, 0xC7); // Enable FIFO
    outb(port + 4, 0x0B); // IRQs enabled, RTS/DSR set
}
int serial_is_transmit_empty(uint16_t port) {
    return inb(port + 5) & 0x20;
}
void serial_putc(uint16_t port, char c) {
    while (!serial_is_transmit_empty(port));
    outb(port, c);
}
void serial_puts(uint16_t port, const char* s) {
    while (*s) {
        serial_putc(port, *s++);
    }
}
void serial_printf(uint16_t port, const char* fmt, ...) {
    va_list args;
    __builtin_va_start(args, fmt);
   
    char buffer[256];
    int pos = 0;
   
    for (int i = 0; fmt[i] && pos < 255; i++) {
        if (fmt[i] != '%') {
            buffer[pos++] = fmt[i];
            continue;
        }
       
        i++;
        switch (fmt[i]) {
            case 's': {
                char* s = __builtin_va_arg(args, char*);
                while (*s && pos < 255) buffer[pos++] = *s++;
                break;
            }
            case 'c': {
                char c = (char)__builtin_va_arg(args, int);
                if (pos < 255) buffer[pos++] = c;
                break;
            }
            case 'd': {
                int val = __builtin_va_arg(args, int);
                char num[12];
                int num_pos = 0;
                if (val == 0) {
                    num[num_pos++] = '0';
                } else {
                    if (val < 0) {
                        if (pos < 255) buffer[pos++] = '-';
                        val = -val;
                    }
                    while (val) {
                        num[num_pos++] = '0' + (val % 10);
                        val /= 10;
                    }
                }
                while (num_pos--) {
                    if (pos < 255) buffer[pos++] = num[num_pos];
                }
                break;
            }
            default:
                if (pos < 255) buffer[pos++] = fmt[i];
                break;
        }
    }
   
    buffer[pos] = 0;
    serial_puts(port, buffer);
   
    __builtin_va_end(args);
}
void printf_col(const char* fmt, ...) {
    va_list args;
    __builtin_va_start(args, fmt);
   
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            putc_k(fmt[i]);
            continue;
        }
       
        i++;
        switch (fmt[i]) {
            case 's': {
                char* s = __builtin_va_arg(args, char*);
                puts_k(s);
                break;
            }
            case 'c': {
                char c = (char)__builtin_va_arg(args, int);
                putc_k(c);
                break;
            }
            case 'd': {
                int val = __builtin_va_arg(args, int);
                print_int(val);
                break;
            }
            case 'x': {
                uint32_t val = __builtin_va_arg(args, uint32_t);
                print_hex(val);
                break;
            }
            case 'u': {
                uint32_t val = __builtin_va_arg(args, uint32_t);
                print_uint(val);
                break;
            }
            default:
                putc_k(fmt[i]);
                break;
        }
    }
   
    __builtin_va_end(args);
}