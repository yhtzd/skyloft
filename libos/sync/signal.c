#include <signal.h>

#include <skyloft/platform.h>
#include <skyloft/sync/signal.h>
#include <utils/log.h>

static void signal_handler(int signum, siginfo_t *info, void *extra)
{
    log_notice("Handle signal: %d tid: %d", signum, _gettid());
    if (signum == SIGINT || signum == SIGTERM)
        exit(EXIT_SUCCESS);
}

int signal_init()
{
    struct sigaction action;

    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = signal_handler;
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    return 0;
}