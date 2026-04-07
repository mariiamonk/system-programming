  DEFAULT REL
  SECTION .text

;------------------------------------------------------------------------------
; VOID
; EFIAPI
; JumpToUefiKernel (
;   VOID *KernelStart,            // rcx
;   VOID *KernelBootParams        // rdx
;   );
;------------------------------------------------------------------------------
global ASM_PFX(JumpToUefiKernel)
ASM_PFX(JumpToUefiKernel):

    mov     rax, 0x36d76289        ; Multiboot2 magic number
    mov     rbx, rdx               ; Указатель на структуру multiboot2

    jmp     rcx
