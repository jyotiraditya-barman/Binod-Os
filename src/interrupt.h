#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>

/* Initialize IDT and syscall handler */
void idt_init(void);

/* C entry called from ISR stub. 'regs' points to pushed registers (pushad order)
   regs[0] = EAX, regs[1] = ECX, regs[2] = EDX, regs[3] = EBX,
   regs[4] = ESP, regs[5] = EBP, regs[6] = ESI, regs[7] = EDI
*/
void isr80_handler(uint32_t *regs);

#endif
