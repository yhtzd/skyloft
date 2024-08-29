#include <stdio.h>
#include <sched.h>

#include <utils/defs.h>
#include <utils/cpu.h>
#include <utils/fxsave.h>
#include <utils/time.h>

#define ITER 100000

struct fxsave fxstate __attribute__((aligned(16)));

void __always_inline fxsave()
{
    __asm__("fxsave64 (%0)" : : "r"(&fxstate));
}

void __always_inline fxrstor()
{
    __asm__("fxrstor64 (%0)" : : "r"(&fxstate));
}

int main()
{
    int i;
    uint64_t tsc;

    bind_to_cpu(2);

    tsc = now_tsc();
    for (i = 0; i < ITER; i++) {
        fxsave();
    }
    printf("fxsave %.3lf cycles\n", (double)(now_tsc() - tsc) / ITER);

    tsc = now_tsc();
    for (i = 0; i < ITER; i++) {
        fxrstor();
    }
    printf("fxrstor %.3lf cycles\n", (double)(now_tsc() - tsc) / ITER);

    tsc = now_tsc();
    for (i = 0; i < ITER; i++) {
        fxsave();
        fxrstor();
    }
    printf("fxsave + fxrstor %.3lf cycles\n", (double)(now_tsc() - tsc) / ITER);

    return 0;
}