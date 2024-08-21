
#include <stdio.h>

#include <skyloft/uapi/task.h>

static int main_argc, main_ret;
static char **main_argv;

int __real_main(int, char **);

static void __real_entry(void *arg)
{
    main_ret = __real_main(main_argc, main_argv);
}

int __wrap_main(int argc, char **argv)
{
    int ret;

    main_argv = &argv[0];
    main_argc = argc;

    ret = sl_libos_start(__real_entry, NULL);
    if (ret) {
        fprintf(stderr, "Failed to start libos\n");
        return ret;
    }

    return main_ret;
}
