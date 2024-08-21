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
#include "jbsq.h"
#include "random.h"

enum {
    IDLE,
    RUNNING,
    FINISHED,
    PREEMPTED,
};

typedef struct {
} jbsq_t;

typedef struct {
    /* Bound to an isolated CPU */
    int cpu_id;
#ifdef MQ
    list_head reqs;
    spinlock_t req_lock;
#elif JBSQ_K > 1
    /* Requests to handle */
    JBSQ(request_t *, reqs);
#else
    request_t *req;
#endif
    /* Database handle */
    rocksdb_t *db;
    /* Worker running status */
    int status;
} __aligned(CACHE_LINE_SIZE) worker_t;

typedef struct {
    /* When a new request arrives */
    __nsec next;
    /* Track allocated requests */
    request_t *requests;
    int issued;
    list_head req_list;
    /* Track worker status */
    worker_t *workers;
} dispatcher_t;

dispatcher_t *dispatcher_create(rocksdb_t *db);
void dispatcher_destroy(dispatcher_t *dispatcher);
void do_dispatching(dispatcher_t *dispatcher);

dispatcher_t *dispatcher_create(rocksdb_t *db)
{
    int i;
    dispatcher_t *dispatcher;
    request_t *req;
    bool range_query = false;

    int target_tput = target_throughput();
    int num_reqs = target_tput * FLAGS_run_time * 2;

    dispatcher = (dispatcher_t *)malloc(sizeof(dispatcher_t));
    dispatcher->requests = (request_t *)malloc(sizeof(request_t) * num_reqs);

    double timestamp = 0;
    for (i = 0; i < num_reqs; i++) {
        timestamp += random_exponential_distribution();
        init_request_bimodal(&dispatcher->requests[i], FLAGS_range_query_ratio,
                             FLAGS_range_query_size);
        dispatcher->requests[i].gen_time = timestamp * NSEC_PER_USEC;
    }
    list_head_init(&dispatcher->req_list);
    dispatcher->issued = 0;
    dispatcher->workers = (worker_t *)malloc(sizeof(worker_t) * (FLAGS_num_workers + 1));
    for (i = 1; i < FLAGS_num_workers + 1; i++) {
        dispatcher->workers[i].cpu_id = i;
        dispatcher->workers[i].status = IDLE;
        dispatcher->workers[i].db = db;
#ifdef MQ
        list_head_init(&dispatcher->workers[i].reqs);
        spin_lock_init(&dispatcher->workers[i].req_lock);
#elif JBSQ_K > 1
        JBSQ_INIT(&dispatcher->workers[i].reqs);
#endif
    }

    return dispatcher;
}

void dispatcher_destroy(dispatcher_t *dispatcher)
{
    free(dispatcher->workers);
    free(dispatcher->requests);
    free(dispatcher);
}

void poll_synthetic_network(dispatcher_t *dispatcher, __nsec start_time)
{
    int i;
    request_t *req = &dispatcher->requests[dispatcher->issued];

    if (now_ns() < start_time + req->gen_time)
        return;

    req->gen_time += start_time;
    req->recv_time = now_ns();
    dispatcher->issued++;

#ifdef MQ
    // Choose a worker queue at random
    i = (rand() % FLAGS_num_workers) + 1;
    spin_lock(&dispatcher->workers[i].req_lock);
    list_add_tail(&dispatcher->workers[i].reqs, &req->link);
    spin_unlock(&dispatcher->workers[i].req_lock);
    // printf("PUSH %d %p\n", i, req);
#else
    list_add_tail(&dispatcher->req_list, &req->link);
#endif
}

/* Run-to-complete request handler */
static void worker_request_handler(void *arg)
{
    request_t *req;
    worker_t *worker = (worker_t *)arg;
    assert(worker != NULL && sl_current_cpu_id() == worker->cpu_id);

#ifdef MQ
    spin_lock(&worker->req_lock);
    req = list_pop(&worker->reqs, request_t, link);
    spin_unlock(&worker->req_lock);
    // printf("POP %d %p\n", sl_current_cpu_id(), req);
#elif JBSQ_K > 1
    req = JBSQ_POP(&worker->reqs);
#else
    req = worker->req;
#endif

    req->start_time = now_ns();
    if (req->type == ROCKSDB_GET) {
        rocksdb_handle_get(worker->db, req);
    } else if (req->type == ROCKSDB_RANGE) {
        rocksdb_handle_range_query(worker->db, req);
    }
    req->end_time = now_ns();

    worker->status = FINISHED;
    // printf("START %lu END %lu\n", req->start_time, req->end_time);
}

