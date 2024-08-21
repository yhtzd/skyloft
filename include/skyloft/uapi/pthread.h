/*
 * pthread.h - support for pthread-like APIs
 */

#ifndef _SKYLOFT_UAPI_PTHREAD_H_
#define _SKYLOFT_UAPI_PTHREAD_H_

#include <pthread.h>

#include <skyloft/uapi/task.h>

#ifdef __cplusplus
extern "C" {
#endif

int sl_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *),
                      void *arg);
int sl_pthread_join(pthread_t thread, void **retval);
int sl_pthread_detach(pthread_t thread);
int sl_pthread_yield(void);
pthread_t sl_pthread_self();
void __attribute__((noreturn)) sl_pthread_exit(void *retval);

int sl_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr);
int sl_pthread_mutex_lock(pthread_mutex_t *mutex);
int sl_pthread_mutex_trylock(pthread_mutex_t *mutex);
int sl_pthread_mutex_unlock(pthread_mutex_t *mutex);
int sl_pthread_mutex_destroy(pthread_mutex_t *mutex);

int sl_pthread_cond_init(pthread_cond_t *__restrict cond,
                         const pthread_condattr_t *__restrict cond_attr);
int sl_pthread_cond_signal(pthread_cond_t *cond);
int sl_pthread_cond_broadcast(pthread_cond_t *cond);
int sl_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int sl_pthread_cond_destroy(pthread_cond_t *cond);

#ifdef __cplusplus
}
#endif

#endif
