#include <cstdio>
#include <stdint.h>
#include <string.h>

#include <climits>
#include <vector>

#include <skyloft/uapi/task.h>

#include "common.h"
#include "random.h"
#include "utils/time.h"

DEFINE_string(rocksdb_path, "/tmp/skyloft_rocksdb",
              "The path to the RocksDB database. Creates the database if it does not exist.");
DEFINE_int32(rocksdb_cache_size, 1024, "The size of the RocksDB cache in MB.");
DEFINE_double(range_query_ratio, 0.001, "The share of requests that are range queries.");
DEFINE_int32(range_query_size, 500, "The size of range.");
DEFINE_int32(get_service_time, 1000, "The duration of Get requests (ns).");
DEFINE_int32(range_query_service_time, 1000000, "The duration of Range queries (ns).");
DEFINE_double(load, 0.75, "The ratio of target throughput to max throughput.");
DEFINE_int32(run_time, 5, "Running time (s) of the experiment.");
DEFINE_int32(discard_time, 2,
             "Discards all results from when the experiment starts to discard time (s) elapses.");
DEFINE_string(output_path, "/tmp/skyloft_synthetic", "The path to the experiment results.");
DEFINE_int32(num_workers, 2, "The number of workers.");
DEFINE_bool(bench_request, false, "Benchmark request service time.");
DEFINE_bool(fake_work, false, "Use fake work (spin) instead of real database operations.");
DEFINE_int32(preemption_quantum, 0,
             "Turn off time-based preemption by setting the preemption quantum to 0.");
DEFINE_bool(detailed_print, false, "Print detailed experiment results.");
DEFINE_bool(slowdown_print, false, "Print experiment results of request slowdown.");
DEFINE_int32(guaranteed_cpus, 5, "Guranteed number of CPUs when running with batch app.");
DEFINE_int32(adjust_quantum, 20, "Scheduler makes core allocation decision every quantum (us).");
DEFINE_double(congestion_thresh, 0.05, "Threshold to detect congestion of applications.");

static void write_percentiles(std::vector<uint64_t> &results, FILE *file, bool stdout = false)
{
    if (!results.size())
        return;

    std::sort(results.begin(), results.end());
    uint64_t sum = 0;
    for (auto &r : results) sum += r;

    int size = results.size() % 2 == 0 ? results.size() - 1 : results.size();
    uint64_t avg = (sum + results.size() / 2) / results.size();
    uint64_t min = results.front();
    uint64_t max = results.back();
    uint64_t p50 = results.at(size * 0.5);
    uint64_t p90 = results.at(size * 0.9);
    uint64_t p95 = results.at(size * 0.95);
    uint64_t p99 = results.at(size * 0.99);
    uint64_t p99_5 = results.at(size * 0.995);
    uint64_t p99_9 = results.at(size * 0.999);

    fprintf(file, "%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n", avg, min, p50, p90, p95, p99, p99_5,
            p99_9, max);

    if (stdout) {
        printf("\tAvg (us) %.3f\n", (double)avg / NSEC_PER_USEC);
        printf("\tMin (us) %.3f\n", (double)min / NSEC_PER_USEC);
        printf("\t50%% (us) %.3f\n", (double)p50 / NSEC_PER_USEC);
        printf("\t90%% (us) %.3f\n", (double)p90 / NSEC_PER_USEC);
        printf("\t95%% (us) %.3f\n", (double)p95 / NSEC_PER_USEC);
        printf("\t99%% (us) %.3f\n", (double)p99 / NSEC_PER_USEC);
        printf("\t99.5%% (us) %.3f\n", (double)p99_5 / NSEC_PER_USEC);
        printf("\t99.9%% (us) %.3f\n", (double)p99_9 / NSEC_PER_USEC);
        printf("\tMax (us) %.3f\n", (double)max / NSEC_PER_USEC);
    }
}

