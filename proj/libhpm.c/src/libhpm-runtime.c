/**
 * libhpm-runtime.c - Autonomous runtime for established HPM UDP paths.
 * Summary: Keeps established paths alive and buffers application datagrams.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libhpm.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef HANDLE hpm_live_thread_t;
typedef CRITICAL_SECTION hpm_live_mutex_t;
typedef CONDITION_VARIABLE hpm_live_cond_t;
#else
#include <errno.h>
#include <pthread.h>
typedef pthread_t hpm_live_thread_t;
typedef pthread_mutex_t hpm_live_mutex_t;
typedef pthread_cond_t hpm_live_cond_t;
#endif

#ifndef HPM_LIVE_POLL_MS
#define HPM_LIVE_POLL_MS       100ULL
#endif
#ifndef HPM_LIVE_KEEPALIVE_MS
#define HPM_LIVE_KEEPALIVE_MS 3000ULL
#endif
#ifndef HPM_LIVE_TIMEOUT_MS
#define HPM_LIVE_TIMEOUT_MS  10000ULL
#endif
#define HPM_LIVE_PACKET_SIZE       16U
#define HPM_LIVE_PACKET_KEEPALIVE   1U
#define HPM_LIVE_PACKET_ACK         2U
#define HPM_LIVE_QUEUE_MAX         256U
#define HPM_LIVE_QUEUE_BYTES (4U * 1024U * 1024U)

static const unsigned char hpm_live_magic[6] = { 'K', 'C', 'H', 'P', 'M', '2' };

typedef struct hpm_base_path hpm_base_path_t;

int hpm_base_connect(hpm_base_path_t **out, const kc_hpm_options_t *options);
int hpm_base_connect_over(hpm_base_path_t **out, const kc_hpm_options_t *options,
    const kc_hpm_udp_t *udp);
int hpm_base_send(hpm_base_path_t *path, const void *data, size_t data_size);
int hpm_base_receive(hpm_base_path_t *path, void **out_data,
    size_t *out_size, uint64_t timeout_ms);
const char *hpm_base_local_host(const hpm_base_path_t *path);
unsigned short hpm_base_local_port(const hpm_base_path_t *path);
const char *hpm_base_peer_host(const hpm_base_path_t *path);
unsigned short hpm_base_peer_port(const hpm_base_path_t *path);
void hpm_base_close(hpm_base_path_t *path);

typedef struct hpm_live_datagram {
    struct hpm_live_datagram *next;
    void *data;
    size_t size;
} hpm_live_datagram_t;

struct kc_hpm_path {
    hpm_base_path_t *base;
    hpm_live_thread_t thread;
    hpm_live_mutex_t mutex;
    hpm_live_cond_t cond;
    hpm_live_datagram_t *head;
    hpm_live_datagram_t *tail;
    size_t queued_count;
    size_t queued_bytes;
    uint64_t token;
    uint64_t last_rx_ms;
    int stopping;
    int terminal_status;
    int thread_started;
};

static uint64_t hpm_live_now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

static uint64_t hpm_live_deadline(uint64_t now, uint64_t timeout_ms)
{
    return UINT64_MAX - now < timeout_ms ? UINT64_MAX : now + timeout_ms;
}

static uint64_t hpm_live_token(const void *address)
{
    uint64_t x = hpm_live_now_ms() ^ (uint64_t)(uintptr_t)address;
#ifdef _WIN32
    x ^= (uint64_t)GetCurrentProcessId() << 32;
#else
    x ^= (uint64_t)(uintptr_t)&x << 17;
#endif
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x ? x : 1ULL;
}

static void hpm_live_store64be(unsigned char out[8], uint64_t value)
{
    unsigned i;
    for (i = 0; i < 8U; ++i) {
        out[7U - i] = (unsigned char)(value & 0xffU);
        value >>= 8;
    }
}

static uint64_t hpm_live_load64be(const unsigned char in[8])
{
    uint64_t value = 0;
    unsigned i;
    for (i = 0; i < 8U; ++i) value = (value << 8) | in[i];
    return value;
}

static void hpm_live_make_packet(unsigned char packet[HPM_LIVE_PACKET_SIZE],
    unsigned type, uint64_t token)
{
    memcpy(packet, hpm_live_magic, sizeof(hpm_live_magic));
    packet[6] = (unsigned char)type;
    packet[7] = 0;
    hpm_live_store64be(packet + 8, token);
}

static int hpm_live_parse_packet(const void *data, size_t size,
    unsigned *type, uint64_t *token)
{
    const unsigned char *packet = (const unsigned char *)data;
    if (!packet || size != HPM_LIVE_PACKET_SIZE || !type || !token) return 0;
    if (memcmp(packet, hpm_live_magic, sizeof(hpm_live_magic)) != 0 ||
        packet[7] != 0) return 0;
    if (packet[6] != HPM_LIVE_PACKET_KEEPALIVE &&
        packet[6] != HPM_LIVE_PACKET_ACK) return 0;
    *type = packet[6];
    *token = hpm_live_load64be(packet + 8);
    return 1;
}

#ifdef _WIN32
static int hpm_live_mutex_init(hpm_live_mutex_t *mutex)
{
    InitializeCriticalSection(mutex);
    return 0;
}
static void hpm_live_mutex_destroy(hpm_live_mutex_t *mutex)
{
    DeleteCriticalSection(mutex);
}
static void hpm_live_lock(hpm_live_mutex_t *mutex)
{
    EnterCriticalSection(mutex);
}
static void hpm_live_unlock(hpm_live_mutex_t *mutex)
{
    LeaveCriticalSection(mutex);
}
static void hpm_live_cond_init(hpm_live_cond_t *cond)
{
    InitializeConditionVariable(cond);
}
static void hpm_live_cond_signal(hpm_live_cond_t *cond)
{
    WakeAllConditionVariable(cond);
}
static int hpm_live_cond_wait(hpm_live_cond_t *cond, hpm_live_mutex_t *mutex,
    uint64_t timeout_ms)
{
    DWORD ms = timeout_ms >= (uint64_t)INFINITE ? INFINITE - 1U :
        (DWORD)timeout_ms;
    if (SleepConditionVariableCS(cond, mutex,
        timeout_ms == 0 ? INFINITE : ms)) return 0;
    return GetLastError() == ERROR_TIMEOUT ? 1 : -1;
}
#else
static int hpm_live_mutex_init(hpm_live_mutex_t *mutex)
{
    return pthread_mutex_init(mutex, NULL);
}
static void hpm_live_mutex_destroy(hpm_live_mutex_t *mutex)
{
    (void)pthread_mutex_destroy(mutex);
}
static void hpm_live_lock(hpm_live_mutex_t *mutex)
{
    (void)pthread_mutex_lock(mutex);
}
static void hpm_live_unlock(hpm_live_mutex_t *mutex)
{
    (void)pthread_mutex_unlock(mutex);
}
static void hpm_live_cond_init(hpm_live_cond_t *cond)
{
    (void)pthread_cond_init(cond, NULL);
}
static void hpm_live_cond_destroy(hpm_live_cond_t *cond)
{
    (void)pthread_cond_destroy(cond);
}
static void hpm_live_cond_signal(hpm_live_cond_t *cond)
{
    (void)pthread_cond_broadcast(cond);
}
static int hpm_live_cond_wait(hpm_live_cond_t *cond, hpm_live_mutex_t *mutex,
    uint64_t timeout_ms)
{
    int rc;
    if (timeout_ms == 0) {
        rc = pthread_cond_wait(cond, mutex);
        return rc == 0 ? 0 : -1;
    }
    {
        struct timespec ts;
        uint64_t ns;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
        ns = (uint64_t)ts.tv_nsec + (timeout_ms % 1000ULL) * 1000000ULL;
        ts.tv_sec += (time_t)(timeout_ms / 1000ULL + ns / 1000000000ULL);
        ts.tv_nsec = (long)(ns % 1000000000ULL);
        rc = pthread_cond_timedwait(cond, mutex, &ts);
    }
    if (rc == 0) return 0;
    return rc == ETIMEDOUT ? 1 : -1;
}
#endif

static void hpm_live_set_terminal(kc_hpm_path_t *path, int status)
{
    hpm_live_lock(&path->mutex);
    if (path->terminal_status == KC_HPM_OK)
        path->terminal_status = status == KC_HPM_OK ? KC_HPM_ENET : status;
    hpm_live_cond_signal(&path->cond);
    hpm_live_unlock(&path->mutex);
}

static int hpm_live_should_stop(kc_hpm_path_t *path)
{
    int stop;
    hpm_live_lock(&path->mutex);
    stop = path->stopping || path->terminal_status != KC_HPM_OK;
    hpm_live_unlock(&path->mutex);
    return stop;
}

static void hpm_live_note_rx(kc_hpm_path_t *path)
{
    hpm_live_lock(&path->mutex);
    path->last_rx_ms = hpm_live_now_ms();
    hpm_live_unlock(&path->mutex);
}

static int hpm_live_queue(kc_hpm_path_t *path, void *data, size_t size)
{
    hpm_live_datagram_t *item;
    int keep = 0;

    item = (hpm_live_datagram_t *)calloc(1, sizeof(*item));
    if (!item) return KC_HPM_ENOMEM;
    item->data = data;
    item->size = size;

    hpm_live_lock(&path->mutex);
    if (!path->stopping && path->terminal_status == KC_HPM_OK &&
        path->queued_count < HPM_LIVE_QUEUE_MAX &&
        size <= HPM_LIVE_QUEUE_BYTES - path->queued_bytes) {
        if (path->tail) path->tail->next = item;
        else path->head = item;
        path->tail = item;
        path->queued_count++;
        path->queued_bytes += size;
        keep = 1;
        hpm_live_cond_signal(&path->cond);
    }
    hpm_live_unlock(&path->mutex);

    if (!keep) {
        free(item);
        return KC_HPM_ETIMEOUT;
    }
    return KC_HPM_OK;
}

static int hpm_live_send_control(kc_hpm_path_t *path, unsigned type,
    uint64_t token)
{
    unsigned char packet[HPM_LIVE_PACKET_SIZE];
    hpm_live_make_packet(packet, type, token);
    return hpm_base_send(path->base, packet, sizeof(packet));
}

#ifdef _WIN32
static DWORD WINAPI hpm_live_worker(LPVOID argument)
#else
static void *hpm_live_worker(void *argument)
#endif
{
    kc_hpm_path_t *path = (kc_hpm_path_t *)argument;
    uint64_t next_keepalive = hpm_live_now_ms() + HPM_LIVE_KEEPALIVE_MS;

    while (!hpm_live_should_stop(path)) {
        void *data = NULL;
        size_t size = 0;
        uint64_t now = hpm_live_now_ms();
        int status;

        if (now >= next_keepalive) {
            status = hpm_live_send_control(path, HPM_LIVE_PACKET_KEEPALIVE,
                path->token);
            if (status != KC_HPM_OK) {
                hpm_live_set_terminal(path, status);
                break;
            }
            next_keepalive = now + HPM_LIVE_KEEPALIVE_MS;
        }

        status = hpm_base_receive(path->base, &data, &size, HPM_LIVE_POLL_MS);
        if (status == KC_HPM_ETIMEOUT) {
            hpm_live_lock(&path->mutex);
            now = hpm_live_now_ms();
            if (!path->stopping && path->terminal_status == KC_HPM_OK &&
                now - path->last_rx_ms >= HPM_LIVE_TIMEOUT_MS) {
                path->terminal_status = KC_HPM_ETIMEOUT;
                hpm_live_cond_signal(&path->cond);
            }
            hpm_live_unlock(&path->mutex);
            continue;
        }
        if (status != KC_HPM_OK) {
            hpm_live_set_terminal(path, status);
            break;
        }

        hpm_live_note_rx(path);
        {
            unsigned type;
            uint64_t token;
            if (hpm_live_parse_packet(data, size, &type, &token)) {
                if (type == HPM_LIVE_PACKET_KEEPALIVE) {
                    status = hpm_live_send_control(path,
                        HPM_LIVE_PACKET_ACK, token);
                    if (status != KC_HPM_OK)
                        hpm_live_set_terminal(path, status);
                }
                kc_hpm_free(data);
                continue;
            }
        }

        status = hpm_live_queue(path, data, size);
        if (status == KC_HPM_ENOMEM) {
            kc_hpm_free(data);
            hpm_live_set_terminal(path, status);
            break;
        }
        if (status != KC_HPM_OK) kc_hpm_free(data);
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void hpm_live_free_queue(kc_hpm_path_t *path)
{
    hpm_live_datagram_t *item = path->head;
    while (item) {
        hpm_live_datagram_t *next = item->next;
        kc_hpm_free(item->data);
        free(item);
        item = next;
    }
    path->head = NULL;
    path->tail = NULL;
    path->queued_count = 0;
    path->queued_bytes = 0;
}

static int hpm_live_start(kc_hpm_path_t **out, hpm_base_path_t *base)
{
    kc_hpm_path_t *path;
    if (!out || !base) return KC_HPM_EINVAL;
    *out = NULL;

    path = (kc_hpm_path_t *)calloc(1, sizeof(*path));
    if (!path) return KC_HPM_ENOMEM;
    path->base = base;
    path->token = hpm_live_token(path);
    path->last_rx_ms = hpm_live_now_ms();
    path->terminal_status = KC_HPM_OK;
    if (hpm_live_mutex_init(&path->mutex) != 0) {
        free(path);
        return KC_HPM_ENET;
    }
    hpm_live_cond_init(&path->cond);
#ifdef _WIN32
    path->thread = CreateThread(NULL, 0, hpm_live_worker, path, 0, NULL);
    if (!path->thread) {
        hpm_live_mutex_destroy(&path->mutex);
        free(path);
        return KC_HPM_ENET;
    }
#else
    if (pthread_create(&path->thread, NULL, hpm_live_worker, path) != 0) {
        hpm_live_cond_destroy(&path->cond);
        hpm_live_mutex_destroy(&path->mutex);
        free(path);
        return KC_HPM_ENET;
    }
#endif
    path->thread_started = 1;
    *out = path;
    return KC_HPM_OK;
}

int kc_hpm_connect(kc_hpm_path_t **out, const kc_hpm_options_t *options)
{
    hpm_base_path_t *base = NULL;
    int status;
    if (!out) return KC_HPM_EINVAL;
    *out = NULL;
    status = hpm_base_connect(&base, options);
    if (status != KC_HPM_OK) return status;
    status = hpm_live_start(out, base);
    if (status != KC_HPM_OK) hpm_base_close(base);
    return status;
}

int kc_hpm_connect_over(kc_hpm_path_t **out, const kc_hpm_options_t *options,
    const kc_hpm_udp_t *udp)
{
    hpm_base_path_t *base = NULL;
    int status;
    if (!out) return KC_HPM_EINVAL;
    *out = NULL;
    status = hpm_base_connect_over(&base, options, udp);
    if (status != KC_HPM_OK) return status;
    status = hpm_live_start(out, base);
    if (status != KC_HPM_OK) hpm_base_close(base);
    return status;
}

int kc_hpm_send(kc_hpm_path_t *path, const void *data, size_t data_size)
{
    int terminal;
    if (!path) return KC_HPM_ECLOSED;
    hpm_live_lock(&path->mutex);
    terminal = path->stopping ? KC_HPM_ECLOSED : path->terminal_status;
    hpm_live_unlock(&path->mutex);
    if (terminal != KC_HPM_OK) return terminal;
    return hpm_base_send(path->base, data, data_size);
}

int kc_hpm_receive(kc_hpm_path_t *path, void **out_data,
    size_t *out_size, uint64_t timeout_ms)
{
    uint64_t deadline = timeout_ms ? hpm_live_deadline(hpm_live_now_ms(), timeout_ms) : 0;
    if (!out_data || !out_size) return KC_HPM_EINVAL;
    *out_data = NULL;
    *out_size = 0;
    if (!path) return KC_HPM_ECLOSED;

    hpm_live_lock(&path->mutex);
    for (;;) {
        hpm_live_datagram_t *item;
        int terminal;
        int wait_status;

        if (path->head) {
            item = path->head;
            path->head = item->next;
            if (!path->head) path->tail = NULL;
            path->queued_count--;
            path->queued_bytes -= item->size;
            *out_data = item->data;
            *out_size = item->size;
            free(item);
            hpm_live_unlock(&path->mutex);
            return KC_HPM_OK;
        }

        terminal = path->stopping ? KC_HPM_ECLOSED : path->terminal_status;
        if (terminal != KC_HPM_OK) {
            hpm_live_unlock(&path->mutex);
            return terminal;
        }

        if (timeout_ms) {
            uint64_t now = hpm_live_now_ms();
            uint64_t remaining;
            if (now >= deadline) {
                hpm_live_unlock(&path->mutex);
                return KC_HPM_ETIMEOUT;
            }
            remaining = deadline - now;
            wait_status = hpm_live_cond_wait(&path->cond, &path->mutex,
                remaining);
            if (wait_status == 1) {
                hpm_live_unlock(&path->mutex);
                return KC_HPM_ETIMEOUT;
            }
        } else {
            wait_status = hpm_live_cond_wait(&path->cond, &path->mutex, 0);
        }
        if (wait_status < 0) {
            hpm_live_unlock(&path->mutex);
            return KC_HPM_ENET;
        }
    }
}

const char *kc_hpm_local_host(const kc_hpm_path_t *path)
{
    return path ? hpm_base_local_host(path->base) : NULL;
}

unsigned short kc_hpm_local_port(const kc_hpm_path_t *path)
{
    return path ? hpm_base_local_port(path->base) : 0;
}

const char *kc_hpm_peer_host(const kc_hpm_path_t *path)
{
    return path ? hpm_base_peer_host(path->base) : NULL;
}

unsigned short kc_hpm_peer_port(const kc_hpm_path_t *path)
{
    return path ? hpm_base_peer_port(path->base) : 0;
}

void kc_hpm_close(kc_hpm_path_t *path)
{
    if (!path) return;
    hpm_live_lock(&path->mutex);
    path->stopping = 1;
    hpm_live_cond_signal(&path->cond);
    hpm_live_unlock(&path->mutex);

    if (path->thread_started) {
#ifdef _WIN32
        WaitForSingleObject(path->thread, INFINITE);
        CloseHandle(path->thread);
#else
        (void)pthread_join(path->thread, NULL);
#endif
        path->thread_started = 0;
    }

    hpm_live_lock(&path->mutex);
    hpm_live_free_queue(path);
    hpm_live_unlock(&path->mutex);
#ifdef _WIN32
    hpm_live_mutex_destroy(&path->mutex);
#else
    hpm_live_cond_destroy(&path->cond);
    hpm_live_mutex_destroy(&path->mutex);
#endif
    hpm_base_close(path->base);
    free(path);
}
