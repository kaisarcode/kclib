/**
 * test.c - librstrm public API contract tests.
 * Summary: Validates reliable ordered streams over lossy datagram transport.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "librstrm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE test_thread_t;
typedef CRITICAL_SECTION test_mutex_t;
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_t test_thread_t;
typedef pthread_mutex_t test_mutex_t;
#endif

#define TEST_TIMEOUT_MS 8000U
#define TEST_LARGE_SIZE (512U * 1024U)
#define TEST_FAULT_SIZE (64U * 1024U)
#define TEST_LOSS_SIZE (32U * 1024U)
#define TEST_WRITERS 4U
#define TEST_WRITES_PER_THREAD 200U
#define TEST_RECORD_SIZE 8U

static int test_case_total = 0;
static int test_case_current = 0;

typedef struct endpoint endpoint_t;

struct endpoint {
    kc_rstrm_t *stream;
    endpoint_t *peer;
    test_mutex_t mutex;
    unsigned char *received;
    size_t received_size;
    size_t received_capacity;
    size_t eof_received_size;
    int callback_fail;
    int eof_count;
    int drain_count;
    int error_count;
    int error_status;
    int callback_closed;
    int echo_on_receive;
    int close_on_receive;
    int close_on_error;
    int fail_output;
    unsigned tx_count;
    unsigned drop_remaining;
    unsigned duplicate_at;
    unsigned reorder_at;
    unsigned dropped;
    unsigned duplicated;
    unsigned reordered;
    size_t max_datagram;
    unsigned char *held;
    size_t held_size;
};

typedef struct {
    kc_rstrm_t *stream;
    unsigned id;
    unsigned count;
    int status;
} writer_arg_t;

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void test_lock(test_mutex_t *mutex)
{
#ifdef _WIN32
    EnterCriticalSection(mutex);
#else
    (void)pthread_mutex_lock(mutex);
#endif
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void test_unlock(test_mutex_t *mutex)
{
#ifdef _WIN32
    LeaveCriticalSection(mutex);
#else
    (void)pthread_mutex_unlock(mutex);
#endif
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(ms / 1000U);
    delay.tv_nsec = (long)(ms % 1000U) * 1000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail)
{
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void run_case(int *rc, int (*fn)(void))
{
    test_case_current++;
    *rc += fn();
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int expect_true(const char *name, int condition)
{
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void endpoint_init(endpoint_t *endpoint)
{
    memset(endpoint, 0, sizeof(*endpoint));
#ifdef _WIN32
    InitializeCriticalSection(&endpoint->mutex);
#else
    (void)pthread_mutex_init(&endpoint->mutex, NULL);
#endif
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void endpoint_fini(endpoint_t *endpoint)
{
    free(endpoint->received);
    free(endpoint->held);
#ifdef _WIN32
    DeleteCriticalSection(&endpoint->mutex);
#else
    (void)pthread_mutex_destroy(&endpoint->mutex);
#endif
}

/**
 * Reads an endpoint stream under its test mutex.
 * @param endpoint Test endpoint.
 * @return Current stream, or NULL.
 */