void write_lat_results_detailed(int issued, request_t *reqs)
{
    char fname[256];
    sprintf(fname, "%s/rocksdb_%s_%s_%.1f_%.1f_%.3f_%.2f_%d_%ld", FLAGS_output_path.c_str(),
            program_invocation_short_name, sl_sched_policy_name(),
            (double)FLAGS_get_service_time / NSEC_PER_USEC,
            (double)FLAGS_range_query_service_time / NSEC_PER_USEC, FLAGS_range_query_ratio,
            FLAGS_load, FLAGS_num_workers, time(NULL));
    FILE *file = fopen(fname, "w");
    assert(file != NULL);
    printf("Write results to %s\n", fname);

    std::vector<uint64_t> ingress_res;
    std::vector<uint64_t> queue_res;
    std::vector<uint64_t> handle_res;
    std::vector<uint64_t> total_res;
    std::vector<request_t *> finished_reqs;

    fprintf(file, "id,type,timestamp,ingress,queue,handle,total\n");

    uint64_t offset = reqs[0].gen_time;
    int completed = 0, _issued = 0;
    for (int i = 0; i < issued; i++) {
        request_t *req = &reqs[i];
        if (offset + FLAGS_discard_time * NSEC_PER_SEC < req->gen_time) {
            _issued++;
            /* Some workers may not finish their work */
            if (req->end_time != 0 && req->start_time != 0) {
                /* Discard cold results */
                completed++;
                finished_reqs.push_back(req);
            }
        }
    }
    std::sort(finished_reqs.begin(), finished_reqs.end(), [](request_t *req1, request_t *req2) {
        return req1->end_time - req1->gen_time < req2->end_time - req2->gen_time;
    });
    for (int i = 0; i < completed; ++i) {
        request_t *req = finished_reqs[i];

        uint64_t ingress_lat = req->recv_time - req->gen_time;
        uint64_t queue_lat = req->start_time - req->recv_time;
        uint64_t handle_lat = req->end_time - req->start_time;
        uint64_t total_lat = req->end_time - req->gen_time;

        ingress_res.push_back(ingress_lat);
        queue_res.push_back(queue_lat);
        handle_res.push_back(handle_lat);
        total_res.push_back(total_lat);

        fprintf(file, "%d,%d,%ld,%ld,%ld,%ld,%ld\n", i, req->type, req->gen_time - offset,
                ingress_lat, queue_lat, handle_lat, total_lat);
    }

    double actual_tput = (double)completed / (FLAGS_run_time - FLAGS_discard_time);
    printf("Results: \n");
    printf("\tMeans service time: %.3lf\n", mean_service_time_us());
    printf("\tMax throughput: %.3lf\n", max_throughput());
    printf("\tTarget throughput: %.3lf\n", target_throughput());
    printf("\tActual thoughput: %.3lf\n", actual_tput);
    printf("\tCompleted/Issued %d/%d\n", completed, _issued);

    fprintf(file, "avg,min,p50,p90,p95,p99,p99_5,p99_9,max\n");
    write_percentiles(ingress_res, file);
    write_percentiles(queue_res, file);
    write_percentiles(handle_res, file);
    write_percentiles(total_res, file, true);

    fprintf(file, "max_tput,target_tput,actual_tput,issued,completed\n");
    fprintf(file, "%.3lf,%.3lf,%.3lf,%d,%d\n", max_throughput(), target_throughput(), actual_tput,
            _issued, completed);
    fclose(file);
}

void write_lat_results(int issued, request_t *reqs)
{

    std::vector<uint64_t> results;
    FILE *file = fopen(FLAGS_output_path.c_str(), "a");
    assert(file != NULL);
    uint64_t offset = reqs[0].gen_time;
    for (int i = 0; i < issued; i++) {
        request_t *req = &reqs[i];
        /* Some workers may not finish their work */
        if (req->end_time != 0 && req->start_time != 0) {
            /* Discard cold results */
            if (offset + FLAGS_discard_time * NSEC_PER_SEC < req->gen_time) {
                results.push_back(req->end_time - req->gen_time);
            }
        }
    }
    if (results.size() > 0) {
        double run_time = FLAGS_run_time - FLAGS_discard_time;
        double actual_tput = (double)results.size() / run_time;
        std::sort(results.begin(), results.end());
        int size = results.size() % 2 == 0 ? results.size() - 1 : results.size();
        uint64_t min = results.front();
        uint64_t p50 = results.at(size * 0.5);
        uint64_t p99 = results.at(size * 0.99);
        uint64_t p99_5 = results.at(size * 0.995);
        uint64_t p99_9 = results.at(size * 0.999);
        uint64_t max = results.back();
        fprintf(file, "%.3lf,%.3lf,%lu,%lu,%lu,%lu,%lu,%lu\n", target_throughput(), actual_tput,
                min, p50, p99, p99_5, p99_9, max);
    } else {
        fprintf(file, "%.3lf,0,0,0,0,0,0,0\n", target_throughput());
    }
    fclose(file);
}

