/* ascii_ray.c - VGA TEXT MODE ASCII RAYCASTER (no syscalls) */

#include <stdint.h>

#define VGA_MEM  ((uint16_t*)0xB8000)
#define SCREEN_W 80
#define SCREEN_H 25

#define MAP_W 8
#define MAP_H 3
#define FOV    1.0f
#define DEPTH 16.0f

/* tiny map */
static char mapData[MAP_H][MAP_W+1] = {
    "########",
    "#   D  #",
    "########"
};

float playerX = 4.0f;
float playerY = 1.5f;
float playerA = 0.0f;

/* small sine/cosine approximations */
static float fsin(float x) {
    const float PI = 3.1415926535f;
    const float TWO = 6.283185307f;
    while (x > PI) x -= TWO;
    while (x < -PI) x += TWO;
    return x * (1 - x*x/6 + x*x*x*x/120);
}
static float fcos(float x) { return fsin(x + 1.57079632679f); }

/* Write ASCII char + color to 80×25 text VGA */
static void putChar(int x, int y, char c, uint8_t color) {
    VGA_MEM[y * SCREEN_W + x] = (color << 8) | c;
}

/* Clear screen */
static void clear(uint8_t col) {
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        VGA_MEM[i] = (col << 8) | ' ';
}

/* Render one ASCII frame */
static void render() {
    for (int x = 0; x < SCREEN_W; x++) {

        float rayA = (playerA - FOV/2.0f) + ((float)x / SCREEN_W) * FOV;
        float dist = 0.0f;
        char hit = ' ';

        float eyeX = fcos(rayA);
        float eyeY = fsin(rayA);

        while (dist < DEPTH && hit == ' ') {
            dist += 0.1f;
            int tx = (int)(playerX + eyeX * dist);
            int ty = (int)(playerY + eyeY * dist);

            if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) {
                hit = '#';
                dist = DEPTH;
            } else {
                if (mapData[ty][tx] != ' ')
                    hit = mapData[ty][tx];
            }
        }

        int ceiling = (SCREEN_H/2) - (float)SCREEN_H / dist;
        if (ceiling < 0) ceiling = 0;
        int floor = SCREEN_H - ceiling;

        for (int y = 0; y < SCREEN_H; y++) {
            if (y < ceiling) {
                putChar(x, y, ' ', 1);
            } else if (y < floor) {

                /* ASCII shading based on distance */
                char shade;
                if (dist <= DEPTH / 4) shade = '#';
                else if (dist <= DEPTH / 3) shade = 'O';
                else if (dist <= DEPTH / 2) shade = 'o';
                else if (dist <= DEPTH) shade = '.';
                else shade = ' ';

                putChar(x, y, shade, 15);

            } else {
                putChar(x, y, '.', 8);
            }
        }
    }
}

/* main entry */
void entry() {
    clear(0);

    for (;;) {
        playerA += 0.05f;
        if (playerA > 6.28f) playerA -= 6.28f;

        render();

        /* simple stall */
       // for (volatile int i = 0; i < 2000000; i++) { }
    }
}
