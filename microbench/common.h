#pragma once

#define DIV_ROUND(sum, count) (((sum) + (count) / 2) / (count))

static inline void dummy_work(uint64_t iter) {
    while (iter--) {
        asm volatile("nop");
    }
}
