/**
 * librstrm.c - Reliable ordered byte stream.
 * Summary: Public reliable-stream lifecycle over caller-supplied datagrams.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

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
#define RSTRM_RECORD_FIN_ACK       3U
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
    int local_fin_acked;
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

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_lock(kc_rstrm_t *rstrm);

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_unlock(kc_rstrm_t *rstrm);

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_signal(kc_rstrm_t *rstrm);

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static int rstrm_is_worker(kc_rstrm_t *rstrm);
static rstrm_buffer_t *rstrm_buffer_new(const void *data, size_t size);

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_buffer_push(rstrm_buffer_t **head, rstrm_buffer_t **tail,
    rstrm_buffer_t *buffer);

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_release(kc_rstrm_t *rstrm);

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static RSTRM_THREAD_RET rstrm_worker(void *arg);

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static int rstrm_kcp_output(const char *buf, int len, ikcpcb *kcp, void *user);

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static uint32_t rstrm_now_ms(void)
{
#ifdef _WIN32
    return (uint32_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL +
        (uint64_t)ts.tv_nsec / 1000000ULL);
#endif
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_lock(kc_rstrm_t *rstrm)
{
#ifdef _WIN32
    EnterCriticalSection(&rstrm->mutex);
#else
    (void)pthread_mutex_lock(&rstrm->mutex);
#endif
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_unlock(kc_rstrm_t *rstrm)
{
#ifdef _WIN32
    LeaveCriticalSection(&rstrm->mutex);
#else
    (void)pthread_mutex_unlock(&rstrm->mutex);
#endif
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_signal(kc_rstrm_t *rstrm)
{
#ifdef _WIN32
    WakeConditionVariable(&rstrm->cond);
#else
    (void)pthread_cond_signal(&rstrm->cond);
#endif
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_wait(kc_rstrm_t *rstrm, unsigned timeout_ms)
{
#ifdef _WIN32
    (void)SleepConditionVariableCS(&rstrm->cond, &rstrm->mutex, timeout_ms);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return;
    ts.tv_sec += (time_t)(timeout_ms / 1000U);
    ts.tv_nsec += (long)(timeout_ms % 1000U) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    (void)pthread_cond_timedwait(&rstrm->cond, &rstrm->mutex, &ts);
#endif
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static int rstrm_is_worker(kc_rstrm_t *rstrm)
{
#ifdef _WIN32
    return rstrm->thread_started && GetCurrentThreadId() == rstrm->worker_id;
#else
    return rstrm->thread_started && pthread_equal(pthread_self(), rstrm->thread);
#endif
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static int rstrm_closing(kc_rstrm_t *rstrm)
{
    int closing;
    rstrm_lock(rstrm);
    closing = rstrm->closing;
    rstrm_unlock(rstrm);
    return closing;
}

/**
 * Allocates one private byte buffer.
 * @param data Bytes to copy.
 * @param size Number of bytes.
 * @return New buffer, or NULL on allocation failure.
 */