void write_slo_results(int issued, request_t *reqs)
{

    std::vector<double> results;
    FILE *file = fopen(FLAGS_output_path.c_str(), "a");
    assert(file != NULL);
    uint64_t offset = reqs[0].gen_time;
    for (int i = 0; i < issued; i++) {
        request_t *req = &reqs[i];
        /* Some workers may not finish their work */
        if (req->end_time != 0 && req->start_time != 0) {
            /* Discard cold results */
            if (offset + FLAGS_discard_time * NSEC_PER_SEC < req->gen_time) {
                if (req->type == ROCKSDB_GET) {
                    results.push_back((double)(req->end_time - req->gen_time) /
                                      FLAGS_get_service_time);
                } else {
                    results.push_back((double)(req->end_time - req->gen_time) /
                                      FLAGS_range_query_service_time);
                }
            }
        }
    }
    if (results.size() > 0) {
        double run_time = FLAGS_run_time - FLAGS_discard_time;
        double actual_tput = (double)results.size() / run_time;
        std::sort(results.begin(), results.end());
        int size = results.size() % 2 == 0 ? results.size() - 1 : results.size();
        double min = results.front();
        double p50 = results.at(size * 0.5);
        double p99 = results.at(size * 0.99);
        double p99_5 = results.at(size * 0.995);
        double p99_9 = results.at(size * 0.999);
        double max = results.back();
        fprintf(file, "%.3lf,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf\n", target_throughput(),
                actual_tput, min, p50, p99, p99_5, p99_9, max);
    } else {
        fprintf(file, "%.3lf,0,0,0,0,0,0,0\n", target_throughput());
    }
    fclose(file);
}

static char *rocksdb_gen_data(uint32_t entry, const char *prefix)
{
    char *data, *entry_str;
    int length;

    data = (char *)malloc(sizeof(char) * (ROCKSDB_DATA_LENGTH + strlen(prefix) + 1));
    sprintf(data, "%s%0*u", prefix, ROCKSDB_DATA_LENGTH, entry);

    return data;
}

rocksdb_t *rocksdb_init(const char *path, int cache_size)
{
    char *err = NULL;
    int i;
    rocksdb_t *db;
    rocksdb_options_t *db_options;
    rocksdb_block_based_table_options_t *table_options;
    rocksdb_cache_t *block_cache;
    rocksdb_writeoptions_t *write_options;
    char *key, *value;

    db_options = rocksdb_options_create();
    rocksdb_options_set_create_if_missing(db_options, 1);
    rocksdb_options_set_allow_mmap_reads(db_options, 1);
    rocksdb_options_set_allow_mmap_writes(db_options, 1);
    rocksdb_options_set_error_if_exists(db_options, 0);
    rocksdb_options_set_compression(db_options, rocksdb_no_compression);
    rocksdb_options_optimize_level_style_compaction(db_options, 512 * 1024 * 1024);

    table_options = rocksdb_block_based_options_create();
    block_cache = rocksdb_cache_create_lru(cache_size * 1024 * 1024);
    rocksdb_block_based_options_set_block_cache(table_options, block_cache);
    rocksdb_options_set_block_based_table_factory(db_options, table_options);

    db = rocksdb_open(db_options, path, &err);
    assert(!db);

    /* Fill in the database */
    write_options = rocksdb_writeoptions_create();
    for (i = 0; i < ROCKSDB_NUM_ENTRIES; ++i) {
        key = rocksdb_gen_data(i, "key");
        value = rocksdb_gen_data(i, "value");
        rocksdb_put(db, write_options, key, ROCKSDB_KEY_LENGTH, value, ROCKSDB_VALUE_LENGTH, &err);
        assert(!err);
        free(key), free(value);
    }

    rocksdb_block_based_options_destroy(table_options);
    rocksdb_cache_destroy(block_cache);
    rocksdb_writeoptions_destroy(write_options);
    rocksdb_options_destroy(db_options);
    return db;
}

