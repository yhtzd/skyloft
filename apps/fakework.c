#include <errno.h>
#include <stdio.h>

#include <skyloft/net.h>
#include <skyloft/task.h>
#include <skyloft/sync/sync.h>
#include <skyloft/uapi/task.h>
#include <skyloft/udp.h>
#include <utils/log.h>

static struct netaddr listen_addr;

static void __attribute__((noinline)) fake_work(unsigned int nloops)
{
    for (unsigned int i = 0; i++ < nloops;) {
        asm volatile("nop");
    }
}

// Shenango loadgen message format
struct payload {
    uint64_t work_iterations;
    uint64_t index;
};

extern __thread struct task* __curr;

static void HandleRequest(udp_spawn_data_t *d)
{
    unsigned int niters = 0;

    if (d->len == sizeof(struct payload)) {
        struct payload *p = (struct payload *)d->buf;
        niters = ntoh64(p->work_iterations) * CPU_FREQ_MHZ / 1000;
    } else {
        panic("invalid message len %lu", d->len);
    }

    // printf("niters: %d\n", niters);
    // uint64_t a = now_tsc();

    __curr->allow_preempt = true;
    fake_work(niters);
    __curr->allow_preempt = false;
    // uint64_t b = now_tsc();
    // printf(" %ld\n", b - a);

    if (udp_respond(d->buf, d->len, d) != (ssize_t)d->len)
        panic("bad write");
    udp_spawn_data_release(d->release_data);
}

static void app_main(void *arg)
{
    udp_spawner_t *s;
    waitgroup_t w;

    int ret = udp_create_spawner(listen_addr, HandleRequest, &s);
    if (ret)
        panic("ret %d", ret);

    waitgroup_init(&w);
    waitgroup_add(&w, 1);
    waitgroup_wait(&w);
}

int main(int argc, char *argv[])
{
    int ret;

    if (argc != 2) {
        printf("usage: %s [portno]\n", argv[0]);
        return -EINVAL;
    }

    listen_addr.port = atoi(argv[1]);

    ret = sl_libos_start(app_main, NULL);
    if (ret) {
        printf("failed to start runtime\n");
        return ret;
    }

    return 0;
}
