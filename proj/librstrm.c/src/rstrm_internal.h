/**
 * rstrm_internal.h - Private reliable-stream runtime definitions.
 */
#ifndef KC_RSTRM_INTERNAL_H
#define KC_RSTRM_INTERNAL_H

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "librstrm.h"
#include "ikcp.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef HANDLE rstrm_thread_t;
typedef CRITICAL_SECTION rstrm_mutex_t;
typedef CONDITION_VARIABLE rstrm_cond_t;
#else
#include <errno.h>
#include <pthread.h>
typedef pthread_t rstrm_thread_t;
typedef pthread_mutex_t rstrm_mutex_t;
typedef pthread_cond_t rstrm_cond_t;
#endif

#ifndef KC_RSTRM_BUILD_VERSION
#define KC_RSTRM_BUILD_VERSION 0
#endif

#define RSTRM_CONV                 0x3153524bU
#define RSTRM_DEFAULT_MTU          1200U
#define RSTRM_MIN_MTU              512U
#define RSTRM_MAX_MTU              65535U
#define RSTRM_INTERVAL_MS          20U
#define RSTRM_SEND_WINDOW          64
#define RSTRM_RECV_WINDOW          128
#define RSTRM_FAST_RESEND          2
#define RSTRM_KCP_HIGH_SEGMENTS    128
#define RSTRM_KCP_LOW_SEGMENTS     64
#define RSTRM_MAX_PENDING_BYTES    (4U * 1024U * 1024U)
#define RSTRM_LOW_PENDING_BYTES    (2U * 1024U * 1024U)
#define RSTRM_MAX_INPUT_BYTES      (4U * 1024U * 1024U)
#define RSTRM_RECORD_PAYLOAD       32768U
#define RSTRM_RECORD_DATA          1U
#define RSTRM_RECORD_FIN           2U
#define RSTRM_EVENT_DATA           1
#define RSTRM_EVENT_EOF            2
#define RSTRM_EVENT_DRAIN          3
#define RSTRM_EVENT_ERROR          4

#ifdef _WIN32
#define RSTRM_THREAD_RET DWORD WINAPI
#else
#define RSTRM_THREAD_RET void *
#endif

typedef struct rstrm_buffer {
    struct rstrm_buffer *next;
    size_t size;
    size_t offset;
    unsigned char data[];
} rstrm_buffer_t;

typedef struct rstrm_emit {
    struct rstrm_emit *next;
    size_t size;
    unsigned char data[];
} rstrm_emit_t;

typedef struct rstrm_event {
    struct rstrm_event *next;
    int type;
    int status;
    size_t size;
    unsigned char data[];
} rstrm_event_t;

struct kc_rstrm {
    ikcpcb *kcp;
    size_t mtu;
    kc_rstrm_receive_fn receive;
    kc_rstrm_datagram_fn datagram;
    kc_rstrm_eof_fn eof;
    kc_rstrm_drain_fn drain;
    kc_rstrm_error_fn error;
    void *userdata;

    rstrm_mutex_t mutex;
    rstrm_cond_t cond;
    rstrm_thread_t thread;
    int thread_started;
#ifdef _WIN32
    DWORD worker_id;
#endif

    int closing;
    int self_release;
    int failed;
    int error_delivered;
    int local_shutdown;
    int local_fin_sent;
    int remote_eof;
    int full_latched;
    int output_error;

    rstrm_buffer_t *send_head;
    rstrm_buffer_t *send_tail;
    size_t pending_bytes;
    rstrm_buffer_t *input_head;
    rstrm_buffer_t *input_tail;
    size_t input_bytes;

    rstrm_emit_t *emit_head;
    rstrm_emit_t *emit_tail;
};

void rstrm_lock(kc_rstrm_t *rstrm);
void rstrm_unlock(kc_rstrm_t *rstrm);
void rstrm_signal(kc_rstrm_t *rstrm);
int rstrm_is_worker(kc_rstrm_t *rstrm);
rstrm_buffer_t *rstrm_buffer_new(const void *data, size_t size);
void rstrm_buffer_push(rstrm_buffer_t **head, rstrm_buffer_t **tail,
    rstrm_buffer_t *buffer);
void rstrm_release(kc_rstrm_t *rstrm);
RSTRM_THREAD_RET rstrm_worker(void *arg);
int rstrm_kcp_output(const char *buf, int len, ikcpcb *kcp, void *user);

#endif
