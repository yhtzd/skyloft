#include <assert.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <climits>
#include <cstddef>
#include <string>

#include <gflags/gflags.h>
#include <rocksdb/c.h>

#include <skyloft/uapi/params.h>
#include <skyloft/uapi/task.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/spinlock.h>
#include <utils/time.h>
#include <utils/types.h>

#include "common.h"
#include "random.h"

#define SQ 1
// #define MQ 1

enum {
    IDLE,
    RUNNING,
    FINISHED,
    PREEMPTED,
};

typedef struct {
    /* Bound to an isolated CPU */
    int cpu_id;
    /* Database handle */
    rocksdb_t *db;
    /* Pointer to dispatcher */
    void *dispatcher;
    /* Worker running status */
    int status;
#ifdef MQ
    /* When a new request arrives */
    __nsec next;
    /* Track allocated requests */
    request_t *requests;
    list_head req_list;
    int issued;
#endif
} __aligned(CACHE_LINE_SIZE) worker_t;

typedef struct {
#ifdef SQ
    /* When a new request arrives */
    __nsec next;
    /* Track allocated requests */
    request_t *requests;
    int issued;
    list_head req_list;
    spinlock_t req_lock;
#endif
    /* Track worker status */
    worker_t *workers;
} dispatcher_t;

dispatcher_t *dispatcher_create(rocksdb_t *db);
void dispatcher_destroy(dispatcher_t *dispatcher);
void do_dispatching(dispatcher_t *dispatcher);

dispatcher_t *dispatcher_create(rocksdb_t *db)
{
    int i, j;
    dispatcher_t *dispatcher;
    request_t *req;

    int target_tput = target_throughput();
    int num_reqs = target_tput * FLAGS_run_time * 2;

    dispatcher = (dispatcher_t *)malloc(sizeof(dispatcher_t));
#ifdef SQ
    dispatcher->next = 0;
    dispatcher->requests = (request_t *)malloc(sizeof(request_t) * num_reqs);

    double timestamp = 0;
    for (i = 0; i < num_reqs; i++) {
        timestamp += random_exponential_distribution();
        init_request_bimodal(&dispatcher->requests[i], FLAGS_range_query_ratio,
                             FLAGS_range_query_size);
        dispatcher->requests[i].gen_time = timestamp * NSEC_PER_USEC;
    }
    list_head_init(&dispatcher->req_list);
    spin_lock_init(&dispatcher->req_lock);
    dispatcher->issued = 0;
#endif
    dispatcher->workers = (worker_t *)malloc(sizeof(worker_t) * FLAGS_num_workers);
    for (i = 0; i < FLAGS_num_workers; i++) {
        dispatcher->workers[i].cpu_id = i;
        dispatcher->workers[i].db = db;
        dispatcher->workers[i].dispatcher = (void *)dispatcher;
        dispatcher->workers[i].status = IDLE;
#ifdef MQ
        dispatcher->workers[i].requests = (request_t *)malloc(sizeof(request_t) * num_reqs);
        for (j = 0; j < num_reqs; j++) init_request(&dispatcher->workers[i].requests[j]);
        dispatcher->workers[i].issued = 0;
        dispatcher->workers[i].next = 0;
        list_head_init(&dispatcher->workers[i].req_list);
#endif
    }

    return dispatcher;
}

void dispatcher_destroy(dispatcher_t *dispatcher)
{
    int i;

    free(dispatcher->workers);
#ifdef SQ
    free(dispatcher->requests);
#elif MQ
    for (i = 0; i < FLAGS_num_cpus; i++) free(dispatcher->workers[i].requests);
#endif
    free(dispatcher);
}

#ifdef SQ
void poll_synthetic_network(dispatcher_t *dispatcher, __nsec start_time)
{
    spin_lock(&dispatcher->req_lock);
    request_t *req = &dispatcher->requests[dispatcher->issued];

    if (now_ns() < start_time + req->gen_time) {
        spin_unlock(&dispatcher->req_lock);
        return;
    }

    req->gen_time += start_time;
    req->recv_time = now_ns();
    dispatcher->issued++;

    list_add_tail(&dispatcher->req_list, &req->link);
    spin_unlock(&dispatcher->req_lock);
}
#elif MQ
void poll_synthetic_network(dispatcher_t *dispatcher)
{
    request_t *req;
    worker_t *worker = &dispatcher->workers[sl_current_cpu_id()];

    if (now_ns() < worker->next)
        return;

    // Avoid issues due to double precision; random generator might be slow
    worker->next = now_ns() + NSEC_PER_USEC * random_exponential_distribution();

    req = &worker->requests[worker->issued++];
    list_add_tail(&worker->req_list, &req->link);
    req->recv_time = now_ns();
}
#endif

