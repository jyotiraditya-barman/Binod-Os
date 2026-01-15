/* bmp_vga13.c - TRUE VGA mode 13h BMP renderer
 * Supports 8 / 24 / 32 bpp BMP
 * Renders scaled to 320x200 using direct VGA memory
 */

#include <stdint.h>
#include "fs.h"
#include "io.h"
#include "vga_mode13.h"

#define BMP_MAX_FILE 131072
#define VGA_W 320
#define VGA_H 200

/* VGA framebuffer */
#define VGA_FB ((uint8_t*)0xA0000)

/* ---------------- Little-endian helpers ---------------- */

static inline uint16_t rd16(const uint8_t *p) {
    return p[0] | (p[1] << 8);
}

static inline uint32_t rd32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

/* ---------------- VGA palette ---------------- */

/* 6x6x6 color cube (216 colors) */
static void vga_set_6x6x6_palette(void) {
    outb(0x3C8, 0);
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                outb(0x3C9, r * 63 / 5);
                outb(0x3C9, g * 63 / 5);
                outb(0x3C9, b * 63 / 5);
            }
}

/* Load BMP 256-color palette */
static void vga_load_bmp_palette(const uint8_t *bmp, uint32_t pal_off) {
    outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        uint8_t b = bmp[pal_off + i * 4 + 0];
        uint8_t g = bmp[pal_off + i * 4 + 1];
        uint8_t r = bmp[pal_off + i * 4 + 2];
        outb(0x3C9, r >> 2);
        outb(0x3C9, g >> 2);
        outb(0x3C9, b >> 2);
    }
}

/* ---------------- BMP renderer ---------------- */

int bmp_draw_mode13(const char *name) {
    static uint8_t file[BMP_MAX_FILE];

    int size = fs_read_file(name, file, BMP_MAX_FILE);
    if (size < 54) return -1;

    if (file[0] != 'B' || file[1] != 'M') return -1;

    uint32_t data_off = rd32(file + 10);
    uint32_t hdr_sz   = rd32(file + 14);
    int32_t  w        = (int32_t)rd32(file + 18);
    int32_t  h        = (int32_t)rd32(file + 22);
    uint16_t bpp      = rd16(file + 28);
    uint32_t comp     = rd32(file + 30);

    if (comp != 0 || w <= 0) return -1;

    int top_down = 0;
    if (h < 0) { top_down = 1; h = -h; }

    /* Switch to VGA */
    vga_set_mode13();
    vga_clear_mode13(0);

    if (bpp == 8) {
        uint32_t pal_off = 14 + hdr_sz;
        vga_load_bmp_palette(file, pal_off);
    } else {
        vga_set_6x6x6_palette();
    }

    uint32_t row_bytes;
    if (bpp == 24) row_bytes = (w * 3 + 3) & ~3U;
    else if (bpp == 32) row_bytes = w * 4;
    else if (bpp == 8) row_bytes = (w + 3) & ~3U;
    else return -1;

    for (int y = 0; y < VGA_H; y++) {
        int sy = (y * h) / VGA_H;
        int row = top_down ? sy : (h - 1 - sy);

        for (int x = 0; x < VGA_W; x++) {
            int sx = (x * w) / VGA_W;
            uint8_t col = 0;

            uint32_t p = data_off + row * row_bytes;

            if (bpp == 8) {
                col = file[p + sx];
            } else {
                uint8_t r, g, b;
                if (bpp == 24) {
                    uint32_t o = p + sx * 3;
                    b = file[o + 0];
                    g = file[o + 1];
                    r = file[o + 2];
                } else {
                    uint32_t o = p + sx * 4;
                    b = file[o + 0];
                    g = file[o + 1];
                    r = file[o + 2];
                }
                int r6 = (r * 5) / 255;
                int g6 = (g * 5) / 255;
                int b6 = (b * 5) / 255;
                col = r6 * 36 + g6 * 6 + b6;
            }

            VGA_FB[y * VGA_W + x] = col;
        }
    }

    return 0;
}
