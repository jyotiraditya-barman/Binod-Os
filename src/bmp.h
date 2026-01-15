/* bmp.h - simple BMP renderer for VGA text mode
 * Draws a BMP file from the filesystem onto the VGA text grid by
 * sampling/scaling the image into character cells and painting
 * each cell's background color to match the image.
 */
#ifndef BMP_H
#define BMP_H

#include <stdint.h>

int bmp_draw(const char *name, int left, int top);
int bmp_draw_mode13(const char *name);

#endif
