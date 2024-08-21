#include <skyloft/uapi/pthread.h>
#include <stdio.h>
#include <stdlib.h>

pthread_mutex_t mutex;
volatile int count;

void *do_work(void *arg)
{
    printf("Hello, world!! %ld\n", (long)arg);
    for (int i = 0; i < 10; i++) {
        sl_pthread_mutex_lock(&mutex);
        count++;
        sl_pthread_mutex_unlock(&mutex);
    }
    return (void *)1;
}

int main(int argc, char *argv[])
{
    pthread_t worker1, worker2;

    printf("Hello, world!\n");
    sl_pthread_mutex_init(&mutex, NULL);
    sl_pthread_create(&worker1, NULL, do_work, (void *)1);
    sl_pthread_create(&worker2, NULL, do_work, (void *)2);
    sl_pthread_join(worker1, NULL);
    sl_pthread_join(worker2, NULL);
    printf("Count %d\n", count);
    fflush(stdout);
    exit(0);
}
