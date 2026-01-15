/* tetris.c -- freestanding 32-bit flat binary for your OS run() loader
   - Direct VGA text mode at 0xB8000 (80x25)
   - Polls keyboard ports (0x64/0x60) for basic keys (non-blocking)
   - Controls: a(left) d(right) s(down) w(rotate) q(quit)
   - Compile with i386 freestanding toolchain and add to disk image.
*/

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef int            i32;

#define SCREEN_COLS 80
#define SCREEN_ROWS 25
#define VGA_ADDR ((volatile u16*)0xB8000)
#define ATTR_NORMAL 0x07
#define ATTR_BLOCK  0x1F

/* Tetris constants */
#define A_WIDTH 10
#define A_HEIGHT 20
#define T_WIDTH 4
#define T_HEIGHT 4

/* tetrominoes (same order as you used) */
static const u8 tetrominoes[7][16] = {
    /* I */ {0,0,0,0, 1,1,1,1, 0,0,0,0, 0,0,0,0},
    /* O */ {0,0,0,0, 0,1,1,0, 0,1,1,0, 0,0,0,0},
    /* S */ {0,0,0,0, 0,0,1,1, 0,1,1,0, 0,0,0,0},
    /* Z */ {0,0,0,0, 1,1,0,0, 0,1,1,0, 0,0,0,0},
    /* T */ {0,0,0,0, 0,1,0,0, 1,1,1,0, 0,0,0,0},
    /* L */ {0,0,0,0, 1,0,0,0, 1,1,1,0, 0,0,0,0},
    /* J */ {0,0,0,0, 0,0,0,1, 1,1,1,0, 0,0,0,0}
};

/* arena */
static u8 arena[A_HEIGHT][A_WIDTH];

/* game state */
static u32 score = 0;
static int currTet = 0;
static int currRot = 0;
static int currX = 3;
static int currY = 0;
int gameOver = 0;

/* Simple PRNG (LFSR) so we have deterministic tetrominoes */
static u32 rng_state = 0xACE1u;
static u32 lfsr32(void){
    u32 l = rng_state;
    l ^= l << 13;
    l ^= l >> 17;
    l ^= l << 5;
    rng_state = l;
    return l;
}

/* port I/O */


/* Non-blocking keyboard: return 0 if none, else ASCII char for keys we handle */
static int kb_poll_key(void){
    if (!(inb(0x64) & 1)) return 0; /* no data */
    u8 sc = inb(0x60);
    /* ignore key releases (set top bit) */
    if (sc & 0x80) return 0;
    /* map scancodes (set 1) for letters and space */
    switch (sc) {
        case 0x1E: return 'a'; /* a */
        case 0x20: return 'd'; /* d */
        case 0x1F: return 's'; /* s */
        case 0x11: return 'w'; /* w */
        case 0x10: return 'q'; /* q */
        case 0x39: return ' '; /* space */
        default: return 0;
    }
}

/* small memory helpers */
static void memclr(void *p, u32 n){ u8 *b=(u8*)p; while(n--) *b++ = 0; }

/* rotate function identical to your rotate */
static int rotate_idx(int x, int y, int rot){
    switch (rot & 3){
        case 0: return x + y * T_WIDTH;
        case 1: return 12 + y - (x * T_WIDTH);
        case 2: return 15 - (y * T_WIDTH) - x;
        case 3: return 3 - y + (x * T_WIDTH);
    }
    return 0;
}

/* check if piece fits */
static int valid_pos(int tet, int rot, int posX, int posY){
    for (int x=0;x<T_WIDTH;x++){
        for (int y=0;y<T_HEIGHT;y++){
            int idx = rotate_idx(x,y,rot);
            if (tetrominoes[tet][idx] != 1) continue;
            int ax = posX + x;
            int ay = posY + y;
            if (ax < 0 || ax >= A_WIDTH || ay >= A_HEIGHT) return 0;
            if (ay >= 0 && arena[ay][ax]) return 0;
        }
    }
    return 1;
}

/* lock piece into arena */
static void lock_piece(void){
    for (int x=0;x<T_WIDTH;x++){
        for (int y=0;y<T_HEIGHT;y++){
            int idx = rotate_idx(x,y,currRot);
            if (tetrominoes[currTet][idx] != 1) continue;
            int ax = currX + x;
            int ay = currY + y;
            if (ay >= 0 && ay < A_HEIGHT && ax >=0 && ax < A_WIDTH)
                arena[ay][ax] = 1;
        }
    }
}

/* clear full lines */
static void clear_lines(void){
    int cleared = 0;
    for (int y=A_HEIGHT-1;y>=0;y--){
        int full = 1;
        for (int x=0;x<A_WIDTH;x++){
            if (!arena[y][x]) { full = 0; break; }
        }
        if (!full) continue;
        /* move everything above down */
        for (int yy=y; yy>0; yy--){
            for (int x=0;x<A_WIDTH;x++) arena[yy][x] = arena[yy-1][x];
        }
        for (int x=0;x<A_WIDTH;x++) arena[0][x]=0;
        cleared++;
        y++; /* re-check the same y (lines shifted) */
    }
    if (cleared) score += 100 * cleared;
}