static kc_rstrm_t *endpoint_stream(endpoint_t *endpoint)
{
    kc_rstrm_t *stream;
    test_lock(&endpoint->mutex);
    stream = endpoint->stream;
    test_unlock(&endpoint->mutex);
    return stream;
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void endpoint_set_stream(endpoint_t *endpoint, kc_rstrm_t *stream)
{
    test_lock(&endpoint->mutex);
    endpoint->stream = stream;
    test_unlock(&endpoint->mutex);
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int endpoint_deliver(endpoint_t *endpoint, const void *data, size_t size)
{
    kc_rstrm_t *stream;
    int status;

    if (!endpoint || !data || size == 0) return 0;
    stream = endpoint_stream(endpoint);
    if (!stream) return 0;
    status = kc_rstrm_input(stream, data, size);
    if (status == KC_RSTRM_OK || status == KC_RSTRM_EFULL ||
        status == KC_RSTRM_ECLOSED)
        return 0;
    return -1;
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void receive_cb(const void *data, size_t data_size, void *userdata)
{
    endpoint_t *endpoint = (endpoint_t *)userdata;
    kc_rstrm_t *action_stream = NULL;
    int echo = 0;
    int close_stream = 0;

    test_lock(&endpoint->mutex);
    if (data_size > SIZE_MAX - endpoint->received_size) {
        endpoint->callback_fail = 1;
    } else if (endpoint->received_size + data_size > endpoint->received_capacity) {
        size_t need = endpoint->received_size + data_size;
        size_t capacity = endpoint->received_capacity ?
            endpoint->received_capacity : 1024U;
        unsigned char *next;
        while (capacity < need && capacity <= SIZE_MAX / 2U)
            capacity *= 2U;
        if (capacity < need) capacity = need;
        next = (unsigned char *)realloc(endpoint->received, capacity);
        if (!next) endpoint->callback_fail = 1;
        else {
            endpoint->received = next;
            endpoint->received_capacity = capacity;
        }
    }
    if (!endpoint->callback_fail) {
        memcpy(endpoint->received + endpoint->received_size, data, data_size);
        endpoint->received_size += data_size;
    }
    echo = endpoint->echo_on_receive;
    if (endpoint->close_on_receive && endpoint->stream) {
        action_stream = endpoint->stream;
        endpoint->stream = NULL;
        endpoint->callback_closed = 1;
        close_stream = 1;
    } else if (echo) {
        action_stream = endpoint->stream;
    }
    test_unlock(&endpoint->mutex);

    if (echo && action_stream && !close_stream)
        (void)kc_rstrm_send(action_stream, data, data_size);
    if (close_stream && action_stream)
        kc_rstrm_close(action_stream);
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int datagram_cb(const void *data, size_t data_size, void *userdata)
{
    endpoint_t *endpoint = (endpoint_t *)userdata;
    unsigned number;
    unsigned drop = 0;
    unsigned duplicate = 0;
    unsigned reorder = 0;
    unsigned char *held = NULL;
    size_t held_size = 0;
    int status;

    test_lock(&endpoint->mutex);
    if (endpoint->fail_output) {
        test_unlock(&endpoint->mutex);
        return -1;
    }
    endpoint->tx_count++;
    number = endpoint->tx_count;
    if (data_size > endpoint->max_datagram)
        endpoint->max_datagram = data_size;
    if (endpoint->drop_remaining > 0U) {
        endpoint->drop_remaining--;
        endpoint->dropped++;
        drop = 1;
    } else if (endpoint->reorder_at != 0U && number == endpoint->reorder_at &&
        endpoint->held == NULL)
    {
        endpoint->held = (unsigned char *)malloc(data_size);
        if (!endpoint->held) {
            test_unlock(&endpoint->mutex);
            return -1;
        }
        memcpy(endpoint->held, data, data_size);
        endpoint->held_size = data_size;
        endpoint->reordered++;
        reorder = 1;
    } else {
        if (endpoint->duplicate_at != 0U && number == endpoint->duplicate_at) {
            endpoint->duplicated++;
            duplicate = 1;
        }
        if (endpoint->held) {
            held = endpoint->held;
            held_size = endpoint->held_size;
            endpoint->held = NULL;
            endpoint->held_size = 0;
        }
    }
    test_unlock(&endpoint->mutex);

    if (drop || reorder) return 0;
    status = endpoint_deliver(endpoint->peer, data, data_size);
    if (status != 0) {
        free(held);
        return status;
    }
    if (duplicate) {
        status = endpoint_deliver(endpoint->peer, data, data_size);
        if (status != 0) {
            free(held);
            return status;
        }
    }
    if (held) {
        status = endpoint_deliver(endpoint->peer, held, held_size);
        free(held);
        if (status != 0) return status;
    }
    return 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void eof_cb(void *userdata)
{
    endpoint_t *endpoint = (endpoint_t *)userdata;
    test_lock(&endpoint->mutex);
    endpoint->eof_count++;
    endpoint->eof_received_size = endpoint->received_size;
    test_unlock(&endpoint->mutex);
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void drain_cb(void *userdata)
{
    endpoint_t *endpoint = (endpoint_t *)userdata;
    test_lock(&endpoint->mutex);
    endpoint->drain_count++;
    test_unlock(&endpoint->mutex);
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void error_cb(int status, void *userdata)
{
    endpoint_t *endpoint = (endpoint_t *)userdata;
    kc_rstrm_t *stream = NULL;

    test_lock(&endpoint->mutex);
    endpoint->error_count++;
    endpoint->error_status = status;
    if (endpoint->close_on_error && endpoint->stream) {
        stream = endpoint->stream;
        endpoint->stream = NULL;
        endpoint->callback_closed = 1;
    }
    test_unlock(&endpoint->mutex);
    if (stream) kc_rstrm_close(stream);
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int pair_open(endpoint_t *a, endpoint_t *b, size_t mtu)
{
    kc_rstrm_options_t options;
    const kc_rstrm_options_t *options_ptr = NULL;
    int status;

    a->peer = b;
    b->peer = a;
    if (mtu != 0U) {
        options.mtu = mtu;
        options_ptr = &options;
    }
    status = kc_rstrm_open(&a->stream, options_ptr, receive_cb, datagram_cb,
        eof_cb, drain_cb, error_cb, a);
    if (status != KC_RSTRM_OK) return status;
    status = kc_rstrm_open(&b->stream, options_ptr, receive_cb, datagram_cb,
        eof_cb, drain_cb, error_cb, b);
    if (status != KC_RSTRM_OK) {
        kc_rstrm_close(a->stream);
        a->stream = NULL;
        return status;
    }
    return KC_RSTRM_OK;
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void pair_close(endpoint_t *a, endpoint_t *b)
{
    kc_rstrm_t *a_stream = endpoint_stream(a);
    kc_rstrm_t *b_stream = endpoint_stream(b);
    endpoint_set_stream(a, NULL);
    endpoint_set_stream(b, NULL);
    kc_rstrm_close(a_stream);
    kc_rstrm_close(b_stream);
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int wait_size(endpoint_t *endpoint, size_t size, unsigned timeout_ms)
{
    unsigned elapsed = 0;
    while (elapsed <= timeout_ms) {
        size_t current;
        int failed;
        test_lock(&endpoint->mutex);
        current = endpoint->received_size;
        failed = endpoint->callback_fail || endpoint->error_count > 0;
        test_unlock(&endpoint->mutex);
        if (current >= size) return 1;
        if (failed) return 0;
        sleep_ms(10);
        elapsed += 10;
    }
    return 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int wait_counter(endpoint_t *endpoint, int field, int value,
    unsigned timeout_ms)
{
    unsigned elapsed = 0;
    while (elapsed <= timeout_ms) {
        int current;
        test_lock(&endpoint->mutex);
        if (field == 1) current = endpoint->eof_count;
        else if (field == 2) current = endpoint->drain_count;
        else if (field == 3) current = endpoint->error_count;
        else current = endpoint->callback_closed;
        test_unlock(&endpoint->mutex);
        if (current >= value) return 1;
        sleep_ms(10);
        elapsed += 10;
    }
    return 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int endpoint_equals(endpoint_t *endpoint, const void *data, size_t size)
{
    int equal;
    test_lock(&endpoint->mutex);
    equal = !endpoint->callback_fail && endpoint->received_size == size &&
        (size == 0U || memcmp(endpoint->received, data, size) == 0);
    test_unlock(&endpoint->mutex);
    return equal;
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void fill_pattern(unsigned char *data, size_t size, unsigned seed)
{
    size_t i;
    uint32_t state = 0x9e3779b9U ^ (uint32_t)seed;
    for (i = 0; i < size; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        data[i] = (unsigned char)(state & 0xffU);
    }
}

/**
 * Runs a reliable-stream contract operation.
 * @return None.
 */
static void store32le(unsigned char out[4], uint32_t value)
{
    out[0] = (unsigned char)(value & 0xffU);
    out[1] = (unsigned char)((value >> 8) & 0xffU);
    out[2] = (unsigned char)((value >> 16) & 0xffU);
    out[3] = (unsigned char)((value >> 24) & 0xffU);
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static uint32_t load32le(const unsigned char in[4])
{
    return (uint32_t)in[0] |
        ((uint32_t)in[1] << 8) |
        ((uint32_t)in[2] << 16) |
        ((uint32_t)in[3] << 24);
}

#ifdef _WIN32

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static DWORD WINAPI writer_thread(LPVOID arg)
#else
static void *writer_thread(void *arg)
#endif
{
    writer_arg_t *writer = (writer_arg_t *)arg;
    unsigned i;
    writer->status = KC_RSTRM_OK;
    for (i = 0; i < writer->count; ++i) {
        unsigned char record[TEST_RECORD_SIZE];
        int status;
        store32le(record, writer->id);
        store32le(record + 4, i);
        do {
            status = kc_rstrm_send(writer->stream, record, sizeof(record));
            if (status == KC_RSTRM_EFULL) sleep_ms(1);
        } while (status == KC_RSTRM_EFULL);
        if (status != KC_RSTRM_OK) {
            writer->status = status;
            break;
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_open(void)
{
    const char *name = "kc_rstrm_open";
    const char *detail = "validates callbacks and MTU bounds";
    endpoint_t endpoint;
    kc_rstrm_t *stream = (kc_rstrm_t *)(uintptr_t)1U;
    kc_rstrm_options_t options;
    int fail = 0;

    endpoint_init(&endpoint);
    fail += expect_true("rejects NULL destination",
        kc_rstrm_open(NULL, NULL, receive_cb, datagram_cb, NULL, NULL, NULL,
            &endpoint) == KC_RSTRM_EINVAL);
    fail += expect_true("rejects NULL receive callback",
        kc_rstrm_open(&stream, NULL, NULL, datagram_cb, NULL, NULL, NULL,
            &endpoint) == KC_RSTRM_EINVAL && stream == NULL);
    stream = (kc_rstrm_t *)(uintptr_t)1U;
    fail += expect_true("rejects NULL datagram callback",
        kc_rstrm_open(&stream, NULL, receive_cb, NULL, NULL, NULL, NULL,
            &endpoint) == KC_RSTRM_EINVAL && stream == NULL);
    options.mtu = 511U;
    stream = (kc_rstrm_t *)(uintptr_t)1U;
    fail += expect_true("rejects MTU below minimum",
        kc_rstrm_open(&stream, &options, receive_cb, datagram_cb, NULL, NULL,
            NULL, &endpoint) == KC_RSTRM_EINVAL && stream == NULL);
    options.mtu = 65536U;
    stream = (kc_rstrm_t *)(uintptr_t)1U;
    fail += expect_true("rejects MTU above maximum",
        kc_rstrm_open(&stream, &options, receive_cb, datagram_cb, NULL, NULL,
            NULL, &endpoint) == KC_RSTRM_EINVAL && stream == NULL);
    endpoint_fini(&endpoint);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_send(void)
{
    const char *name = "kc_rstrm_send";
    const char *detail = "presents one ordered byte stream across send boundaries";
    endpoint_t a, b;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    fail += expect_true("opens stream pair", pair_open(&a, &b, 0) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("accepts zero-length send while open",
            kc_rstrm_send(a.stream, NULL, 0) == KC_RSTRM_OK);
        fail += expect_true("queues first bytes",
            kc_rstrm_send(a.stream, "abc", 3) == KC_RSTRM_OK);
        fail += expect_true("queues second bytes",
            kc_rstrm_send(a.stream, "def", 3) == KC_RSTRM_OK);
        fail += expect_true("receives combined bytes", wait_size(&b, 6, 2000));
        fail += expect_true("preserves byte order", endpoint_equals(&b,
            "abcdef", 6));
    }
    pair_close(&a, &b);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_reliability(void)
{
    const char *name = "kc_rstrm_reliability";
    const char *detail = "retransmits bytes after finite data and ACK loss";
    endpoint_t a, b;
    unsigned char *payload = NULL;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    a.drop_remaining = 3U;
    b.drop_remaining = 2U;
    payload = (unsigned char *)malloc(TEST_LOSS_SIZE);
    fail += expect_true("allocates loss payload", payload != NULL);
    if (payload) fill_pattern(payload, TEST_LOSS_SIZE, 1U);
    fail += expect_true("opens lossy pair", pair_open(&a, &b, 900U) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues lossy payload",
            kc_rstrm_send(a.stream, payload, TEST_LOSS_SIZE) == KC_RSTRM_OK);
        fail += expect_true("recovers complete payload",
            wait_size(&b, TEST_LOSS_SIZE, TEST_TIMEOUT_MS));
        fail += expect_true("recovered payload is exact",
            endpoint_equals(&b, payload, TEST_LOSS_SIZE));
        fail += expect_true("drops occurred in both directions",
            a.dropped == 3U && b.dropped == 2U);
    }
    pair_close(&a, &b);
    free(payload);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_ordering(void)
{
    const char *name = "kc_rstrm_ordering";
    const char *detail = "restores order and ignores duplicate datagrams";
    endpoint_t a, b;
    unsigned char *payload = NULL;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    a.reorder_at = 20U;
    a.duplicate_at = 21U;
    payload = (unsigned char *)malloc(TEST_FAULT_SIZE);
    fail += expect_true("allocates ordering payload", payload != NULL);
    if (payload) fill_pattern(payload, TEST_FAULT_SIZE, 2U);
    fail += expect_true("opens reordered pair", pair_open(&a, &b, 900U) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues ordering payload",
            kc_rstrm_send(a.stream, payload, TEST_FAULT_SIZE) == KC_RSTRM_OK);
        fail += expect_true("receives reordered payload",
            wait_size(&b, TEST_FAULT_SIZE, TEST_TIMEOUT_MS));
        fail += expect_true("ordered payload is exact",
            endpoint_equals(&b, payload, TEST_FAULT_SIZE));
        fail += expect_true("reorder injector activated", a.reordered == 1U);
        fail += expect_true("duplicate injector activated", a.duplicated == 1U);
    }
    pair_close(&a, &b);
    free(payload);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_mtu(void)
{
    const char *name = "kc_rstrm_mtu";
    const char *detail = "fragments a large stream within the configured datagram MTU";
    endpoint_t a, b;
    unsigned char *payload = NULL;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    payload = (unsigned char *)malloc(TEST_LARGE_SIZE);
    fail += expect_true("allocates large payload", payload != NULL);
    if (payload) fill_pattern(payload, TEST_LARGE_SIZE, 3U);
    fail += expect_true("opens minimum-MTU pair", pair_open(&a, &b, 512U) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues large payload",
            kc_rstrm_send(a.stream, payload, TEST_LARGE_SIZE) == KC_RSTRM_OK);
        fail += expect_true("receives large payload",
            wait_size(&b, TEST_LARGE_SIZE, TEST_TIMEOUT_MS));
        fail += expect_true("large payload is exact",
            endpoint_equals(&b, payload, TEST_LARGE_SIZE));
        fail += expect_true("emitted datagrams honor MTU",
            a.max_datagram > 0U && a.max_datagram <= 512U &&
            b.max_datagram <= 512U);
    }
    pair_close(&a, &b);
    free(payload);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_full_duplex(void)
{
    const char *name = "kc_rstrm_full_duplex";
    const char *detail = "moves independent byte streams in both directions";
    endpoint_t a, b;
    unsigned char *from_a = NULL;
    unsigned char *from_b = NULL;
    const size_t size = 128U * 1024U;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    from_a = (unsigned char *)malloc(size);
    from_b = (unsigned char *)malloc(size);
    fail += expect_true("allocates duplex payloads", from_a && from_b);
    if (from_a) fill_pattern(from_a, size, 4U);
    if (from_b) fill_pattern(from_b, size, 5U);
    fail += expect_true("opens duplex pair", pair_open(&a, &b, 1000U) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues A to B",
            kc_rstrm_send(a.stream, from_a, size) == KC_RSTRM_OK);
        fail += expect_true("queues B to A",
            kc_rstrm_send(b.stream, from_b, size) == KC_RSTRM_OK);
        fail += expect_true("A receives reverse stream",
            wait_size(&a, size, TEST_TIMEOUT_MS));
        fail += expect_true("B receives forward stream",
            wait_size(&b, size, TEST_TIMEOUT_MS));
        fail += expect_true("A reverse bytes are exact",
            endpoint_equals(&a, from_b, size));
        fail += expect_true("B forward bytes are exact",
            endpoint_equals(&b, from_a, size));
    }
    pair_close(&a, &b);
    free(from_a);
    free(from_b);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_concurrency(void)
{
    const char *name = "kc_rstrm_concurrency";
    const char *detail = "serializes concurrent senders without corrupting per-thread order";
    endpoint_t a, b;
    writer_arg_t writers[TEST_WRITERS];
    test_thread_t threads[TEST_WRITERS];
    int started[TEST_WRITERS] = {0};
    uint32_t next_seq[TEST_WRITERS] = {0};
    const size_t expected = TEST_WRITERS * TEST_WRITES_PER_THREAD * TEST_RECORD_SIZE;
    unsigned i;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    fail += expect_true("opens concurrent pair", pair_open(&a, &b, 0) == KC_RSTRM_OK);
    if (!fail) {
        for (i = 0; i < TEST_WRITERS; ++i) {
            writers[i].stream = a.stream;
            writers[i].id = i;
            writers[i].count = TEST_WRITES_PER_THREAD;
            writers[i].status = KC_RSTRM_EINVAL;
#ifdef _WIN32
            threads[i] = CreateThread(NULL, 0, writer_thread, &writers[i], 0, NULL);
            started[i] = threads[i] != NULL;
#else
            started[i] = pthread_create(&threads[i], NULL, writer_thread,
                &writers[i]) == 0;
#endif
        }
        for (i = 0; i < TEST_WRITERS; ++i) {
            fail += expect_true("starts producer thread", started[i]);
            if (!started[i]) continue;
#ifdef _WIN32
            (void)WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
#else
            (void)pthread_join(threads[i], NULL);
#endif
            fail += expect_true("producer completes",
                writers[i].status == KC_RSTRM_OK);
        }
        fail += expect_true("receives all concurrent writes",
            wait_size(&b, expected, TEST_TIMEOUT_MS));
        test_lock(&b.mutex);
        if (b.received_size == expected) {
            for (i = 0; i < expected / TEST_RECORD_SIZE; ++i) {
                const unsigned char *record = b.received + i * TEST_RECORD_SIZE;
                uint32_t id = load32le(record);
                uint32_t seq = load32le(record + 4);
                if (id >= TEST_WRITERS || seq != next_seq[id]) {
                    fail += expect_true("preserves producer record order", 0);
                    break;
                }
                next_seq[id]++;
            }
        }
        test_unlock(&b.mutex);
        for (i = 0; i < TEST_WRITERS; ++i)
            fail += expect_true("observes every producer record",
                next_seq[i] == TEST_WRITES_PER_THREAD);
    }
    pair_close(&a, &b);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_shutdown(void)
{
    const char *name = "kc_rstrm_shutdown";
    const char *detail = "orders EOF after queued bytes and keeps reverse direction open";
    endpoint_t a, b;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    fail += expect_true("opens half-close pair", pair_open(&a, &b, 0) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues bytes before shutdown",
            kc_rstrm_send(a.stream, "before", 6) == KC_RSTRM_OK);
        fail += expect_true("shutdown succeeds",
            kc_rstrm_shutdown(a.stream) == KC_RSTRM_OK);
        fail += expect_true("repeated shutdown is harmless",
            kc_rstrm_shutdown(a.stream) == KC_RSTRM_OK);
        fail += expect_true("peer observes EOF", wait_counter(&b, 1, 1, 3000));
        fail += expect_true("all bytes precede EOF",
            endpoint_equals(&b, "before", 6) && b.eof_received_size == 6U);
        fail += expect_true("non-empty send after shutdown is closed",
            kc_rstrm_send(a.stream, "x", 1) == KC_RSTRM_ECLOSED);
        fail += expect_true("zero-length send after shutdown is closed",
            kc_rstrm_send(a.stream, NULL, 0) == KC_RSTRM_ECLOSED);
        fail += expect_true("reverse direction remains writable",
            kc_rstrm_send(b.stream, "reply", 5) == KC_RSTRM_OK);
        fail += expect_true("shutdown side still receives",
            wait_size(&a, 5, 3000));
        fail += expect_true("reverse bytes are exact",
            endpoint_equals(&a, "reply", 5));
    }
    pair_close(&a, &b);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_backpressure(void)
{
    const char *name = "kc_rstrm_backpressure";
    const char *detail = "reports EFULL, emits drain, and resumes sending";
    endpoint_t a, b;
    unsigned char *oversized = NULL;
    const size_t oversized_size = 4U * 1024U * 1024U + 1U;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    oversized = (unsigned char *)malloc(oversized_size);
    fail += expect_true("allocates backpressure buffer", oversized != NULL);
    if (oversized) memset(oversized, 0xa5, oversized_size);
    fail += expect_true("opens backpressure pair", pair_open(&a, &b, 0) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("oversized pending send returns EFULL",
            kc_rstrm_send(a.stream, oversized, oversized_size) == KC_RSTRM_EFULL);
        fail += expect_true("drain follows EFULL",
            wait_counter(&a, 2, 1, 2000));
        fail += expect_true("send resumes after drain",
            kc_rstrm_send(a.stream, "ok", 2) == KC_RSTRM_OK);
        fail += expect_true("resumed bytes arrive", wait_size(&b, 2, 2000));
        fail += expect_true("resumed bytes are exact",
            endpoint_equals(&b, "ok", 2));
    }
    pair_close(&a, &b);
    free(oversized);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_receive_callback(void)
{
    const char *name = "kc_rstrm_receive_callback";
    const char *detail = "permits reentrant send from the receive callback";
    endpoint_t a, b;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    b.echo_on_receive = 1;
    fail += expect_true("opens callback pair", pair_open(&a, &b, 0) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("sends callback input",
            kc_rstrm_send(a.stream, "echo", 4) == KC_RSTRM_OK);
        fail += expect_true("callback echo returns", wait_size(&a, 4, 3000));
        fail += expect_true("callback echo is exact",
            endpoint_equals(&a, "echo", 4));
    }
    pair_close(&a, &b);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_close(void)
{
    const char *name = "kc_rstrm_close";
    const char *detail = "allows callback close and suppresses later callbacks";
    endpoint_t a, b;
    size_t received_after_close = 0;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    b.close_on_receive = 1;
    fail += expect_true("opens callback-close pair", pair_open(&a, &b, 0) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("sends callback-close bytes",
            kc_rstrm_send(a.stream, "bye", 3) == KC_RSTRM_OK);
        fail += expect_true("receive callback closes stream",
            wait_counter(&b, 4, 1, 3000));
        sleep_ms(100);
        test_lock(&b.mutex);
        received_after_close = b.received_size;
        test_unlock(&b.mutex);
        fail += expect_true("close callback received triggering bytes",
            received_after_close == 3U);
        (void)kc_rstrm_send(a.stream, "later", 5);
        sleep_ms(150);
        test_lock(&b.mutex);
        fail += expect_true("no callbacks after callback close",
            b.received_size == received_after_close);
        test_unlock(&b.mutex);
    }
    {
        kc_rstrm_t *a_stream = endpoint_stream(&a);
        endpoint_set_stream(&a, NULL);
        kc_rstrm_close(a_stream);
    }
    endpoint_set_stream(&b, NULL);
    kc_rstrm_close(NULL);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_transport_error(void)
{
    const char *name = "kc_rstrm_transport_error";
    const char *detail = "reports one terminal ETRANSPORT and closes operations";
    endpoint_t endpoint;
    kc_rstrm_t *stream = NULL;
    int fail = 0;

    endpoint_init(&endpoint);
    endpoint.fail_output = 1;
    fail += expect_true("opens failing transport",
        kc_rstrm_open(&endpoint.stream, NULL, receive_cb, datagram_cb, eof_cb,
            drain_cb, error_cb, &endpoint) == KC_RSTRM_OK);
    stream = endpoint.stream;
    if (!fail) {
        fail += expect_true("queues data before transport failure",
            kc_rstrm_send(stream, "x", 1) == KC_RSTRM_OK);
        fail += expect_true("transport error callback arrives",
            wait_counter(&endpoint, 3, 1, 2000));
        test_lock(&endpoint.mutex);
        fail += expect_true("transport error is terminal and unique",
            endpoint.error_status == KC_RSTRM_ETRANSPORT &&
            endpoint.error_count == 1);
        test_unlock(&endpoint.mutex);
        fail += expect_true("send closes after transport failure",
            kc_rstrm_send(stream, "x", 1) == KC_RSTRM_ECLOSED);
        fail += expect_true("shutdown closes after transport failure",
            kc_rstrm_shutdown(stream) == KC_RSTRM_ECLOSED);
    }
    endpoint_set_stream(&endpoint, NULL);
    kc_rstrm_close(stream);
    endpoint_fini(&endpoint);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_protocol_error(void)
{
    const char *name = "kc_rstrm_protocol_error";
    const char *detail = "reports one terminal EPROTO for malformed wire data";
    endpoint_t endpoint;
    kc_rstrm_t *stream = NULL;
    const unsigned char junk[3] = {1U, 2U, 3U};
    int fail = 0;

    endpoint_init(&endpoint);
    fail += expect_true("opens malformed-input stream",
        kc_rstrm_open(&endpoint.stream, NULL, receive_cb, datagram_cb, eof_cb,
            drain_cb, error_cb, &endpoint) == KC_RSTRM_OK);
    stream = endpoint.stream;
    if (!fail) {
        fail += expect_true("queues malformed input",
            kc_rstrm_input(stream, junk, sizeof(junk)) == KC_RSTRM_OK);
        fail += expect_true("protocol error callback arrives",
            wait_counter(&endpoint, 3, 1, 2000));
        test_lock(&endpoint.mutex);
        fail += expect_true("protocol error is terminal and unique",
            endpoint.error_status == KC_RSTRM_EPROTO && endpoint.error_count == 1);
        test_unlock(&endpoint.mutex);
        fail += expect_true("input closes after protocol failure",
            kc_rstrm_input(stream, junk, sizeof(junk)) == KC_RSTRM_ECLOSED);
    }
    endpoint_set_stream(&endpoint, NULL);
    kc_rstrm_close(stream);
    endpoint_fini(&endpoint);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_error_callback(void)
{
    const char *name = "kc_rstrm_error_callback";
    const char *detail = "permits close from the terminal error callback";
    endpoint_t endpoint;
    int fail = 0;

    endpoint_init(&endpoint);
    endpoint.fail_output = 1;
    endpoint.close_on_error = 1;
    fail += expect_true("opens self-closing error stream",
        kc_rstrm_open(&endpoint.stream, NULL, receive_cb, datagram_cb, eof_cb,
            drain_cb, error_cb, &endpoint) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues data for terminal failure",
            kc_rstrm_send(endpoint.stream, "x", 1) == KC_RSTRM_OK);
        fail += expect_true("error callback closes stream",
            wait_counter(&endpoint, 4, 1, 2000));
        sleep_ms(100);
        test_lock(&endpoint.mutex);
        fail += expect_true("callback reports transport error",
            endpoint.error_count == 1 &&
            endpoint.error_status == KC_RSTRM_ETRANSPORT &&
            endpoint.stream == NULL);
        test_unlock(&endpoint.mutex);
    }
    endpoint_fini(&endpoint);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_input(void)
{
    const char *name = "kc_rstrm_input";
    const char *detail = "validates complete incoming datagrams synchronously";
    endpoint_t endpoint;
    unsigned char one = 0U;
    unsigned char *large = NULL;
    kc_rstrm_t *stream = NULL;
    int fail = 0;

    endpoint_init(&endpoint);
    fail += expect_true("opens input-validation stream",
        kc_rstrm_open(&endpoint.stream, NULL, receive_cb, datagram_cb, eof_cb,
            drain_cb, error_cb, &endpoint) == KC_RSTRM_OK);
    stream = endpoint.stream;
    large = (unsigned char *)malloc(65536U);
    fail += expect_true("allocates oversized datagram", large != NULL);
    if (!fail) {
        fail += expect_true("rejects NULL input bytes",
            kc_rstrm_input(stream, NULL, 1) == KC_RSTRM_EINVAL);
        fail += expect_true("rejects empty datagram",
            kc_rstrm_input(stream, &one, 0) == KC_RSTRM_EINVAL);
        fail += expect_true("rejects datagram above maximum",
            kc_rstrm_input(stream, large, 65536U) == KC_RSTRM_EINVAL);
        fail += expect_true("rejects NULL stream",
            kc_rstrm_input(NULL, &one, 1) == KC_RSTRM_EINVAL);
    }
    endpoint_set_stream(&endpoint, NULL);
    kc_rstrm_close(stream);
    free(large);
    endpoint_fini(&endpoint);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_kc_rstrm_status(void)
{
    const char *name = "kc_rstrm_status";
    const char *detail = "exposes stable status text and the build version";
    const int statuses[] = {
        KC_RSTRM_OK,
        KC_RSTRM_EINVAL,
        KC_RSTRM_ECLOSED,
        KC_RSTRM_EFULL,
        KC_RSTRM_ETRANSPORT,
        KC_RSTRM_EPROTO,
        KC_RSTRM_ENOMEM,
        -999
    };
    size_t i;
    int fail = 0;

    for (i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i)
        fail += expect_true("status string is non-empty",
            kc_rstrm_strerror(statuses[i]) != NULL &&
            kc_rstrm_strerror(statuses[i])[0] != '\0');
    (void)kc_rstrm_version();
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
static int case_all(void)
{
    int rc = 0;
    test_case_total = 16;
    test_case_current = 0;
    run_case(&rc, case_kc_rstrm_open);
    run_case(&rc, case_kc_rstrm_send);
    run_case(&rc, case_kc_rstrm_reliability);
    run_case(&rc, case_kc_rstrm_ordering);
    run_case(&rc, case_kc_rstrm_mtu);
    run_case(&rc, case_kc_rstrm_full_duplex);
    run_case(&rc, case_kc_rstrm_concurrency);
    run_case(&rc, case_kc_rstrm_shutdown);
    run_case(&rc, case_kc_rstrm_backpressure);
    run_case(&rc, case_kc_rstrm_receive_callback);
    run_case(&rc, case_kc_rstrm_close);
    run_case(&rc, case_kc_rstrm_transport_error);
    run_case(&rc, case_kc_rstrm_protocol_error);
    run_case(&rc, case_kc_rstrm_error_callback);
    run_case(&rc, case_kc_rstrm_input);
    run_case(&rc, case_kc_rstrm_status);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs a reliable-stream contract operation.
 * @return Operation result.
 */
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_rstrm_open") == 0) return case_kc_rstrm_open();
    if (strcmp(argv[1], "kc_rstrm_send") == 0) return case_kc_rstrm_send();
    if (strcmp(argv[1], "kc_rstrm_reliability") == 0)
        return case_kc_rstrm_reliability();
    if (strcmp(argv[1], "kc_rstrm_ordering") == 0)
        return case_kc_rstrm_ordering();
    if (strcmp(argv[1], "kc_rstrm_mtu") == 0) return case_kc_rstrm_mtu();
    if (strcmp(argv[1], "kc_rstrm_full_duplex") == 0)
        return case_kc_rstrm_full_duplex();
    if (strcmp(argv[1], "kc_rstrm_concurrency") == 0)
        return case_kc_rstrm_concurrency();
    if (strcmp(argv[1], "kc_rstrm_shutdown") == 0)
        return case_kc_rstrm_shutdown();
    if (strcmp(argv[1], "kc_rstrm_backpressure") == 0)
        return case_kc_rstrm_backpressure();
    if (strcmp(argv[1], "kc_rstrm_receive_callback") == 0)
        return case_kc_rstrm_receive_callback();
    if (strcmp(argv[1], "kc_rstrm_close") == 0) return case_kc_rstrm_close();
    if (strcmp(argv[1], "kc_rstrm_transport_error") == 0)
        return case_kc_rstrm_transport_error();
    if (strcmp(argv[1], "kc_rstrm_protocol_error") == 0)
        return case_kc_rstrm_protocol_error();
    if (strcmp(argv[1], "kc_rstrm_error_callback") == 0)
        return case_kc_rstrm_error_callback();
    if (strcmp(argv[1], "kc_rstrm_input") == 0) return case_kc_rstrm_input();
    if (strcmp(argv[1], "kc_rstrm_status") == 0) return case_kc_rstrm_status();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
