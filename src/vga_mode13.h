#ifndef VGA_MODE13_H
#define VGA_MODE13_H

#include <stdint.h>

/* Set VGA to 320x200x256 mode (mode 13h) */
void vga_set_mode13(void);

/* Set VGA back to 80x25 text mode */
void vga_set_text_mode(void);

/* Set default 6x6x6 color palette */
void vga_set_palette_default(void);

/* Draw a pixel at (x,y) with color */
void vga_putpixel(int x, int y, uint8_t color);

/* Clear entire mode13 screen with color */
void vga_clear_mode13(uint8_t color);

/* Clear text mode screen */
void vga_clear_screen(void);

#endif