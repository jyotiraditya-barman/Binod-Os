#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

/* Initialize framebuffer subsystem from multiboot magic & addr
 * magic should be 0x2BADB002 for multiboot; addr is pointer to multiboot info
 */
void fb_init(uint32_t magic, uint32_t addr);

/* Query availability */
int fb_available(void);

/* Dimensions and pitch (bytes per scanline) */
uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_pitch(void);
uint32_t fb_bpp(void);

/* Draw functions (24/32-bit) */
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color); /* color: 0xRRGGBB */
void fb_clear(uint32_t color);

/* Return human-readable status (for diagnostics). Buffer must be >= 64 bytes */
void fb_status(char *buf, int buflen);

#endif
