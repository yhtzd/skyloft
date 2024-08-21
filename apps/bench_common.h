#include <stdio.h>

#include <utils/time.h>

static void bench_one(const char *name, void(bench_fn)(), int rounds)
{
    __nsec before = now_ns();
    bench_fn();
    __nsec after = now_ns();
    __nsec elapsed = (after - before + rounds / 2) / rounds;

    printf("%s: %ldns (%ld / %d)\n", name, elapsed, after - before, rounds);
}
