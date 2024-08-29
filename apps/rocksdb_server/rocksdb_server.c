#include <c.h>
#include <errno.h>
#include <stdio.h>

#include <skyloft/net.h>
#include <skyloft/sync/sync.h>
#include <skyloft/task.h>
#include <skyloft/uapi/task.h>
#include <skyloft/udp.h>
#include <utils/log.h>

static rocksdb_t *db;
static struct netaddr listen_addr;

extern __thread struct task *__curr;

// Shenango loadgen message format
struct payload {
    uint64_t work_iterations;
    uint64_t index;
};

static void __attribute__((noinline)) simulated_work(unsigned int nloops)
{
    for (unsigned int i = 0; i++ < nloops;) {
        asm volatile("nop");
    }
}

static inline void DoScan(rocksdb_readoptions_t *readoptions)
{
    const char *retr_key;
    size_t klen;

    rocksdb_iterator_t *iter = rocksdb_create_iterator(db, readoptions);
    rocksdb_iter_seek_to_first(iter);
    while (rocksdb_iter_valid(iter)) {
        retr_key = rocksdb_iter_key(iter, &klen);
        printf("Scanned key %s\n", retr_key);
        rocksdb_iter_next(iter);
    }
    rocksdb_iter_destroy(iter);
}

static inline void DoGet(rocksdb_readoptions_t *readoptions)
{
    const char *retr_key;
    size_t klen;

    rocksdb_iterator_t *iter = rocksdb_create_iterator(db, readoptions);
    rocksdb_iter_seek_to_first(iter);
    if (rocksdb_iter_valid(iter)) {
        retr_key = rocksdb_iter_key(iter, &klen);
        printf("Scanned key %s\n", retr_key);
    } else {
        printf("No keys found\n");
    }
    rocksdb_iter_destroy(iter);
}

static void HandleRequest(udp_spawn_data_t *d)
{
    rocksdb_readoptions_t *readoptions = rocksdb_readoptions_create();

    struct payload *p = (struct payload *)d->buf;
    uint64_t type = ntoh64(p->work_iterations) * CPU_FREQ_MHZ / 1000;

    __curr->allow_preempt = true;
    simulated_work(type);
    __curr->allow_preempt = false;

    rocksdb_readoptions_destroy(readoptions);
    barrier();

    if (udp_respond(d->buf, d->len, d) != (ssize_t)d->len)
        panic("bad write");
    udp_spawn_data_release(d->release_data);
}

static int cmpfunc(const void *a, const void *b)
{
    int arg1 = *(const int *)a;
    int arg2 = *(const int *)b;
    if (arg1 < arg2)
        return -1;
    if (arg1 > arg2)
        return 1;
    return 0;
}

#define BENCH_ITER 5000

static void bench_ops()
{
    const int cycles_per_us = CPU_FREQ_MHZ;
    uint64_t durations[BENCH_ITER];
    unsigned int i = 0;
    uint64_t sum = 0;

    sum = 0;
    for (i = 0; i < BENCH_ITER; i++) {
        uint64_t start = now_tsc();
        rocksdb_readoptions_t *readoptions = rocksdb_readoptions_create();
        DoScan(readoptions);
        rocksdb_readoptions_destroy(readoptions);
        uint64_t end = now_tsc();
        durations[i] = end - start;
        sum += durations[i];
    }
    qsort(durations, BENCH_ITER, sizeof(uint64_t), cmpfunc);

    fprintf(stderr, "stats for %u SCANs: \n", i);
    fprintf(stderr, "avg: %0.3f\n", (double)sum / (double)(cycles_per_us * BENCH_ITER));
    fprintf(stderr, "median: %0.3f\n", (double)durations[i / 2] / (double)cycles_per_us);
    fprintf(stderr, "p99.9: %0.3f\n", (double)durations[i * 999 / 1000] / (double)cycles_per_us);

    sum = 0;
    for (i = 0; i < BENCH_ITER; i++) {
        uint64_t start = now_tsc();
        rocksdb_readoptions_t *readoptions = rocksdb_readoptions_create();
        DoGet(readoptions);
        rocksdb_readoptions_destroy(readoptions);
        uint64_t end = now_tsc();
        durations[i] = end - start;
        sum += durations[i];
    }

    qsort(durations, BENCH_ITER, sizeof(uint64_t), cmpfunc);
    fprintf(stderr, "stats for %u GETs: \n", i);
    fprintf(stderr, "avg: %0.3f\n", (double)sum / (double)(cycles_per_us * BENCH_ITER));
    fprintf(stderr, "median: %0.3f\n", (double)durations[i / 2] / (double)cycles_per_us);
    fprintf(stderr, "p99.9: %0.3f\n", (double)durations[i * 999 / 1000] / (double)cycles_per_us);
}

void rocksdb_init(const char *path)
{
    log_info("Initialized RocksDB\n");

    rocksdb_options_t *options = rocksdb_options_create();
    rocksdb_options_set_allow_mmap_reads(options, 1);
    rocksdb_options_set_allow_mmap_writes(options, 1);
    rocksdb_slicetransform_t *prefix_extractor = rocksdb_slicetransform_create_fixed_prefix(8);
    rocksdb_options_set_prefix_extractor(options, prefix_extractor);
    rocksdb_options_set_plain_table_factory(options, 0, 10, 0.75, 3);
    // Optimize RocksDB. This is the easiest way to
    // get RocksDB to perform well
    rocksdb_options_increase_parallelism(options, 0);
    rocksdb_options_optimize_level_style_compaction(options, 512 * 1024 * 1024);
    // create the DB if it's not already present
    rocksdb_options_set_create_if_missing(options, 0);

    char *err = NULL;
    db = rocksdb_open(options, path, &err);

    if (err) {
        printf("Could not open RocksDB database: %s\n", err);
        return;
    }
}

static void app_main(void *arg)
{
    udp_spawner_t *s;
    waitgroup_t w;

    // open DB
    rocksdb_init("/tmp/my_db");
    // bench_ops();

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
