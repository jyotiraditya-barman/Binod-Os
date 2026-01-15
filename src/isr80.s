    .section .text
    .globl isr80_stub
isr80_stub:
    /* Save general registers */
    pushal
    /* Pass pointer to regs (current esp) to C handler */
    movl %esp, %eax
    pushl %eax
    call isr80_handler
    addl $4, %esp
    /* Restore registers and return from interrupt */
    popal
    iret
