/* SPDX-License-Identifier: GPL-2.0-or-later */
/* select()-based NUT server: one task, one listener, up to UPSD_MAX_CLIENTS line-framed clients. */
#include "upsd.h"

#include <errno.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "nutshim.h"

#define TAG "upsd"
#define SEND_TIMEOUT_S 2 /* a client that stops reading is dropped instead of wedging every other client */

struct client {
    int fd;
    char buf[UPSD_LINE_MAX];
    size_t n;
    nutproto_session sess;
};

static struct client clients[UPSD_MAX_CLIENTS];
static nutproto_hooks hooks;
static int listen_fd = -1;
static uint16_t bound_port;
static volatile bool stop_req;
static os_sem_t done_sem;

static int client_send(void *ctx, const char *buf, size_t len) {
    struct client *c = ctx;
    while (len) {
        ssize_t n = send(c->fd, buf, len, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        buf += n;
        len -= (size_t) n;
    }
    return 0;
}

int upsd_num_logins(void) {
    int n = 0;
    for (int i = 0; i < UPSD_MAX_CLIENTS; i++)
        if (clients[i].fd >= 0 && clients[i].sess.logged_in) n++;
    return n;
}

uint16_t upsd_port(void) { return bound_port; }

static void drop(struct client *c) {
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    c->n = 0;
}

/* Handles every complete line in the buffer; returns false when the client must be dropped. */
static bool feed(struct client *c) {
    for (;;) {
        char *nl = memchr(c->buf, '\n', c->n);
        if (!nl) break;
        size_t len = (size_t) (nl - c->buf);
        c->buf[len] = 0;
        if (len && c->buf[len - 1] == '\r') c->buf[len - 1] = 0;
        int close_it = nutproto_handle(&c->sess, c->buf);
        size_t used = len + 1;
        memmove(c->buf, c->buf + used, c->n - used);
        c->n -= used;
        if (close_it) return false;
    }
    if (c->n >= sizeof c->buf) { /* a full buffer with no terminator: refuse, never overflow */
        client_send(c, "ERR INVALID-ARGUMENT\n", 21);
        return false;
    }
    return true;
}

static void accept_one(void) {
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) return;
    struct client *c = NULL;
    for (int i = 0; i < UPSD_MAX_CLIENTS && !c; i++)
        if (clients[i].fd < 0) c = &clients[i];
    if (!c) {
        close(fd); /* fifth client: no greeting, the live four are untouched */
        os_log(OS_LOG_WARN, TAG, "refused client: %d already connected", UPSD_MAX_CLIENTS);
        return;
    }
    int on = 1;
    struct timeval tv = {SEND_TIMEOUT_S, 0};
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    c->fd = fd;
    c->n = 0;
    nutproto_session_init(&c->sess, &hooks, c);
}

static void server_task(void *arg) {
    (void) arg;
    while (!stop_req) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(listen_fd, &rs);
        int maxfd = listen_fd;
        for (int i = 0; i < UPSD_MAX_CLIENTS; i++)
            if (clients[i].fd >= 0) {
                FD_SET(clients[i].fd, &rs);
                if (clients[i].fd > maxfd) maxfd = clients[i].fd;
            }
        struct timeval tv = {0, 200000};
        if (select(maxfd + 1, &rs, NULL, NULL, &tv) <= 0) continue;
        if (FD_ISSET(listen_fd, &rs)) accept_one();
        for (int i = 0; i < UPSD_MAX_CLIENTS; i++) {
            struct client *c = &clients[i];
            if (c->fd < 0 || !FD_ISSET(c->fd, &rs)) continue;
            ssize_t n = recv(c->fd, c->buf + c->n, sizeof c->buf - c->n, 0);
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (n <= 0) { drop(c); continue; }
            c->n += (size_t) n;
            if (!feed(c)) drop(c);
        }
    }
    for (int i = 0; i < UPSD_MAX_CLIENTS; i++) drop(&clients[i]);
    close(listen_fd);
    listen_fd = -1;
    os_sem_give(done_sem);
}

int upsd_start(const nutproto_hooks *h, uint16_t port) {
    if (listen_fd >= 0) return -1;
    hooks = *h;
    hooks.send = client_send; /* the caller does not know the transport */
    if (!done_sem) done_sem = os_sem_create();
    for (int i = 0; i < UPSD_MAX_CLIENTS; i++) clients[i].fd = -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    socklen_t al = sizeof a;
    if (bind(fd, (struct sockaddr *) &a, sizeof a) < 0 || listen(fd, UPSD_MAX_CLIENTS) < 0 ||
        getsockname(fd, (struct sockaddr *) &a, &al) < 0) {
        close(fd);
        return -1;
    }
    bound_port = ntohs(a.sin_port);
    listen_fd = fd;
    stop_req = false;
    if (os_task_start("upsd", server_task, NULL, 8192, 5) != 0) {
        close(fd);
        listen_fd = -1;
        return -1;
    }
    os_log(OS_LOG_INFO, TAG, "NUT server listening on port %u", bound_port);
    return 0;
}

void upsd_stop(void) {
    if (listen_fd < 0) return;
    stop_req = true;
    os_sem_take(done_sem, 3000);
}
