/* vga_mode13.c - VGA mode switching between text and graphics modes
 * Mode 13h (320x200x256) and 80x25 text mode
 */

#include "vga_mode13.h"
#include <stdint.h>
#include "io.h"

/* port I/O */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Mode 13h register values */
static const uint8_t seq_regs[5] = { 0x03, 0x01, 0x0F, 0x00, 0x0E };
static const uint8_t crtc_regs[25] = {
    0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0x41,0x00,0x00,0x00,0x00,0x9C,0x0E,0x8F,0x28,0x40,0x96,0xB9,0xA3,0xFF,0x00,0x00
};
static const uint8_t gc_regs[9] = { 0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF };
static const uint8_t ac_regs[21] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x41,0x00,0x0F,0x00,0x00
};

/* Text mode register values (80x25) */
static const uint8_t text_seq_regs[5] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
static const uint8_t text_crtc_regs[25] = {
    0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,0x00,0x4F,0x0D,0x0E,0x00,0x00,0x00,0x00,
    0x9C,0x0E,0x8F,0x28,0x1F,0x96,0xB9,0xA3,0xFF
};
static const uint8_t text_gc_regs[9] = { 0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF };
static const uint8_t text_ac_regs[21] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x0C,0x00,0x0F,0x08,0x00
};

/* write registers to VGA */
static void write_regs_mode13(void) {
    int i;
    /* misc output */
    outb(0x3C2, 0x63);

    /* sequencer */
    for (i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, seq_regs[i]);
    }

    /* unlock CRTC registers */
    outb(0x3D4, 0x03);
    outb(0x3D5, inb(0x3D5) | 0x80);
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & ~0x80);

    /* CRTC */
    for (i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc_regs[i]);
    }

    /* graphics controller */
    for (i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, gc_regs[i]);
    }

    /* attribute controller */
    for (i = 0; i < 21; i++) {
        (void)inb(0x3DA); /* reset flip-flop */
        outb(0x3C0, i);
        outb(0x3C0, ac_regs[i]);
    }

    /* enable video */
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
}

static void write_regs_text(void) {
    int i;
    /* misc output */
    outb(0x3C2, 0x67);

    /* sequencer */
    for (i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, text_seq_regs[i]);
    }

    /* unlock CRTC registers */
    outb(0x3D4, 0x03);
    outb(0x3D5, inb(0x3D5) | 0x80);
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & ~0x80);

    /* CRTC */
    for (i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, text_crtc_regs[i]);
    }

    /* graphics controller */
    for (i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, text_gc_regs[i]);
    }

    /* attribute controller */
    for (i = 0; i < 21; i++) {
        (void)inb(0x3DA);
        outb(0x3C0, i);
        outb(0x3C0, text_ac_regs[i]);
    }

    /* enable video */
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
}

void vga_set_mode13(void) {
    /* disable blinking cursor */
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
    
    write_regs_mode13();
}

void vga_set_text_mode(void) {
    /* Use BIOS interrupt to reliably return to text mode */
    __asm__ volatile (
        "int $0x10"
        :
        : "a"((uint16_t)0x0003)  /* AH=0 (set mode), AL=3 (80x25 text) */
        : "memory"
    );
}

void vga_set_palette_default(void) {
    /* program palette: 6x6x6 color cube for indices 0..215, then 216..255 grayscale */
    outb(0x3C8, 0); /* start index */
    for (int r = 0; r < 6; r++) for (int g = 0; g < 6; g++) for (int b = 0; b < 6; b++) {
        uint8_t vr = (uint8_t)((r * 255) / 5);
        uint8_t vg = (uint8_t)((g * 255) / 5);
        uint8_t vb = (uint8_t)((b * 255) / 5);
        /* DAC expects 0..63 */
        outb(0x3C9, vr >> 2);
        outb(0x3C9, vg >> 2);
        outb(0x3C9, vb >> 2);
    }
    /* grayscale ramp */
    for (int i = 216; i < 256; i++) {
        uint8_t v = (uint8_t)(((i - 216) * 255) / (256 - 216 - 1));
        outb(0x3C9, v >> 2);
        outb(0x3C9, v >> 2);
        outb(0x3C9, v >> 2);
    }
}

void vga_putpixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return;
    volatile uint8_t *fb = (volatile uint8_t*)0xA0000;
    fb[y * 320 + x] = color;
}

void vga_clear_mode13(uint8_t color) {
    volatile uint8_t *fb = (volatile uint8_t*)0xA0000;
    for (int i = 0; i < 320*200; i++) fb[i] = color;
}


void vga_clear_screen(void) {
    /* Clear text screen by writing spaces to video memory */
    volatile uint16_t *vram = (volatile uint16_t*)0xB8000;
    for (int i = 0; i < 80*25; i++) {
        vram[i] = 0x0720; /* white on black space */
    }
    vga_set_cursor(0, 0);
}