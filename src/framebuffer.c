#include "framebuffer.h"
#include <stdint.h>
#include "io.h"

/* Minimal multiboot info struct (multiboot v1 layout) */
typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode;
    uint32_t vbe_interface_seg;
    uint32_t vbe_interface_off;
    uint32_t vbe_interface_len;
} multiboot_info_t;

/* Runtime framebuffer state */
static volatile uint8_t *fb_ptr = 0;
static uint32_t fb_w = 0, fb_h = 0, fb_pitch_bytes = 0, fb_bpp = 0;
static int fb_is_available = 0;

void fb_init(uint32_t magic, uint32_t addr) {
    fb_is_available = 0;
    fb_ptr = 0; fb_w = fb_h = fb_pitch_bytes = fb_bpp = 0;

    /* Only proceed when multiboot magic is correct and addr is non-zero */
    if (magic != 0x2BADB002 || addr == 0) return;

    multiboot_info_t *mb = (multiboot_info_t*) (uintptr_t)addr;

    /* If the bootloader provided VBE information, vbe_mode will be non-zero.
     * Many GRUB installations will set the video mode (via gfxmode/gfxpayload)
     * and populate the VBE fields so the kernel can use the linear framebuffer.
     * Here we only detect availability and try to read a few useful fields from
     * the VBE Mode Info if present.
     */
    if (mb->vbe_mode != 0 && mb->vbe_control_info != 0) {
        /* In many setups, GRUB provides a pointer to the VBE ModeInfoBlock via
         * the vbe_control_info or through other means. Unfortunately multiboot1
         * doesn't standardize a direct ModeInfo pointer; full support requires
         * either calling BIOS (real mode) or using Multiboot2 framebuffer data.
         *
         * For now, we'll check common GRUB behavior: GRUB often sets the linear
         * framebuffer pointer in the VBE Mode Info block. Some builds also
         * export a "physbase" pointer at an offset inside the control info.
         * Because layout varies and we must remain safe, we cautiously attempt
         * a few candidate offsets and validate the pointer range.
         */
        uint8_t *ctrl = (uint8_t*)(uintptr_t)mb->vbe_control_info;
        /* Candidate offsets (best-effort): 0x0C or 0x0A depending on VBE version.
         * We'll try 0x0C first (common for ModeInfoBlock.PhysBasePtr).
         */
        uint32_t phys_ptr = 0;
        uint32_t *p = (uint32_t*)(ctrl + 0x0C);
        phys_ptr = p ? *p : 0;

        if (phys_ptr == 0) {
            /* fallback: try reading a 32-bit at 0x10 */
            p = (uint32_t*)(ctrl + 0x10);
            phys_ptr = p ? *p : 0;
        }

        if (phys_ptr != 0) {
            /* We won't validate the whole memory map here. Instead, assume the
             * machine/VM maps the framebuffer into our address space at that
             * physical address (typical for QEMU/GRUB setups). We'll also try
             * to read a few other guessable fields such as width/height/bpp
             * from neighboring offsets — this is heuristic and may not work on
             * all platforms. When it fails, the kernel will fall back to mode13.
             */
            fb_ptr = (volatile uint8_t*)(uintptr_t)phys_ptr;

            /* Try heuristically to read width/height/bytes_per_scanline and bpp
             * from offsets around the ModeInfoBlock. Common offsets:
             * 0x12: XResolution (word), 0x14: YResolution (word),
             * 0x16: XCharSize... but layouts vary.
             */
            uint16_t *w16 = (uint16_t*)(ctrl + 0x12);
            uint16_t *h16 = (uint16_t*)(ctrl + 0x14);
            uint16_t *bpp16 = (uint16_t*)(ctrl + 0x1C);
            uint16_t *pitch16 = (uint16_t*)(ctrl + 0x10);

            if (w16 && h16 && *w16 != 0 && *h16 != 0) {
                fb_w = *w16;
                fb_h = *h16;
            }
            if (pitch16 && *pitch16 != 0) {
                fb_pitch_bytes = *pitch16;
            }
            if (bpp16 && *bpp16 != 0) fb_bpp = *bpp16;

            /* Basic validation: width/height reasonable
             * If validation passes, mark available. Otherwise clear values.
             */
            if (fb_ptr != 0 && fb_w >= 320 && fb_h >= 200 && (fb_bpp == 32 || fb_bpp == 24 || fb_bpp == 24)) {
                fb_is_available = 1;
            } else {
                /* Clear heuristic fields to avoid accidental misuse */
                fb_w = fb_h = fb_pitch_bytes = fb_bpp = 0;
                /* still mark fb_ptr non-null so a more advanced parser can use it */
                fb_is_available = 0;
            }
        }
    }
}

