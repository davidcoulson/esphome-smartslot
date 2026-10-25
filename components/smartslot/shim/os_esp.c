/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef ESP_PLATFORM
#include "os.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static TickType_t ticks(uint32_t ms) { return pdMS_TO_TICKS(ms); }

os_mutex_t os_mutex_create(void) { return (os_mutex_t) xSemaphoreCreateMutex(); }
void os_mutex_lock(os_mutex_t m) { xSemaphoreTake((SemaphoreHandle_t) m, portMAX_DELAY); }
void os_mutex_unlock(os_mutex_t m) { xSemaphoreGive((SemaphoreHandle_t) m); }

os_sem_t os_sem_create(void) { return (os_sem_t) xSemaphoreCreateBinary(); }
void os_sem_give(os_sem_t s) { xSemaphoreGive((SemaphoreHandle_t) s); }
bool os_sem_take(os_sem_t s, uint32_t timeout_ms) {
    return xSemaphoreTake((SemaphoreHandle_t) s, ticks(timeout_ms)) == pdTRUE;
}

os_queue_t os_queue_create(size_t depth) { return (os_queue_t) xQueueCreate(depth, sizeof(void *)); }
bool os_queue_send(os_queue_t q, void *item, uint32_t timeout_ms) {
    return xQueueSend((QueueHandle_t) q, &item, ticks(timeout_ms)) == pdTRUE;
}
bool os_queue_receive(os_queue_t q, void **item, uint32_t timeout_ms) {
    return xQueueReceive((QueueHandle_t) q, item, ticks(timeout_ms)) == pdTRUE;
}

int os_task_start(const char *name, void (*fn)(void *), void *arg, size_t stack_bytes, int prio) {
    return xTaskCreatePinnedToCore(fn, name, stack_bytes, arg, prio, NULL, tskNO_AFFINITY) == pdPASS ? 0 : -1;
}
void os_sleep_ms(uint32_t ms) { vTaskDelay(ticks(ms)); }
uint64_t os_now_ms(void) { return (uint64_t) (esp_timer_get_time() / 1000); }
void os_wdt_reset(void) { (void) esp_task_wdt_reset(); } /* error if this task did not subscribe: ignored */
void os_restart(void) { esp_restart(); }

void os_log(int level, const char *tag, const char *fmt, ...) {
    static const esp_log_level_t map[] = {ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG};
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ESP_LOG_LEVEL(map[level], tag, "%s", buf);
}
#endif
