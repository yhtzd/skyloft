.intel_syntax noprefix
.text

.align 16
.globl __context_switch
.type __context_switch, @function
__context_switch:
    push rdi
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx
    mov [rdi], rsp
    
    /* clear the stack busy flag */
    mov byte ptr [rdx], 0

    mov rsp, rsi
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15
    pop rdi
#ifdef SKYLOFT_UINTR
    /* enable preemption */
    stui
#endif
    ret

.align 16
.globl __context_switch_from_idle
.type __context_switch_from_idle, @function
__context_switch_from_idle:
    mov rsp, rdi
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15
    pop rdi
#ifdef SKYLOFT_UINTR
    /* enable preemption */
    stui
#endif
    ret

.align 16
.globl __context_switch_to_idle
.type __context_switch_to_idle, @function
__context_switch_to_idle:
    push rdi
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx
    mov [rdi], rsp

    mov rsp, rsi
    jmp schedule

.align 16
.globl __context_switch_to_fn_nosave
.type __context_switch_to_fn_nosave, @function
__context_switch_to_fn_nosave:
    mov rsp, rsi
    jmp rdi