#if JBSQ_K > 1 && !defined(MQ)
static inline void choose_shortest(dispatcher_t *dispatcher)
{
    int i, min_i, len, min_len;
    request_t *req;

    min_len = INT_MAX;
    for (i = 1; i < FLAGS_num_workers + 1; i++) {
        len = JBSQ_LEN(&dispatcher->workers[i].reqs);
        if (len < min_len && len < JBSQ_K) {
            min_len = len;
            min_i = i;
        }
    }

    if (min_len == INT_MAX)
        return;

    req = list_pop(&dispatcher->req_list, request_t, link);
    if (req != NULL)
        JBSQ_PUSH(&dispatcher->workers[min_i].reqs, req);
}
#endif

static inline void handle_worker(dispatcher_t *dispatcher, int i)
{
    request_t *req;
    worker_t *worker = &dispatcher->workers[i];

#ifdef MQ
    if (worker->status != RUNNING && !list_empty(&worker->reqs)) {
        worker->status = RUNNING;
        sl_task_spawn_oncpu(i, worker_request_handler, (void *)worker, 0);
    }
#elif JBSQ_K > 1
    if (worker->status != RUNNING && !JBSQ_EMPTY(&worker->reqs)) {
        worker->status = RUNNING;
        sl_task_spawn_oncpu(i, worker_request_handler, (void *)worker, 0);
    }
#else
    if (worker->status != RUNNING) {
        worker->req = list_pop(&dispatcher->req_list, request_t, link);
        if (worker->req != NULL) {
            worker->status = RUNNING;
            sl_task_spawn_oncpu(i, worker_request_handler, (void *)worker, 0);
        }
    }
#endif
}

void do_dispatching(dispatcher_t *dispatcher)
{
    int i;
    request_t *req;
    worker_t *worker;
    __nsec start;
    int min_len, min_i;

    start = now_ns();

    for (;;) {
        poll_synthetic_network(dispatcher, start);
#if JBSQ_K > 1 && !defined(MQ)
        choose_shortest(dispatcher);
#endif
        for (i = 1; i < FLAGS_num_workers + 1; ++i) handle_worker(dispatcher, i);

        /* Terminate all workers */
        if (now_ns() - start > FLAGS_run_time * NSEC_PER_SEC)
            break;
    }
    printf("Dispatcher %d\n", dispatcher->issued);
}

static void experiment_main(void *arg)
{
    rocksdb_t *db;
    worker_t *curr_worker;
    dispatcher_t *dispatcher;
    result_t result;
    FILE *output;
    int i;

    if (FLAGS_load < 0 || FLAGS_load > 1) {
        printf("Invalid load: %f\n", FLAGS_load);
        return;
    }
    if (FLAGS_get_service_time < 0 || FLAGS_get_service_time > 1000) {
        printf("Invalid get_service_time: %f\n", FLAGS_get_service_time);
        return;
    }

    printf("Dispatcher running on CPU %d, num workers: %d\n", sl_current_cpu_id(),
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

    printf("Results: \n");
    result = get_result(dispatcher->issued, dispatcher->requests, FLAGS_run_time);
    output = fopen((FLAGS_output_path + "/rocksdb_" + std::to_string(time(NULL))).c_str(), "w");
    assert(output != NULL);
    output_result(result, output);
    print_result(result);

    fclose(output);
    dispatcher_destroy(dispatcher);
    rocksdb_close(db);
    printf("Experiment exits gracefully.\n");
}

int main(int argc, char **argv)
{
    gflags::SetUsageMessage("test_rocksdb [options]");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    gflags::ShutDownCommandLineFlags();

    if (FLAGS_num_workers + 1 > USED_CPUS) {
        printf("Too many workers %d + 1 > %d\n", FLAGS_num_workers, USED_CPUS);
        return 0;
    }

    sl_libos_start(experiment_main, NULL);
}
