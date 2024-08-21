#pragma once

#include <skyloft/mm/mempool.h>
#include <skyloft/mm/page.h>
#include <skyloft/mm/slab.h>
#include <skyloft/mm/smalloc.h>
#include <skyloft/mm/stack.h>
#include <skyloft/mm/tcache.h>

int mm_init_percpu();
int mm_init();
