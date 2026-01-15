ENTRY(start)

SECTIONS
{
  . = 0x00100000; /* load kernel at 1MB */

  .text : {
    *(.multiboot)
    *(.text*)
  }

  .rodata : { *(.rodata*) }

  .data : { *(.data*) }

  .bss : {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  }
}
