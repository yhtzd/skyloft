#include <errno.h>
#include <stdio.h>

#include <net/arp.h>
#include <skyloft/net.h>
#include <skyloft/sync/timer.h>
#include <skyloft/uapi/task.h>
#include <utils/time.h>

static int str_to_ip(const char *str, uint32_t *addr)
{
    uint8_t a, b, c, d;
    if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
        return -EINVAL;
    }

    *addr = MAKE_IP_ADDR(a, b, c, d);
    return 0;
}

static int str_to_mac(const char *str, struct eth_addr *addr)
{
    size_t i;
    static const char *fmts[] = {"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx",
                                 "%hhx%hhx%hhx%hhx%hhx%hhx"};

    for (i = 0; i < ARRAY_SIZE(fmts); i++) {
        if (sscanf(str, fmts[i], &addr->addr[0], &addr->addr[1], &addr->addr[2], &addr->addr[3],
                   &addr->addr[4], &addr->addr[5]) == 6) {
            return 0;
        }
    }
    return -EINVAL;
}

static void entry(void *arg)
{
    uint32_t addr;
    str_to_ip("192.168.1.3", &addr);

    for (;;) {
        printf("Hello\n");
        timer_sleep(USEC_PER_SEC);
    }
}

int main()
{
    sl_libos_start(entry, NULL);
}
