#pragma once

#ifdef SKYLOFT_UINTR

#include <syscall.h>
#include <x86gprintrin.h>

#define uintr_register_handler(handler, flags) syscall(__NR_uintr_register_handler, handler, flags)
#define uintr_unregister_handler(flags)        syscall(__NR_uintr_unregister_handler, flags)
#define uintr_vector_fd(vector, flags)         syscall(__NR_uintr_vector_fd, vector, flags)
#define uintr_register_sender(fd, flags)       syscall(__NR_uintr_register_sender, fd, flags)
#define uintr_unregister_sender(ipi_idx, flags) \
    syscall(__NR_uintr_unregister_sender, ipi_idx, flags)
#define uintr_wait(usec, flags)            syscall(__NR_uintr_wait, usec, flags)
#define uintr_register_self(vector, flags) syscall(__NR_uintr_register_self, vector, flags)
#define uintr_alt_stack(sp, size, flags)   syscall(__NR_uintr_alt_stack, sp, size, flags)
#define uintr_ipi_fd(flags)                syscall(__NR_uintr_ipi_fd, flags)

#define __NR_uintr_register_handler   471
#define __NR_uintr_unregister_handler 472
#define __NR_uintr_vector_fd          473
#define __NR_uintr_register_sender    474
#define __NR_uintr_unregister_sender  475
#define __NR_uintr_wait               476
#define __NR_uintr_register_self      477
#define __NR_uintr_alt_stack          478
#define __NR_uintr_ipi_fd             479

#define local_irq_save(flags) \
    do {                      \
        flags = _testui();    \
        _clui();              \
    } while (0)
#define local_irq_restore(flags) \
    do {                         \
        if (flags)               \
            _stui();             \
    } while (0)

#else
#define local_irq_save(flags)
#define local_irq_restore(flags)
#endif