int fb_available(void) { return fb_is_available; }
uint32_t fb_width(void) { return fb_w; }
uint32_t fb_height(void) { return fb_h; }
uint32_t fb_pitch(void) { return fb_pitch_bytes; }
uint32_t fb_bpp(void) { return fb_bpp; }

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_is_available) return;
    if (x >= fb_w || y >= fb_h) return;
    uint8_t *p = (uint8_t*)fb_ptr + (uint64_t)y * fb_pitch_bytes + x * (fb_bpp/8);
    if (fb_bpp == 32) {
        /* assume XRGB or RGB in memory: store as 0x00RRGGBB */
        uint32_t val = (color & 0x00FFFFFF);
        *((uint32_t*)p) = val;
    } else if (fb_bpp == 24) {
        p[0] = (uint8_t)(color & 0xFF);
        p[1] = (uint8_t)((color >> 8) & 0xFF);
        p[2] = (uint8_t)((color >> 16) & 0xFF);
    }
}

void fb_clear(uint32_t color) {
    if (!fb_is_available) return;
    for (uint32_t y = 0; y < fb_h; y++) {
        for (uint32_t x = 0; x < fb_w; x++) fb_putpixel(x,y,color);
    }
}

void fb_status(char *buf, int buflen) {
    if (!buf || buflen <= 0) return;
    if (fb_is_available) {
        /* Compose a short status string */
        int n = 0;
        const char *t = "FB: available ";
        while (*t && n < buflen-1) buf[n++] = *t++;
        /* append dims as WxHxBPP */
        /* rudimentary integer to string for width */
        char tmp[48]; int m = 0;
        /* width */
        uint32_t v = fb_w;
        int start = m;
        if (v == 0) tmp[m++] = '0';
        else {
            char rev[16]; int r = 0;
            while (v > 0 && r < (int)sizeof(rev)) { rev[r++] = '0' + (v % 10); v /= 10; }
            while (r-- > 0) tmp[m++] = rev[r];
        }
        tmp[m++] = 'x';
        /* height */
        v = fb_h;
        if (v == 0) tmp[m++] = '0'; else { char rev[16]; int r = 0; while (v > 0 && r < (int)sizeof(rev)) { rev[r++] = '0' + (v % 10); v /= 10; } while (r-- > 0) tmp[m++] = rev[r]; }
        tmp[m++] = 'x';
        /* bpp */
        v = fb_bpp;
        if (v == 0) tmp[m++] = '0'; else { char rev[8]; int r = 0; while (v > 0 && r < (int)sizeof(rev)) { rev[r++] = '0' + (v % 10); v /= 10; } while (r-- > 0) tmp[m++] = rev[r]; }
        tmp[m] = '\0';
        /* copy tmp into buf */
        int i = 0;
        while (tmp[i] && n < buflen-1) { buf[n++] = tmp[i++]; }
        buf[n] = '\0';
    } else {
        const char *t = "FB: unavailable (using mode13)";
        int i = 0;
        while (t[i] && i < buflen-1) { buf[i] = t[i]; i++; }
        buf[i] = '\0';
    }
}
