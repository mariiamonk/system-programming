; NASM, x86_64, Multiboot 2
extern kernel_main

section .multiboot_header
header_start:
    dd 0xE85250D6                                       ; Магическое число Multiboot 2
    dd 0                                                ; Архитектура (0 = x86)
    dd header_end - header_start                        ; Длина заголовка
    dd -(0xE85250D6 + 0 + (header_end - header_start))  ; Контрольная сумма

    ; Framebuffer
    align 8
    dw 5                   ; Тип тега
    dw 1                   ; Флаги (обязательный)
    dd 20                  ; Размер тега
    dd 1024                ; Ширина
    dd 768                 ; Высота
    dd 32                  ; Глубина цвета (bpp)

    ; EFI boot services tag
    align 8
    dw 7                   ; Тип тега
    dw 0                   ; Флаги (обязательный)
    dd 8                   ; Размер тега

    ; EFI amd64 entry address
    align 8
    dw 9                   ; Тип тега
    dw 1                   ; Флаги (обязательный)
    dd 12                  ; Размер тега
    dd _start              ; entry address

    ; Конец заголовка
    align 8
    dw 0    ; Тип тега: конец
    dw 0    ; Флаги
    dd 8    ; Размер
header_end:

section .text
global _start

_start:
    mov rdi, rax           ; Указатель на структуру Multiboot (2-й аргумент)
    mov rsi, rbx           ; Магическое число Multiboot (1-й аргумент)

    call kernel_main       ; Переход в ядро на C
    hlt

section .bss
align 16
stack_bottom:
    resb 16384             ; 16 КБ стека
stack_top:
