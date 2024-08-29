#ifndef _SKYLOFT_EXPERIMENT_COMMON_H_
#define _SKYLOFT_EXPERIMENT_COMMON_H_

#include <stdint.h>

#include <gflags/gflags.h>
#include <rocksdb/c.h>

#include <utils/list.h>
#include <utils/time.h>

DECLARE_string(rocksdb_path);
DECLARE_int32(rocksdb_cache_size);
DECLARE_double(range_query_ratio);
DECLARE_int32(range_query_size);
DECLARE_int32(get_service_time);
DECLARE_int32(range_query_service_time);
DECLARE_double(load);
DECLARE_int32(run_time);
DECLARE_int32(discard_time);
DECLARE_string(output_path);
DECLARE_int32(num_workers);
DECLARE_bool(bench_request);
DECLARE_bool(fake_work);
DECLARE_int32(preemption_quantum);
DECLARE_bool(detailed_print);
DECLARE_bool(slowdown_print);
DECLARE_int32(guaranteed_cpus);
DECLARE_int32(adjust_quantum);
DECLARE_double(congestion_thresh);

enum {
    ROCKSDB_GET,
    ROCKSDB_RANGE,
};

typedef struct {
    uint8_t type;
    union {
        struct {
            uint32_t entry;
        } get;
        struct {
            uint32_t start;
            uint32_t size;
        } range;
    } work;
    /* When the request is generated. */
    __nsec gen_time;
    /* When the request is received. */
    __nsec recv_time;
    /* When the request is assigned to a worker. */
    __nsec assigned_time;
    /* When the request started to be handled by a worker. */
    __nsec start_time;
    /* When the worker finished handling the request. */
    __nsec end_time;
    struct list_node link;
} request_t;

static inline double mean_service_time_us()
{
    return (double)FLAGS_get_service_time / NSEC_PER_USEC * (1 - FLAGS_range_query_ratio) +
           (double)FLAGS_range_query_service_time / NSEC_PER_USEC * FLAGS_range_query_ratio;
}

static inline double max_throughput()
{
    return USEC_PER_SEC / mean_service_time_us() * FLAGS_num_workers;
}

static inline double target_throughput()
{
    return max_throughput() * FLAGS_load;
}

void write_lat_results_detailed(int issued, request_t *reqs);
void write_lat_results(int issued, request_t *reqs);
void write_slo_results(int issued, request_t *reqs);

#define ROCKSDB_NUM_ENTRIES  1000000
#define ROCKSDB_DATA_LENGTH  16
#define ROCKSDB_KEY_LENGTH   (3 + ROCKSDB_DATA_LENGTH)
#define ROCKSDB_VALUE_LENGTH (5 + ROCKSDB_DATA_LENGTH)

rocksdb_t *rocksdb_init(const char *path, int cache_size);
void rocksdb_handle_get(rocksdb_t *db, request_t *req);
void rocksdb_handle_range_query(rocksdb_t *db, request_t *req);
void benchmark_request(rocksdb_t *db);
void init_request(request_t *req);
void init_request_file(request_t *req);
void init_request_bimodal(request_t *req, double ratio, int size);
void fake_work(uint64_t service_time);

/* partitioned-FCFS */
#define MQ 1

#define CPU_FREQ_GHZ 2

#endif
