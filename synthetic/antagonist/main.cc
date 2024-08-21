#include <cstdlib>
#include <gflags/gflags.h>

#include <skyloft/uapi/params.h>
#include <skyloft/uapi/task.h>
#include <utils/time.h>

DEFINE_int32(run_time, 5, "Running time (s) of the experiment.");
DEFINE_double(period, 100, "For each period (ms), the worker uses a fixed share of CPU time.");
DEFINE_int32(num_workers, 1, "The number of workers.");
DEFINE_double(work_share, 1.0,
              "Each thread tries to target this share of the cycles on a CPU. For "
              "example, if 'work_share' is 0.5, each thread tries to target 50%% "
              "of cycles on a CPU. Note that 'work_share' must be greater than or "
              "equal to 0.0 and less than or equal to 1.0. (default: 1.0)");
DEFINE_string(output_path, "/tmp/skyloft_synthetic", "The path to the experiment results.");

struct worker_t {
    __nsec start;
    __nsec usage;
    int nth;
} __aligned_cacheline;

static struct worker_t workers[USED_CPUS];

static void synthetic_worker(void *arg)
{
    struct worker_t *worker = (struct worker_t *)arg;
    __nsec period = FLAGS_period * NSEC_PER_MSEC;
    __nsec share = FLAGS_work_share * period;
    __nsec usage, usage_start, finish;
    int n;

    if (!worker->start) {
        worker->start = now_ns();
    }

    while (1) {
        if (now_ns() - worker->start > FLAGS_run_time * NSEC_PER_SEC)
            break;

        /* nth period */
        n = (now_ns() - worker->start + period - 1) / period;
        if (n <= worker->nth) {
            sl_task_yield();
            continue;
        }
        worker->nth = n;

        finish = worker->start + n * period;
        usage = 0;
        usage_start = now_ns();
        while (now_ns() < finish && (usage = now_ns() - usage_start) < share);
        worker->usage += usage;

        sl_task_yield();
    }
}

static void write_results()
{
    int i;
    __nsec usage = 0;

    printf("Antagonist CPU share:\n");
    for (i = 1; i < FLAGS_num_workers + 1; i++) {
        usage += workers[i].usage;
        printf("\tWorker %d %.3lf\n", i, (double)workers[i].usage / FLAGS_run_time / NSEC_PER_SEC);
    }
    printf("\tTotal %.3lf\n", (double)usage / FLAGS_run_time / NSEC_PER_SEC / FLAGS_num_workers);

    FILE *file = fopen(FLAGS_output_path.c_str(), "a");
    fprintf(file, "%.3lf\n", (double)usage / FLAGS_run_time / NSEC_PER_SEC / FLAGS_num_workers);
    fclose(file);
}

static void antagonist_main(void *arg)
{
    int i;
    __nsec usage = 0;

    printf("Antagonist %d starts on CPU %d\n", sl_current_app_id(), sl_current_cpu_id());

    for (i = 0; i < FLAGS_num_workers; i++) {
        workers[i].start = 0;
        workers[i].usage = 0;
        workers[i].nth = -1;
    }

    for (i = 1; i < FLAGS_num_workers + 1; i++) {
        sl_task_spawn_oncpu(i, synthetic_worker, (void *)&workers[i], 0);
    }

    sl_task_yield();

    write_results();

    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    gflags::SetUsageMessage("antagonist [options]");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_num_workers > USED_CPUS) {
        perror("Too many workers\n");
    }

    gflags::ShutDownCommandLineFlags();

    printf("Antagonist with %d thread(s)\n", FLAGS_num_workers);
    sl_libos_start(antagonist_main, NULL);
}