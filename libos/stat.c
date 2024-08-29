#include <stdio.h>

#include <skyloft/io.h>
#include <skyloft/stat.h>
#include <utils/defs.h>
#include <utils/log.h>

void print_stats(void)
{
    int i, j;

    printf("%3c", ' ');
    for (i = 0; i < STAT_NR; i++) printf("%16s", stat_str(i));
    printf("\n");
    for (i = 0; i < proc->nr_ks; i++) {
        printf("%2d:", i);
        for (j = 0; j < STAT_NR; j++) printf("%16ld", proc->all_ks[i].stats[j]);
        printf("\n");
    }
}
