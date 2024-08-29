#pragma once

#include <stdio.h>

#include <utils/time.h>

#define DIV_ROUND(sum, count) (((sum) + (count) / 2) / (count))

static inline void dummy_work(uint64_t iter)
{
    while (iter--) {
        asm volatile("nop");
    }
}

static void bench_one(const char *name, void(bench_fn)(), int rounds)
{
    __nsec before = now_ns();
    bench_fn();
    __nsec after = now_ns();
    __nsec elapsed = (after - before + rounds / 2) / rounds;

    printf("%s: %ldns (%ld / %d)\n", name, elapsed, after - before, rounds);
}
