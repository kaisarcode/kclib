/**
 * rstrm_runtime.c - Private reliable-stream scheduler and protocol runtime.
 */
#include "rstrm_internal.h"

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

void rstrm_lock(kc_rstrm_t *rstrm)
{
#ifdef _WIN32
    EnterCriticalSection(&rstrm->mutex);
#else
    (void)pthread_mutex_lock(&rstrm->mutex);
#endif
}

void rstrm_unlock(kc_rstrm_t *rstrm)
{
#ifdef _WIN32
    LeaveCriticalSection(&rstrm->mutex);
#else
    (void)pthread_mutex_unlock(&rstrm->mutex);
#endif
}

void rstrm_signal(kc_rstrm_t *rstrm)
{
#ifdef _WIN32
    WakeConditionVariable(&rstrm->cond);
#else
    (void)pthread_cond_signal(&rstrm->cond);
#endif
}

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

int rstrm_is_worker(kc_rstrm_t *rstrm)
{
#ifdef _WIN32
    return rstrm->thread_started && GetCurrentThreadId() == rstrm->worker_id;
#else
    return rstrm->thread_started && pthread_equal(pthread_self(), rstrm->thread);
#endif
}

static int rstrm_closing(kc_rstrm_t *rstrm)
{
    int closing;
    rstrm_lock(rstrm);
    closing = rstrm->closing;
    rstrm_unlock(rstrm);
    return closing;
}

rstrm_buffer_t *rstrm_buffer_new(const void *data, size_t size)
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

void rstrm_buffer_push(rstrm_buffer_t **head, rstrm_buffer_t **tail,
    rstrm_buffer_t *buffer)
{
    if (*tail) (*tail)->next = buffer;
    else *head = buffer;
    *tail = buffer;
}

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

static void rstrm_buffer_list_free(rstrm_buffer_t *head)
{
    while (head) {
        rstrm_buffer_t *next = head->next;
        free(head);
        head = next;
    }
}

static void rstrm_emit_list_free(rstrm_emit_t *head)
{
    while (head) {
        rstrm_emit_t *next = head->next;
        free(head);
        head = next;
    }
}

static void rstrm_event_list_free(rstrm_event_t *head)
{
    while (head) {
        rstrm_event_t *next = head->next;
        free(head);
        head = next;
    }
}

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

static void rstrm_fail_locked(kc_rstrm_t *rstrm, int status)
{
    if (!rstrm->failed) rstrm->failed = status;
}

int rstrm_kcp_output(const char *buf, int len, ikcpcb *kcp, void *user)
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
            if (size != 1 || rstrm->remote_eof) {
                free(record);
                return KC_RSTRM_EPROTO;
            }
            rstrm->remote_eof = 1;
            if (rstrm_event_push(events, events_tail, RSTRM_EVENT_EOF, 0,
                NULL, 0) != 0)
            {
                free(record);
                return KC_RSTRM_ENOMEM;
            }
        } else {
            free(record);
            return KC_RSTRM_EPROTO;
        }
        free(record);
    }
}

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

void rstrm_release(kc_rstrm_t *rstrm)
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

RSTRM_THREAD_RET rstrm_worker(void *arg)
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