/* spawn new tetromino */
static void new_piece(void){
    currTet = (int)(lfsr32() % 7);
    currRot = 0;
    currX = (A_WIDTH/2) - (T_WIDTH/2);
    currY = -1; /* start slightly above visible area */
    if (!valid_pos(currTet, currRot, currX, currY)){
        gameOver = 1;
    }
}

/* print to VGA text buffer at specified (row,col) with attribute */
static void vga_putch_at(int row, int col, char ch, u8 attr){
    if (row < 0 || row >= SCREEN_ROWS || col < 0 || col >= SCREEN_COLS) return;
    volatile u16 *vga = VGA_ADDR;
   // u16 val = ((u16)attr << 8) | (u8)ch;
    vga[row * SCREEN_COLS + col] = (10 << 8) | ch;
}

/* draw the whole playfield to VGA text area */
static void draw_all(void){
    /* position top-left of arena on screen */
    const int start_row = 1;
    const int start_col = 10;
    /* clear area */
    for (int r=0;r<SCREEN_ROWS;r++){
        for (int c=0;c<SCREEN_COLS;c++){
            vga_putch_at(r,c,' ', ATTR_NORMAL);
        }
    }

    /* draw border */
    for (int y=0;y<=A_HEIGHT+1;y++){
        vga_putch_at(start_row + y, start_col - 1, '|', ATTR_NORMAL);
        vga_putch_at(start_row + y, start_col + A_WIDTH, '|', ATTR_NORMAL);
    }
    /* draw arena */
    for (int y=0;y<A_HEIGHT;y++){
        for (int x=0;x<A_WIDTH;x++){
            char ch = arena[y][x] ? '#' : ' ';
            u8 attr = arena[y][x] ? ATTR_BLOCK : ATTR_NORMAL;
            vga_putch_at(start_row + 1 + y, start_col + x, ch, attr);
        }
    }
    /* draw current piece on top (so it appears falling) */
    for (int x=0;x<T_WIDTH;x++){
        for (int y=0;y<T_HEIGHT;y++){
            int idx = rotate_idx(x,y,currRot);
            if (tetrominoes[currTet][idx] != 1) continue;
            int ax = currX + x;
            int ay = currY + y;
            if (ay >= 0 && ay < A_HEIGHT && ax >=0 && ax < A_WIDTH){
                vga_putch_at(start_row + 1 + ay, start_col + ax, '#', ATTR_BLOCK);
            }
        }
    }
    /* score */
    const char *label = "Score:";
    int sr = start_row;
    int sc = start_col + A_WIDTH + 3;
    for (int i=0; label[i]; i++) vga_putch_at(sr, sc+i, label[i], ATTR_NORMAL);

    /* print numeric score (small routine) */
    char numbuf[12]; int ni=0;
    u32 t = score;
    if (t==0) numbuf[ni++]='0';
    else {
        char tmp[12]; int ti=0;
        while (t){ tmp[ti++]= '0' + (t%10); t/=10; }
        while (ti--) numbuf[ni++]=tmp[ti];
    }
    for (int i=0;i<ni;i++) vga_putch_at(sr+1, sc+i, numbuf[i], ATTR_NORMAL);
}

/* tiny sleep-ish busy wait (calibrated on simple loops) */
static void delay_ms(int ms){
    volatile u32 cnt = (u32)(ms * 20000);
    while (cnt--) { __asm__ volatile("nop"); }
}

/* main */
void tetris(void){
   /* init */
    memclr(arena, sizeof(arena));
    new_piece();

    /* draw initial */
    draw_all();

    /* timing variables */
    int tick_ms = 1500; /* gravity interval (ms) */
    int frame_ms = 25; /* redraw/input poll interval */

    int quit = 0;
    gameOver=0;
    while (!gameOver && !quit){
        /* poll input several times during tick */
        int elapsed = 0;
        while (elapsed < tick_ms){
            int k = kb_poll_key();
            if (k){
                if (k == 'q') { quit = 1; break; }
                else if (k == 'a'){
                    if (valid_pos(currTet, currRot, currX - 1, currY)) currX--;
                } else if (k == 'd'){
                    if (valid_pos(currTet, currRot, currX + 1, currY)) currX++;
                } else if (k == 's'){
                    if (valid_pos(currTet, currRot, currX, currY + 1)) currY++;
                } else if (k == 'w'){
                    int nr = (currRot + 1) & 3;
                    if (valid_pos(currTet, nr, currX, currY)) currRot = nr;
                }
                draw_all();
            }
            delay_ms(frame_ms);
            elapsed += frame_ms;
        }

        /* gravity step */
        if (valid_pos(currTet, currRot, currX, currY + 1)){
            currY++;
        } else {
            /* cannot move down: lock piece and new piece */
            lock_piece();
            clear_lines();
            new_piece();
        }
        draw_all();
    }

    /* game over message */
    const char *gm = "GAME OVER - press q to exit or space to restart";
    int gr = 12, gc = 10;
    for (int i=0; gm[i]; i++) vga_putch_at(gr, gc+i, gm[i], ATTR_NORMAL);

    /* wait until user presses q to return to kernel */
    while (1){
        int k = kb_poll_key();
        if (k == 'q') break;
        else if (k == ' ') tetris();
    }

    /* return to kernel (ret) */
  
}
