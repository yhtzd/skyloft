/*
 * task.h: support for user-level tasks
 */

#pragma once

#include <stdint.h>

#include <skyloft/sched.h>
#include <utils/list.h>

enum task_state {
    TASK_IDLE,
    TASK_RUNNABLE,
    TASK_BLOCKED,
};

struct switch_frame {
    /* callee saved registers */
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rdi; /* first argument */
    uint64_t rip;
};
struct stack {
    union {
        void *ptr;
        uint8_t payload[RUNTIME_STACK_SIZE];
    };
};

BUILD_ASSERT(sizeof(struct stack) == RUNTIME_STACK_SIZE);

static inline uint64_t stack_top(struct stack *stack)
{
    return (uint64_t)stack + RUNTIME_STACK_SIZE;
}

typedef void (*thread_fn_t)(void *arg);

struct task {
    /* cache line 0 */
    struct list_node link;
    struct stack *stack;
    enum task_state state;
    int id, app_id;
    uint8_t stack_busy;
    bool allow_preempt;
    bool skip_free;
    uint64_t rsp;
    uint8_t pad0[16];
    /* cache line 1~2 */
    uint8_t policy_task_data[POLICY_TASK_DATA_SIZE];
} __aligned_cacheline;

#define task_is_idle(t)     ((t)->state == TASK_IDLE)
#define task_is_runnable(t) ((t)->state == TASK_RUNNABLE)
#define task_is_blocked(t)  ((t)->state == TASK_BLOCKED)
#define task_is_dead(t)     ((t)->state == TASK_DEAD)


struct task *task_create(thread_fn_t fn, void *arg);
struct task *task_create_with_buf(thread_fn_t fn, void **buf, size_t buf_len);
struct task *task_create_idle();
void task_free(struct task *task);

int sched_task_init(void *base);
int sched_task_init_percpu(void);

#ifdef SCHED_PERCPU
#define TASK_SIZE_PER_APP (align_up(sizeof(struct task) * MAX_TASKS_PER_APP, PGSIZE_2MB))
#else
#define TASK_SIZE_PER_APP \
    (align_up((sizeof(struct task) + sizeof(struct stack)) * MAX_TASKS_PER_APP, PGSIZE_2MB))
#endif