/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Minimal OS layer: ESP-IDF/FreeRTOS on target (os_esp.c), pthreads on the host (tests/os_posix.c). */
#ifndef SMARTSLOT_OS_H
#define SMARTSLOT_OS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct os_mutex *os_mutex_t;
os_mutex_t os_mutex_create(void);
void os_mutex_lock(os_mutex_t m);
void os_mutex_unlock(os_mutex_t m);

typedef struct os_sem *os_sem_t;
os_sem_t os_sem_create(void);
void os_sem_give(os_sem_t s);
bool os_sem_take(os_sem_t s, uint32_t timeout_ms); /* false on timeout */

typedef struct os_queue *os_queue_t;
os_queue_t os_queue_create(size_t depth);
bool os_queue_send(os_queue_t q, void *item, uint32_t timeout_ms);
bool os_queue_receive(os_queue_t q, void **item, uint32_t timeout_ms);

int os_task_start(const char *name, void (*fn)(void *), void *arg, size_t stack_bytes, int prio); /* 0 on success */
void os_sleep_ms(uint32_t ms);
uint64_t os_now_ms(void);
void os_wdt_reset(void);
void os_restart(void) __attribute__((noreturn));

enum { OS_LOG_ERROR, OS_LOG_WARN, OS_LOG_INFO, OS_LOG_DEBUG };
void os_log(int level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#ifdef __cplusplus
}
#endif
#endif