/* Run-to-complete request handler */
static void* request_handler(void *arg)
{
    request_t *req;
    worker_t *worker = (worker_t *)arg;
    dispatcher_t *dispatcher = (dispatcher_t *)worker->dispatcher;

#ifdef SQ
    spin_lock(&dispatcher->req_lock);
    req = list_pop(&dispatcher->req_list, request_t, link);
    spin_unlock(&dispatcher->req_lock);
#elif MQ
    req = list_pop(&worker->req_list, request_t, link);
#endif

    if (req == NULL) {
        worker->status = IDLE;
        return 0;
    }

    req->start_time = now_ns();
    if (req->type == ROCKSDB_GET) {
        rocksdb_handle_get(worker->db, req);
    } else if (req->type == ROCKSDB_RANGE) {
        rocksdb_handle_range_query(worker->db, req);
    }
    req->end_time = now_ns();

    worker->status = FINISHED;
    return 0;
}

static __nsec global_start;

static void* worker_percpu(void *arg)
{
    request_t *req;
    worker_t *worker = (worker_t *)arg;
    dispatcher_t *dispatcher = (dispatcher_t *)worker->dispatcher;
    assert(worker != NULL);

    printf("Worker %d is running ...\n", worker->cpu_id);

    __nsec start = now_ns();
    __nsec end = now_ns() + FLAGS_run_time * NSEC_PER_SEC;
    while (now_ns() < end) {
        poll_synthetic_network(dispatcher, start);

        worker->status = RUNNING;
        sl_task_spawn(request_handler, (void *)worker, 0);
        while (worker->status == RUNNING) sl_task_yield();
    }
    return 0;
}

void do_dispatching(dispatcher_t *dispatcher)
{
    for (int i = 1; i < FLAGS_num_workers; i++) {
        sl_task_spawn_oncpu(i, worker_percpu, (void *)&dispatcher->workers[i], 0);
    }
    worker_percpu((void *)&dispatcher->workers[0]);
}

static void* experiment_main(void *arg)
{
    rocksdb_t *db;
    worker_t *curr_worker;
    dispatcher_t *dispatcher;
    int i, j;

    if (FLAGS_load < 0 || FLAGS_load > 1) {
        printf("Invalid load: %f\n", FLAGS_load);
        return (void *)-EINVAL;
    }
    if (FLAGS_get_service_time < 0 || FLAGS_get_service_time > 1000) {
        printf("Invalid get_service_time: %f\n", FLAGS_get_service_time);
        return (void *)-EINVAL;
    }

    printf("Experiment running on CPU %d, num workers: %d\n", sl_current_cpu_id(),
           FLAGS_num_workers);

    // TODO: multiple load dispatchers
    printf("RocksDB path: %s\n", FLAGS_rocksdb_path.c_str());
    printf("Initializing RocksDB...\n");
    db = rocksdb_init(FLAGS_rocksdb_path.c_str(), FLAGS_rocksdb_cache_size);
    if (FLAGS_bench_request)
        benchmark_request(db);

    // TODO: RQ might be drained
    random_init();
    double mean_arrive_time_us = 1e6 / target_throughput();
    random_exponential_distribution_init(1.0 / mean_arrive_time_us);

    printf("Initializing load dispatcher...\n");
    dispatcher = dispatcher_create(db);

    __nsec start = now_ns();
    for (i = 0; i < 10000; i++) random_exponential_distribution();
    printf("Benchmarking random generator: %.3f ns\n", (double)(now_ns() - start) / 10000);

    printf("Generating requests...\n");
    do_dispatching(dispatcher);

#ifdef SQ
    write_lat_results_detailed(dispatcher->issued, dispatcher->requests);
#elif MQ
    int issued = 0;
    int num_reqs = target_throughput() * FLAGS_run_time * FLAGS_num_cpus;
    request_t *requests = (request_t *)malloc(sizeof(request_t) * num_reqs);
    for (i = 0; i < FLAGS_num_cpus; i++)
        for (j = 0; j < dispatcher->workers[i].issued; j++) {
            if (issued > num_reqs)
                break;
            requests[issued++] = dispatcher->workers[i].requests[j];
        }
    write_lat_results_detailed(issued, requests);
#endif
    dispatcher_destroy(dispatcher);
    rocksdb_close(db);
    printf("Experiment exits gracefully.\n");
    return 0;
}

int main(int argc, char **argv)
{
    gflags::SetUsageMessage("test_rocksdb [options]");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    gflags::ShutDownCommandLineFlags();

    if (FLAGS_num_workers > USED_CPUS) {
        printf("Too many CPUs %d > %d\n", FLAGS_num_workers, USED_CPUS);
        return 0;
    }

    sl_libos_start(experiment_main, NULL);
}
