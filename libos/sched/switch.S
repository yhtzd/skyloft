.intel_syntax noprefix
.text

#ifdef SKYLOFT_FXSAVE

.macro SAVE_FXSTATE
    sub rsp, 512
    fxsave64 [rsp]
.endm

.macro RESTORE_FXSTATE
    fxrstor64 [rsp]
    add rsp, 512
.endm

#else

.macro SAVE_FXSTATE
.endm

.macro RESTORE_FXSTATE
.endm

#endif

.macro SAVE_CALLEE
    push rdi
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx
.endm

.macro RESTORE_CALLEE
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15
    pop rdi
.endm

.align 16
.globl __context_switch
.type __context_switch, @function
__context_switch:
    SAVE_CALLEE
    SAVE_FXSTATE

    mov [rdi], rsp

    /* clear the stack busy flag */
    mov byte ptr [rdx], 0

    mov rsp, rsi

    RESTORE_FXSTATE
    RESTORE_CALLEE
#ifdef SKYLOFT_UINTR
    /* enable preemption */
    stui
#endif
    ret

.align 16
.globl __context_switch_init
.type __context_switch_init, @function
__context_switch_init:
    SAVE_CALLEE
    SAVE_FXSTATE

    mov [rdi], rsp

    /* clear the stack busy flag */
    mov byte ptr [rdx], 0

    mov rsp, rsi

    /* without fxstate */ 
    RESTORE_CALLEE
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
    RESTORE_FXSTATE
    RESTORE_CALLEE
#ifdef SKYLOFT_UINTR
    /* enable preemption */
    stui
#endif
    ret

.align 16
.globl __context_switch_from_idle_init
.type __context_switch_from_idle_init, @function
__context_switch_from_idle_init:
    mov rsp, rdi
    /* without fxstate */ 
    RESTORE_CALLEE
#ifdef SKYLOFT_UINTR
    /* enable preemption */
    stui
#endif
    ret

.align 16
.globl __context_switch_to_idle
.type __context_switch_to_idle, @function
__context_switch_to_idle:
    SAVE_CALLEE
    SAVE_FXSTATE
    mov [rdi], rsp

    mov rsp, rsi
    jmp schedule

.align 16
.globl __context_switch_to_fn_nosave
.type __context_switch_to_fn_nosave, @function
__context_switch_to_fn_nosave:
    mov rsp, rsi
    jmp rdi

.align 16
.globl __context_switch_to_idle_and_run
.type __context_switch_to_idle_and_run, @function
__context_switch_to_idle_and_run:
    SAVE_CALLEE
    SAVE_FXSTATE
    mov [rdi], rsp      # arg1: prev rsp

    mov rsp, [rsi]      # arg2: next rsp
    mov rdi, rcx        # arg4: fn_arg
    and rsp, -16
    call rdx            # arg3: fn
    call schedule
