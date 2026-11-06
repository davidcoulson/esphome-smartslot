/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The task that owns the NUT driver: probe gate, poll loop, and the INSTCMD/SET request queue. */
#include "nutdrv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nutshim.h"
#include "serial.h"

#define TAG "nutdrv"
#define SLOTS 4
#define STALE_POLLS_BEFORE_REPROBE 5

enum { REQ_INSTCMD, REQ_SETVAR };
enum { SLOT_FREE, SLOT_QUEUED, SLOT_RUNNING, SLOT_DONE };

struct slot {
    int state, type, result;
    bool abandoned, detached, has_param;
    char name[64], param[64];
    os_sem_t done;
};

uint32_t nutdrv_probe_retry_ms = 30000;

static struct slot slots[SLOTS];
static os_mutex_t slot_lock;
static os_queue_t queue;
static os_sem_t stopped_sem;
static volatile nutdrv_state_t state = NUTDRV_STARTING;
static volatile bool stop_req, running;
static nutdrv_config cfg;
static char baud_str[16];
static const char *kv[4];

nutdrv_state_t nutdrv_state(void) { return state; }

static int request(int type, const char *name, const char *param, uint32_t timeout_ms) {
    int fail = type == REQ_INSTCMD ? STAT_INSTCMD_FAILED : STAT_SET_FAILED;
    if (strlen(name) >= sizeof slots[0].name || (param && strlen(param) >= sizeof slots[0].param)) return fail;
    struct slot *s = NULL;
    /* Admission, slot allocation and the (non-blocking) enqueue happen under one lock, and the driver task flips
     * `state` under the same lock before it drains the queue: nothing can slip in behind a drain and run on a later
     * UPS session. */
    os_mutex_lock(slot_lock);
    if (running && state == NUTDRV_RUNNING)
        for (int i = 0; i < SLOTS && !s; i++)
            if (slots[i].state == SLOT_FREE) s = &slots[i];
    if (s) {
        s->state = SLOT_QUEUED;
        s->type = type;
        s->abandoned = false;
        s->detached = timeout_ms == 0; /* queue and forget: the result is only logged */
        s->has_param = param != NULL;
        snprintf(s->name, sizeof s->name, "%s", name);
        snprintf(s->param, sizeof s->param, "%s", param ? param : "");
        while (os_sem_take(s->done, 0)) {} /* drop a late signal left by an abandoned request */
        if (!os_queue_send(queue, s, 0)) {
            s->state = SLOT_FREE;
            s = NULL;
        }
    }
    os_mutex_unlock(slot_lock);
    if (!s) return fail;
    /* A detached slot belongs to the driver task from here on (it may already be free and reused): do not touch it. */
    if (timeout_ms == 0) return type == REQ_INSTCMD ? STAT_INSTCMD_HANDLED : STAT_SET_HANDLED;
    os_sem_take(s->done, timeout_ms);
    int result = fail;
    os_mutex_lock(slot_lock);
    if (s->state == SLOT_DONE) {
        result = s->result;
        s->state = SLOT_FREE;
    } else {
        s->abandoned = true; /* the driver task frees the slot; a queued request is skipped */
        os_log(OS_LOG_WARN, TAG, "%s timed out after %u ms", name, (unsigned) timeout_ms);
    }
    os_mutex_unlock(slot_lock);
    return result;
}

int nutdrv_instcmd(const char *cmd, const char *param, uint32_t timeout_ms) {
    return request(REQ_INSTCMD, cmd, param, timeout_ms);
}
int nutdrv_setvar(const char *var, const char *val, uint32_t timeout_ms) {
    return request(REQ_SETVAR, var, val, timeout_ms);
}

static void run_slot(struct slot *s) {
    os_mutex_lock(slot_lock);
    if (s->abandoned) { /* the caller already reported failure: do not act on it now */
        s->state = SLOT_FREE;
        os_mutex_unlock(slot_lock);
        return;
    }
    s->state = SLOT_RUNNING;
    os_mutex_unlock(slot_lock);
    int r;
    if (s->type == REQ_INSTCMD) r = upsh.instcmd ? upsh.instcmd(s->name, s->has_param ? s->param : NULL) : STAT_INSTCMD_FAILED;
    else r = upsh.setvar ? upsh.setvar(s->name, s->param) : STAT_SET_FAILED;
    os_mutex_lock(slot_lock);
    if (s->abandoned || s->detached) {
        if (s->detached && r != 0) os_log(OS_LOG_WARN, TAG, "%s %s failed (%d)", s->type == REQ_INSTCMD ? "INSTCMD" : "SET", s->name, r);
        s->state = SLOT_FREE;
    } else {
        s->result = r;
        s->state = SLOT_DONE;
        os_sem_give(s->done);
    }
    os_mutex_unlock(slot_lock);
}

