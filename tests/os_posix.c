/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "os.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

struct os_mutex { pthread_mutex_t m; };
os_mutex_t os_mutex_create(void) {
    os_mutex_t m = malloc(sizeof *m);
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m->m, &a);
    return m;
}
void os_mutex_lock(os_mutex_t m) { pthread_mutex_lock(&m->m); }
void os_mutex_unlock(os_mutex_t m) { pthread_mutex_unlock(&m->m); }

struct os_sem { sem_t s; };
os_sem_t os_sem_create(void) { os_sem_t s = malloc(sizeof *s); sem_init(&s->s, 0, 0); return s; }
void os_sem_give(os_sem_t s) { sem_post(&s->s); }

static void deadline(struct timespec *ts, uint32_t ms) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long) (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}
bool os_sem_take(os_sem_t s, uint32_t timeout_ms) {
    struct timespec ts;
    deadline(&ts, timeout_ms);
    int r;
    while ((r = sem_timedwait(&s->s, &ts)) != 0 && errno == EINTR) {}
    return r == 0;
}

#define QMAX 16
struct os_queue { pthread_mutex_t m; pthread_cond_t c; void *items[QMAX]; size_t head, n, depth; };
os_queue_t os_queue_create(size_t depth) {
    os_queue_t q = calloc(1, sizeof *q);
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->c, NULL);
    q->depth = depth > QMAX ? QMAX : depth;
    return q;
}
bool os_queue_send(os_queue_t q, void *item, uint32_t timeout_ms) {
    struct timespec ts;
    deadline(&ts, timeout_ms);
    pthread_mutex_lock(&q->m);
    while (q->n == q->depth)
        if (pthread_cond_timedwait(&q->c, &q->m, &ts) != 0) { pthread_mutex_unlock(&q->m); return false; }
    q->items[(q->head + q->n++) % QMAX] = item;
    pthread_cond_broadcast(&q->c);
    pthread_mutex_unlock(&q->m);
    return true;
}
bool os_queue_receive(os_queue_t q, void **item, uint32_t timeout_ms) {
    struct timespec ts;
    deadline(&ts, timeout_ms);
    pthread_mutex_lock(&q->m);
    while (q->n == 0)
        if (pthread_cond_timedwait(&q->c, &q->m, &ts) != 0) { pthread_mutex_unlock(&q->m); return false; }
    *item = q->items[q->head];
    q->head = (q->head + 1) % QMAX;
    q->n--;
    pthread_cond_broadcast(&q->c);
    pthread_mutex_unlock(&q->m);
    return true;
}

struct task_start { void (*fn)(void *); void *arg; };
static void *task_tramp(void *p) { struct task_start t = *(struct task_start *) p; free(p); t.fn(t.arg); return NULL; }
int os_task_start(const char *name, void (*fn)(void *), void *arg, size_t stack_bytes, int prio) {
    (void) name; (void) stack_bytes; (void) prio;
    struct task_start *t = malloc(sizeof *t);
    t->fn = fn; t->arg = arg;
    pthread_t th;
    if (pthread_create(&th, NULL, task_tramp, t) != 0) { free(t); return -1; }
    pthread_detach(th);
    return 0;
}
void os_sleep_ms(uint32_t ms) {
    struct timespec ts = {ms / 1000, (long) (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
}
uint64_t os_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
void os_wdt_reset(void) {}
void os_restart(void) { exit(1); }
void os_log(int level, const char *tag, const char *fmt, ...) {
    static const char *const names[] = {"E", "W", "I", "D"};
    if (!getenv("SMARTSLOT_TEST_LOG")) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s %s: ", names[level], tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
