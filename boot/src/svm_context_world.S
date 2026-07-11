.text

.extern svm_prepare_cpu_context
.extern svm_handle_guest_exit
.extern svm_host_exception_fatal

.macro HOST_EXCEPTION_NO_ERROR name, vector
.globl \name
.def \name; .scl 2; .type 32; .endef
\name:
    pushq $0
    pushq $\vector
    jmp .Lhost_exception_common
.endm

.macro HOST_EXCEPTION_ERROR name, vector
.globl \name
.def \name; .scl 2; .type 32; .endef
\name:
    pushq $\vector
    jmp .Lhost_exception_common
.endm

HOST_EXCEPTION_NO_ERROR svm_host_exception_2, 2
HOST_EXCEPTION_ERROR    svm_host_exception_8, 8
HOST_EXCEPTION_ERROR    svm_host_exception_13, 13
HOST_EXCEPTION_ERROR    svm_host_exception_14, 14
HOST_EXCEPTION_NO_ERROR svm_host_exception_18, 18

.Lhost_exception_common:
    mov 0x00(%rsp), %rcx
    mov 0x08(%rsp), %rdx
    mov 0x10(%rsp), %r8
    mov %cr2, %r9
    and $-16, %rsp
    sub $0x20, %rsp
    call svm_host_exception_fatal
    cli
.Lhost_exception_halt:
    hlt
    jmp .Lhost_exception_halt

.globl svm_enter_context_resume
.def svm_enter_context_resume; .scl 2; .type 32; .endef

/*
 * Microsoft x64 ABI. RCX = HV_CPU *.
 *
 * This function never returns in host mode. svm_prepare_cpu_context captures a guest
 * continuation at .Lguest_context_resume. The first VMRUN resumes there and the
 * function returns 1 to its original UEFI caller, now entirely in guest mode.
 */
svm_enter_context_resume:
    /* Preserve the guest GPR image before calling C. */
    mov %rbx, 0x10(%rcx)
    mov %rdx, 0x20(%rcx)
    mov %rbp, 0x28(%rcx)
    mov %rsi, 0x30(%rcx)
    mov %rdi, 0x38(%rcx)
    mov %r8,  0x40(%rcx)
    mov %r9,  0x48(%rcx)
    mov %r10, 0x50(%rcx)
    mov %r11, 0x58(%rcx)
    mov %r12, 0x60(%rcx)
    mov %r13, 0x68(%rcx)
    mov %r14, 0x70(%rcx)
    mov %r15, 0x78(%rcx)
    mov %rcx, 0x18(%rcx)

    mov %rsp, %rdx
    lea .Lguest_context_resume(%rip), %r8
    sub $0x28, %rsp
    call svm_prepare_cpu_context
    add $0x28, %rsp

    /* Move permanently to the CPU's private host stack. */
    mov 0x08(%rax), %rsp
    and $-16, %rsp
    sub $8, %rsp
    mov %rax, (%rsp)

.Lsvm_context_run:
    /* Preserve host FPU/SSE, then load guest-only state and all live GPRs. */
    mov (%rsp), %rax
    fxsave64 0x2b0(%rax)
    mov (%rsp), %rax
    fxrstor64 0xb0(%rax)
    mov (%rsp), %rax
    mov (%rax), %rax
    .byte 0x0f, 0x01, 0xda       /* VMLOAD RAX */

    mov (%rsp), %rax
    mov 0x10(%rax), %rbx
    mov 0x18(%rax), %rcx
    mov 0x20(%rax), %rdx
    mov 0x28(%rax), %rbp
    mov 0x30(%rax), %rsi
    mov 0x38(%rax), %rdi
    mov 0x40(%rax), %r8
    mov 0x48(%rax), %r9
    mov 0x50(%rax), %r10
    mov 0x58(%rax), %r11
    mov 0x60(%rax), %r12
    mov 0x68(%rax), %r13
    mov 0x70(%rax), %r14
    mov 0x78(%rax), %r15
    mov (%rsp), %rax
    mov (%rax), %rax
    .byte 0x0f, 0x01, 0xd8       /* VMRUN RAX */
    /* VMRUN sets GIF on entry and #VMEXIT clears it; no STGI/CLGI is needed. */
    /* RAX is the guest RAX after VMEXIT. Reload the VMCB PA for VMSAVE. */
    mov (%rsp), %rax
    mov (%rax), %rax
    .byte 0x0f, 0x01, 0xdb       /* VMSAVE RAX */

    /* Restore host-only FS/GS/TR/syscall state before entering C. */
    mov (%rsp), %rax
    fxsave64 0xb0(%rax)
    mov 0x80(%rax), %rax
    .byte 0x0f, 0x01, 0xda       /* VMLOAD host VMCB */
    mov (%rsp), %rax
    fxrstor64 0x2b0(%rax)

    /* Save all live guest GPRs before host C code can destroy them. */
    sub $0x70, %rsp
    mov %r15, 0x00(%rsp)
    mov %r14, 0x08(%rsp)
    mov %r13, 0x10(%rsp)
    mov %r12, 0x18(%rsp)
    mov %r11, 0x20(%rsp)
    mov %r10, 0x28(%rsp)
    mov %r9,  0x30(%rsp)
    mov %r8,  0x38(%rsp)
    mov %rdi, 0x40(%rsp)
    mov %rsi, 0x48(%rsp)
    mov %rbp, 0x50(%rsp)
    mov %rdx, 0x58(%rsp)
    mov %rcx, 0x60(%rsp)
    mov %rbx, 0x68(%rsp)

    mov 0x70(%rsp), %rcx         /* CPU pointer kept above frame */
    mov %rsp, %rdx
    /* Frame leaves RSP misaligned by 8; 0x28 supplies shadow space + align. */
    sub $0x28, %rsp
    call svm_handle_guest_exit
    add $0x28, %rsp

    mov 0x00(%rsp), %r15
    mov 0x08(%rsp), %r14
    mov 0x10(%rsp), %r13
    mov 0x18(%rsp), %r12
    mov 0x20(%rsp), %r11
    mov 0x28(%rsp), %r10
    mov 0x30(%rsp), %r9
    mov 0x38(%rsp), %r8
    mov 0x40(%rsp), %rdi
    mov 0x48(%rsp), %rsi
    mov 0x50(%rsp), %rbp
    mov 0x58(%rsp), %rdx
    mov 0x60(%rsp), %rcx
    mov 0x68(%rsp), %rbx
    add $0x70, %rsp
    jmp .Lsvm_context_run

.Lguest_context_resume:
    mov $1, %eax
    ret