void rocksdb_handle_get(rocksdb_t *db, request_t *req)
{
    __nsec service_time;
    rocksdb_readoptions_t *readoptions;
    size_t len;
    char *err = NULL;
    char *key;

    assert(req->type == ROCKSDB_GET);

    readoptions = rocksdb_readoptions_create();
    key = rocksdb_gen_data(req->work.get.entry, "key");
    rocksdb_get(db, readoptions, key, ROCKSDB_KEY_LENGTH, &len, &err);
    assert(!err);
    rocksdb_readoptions_destroy(readoptions);
    free(key);
}

void rocksdb_handle_range_query(rocksdb_t *db, request_t *req)
{
    __nsec service_time;
    rocksdb_readoptions_t *readoptions;
    size_t len;
    char *err = NULL;
    rocksdb_iterator_t *iter;
    size_t i;
    uint32_t start_entry;
    char *key, *value;
    const char *result;

    assert(req->type == ROCKSDB_RANGE);

    start_entry = req->work.range.start;
    readoptions = rocksdb_readoptions_create();
    iter = rocksdb_create_iterator(db, readoptions);
    key = rocksdb_gen_data(start_entry, "key");
    rocksdb_iter_seek(iter, key, ROCKSDB_KEY_LENGTH);
    for (i = 0; i < req->work.range.size; ++i) {
        assert(rocksdb_iter_valid(iter));
        value = rocksdb_gen_data(start_entry + i, "value");
        result = rocksdb_iter_value(iter, &len);
        assert(!strcmp(result, value));
        rocksdb_iter_next(iter);
        free(value);
    }
    rocksdb_readoptions_destroy(readoptions);
    free(key);
}

void benchmark_request(rocksdb_t *db)
{
    int i;
    request_t req;
    const int get_times = 10000;
    const int range_query_times = 1000;
    double get_avg, range_avg;
    __nsec start;

    get_avg = 0;
    req.type = ROCKSDB_GET;
    for (i = 0; i < get_times; i++) {
        if (FLAGS_fake_work) {
            start = now_ns();
            fake_work(FLAGS_get_service_time);
            get_avg += (double)(now_ns() - start) / NSEC_PER_USEC;
        } else {
            req.work.get.entry = random_int_uniform_distribution(0, ROCKSDB_NUM_ENTRIES);
            req.start_time = now_ns();
            rocksdb_handle_get(db, &req);
            req.end_time = now_ns();
            get_avg += (double)(req.end_time - req.start_time) / NSEC_PER_USEC;
        }
    }
    printf("  GET Avg (us) %.3f\n", get_avg / get_times);

    range_avg = 0;
    req.type = ROCKSDB_RANGE;
    for (i = 0; i < range_query_times; i++) {
        if (FLAGS_fake_work) {
            start = now_ns();
            fake_work(FLAGS_range_query_service_time);
            range_avg += (double)(now_ns() - start) / NSEC_PER_USEC;
        } else {
            req.work.range.size = FLAGS_range_query_size;
            req.work.range.start =
                random_int_uniform_distribution(0, ROCKSDB_NUM_ENTRIES - req.work.range.size);
            req.start_time = now_ns();
            rocksdb_handle_range_query(db, &req);
            req.end_time = now_ns();
            range_avg += (double)(req.end_time - req.start_time) / NSEC_PER_USEC;
        }
    }
    printf("  RANGE Avg (us) %.3f\n", range_avg / range_query_times);
}

void init_request(request_t *req)
{
    req->recv_time = 0;
    req->start_time = 0;
    req->end_time = 0;
    req->type = ROCKSDB_GET;
    req->work.get.entry = random_int_uniform_distribution(0, ROCKSDB_NUM_ENTRIES - 1);
}

void init_request_bimodal(request_t *req, double ratio, int size)
{
    bool range_query = false;

    req->recv_time = 0;
    req->start_time = 0;
    req->end_time = 0;

    range_query = random_bernouli_distribution(ratio);
    if (range_query) {
        req->type = ROCKSDB_RANGE;
        req->work.range.size = size;
        req->work.range.start =
            random_int_uniform_distribution(0, ROCKSDB_NUM_ENTRIES - req->work.range.size);
    } else {
        req->type = ROCKSDB_GET;
        req->work.get.entry = random_int_uniform_distribution(0, ROCKSDB_NUM_ENTRIES - 1);
    }
}

void fake_work(uint64_t service_time)
{
    uint64_t i = 0, n = service_time * CPU_FREQ_GHZ;
    do {
        asm volatile("nop");
        i++;
    } while (i < n);
}
