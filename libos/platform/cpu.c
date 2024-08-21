#include <sched.h>
#include <stdlib.h>
#include <string.h>

#include <skyloft/global.h>
#include <skyloft/params.h>
#include <skyloft/platform.h>
#include <utils/bitmap.h>
#include <utils/log.h>

define_cpu_array(int, hw_cpu_list, USED_CPUS);
define_cpu_array(int, sibling_cpu_map, MAX_CPUS);
define_cpu_array(int, cpu_siblings, USED_CPUS);

static int parse_cpu_list_str(const char *str, int n, int *cpu_list)
{
    int i = 0;
    int j = 0;
    int k = 0;
    int len = strlen(str);
    char buf[10];

    if (!n)
        return -1;

    while (i < len) {
        if (str[i] == ',') {
            buf[j] = '\0';
            cpu_list[k++] = atoi(buf);
            if (k == n) {
                return 0;
            }
            j = 0;
        } else {
            buf[j++] = str[i];
        }
        i++;
    }
    buf[j] = '\0';
    cpu_list[k++] = atoi(buf);
    return 0;
}

int bind_to_cpu(int tid, int cpu_id)
{
    int hw_cpu = hw_cpu_id(cpu_id);
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(hw_cpu, &set);

    int ret = sched_setaffinity(tid, sizeof(set), &set);
    if (ret) {
        log_warn("failed to bind to cpu %d(%d)", cpu_id, hw_cpu);
    }
    return ret;
}

int unbind_cpus(int tid)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int i = 0; i < MAX_CPUS; i++) {
        CPU_SET(i, &set);
    }
    for (int i = 0; i < USED_CPUS; i++) {
        CPU_CLR(hw_cpu_id(i), &set);
    }

    int ret = sched_setaffinity(tid, sizeof(set), &set);
    if (ret) {
        log_warn("failed to unbind cpus");
    }
    return ret;
}

int platform_cpu_init()
{
    // TODO: parse /sys/devices/system/cpu/cpu*/topology/thread_siblings_list

    int ret;
    if ((ret = parse_cpu_list_str(USED_HW_CPU_LIST, USED_CPUS, hw_cpu_list))) {
        log_err("failed to parse hw cpu list");
        return ret;
    }

    if ((ret = parse_cpu_list_str(SIBLING_CPU_MAP, MAX_CPUS, sibling_cpu_map))) {
        log_err("failed to parse sibling cpu map");
        return ret;
    }

    for (int i = 0; i < USED_CPUS; i++) {
        int hw_cpu = hw_cpu_list[i];
        int sibling = sibling_cpu_map[hw_cpu];
        cpu_siblings[i] = -1;
        for (int j = 0; j < USED_CPUS; j++) {
            if (hw_cpu_list[j] == sibling) {
                cpu_siblings[i] = j;
                break;
            }
        }
        // BUG_ON(cpu_siblings[i] == -1);
    }

    return 0;
}
