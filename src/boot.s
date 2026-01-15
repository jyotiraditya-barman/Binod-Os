/* boot.s - multiboot header and jump to kernel_main */
    .section .multiboot
    .align 4
    .long 0x1BADB002      /* magic */
    .long 0x0             /* flags (no mods) */
    .long -(0x1BADB002)   /* checksum */

    .text
    .global start
    .extern kernel_main

start:
    cli
    /* Set up stack (simple) */
    mov $0x90000, %esp

    /* Call kernel_main with multiboot magic & addr as passed by the bootloader
     * According to the Multiboot spec: on entry EAX = magic, EBX = pointer to
     * multiboot information. We must forward those registers to kernel_main so
     * the kernel can examine bootloader-provided data (framebuffer, modules, etc.).
     */
    pushl %ebx   /* push multiboot info addr */
    pushl %eax   /* push multiboot magic */
    call kernel_main

hang:
    hlt
    jmp hang

