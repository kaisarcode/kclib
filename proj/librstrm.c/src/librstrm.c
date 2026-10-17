/**
 * librstrm.c - Reliable ordered byte stream.
 * Summary: Public reliable-stream lifecycle over caller-supplied datagrams.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */
#include "rstrm_internal.h"

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

uint64_t kc_rstrm_version(void)
{
    return (uint64_t)KC_RSTRM_BUILD_VERSION;
}
