; hello.asm - simple position-independent ELF32 program for this toy kernel
; Uses int 0x80 syscall: EAX=1 -> print NUL-terminated string at EBX

BITS 32

section .text
global _start
_start:
    ; get current EIP into ESI (call/pop trick)
    call get_eip
get_eip:
    pop esi

    ; compute pointer to msg and put it in EBX
    mov ebx, esi
    add ebx, msg - get_eip

    ; syscall 1 = print string
    mov eax, 1
    int 0x80

    ; return to caller (fs_run calls entry as a function)
    ret

section .data
align 4
msg: db "Hello from hello.asm!", 0