/* Fails everything still queued when the driver goes away, so no caller waits for its full timeout. */
static void drain_queue(bool run) {
    void *item;
    while (os_queue_receive(queue, &item, 0)) {
        if (run) {
            run_slot(item);
        } else {
            struct slot *s = item;
            os_mutex_lock(slot_lock);
            if (s->abandoned || s->detached) s->state = SLOT_FREE;
            else { s->result = s->type == REQ_INSTCMD ? STAT_INSTCMD_FAILED : STAT_SET_FAILED; s->state = SLOT_DONE; os_sem_give(s->done); }
            os_mutex_unlock(slot_lock);
        }
    }
}

static void sleep_checked(uint32_t ms) {
    while (ms && !stop_req) {
        uint32_t step = ms > 100 ? 100 : ms;
        os_sleep_ms(step);
        os_wdt_reset();
        ms -= step;
    }
}

static void task(void *arg) {
    (void) arg;
    kv[0] = "cmdsrc"; kv[1] = cfg.cmdsrc; kv[2] = "baudrate"; kv[3] = baud_str;
    nutshim_setvar_table(kv, 4);
    upsdrv_makevartable();
    while (!stop_req) {
        state = NUTDRV_NO_UPS;
        dstate_datastale();
        while (!stop_req && !microlink_probe()) {
            os_log(OS_LOG_WARN, TAG, "no Microlink UPS answered the probe, retrying in %u ms", (unsigned) nutdrv_probe_retry_ms);
            sleep_checked(nutdrv_probe_retry_ms);
        }
        if (stop_req) break;
        state = NUTDRV_STARTING;
        device_path = (char *) "smartslot";
        upsdrv_initups();
        upsdrv_initinfo();
        state = NUTDRV_RUNNING;
        int stale = 0;
        while (!stop_req && stale < STALE_POLLS_BEFORE_REPROBE) {
            upsdrv_updateinfo();
            stale = dstate_is_dataok() ? 0 : stale + 1;
            uint64_t end = os_now_ms() + (poll_interval > 0 ? (uint64_t) poll_interval * 1000 : 10);
            for (;;) { /* wait for the next poll, serving requests as they arrive */
                uint64_t now = os_now_ms();
                void *item;
                if (stop_req || now >= end) break;
                uint64_t left = end - now > 100 ? 100 : end - now;
                if (os_queue_receive(queue, &item, (uint32_t) left)) run_slot(item);
                os_wdt_reset();
            }
        }
        os_mutex_lock(slot_lock); /* see request(): no admission after this point until the next session */
        state = NUTDRV_NO_UPS;
        os_mutex_unlock(slot_lock);
        drain_queue(false);
        dstate_datastale();
        upsdrv_cleanup();
        if (!stop_req) os_log(OS_LOG_WARN, TAG, "UPS data stale for %d polls, probing again", STALE_POLLS_BEFORE_REPROBE);
    }
    state = NUTDRV_NO_UPS;
    drain_queue(false);
    running = false;
    os_sem_give(stopped_sem);
}

int nutdrv_start(const nutdrv_config *c) {
    if (running) return -1;
    if (!slot_lock) {
        slot_lock = os_mutex_create();
        queue = os_queue_create(SLOTS);
        stopped_sem = os_sem_create();
        for (int i = 0; i < SLOTS; i++) slots[i].done = os_sem_create();
    }
    for (int i = 0; i < SLOTS; i++) slots[i].state = SLOT_FREE;
    cfg = *c;
    snprintf(baud_str, sizeof baud_str, "%u", (unsigned) cfg.baudrate);
    if (cfg.uartsel_gpio >= 0) uartsel_init(cfg.uartsel_gpio, cfg.uartsel_level);
    stop_req = false;
    running = true;
    state = NUTDRV_STARTING;
    if (os_task_start("nutdrv", task, NULL, 8192, 6) != 0) {
        running = false;
        return -1;
    }
    return 0;
}

void nutdrv_stop(void) {
    if (!running) return;
    stop_req = true;
    os_sem_take(stopped_sem, 5000);
}

int nutshim_lookup(const char *var, char *buf, size_t len) {
    if (state != NUTDRV_RUNNING || !dstate_is_servable()) return -1;
    return len && dstate_copyinfo(var, buf, len) ? 1 : 0; /* copies under the dstate mutex */
}

bool nutshim_value_for_publish(const char *var, char *buf, size_t len) { return nutshim_lookup(var, buf, len) == 1; }

bool nutshim_parse_number(const char *s, float *out) {
    char *end;
    float v = strtof(s, &end);
    if (end == s || *end || !isfinite(v)) return false;
    *out = v;
    return true;
}
