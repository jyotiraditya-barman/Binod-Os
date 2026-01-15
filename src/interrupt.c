#include "interrupt.h"
#include "io.h"
#include "vga_mode13.h"
#include <stdint.h>

/* IDT entry (8 bytes) */
struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void isr80_stub(void);

static void idt_set_gate(int n, uint32_t handler, uint16_t sel, uint8_t flags) {
    idt[n].base_lo = handler & 0xFFFF;
    idt[n].sel = sel;
    idt[n].always0 = 0;
    idt[n].flags = flags;
    idt[n].base_hi = (handler >> 16) & 0xFFFF;
}

/* load idt */
static inline void lidt(void *p) {
    asm volatile ("lidtl (%0)" : : "r" (p));
}

void idt_init(void) {
    /* zero IDT */
    for (int i = 0; i < 256; i++) {
        idt[i].base_lo = 0;
        idt[i].sel = 0;
        idt[i].always0 = 0;
        idt[i].flags = 0;
        idt[i].base_hi = 0;
    }
    /* set syscall vector 0x80, selector 0x08 (kernel code), flags 0x8E (present, DPL=0, 32-bit interrupt gate) */
    idt_set_gate(0x80, (uint32_t)isr80_stub, 0x08, 0x8E);

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;
    lidt(&idtp);
}

/* C handler called from isr80_stub. regs points to saved registers (pushad order). */
void isr80_handler(uint32_t *regs) {
     /* The assembly stub uses "pushal" which pushes registers in the
         following order (memory lowest->highest after the pushes):
            [0]=EDI, [1]=ESI, [2]=EBP, [3]=ESP (original), [4]=EBX,
            [5]=EDX, [6]=ECX, [7]=EAX

         We therefore map logical registers (0=EAX,1=ECX,2=EDX,3=EBX,
         4=ESP,5=EBP,6=ESI,7=EDI) to the fixed indexes above. This is
         deterministic and removes the previous heuristic rotation which
         caused incorrect register reads.
     */
     /* R(i) returns the logical register: 0->EAX,1->ECX,...,7->EDI */
     #define R(idx) regs[(7 - (idx))]
    uint32_t num = R(0);
    switch (num) {
        case 1: {
            /* syscall 1: print NUL-terminated string at EBX */
            const char *s = (const char*)R(3); /* EBX */
            if (s) printf_k("%s", s);
            R(0) = 0; /* return 0 in EAX */
            break;
        }
        case 2: {
            /* syscall 2: write buffer of length ECX from EBX (not fd-aware) */
            const char *buf = (const char*)R(3); /* EBX */
            uint32_t len = R(1); /* ECX */
            uint32_t written = 0;
            if (buf && len > 0) {
                for (uint32_t i = 0; i < len; i++) {
                    char c = buf[i];
                    putc_k(c);
                    written++;
                }
            }
            R(0) = written; /* return number of bytes written */
            break;
        }
        case 3: {
            /* syscall 3: read a line into buffer at EBX, up to ECX-1 bytes; returns length */
            char *buf = (char*)R(3); /* EBX */
            int max = (int)R(1); /* ECX */
            if (!buf || max <= 0) {
                regs[0] = 0;
                break;
            }
            int n = readline(buf, max);
            R(0) = (uint32_t)n;
            break;
        }
        case 4: {
            /* syscall 4: set color; EBX = fg (0-15), ECX = bg (0-15) */
            uint32_t fg = R(3);
            uint32_t bg = R(1);
            if (fg <= 15 && bg <= 15) vga_set_color((uint8_t)fg, (uint8_t)bg);
            R(0) = 0;
            break;
        }
        case 5: {
            /* syscall 5: set cursor position; EBX = x, ECX = y */
            int x = (int)R(3);
            int y = (int)R(1);
            vga_set_cursor(x, y);
            R(0) = 0;
            break;
        }
        case 6: {
            /* syscall 6: get cursor position; EBX = ptr to two ints [x,y] */
            int *out = (int*)R(3);
            if (out) {
                int x, y; vga_get_cursor(&x, &y);
                out[0] = x; out[1] = y;
                R(0) = 0;
            } else {
                R(0) = (uint32_t)-1;
            }
            break;
        }
        case 7: {
            /* syscall 7: clear screen */
            clrscr();
            R(0) = 0;
            break;
        }
        case 8: {
            /* syscall 8: switch to VGA mode13 */
            vga_set_mode13();
            R(0) = 0;
            break;
        }
        case 9: {
            /* syscall 9: putpixel in mode13; EBX=x, ECX=y, EDX=color */
            int x = (int)R(3);
            int y = (int)R(1);
            int color = (int)R(2);
            vga_putpixel(x, y, (uint8_t)color);
            R(0) = 0;
            break;
        }
        case 10: {
            /* syscall 10: set default mode13 palette */
            vga_set_palette_default();
            R(0) = 0;
            break;
        }
        case 11: {
            /* syscall 11: clear mode13 with color in EBX */
            int color = (int)R(3);
            vga_clear_mode13((uint8_t)color);
            R(0) = 0;
            break;
        }
        case 12: {
            /* syscall 12: switch back to text mode */
            vga_set_text_mode();
            R(0) = 0;
            break;
        }
        default: {
            /* Helpful debug: print unsupported syscall number and register snapshot */
            printf_k("Unknown syscall %u\n", num);
            printf_k("regs: EAX=%x ECX=%x EDX=%x EBX=%x ESI=%x EDI=%x EBP=%x ESP=%x\n",
                     R(0), R(1), R(2), R(3), R(6), R(7), R(5), R(4));
            printf_k("Supported: 1=print,2=write,3=read,4=setcolor,5=setcursor,6=getcursor,7=clear\n");
            R(0) = (uint32_t)-1;
            break;
        }
    }
    /* cleanup helper macro */
    #undef R
}