static rstrm_buffer_t *rstrm_buffer_new(const void *data, size_t size)
{
    rstrm_buffer_t *buffer;
    if (size > SIZE_MAX - sizeof(*buffer)) return NULL;
    buffer = (rstrm_buffer_t *)malloc(sizeof(*buffer) + size);
    if (!buffer) return NULL;
    buffer->next = NULL;
    buffer->size = size;
    buffer->offset = 0;
    if (size > 0) memcpy(buffer->data, data, size);
    return buffer;
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_buffer_push(rstrm_buffer_t **head, rstrm_buffer_t **tail,
    rstrm_buffer_t *buffer)
{
    if (*tail) (*tail)->next = buffer;
    else *head = buffer;
    *tail = buffer;
}

/**
 * Removes one private byte buffer from a queue.
 * @param head Queue head.
 * @param tail Queue tail.
 * @return Removed buffer, or NULL when the queue is empty.
 */
static rstrm_buffer_t *rstrm_buffer_pop(rstrm_buffer_t **head,
    rstrm_buffer_t **tail)
{
    rstrm_buffer_t *buffer = *head;
    if (!buffer) return NULL;
    *head = buffer->next;
    if (!*head) *tail = NULL;
    buffer->next = NULL;
    return buffer;
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_buffer_list_free(rstrm_buffer_t *head)
{
    while (head) {
        rstrm_buffer_t *next = head->next;
        free(head);
        head = next;
    }
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_emit_list_free(rstrm_emit_t *head)
{
    while (head) {
        rstrm_emit_t *next = head->next;
        free(head);
        head = next;
    }
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_event_list_free(rstrm_event_t *head)
{
    while (head) {
        rstrm_event_t *next = head->next;
        free(head);
        head = next;
    }
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static int rstrm_event_push(rstrm_event_t **head, rstrm_event_t **tail,
    int type, int status, const void *data, size_t size)
{
    rstrm_event_t *event;
    if (size > SIZE_MAX - sizeof(*event)) return -1;
    event = (rstrm_event_t *)malloc(sizeof(*event) + size);
    if (!event) return -1;
    event->next = NULL;
    event->type = type;
    event->status = status;
    event->size = size;
    if (size > 0) memcpy(event->data, data, size);
    if (*tail) (*tail)->next = event;
    else *head = event;
    *tail = event;
    return 0;
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_fail_locked(kc_rstrm_t *rstrm, int status)
{
    if (!rstrm->failed) rstrm->failed = status;
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static int rstrm_kcp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
    kc_rstrm_t *rstrm = (kc_rstrm_t *)user;
    rstrm_emit_t *emit;
    (void)kcp;
    if (!rstrm || !buf || len <= 0 || (size_t)len > rstrm->mtu) {
        if (rstrm) rstrm->output_error = KC_RSTRM_EPROTO;
        return -1;
    }
    if ((size_t)len > SIZE_MAX - sizeof(*emit)) {
        rstrm->output_error = KC_RSTRM_ENOMEM;
        return -1;
    }
    emit = (rstrm_emit_t *)malloc(sizeof(*emit) + (size_t)len);
    if (!emit) {
        rstrm->output_error = KC_RSTRM_ENOMEM;
        return -1;
    }
    emit->next = NULL;
    emit->size = (size_t)len;
    memcpy(emit->data, buf, (size_t)len);
    if (rstrm->emit_tail) rstrm->emit_tail->next = emit;
    else rstrm->emit_head = emit;
    rstrm->emit_tail = emit;
    return 0;
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static int rstrm_feed_send_locked(kc_rstrm_t *rstrm)
{
    unsigned char record[RSTRM_RECORD_PAYLOAD + 1U];

    while (rstrm->send_head &&
        ikcp_waitsnd(rstrm->kcp) < RSTRM_KCP_HIGH_SEGMENTS)
    {
        rstrm_buffer_t *buffer = rstrm->send_head;
        size_t remaining = buffer->size - buffer->offset;
        size_t part = remaining > RSTRM_RECORD_PAYLOAD ?
            RSTRM_RECORD_PAYLOAD : remaining;
        int rc;

        record[0] = RSTRM_RECORD_DATA;
        memcpy(record + 1, buffer->data + buffer->offset, part);
        rc = ikcp_send(rstrm->kcp, (const char *)record, (int)(part + 1U));
        if (rc < 0) return KC_RSTRM_ENOMEM;

        buffer->offset += part;
        rstrm->pending_bytes -= part;
        if (buffer->offset == buffer->size) {
            buffer = rstrm_buffer_pop(&rstrm->send_head, &rstrm->send_tail);
            free(buffer);
        }
    }

    if (rstrm->local_shutdown && !rstrm->local_fin_sent &&
        !rstrm->send_head &&
        ikcp_waitsnd(rstrm->kcp) < RSTRM_KCP_HIGH_SEGMENTS)
    {
        const unsigned char fin = RSTRM_RECORD_FIN;
        if (ikcp_send(rstrm->kcp, (const char *)&fin, 1) < 0)
            return KC_RSTRM_ENOMEM;
        rstrm->local_fin_sent = 1;
    }
    return KC_RSTRM_OK;
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static int rstrm_feed_input_locked(kc_rstrm_t *rstrm)
{
    rstrm_buffer_t *buffer;
    while ((buffer = rstrm_buffer_pop(&rstrm->input_head,
        &rstrm->input_tail)) != NULL)
    {
        int rc;
        rstrm->input_bytes -= buffer->size;
        rc = ikcp_input(rstrm->kcp, (const char *)buffer->data,
            (long)buffer->size);
        free(buffer);
        if (rc < 0) return KC_RSTRM_EPROTO;
    }
    return KC_RSTRM_OK;
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static int rstrm_collect_receive_locked(kc_rstrm_t *rstrm,
    rstrm_event_t **events, rstrm_event_t **events_tail)
{
    for (;;) {
        int size = ikcp_peeksize(rstrm->kcp);
        unsigned char *record;
        int got;

        if (size < 0) return KC_RSTRM_OK;
        if (size == 0) return KC_RSTRM_EPROTO;
        record = (unsigned char *)malloc((size_t)size);
        if (!record) return KC_RSTRM_ENOMEM;
        got = ikcp_recv(rstrm->kcp, (char *)record, size);
        if (got != size) {
            free(record);
            return KC_RSTRM_EPROTO;
        }

        if (record[0] == RSTRM_RECORD_DATA) {
            if (size <= 1 || rstrm->remote_eof ||
                rstrm_event_push(events, events_tail, RSTRM_EVENT_DATA, 0,
                    record + 1, (size_t)size - 1U) != 0)
            {
                int status = rstrm->remote_eof ? KC_RSTRM_EPROTO :
                    (size <= 1 ? KC_RSTRM_EPROTO : KC_RSTRM_ENOMEM);
                free(record);
                return status;
            }
        } else if (record[0] == RSTRM_RECORD_FIN) {
            const unsigned char fin_ack = RSTRM_RECORD_FIN_ACK;

            if (size != 1 || rstrm->remote_eof) {
                free(record);
                return KC_RSTRM_EPROTO;
            }
            rstrm->remote_eof = 1;
            if (ikcp_send(rstrm->kcp, (const char *)&fin_ack, 1) < 0) {
                free(record);
                return KC_RSTRM_ENOMEM;
            }
            if (rstrm_event_push(events, events_tail, RSTRM_EVENT_EOF, 0,
                NULL, 0) != 0)
            {
                free(record);
                return KC_RSTRM_ENOMEM;
            }
        } else if (record[0] == RSTRM_RECORD_FIN_ACK) {
            if (size != 1 || !rstrm->local_fin_sent ||
                rstrm->local_fin_acked)
            {
                free(record);
                return KC_RSTRM_EPROTO;
            }
            rstrm->local_fin_acked = 1;
        } else {
            free(record);
            return KC_RSTRM_EPROTO;
        }
        free(record);
    }
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_dispatch_events(kc_rstrm_t *rstrm, rstrm_event_t *events)
{
    rstrm_event_t *event = events;
    while (event) {
        rstrm_event_t *next = event->next;
        if (event->type == RSTRM_EVENT_DATA && rstrm->receive)
            rstrm->receive(event->data, event->size, rstrm->userdata);
        else if (event->type == RSTRM_EVENT_EOF && rstrm->eof)
            rstrm->eof(rstrm->userdata);
        else if (event->type == RSTRM_EVENT_DRAIN && rstrm->drain)
            rstrm->drain(rstrm->userdata);
        else if (event->type == RSTRM_EVENT_ERROR && rstrm->error)
            rstrm->error(event->status, rstrm->userdata);
        free(event);
        event = next;
        if (rstrm_closing(rstrm)) {
            rstrm_event_list_free(event);
            break;
        }
    }
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static int rstrm_dispatch_emits(kc_rstrm_t *rstrm, rstrm_emit_t *emits)
{
    rstrm_emit_t *emit = emits;
    while (emit) {
        rstrm_emit_t *next = emit->next;
        int rc = rstrm->datagram(emit->data, emit->size, rstrm->userdata);
        free(emit);
        if (rc != 0) {
            rstrm_emit_list_free(next);
            return KC_RSTRM_ETRANSPORT;
        }
        emit = next;
        if (rstrm_closing(rstrm)) {
            rstrm_emit_list_free(emit);
            break;
        }
    }
    return KC_RSTRM_OK;
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
static void rstrm_release(kc_rstrm_t *rstrm)
{
    if (!rstrm) return;
    rstrm_buffer_list_free(rstrm->send_head);
    rstrm_buffer_list_free(rstrm->input_head);
    rstrm_emit_list_free(rstrm->emit_head);
    if (rstrm->kcp) ikcp_release(rstrm->kcp);
#ifdef _WIN32
    DeleteCriticalSection(&rstrm->mutex);
    if (rstrm->thread) CloseHandle(rstrm->thread);
#else
    (void)pthread_cond_destroy(&rstrm->cond);
    (void)pthread_mutex_destroy(&rstrm->mutex);
#endif
    free(rstrm);
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
static RSTRM_THREAD_RET rstrm_worker(void *arg)
{
    kc_rstrm_t *rstrm = (kc_rstrm_t *)arg;
#ifdef _WIN32
    rstrm->worker_id = GetCurrentThreadId();
#endif

    for (;;) {
        rstrm_emit_t *emits = NULL;
        rstrm_event_t *events = NULL;
        rstrm_event_t *events_tail = NULL;
        int status = KC_RSTRM_OK;
        int should_wait = 0;

        rstrm_lock(rstrm);
        if (rstrm->closing) {
            int self_release = rstrm->self_release;
            rstrm_unlock(rstrm);
            if (self_release) rstrm_release(rstrm);
#ifdef _WIN32
            return 0;
#else
            return NULL;
#endif
        }

        if (rstrm->failed) {
            if (!rstrm->error_delivered) {
                (void)rstrm_event_push(&events, &events_tail,
                    RSTRM_EVENT_ERROR, rstrm->failed, NULL, 0);
                rstrm->error_delivered = 1;
            } else {
                should_wait = 1;
            }
        } else {
            status = rstrm_feed_input_locked(rstrm);
            if (status == KC_RSTRM_OK)
                status = rstrm_feed_send_locked(rstrm);
            if (status == KC_RSTRM_OK) {
                ikcp_update(rstrm->kcp, rstrm_now_ms());
                if (rstrm->output_error) {
                    status = rstrm->output_error;
                    rstrm->output_error = 0;
                }
            }
            if (status == KC_RSTRM_OK)
                status = rstrm_collect_receive_locked(rstrm,
                    &events, &events_tail);

            if (status != KC_RSTRM_OK) {
                rstrm_fail_locked(rstrm, status);
            } else if (rstrm->full_latched &&
                rstrm->pending_bytes <= RSTRM_LOW_PENDING_BYTES &&
                ikcp_waitsnd(rstrm->kcp) <= RSTRM_KCP_LOW_SEGMENTS)
            {
                rstrm->full_latched = 0;
                if (rstrm_event_push(&events, &events_tail,
                    RSTRM_EVENT_DRAIN, 0, NULL, 0) != 0)
                    rstrm_fail_locked(rstrm, KC_RSTRM_ENOMEM);
            }
        }

        emits = rstrm->emit_head;
        rstrm->emit_head = NULL;
        rstrm->emit_tail = NULL;

        if (!events && !emits && !rstrm->closing && !rstrm->failed)
            should_wait = 1;
        if (should_wait) rstrm_wait(rstrm, RSTRM_INTERVAL_MS);
        rstrm_unlock(rstrm);

        if (emits) {
            status = rstrm_dispatch_emits(rstrm, emits);
            if (status != KC_RSTRM_OK) {
                rstrm_lock(rstrm);
                rstrm_fail_locked(rstrm, status);
                rstrm_signal(rstrm);
                rstrm_unlock(rstrm);
            }
        }
        if (events) {
            if (rstrm_closing(rstrm)) rstrm_event_list_free(events);
            else rstrm_dispatch_events(rstrm, events);
        }
    }
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
int kc_rstrm_open(kc_rstrm_t **out, const kc_rstrm_options_t *options,
    kc_rstrm_receive_fn receive, kc_rstrm_datagram_fn datagram,
    kc_rstrm_eof_fn eof, kc_rstrm_drain_fn drain, kc_rstrm_error_fn error,
    void *userdata)
{
    kc_rstrm_t *rstrm;
    size_t mtu = options && options->mtu ? options->mtu : RSTRM_DEFAULT_MTU;

    if (!out) return KC_RSTRM_EINVAL;
    *out = NULL;
    if (!receive || !datagram || mtu < RSTRM_MIN_MTU || mtu > RSTRM_MAX_MTU ||
        mtu > (size_t)INT_MAX)
        return KC_RSTRM_EINVAL;

    rstrm = (kc_rstrm_t *)calloc(1, sizeof(*rstrm));
    if (!rstrm) return KC_RSTRM_ENOMEM;
    rstrm->mtu = mtu;
    rstrm->receive = receive;
    rstrm->datagram = datagram;
    rstrm->eof = eof;
    rstrm->drain = drain;
    rstrm->error = error;
    rstrm->userdata = userdata;

#ifdef _WIN32
    InitializeCriticalSection(&rstrm->mutex);
    InitializeConditionVariable(&rstrm->cond);
#else
    if (pthread_mutex_init(&rstrm->mutex, NULL) != 0) {
        free(rstrm);
        return KC_RSTRM_ENOMEM;
    }
    if (pthread_cond_init(&rstrm->cond, NULL) != 0) {
        (void)pthread_mutex_destroy(&rstrm->mutex);
        free(rstrm);
        return KC_RSTRM_ENOMEM;
    }
#endif

    rstrm->kcp = ikcp_create(RSTRM_CONV, rstrm);
    if (!rstrm->kcp || ikcp_setmtu(rstrm->kcp, (int)mtu) != 0 ||
        ikcp_wndsize(rstrm->kcp, RSTRM_SEND_WINDOW, RSTRM_RECV_WINDOW) != 0 ||
        ikcp_nodelay(rstrm->kcp, 0, RSTRM_INTERVAL_MS,
            RSTRM_FAST_RESEND, 0) != 0)
    {
        rstrm_release(rstrm);
        return KC_RSTRM_ENOMEM;
    }
    ikcp_setoutput(rstrm->kcp, rstrm_kcp_output);

#ifdef _WIN32
    rstrm->thread = CreateThread(NULL, 0, rstrm_worker, rstrm, 0, NULL);
    if (!rstrm->thread) {
        rstrm_release(rstrm);
        return KC_RSTRM_ENOMEM;
    }
#else
    if (pthread_create(&rstrm->thread, NULL, rstrm_worker, rstrm) != 0) {
        rstrm_release(rstrm);
        return KC_RSTRM_ENOMEM;
    }
#endif
    rstrm->thread_started = 1;
    *out = rstrm;
    return KC_RSTRM_OK;
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
int kc_rstrm_send(kc_rstrm_t *rstrm, const void *data, size_t data_size)
{
    rstrm_buffer_t *buffer;
    if (!rstrm || (data_size > 0 && !data)) return KC_RSTRM_EINVAL;

    rstrm_lock(rstrm);
    if (rstrm->closing || rstrm->failed || rstrm->local_shutdown) {
        rstrm_unlock(rstrm);
        return KC_RSTRM_ECLOSED;
    }
    if (data_size == 0) {
        rstrm_unlock(rstrm);
        return KC_RSTRM_OK;
    }
    if (data_size > RSTRM_MAX_PENDING_BYTES - rstrm->pending_bytes ||
        ikcp_waitsnd(rstrm->kcp) >= RSTRM_KCP_HIGH_SEGMENTS)
    {
        rstrm->full_latched = 1;
        rstrm_unlock(rstrm);
        return KC_RSTRM_EFULL;
    }
    buffer = rstrm_buffer_new(data, data_size);
    if (!buffer) {
        rstrm_unlock(rstrm);
        return KC_RSTRM_ENOMEM;
    }
    rstrm_buffer_push(&rstrm->send_head, &rstrm->send_tail, buffer);
    rstrm->pending_bytes += data_size;
    rstrm_signal(rstrm);
    rstrm_unlock(rstrm);
    return KC_RSTRM_OK;
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
int kc_rstrm_input(kc_rstrm_t *rstrm, const void *data, size_t data_size)
{
    rstrm_buffer_t *buffer;
    if (!rstrm || !data || data_size == 0 || data_size > RSTRM_MAX_MTU ||
        data_size > (size_t)LONG_MAX)
        return KC_RSTRM_EINVAL;

    rstrm_lock(rstrm);
    if (rstrm->closing || rstrm->failed) {
        rstrm_unlock(rstrm);
        return KC_RSTRM_ECLOSED;
    }
    if (data_size > RSTRM_MAX_INPUT_BYTES - rstrm->input_bytes) {
        rstrm_unlock(rstrm);
        return KC_RSTRM_EFULL;
    }
    buffer = rstrm_buffer_new(data, data_size);
    if (!buffer) {
        rstrm_unlock(rstrm);
        return KC_RSTRM_ENOMEM;
    }
    rstrm_buffer_push(&rstrm->input_head, &rstrm->input_tail, buffer);
    rstrm->input_bytes += data_size;
    rstrm_signal(rstrm);
    rstrm_unlock(rstrm);
    return KC_RSTRM_OK;
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
int kc_rstrm_shutdown(kc_rstrm_t *rstrm)
{
    if (!rstrm) return KC_RSTRM_EINVAL;
    rstrm_lock(rstrm);
    if (rstrm->closing || rstrm->failed) {
        rstrm_unlock(rstrm);
        return KC_RSTRM_ECLOSED;
    }
    rstrm->local_shutdown = 1;
    rstrm_signal(rstrm);
    rstrm_unlock(rstrm);
    return KC_RSTRM_OK;
}

/**
 * Checks whether the peer acknowledged the local graceful shutdown.
 * @param rstrm Open stream.
 * @return Nonzero when the local FIN reached the peer, zero otherwise.
 */
int kc_rstrm_shutdown_complete(kc_rstrm_t *rstrm)
{
    int complete;

    if (!rstrm) return 0;
    rstrm_lock(rstrm);
    complete = rstrm->local_shutdown && rstrm->local_fin_acked;
    rstrm_unlock(rstrm);
    return complete;
}

/**
 * Runs a reliable-stream operation.
 * @return None.
 */
void kc_rstrm_close(kc_rstrm_t *rstrm)
{
    int self;
    if (!rstrm) return;

    rstrm_lock(rstrm);
    self = rstrm_is_worker(rstrm);
    if (rstrm->closing) {
        rstrm_unlock(rstrm);
        return;
    }
    rstrm->closing = 1;
    if (self) rstrm->self_release = 1;
    rstrm_signal(rstrm);
    rstrm_unlock(rstrm);

    if (self) {
#ifndef _WIN32
        (void)pthread_detach(pthread_self());
#endif
        return;
    }

#ifdef _WIN32
    if (rstrm->thread_started)
        (void)WaitForSingleObject(rstrm->thread, INFINITE);
#else
    if (rstrm->thread_started)
        (void)pthread_join(rstrm->thread, NULL);
#endif
    rstrm_release(rstrm);
}

/**
 * Returns a stable message for one public status code.
 * @param status Public rstrm status.
 * @return Static status message.
 */
const char *kc_rstrm_strerror(int status)
{
    switch (status) {
        case KC_RSTRM_OK: return "ok";
        case KC_RSTRM_EINVAL: return "invalid argument";
        case KC_RSTRM_ECLOSED: return "stream closed";
        case KC_RSTRM_EFULL: return "stream buffer full";
        case KC_RSTRM_ETRANSPORT: return "transport failure";
        case KC_RSTRM_EPROTO: return "invalid stream protocol data";
        case KC_RSTRM_ENOMEM: return "out of memory";
        default: return "unknown rstrm status";
    }
}

/**
 * Runs a reliable-stream operation.
 * @return Operation result.
 */
uint64_t kc_rstrm_version(void)
{
    return (uint64_t)KC_RSTRM_BUILD_VERSION;
}
