[BITS 32]

; hello_color.asm - demo program that uses new syscalls to set color/cursor and print
; Syscalls:
;  EAX=1, EBX=ptr -> print NUL-terminated string
;  EAX=2, EBX=ptr, ECX=len -> write buffer
;  EAX=4, EBX=fg, ECX=bg -> set color
;  EAX=5, EBX=x, ECX=y -> set cursor
;  EAX=7 -> clear screen

global _start
section .text
_start:
    call .L_eip
.L_eip:
    pop esi
    ; make esi point to start of our data (msg)
    add esi, msg - .L_eip

    ; clear screen
    mov eax, 7
    int 0x80

    ; set color: yellow (14) on blue (1)
    mov eax, 4
    mov ebx, 14
    mov ecx, 1
    int 0x80

    ; set cursor to 0,0
    mov eax, 5
    mov ebx, 0
    mov ecx, 0
    int 0x80

    ; print greeting (syscall 1) - EBX = pointer to NUL string
    mov eax, 1
    mov ebx, esi
    int 0x80

    ; print second message using write (syscall 2): EBX=ptr, ECX=len
    mov eax, 2
    mov ebx, esi
    add ebx, msg2 - msg
    mov ecx, msg2_len
    int 0x80

    ret

section .data
msg:    db "Hello World! (colored via syscall)", 0
msg2:   db 0x0A, "This is line two using syscall 2.", 0x0A, 0
msg2_len: equ $ - msg2
