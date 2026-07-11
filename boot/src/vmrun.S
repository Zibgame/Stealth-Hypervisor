.text
.globl svm_vmrun
.def svm_vmrun; .scl 2; .type 32; .endef

/*
 * Windows x64 ABI: first argument is RCX.
 * RCX points to the VMCB. VMRUN itself requires RAX = physical VMCB address.
 *
 * AMD SVM does not automatically load/store most general purpose registers
 * through the VMCB. The guest sees the live CPU GPRs. Keep the host ABI intact,
 * load guest GPRs before VMRUN, and save them back after VMEXIT.
 */
svm_vmrun:
    push %rbx
    push %rbp
    push %rsi
    push %rdi
    push %r12
    push %r13
    push %r14
    push %r15

    push %rcx                 /* saved VMCB pointer */
    mov  %rcx, %rax           /* VMRUN operand */

    mov  0x808(%rax), %rbx
    mov  0x818(%rax), %rbp
    mov  0x820(%rax), %rsi
    mov  0x828(%rax), %rdi
    mov  0x830(%rax), %r8
    mov  0x838(%rax), %r9
    mov  0x840(%rax), %r10
    mov  0x848(%rax), %r11
    mov  0x850(%rax), %r12
    mov  0x858(%rax), %r13
    mov  0x860(%rax), %r14
    mov  0x868(%rax), %r15
    mov  0x7F8(%rax), %rcx
    mov  0x800(%rax), %rdx

    .byte 0x0F, 0x01, 0xD8    /* VMRUN */

    mov  (%rsp), %rax         /* recover VMCB pointer */
    mov  %rbx, 0x808(%rax)
    mov  %rbp, 0x818(%rax)
    mov  %rsi, 0x820(%rax)
    mov  %rdi, 0x828(%rax)
    mov  %r8,  0x830(%rax)
    mov  %r9,  0x838(%rax)
    mov  %r10, 0x840(%rax)
    mov  %r11, 0x848(%rax)
    mov  %r12, 0x850(%rax)
    mov  %r13, 0x858(%rax)
    mov  %r14, 0x860(%rax)
    mov  %r15, 0x868(%rax)
    mov  %rcx, 0x7F8(%rax)
    mov  %rdx, 0x800(%rax)

    add  $8, %rsp
    pop  %r15
    pop  %r14
    pop  %r13
    pop  %r12
    pop  %rdi
    pop  %rsi
    pop  %rbp
    pop  %rbx
    ret
