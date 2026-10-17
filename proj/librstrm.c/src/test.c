/**
 * test.c - librstrm public API contract tests.
 * Summary: Validates reliability, ordering, stream lifecycle, backpressure,
 * fault tolerance, concurrency, and callback safety.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

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
#define TEST_FAULT_SIZE  (128U * 1024U)
#define TEST_WRITERS 4U
#define TEST_WRITES_PER_THREAD 200U
#define TEST_RECORD_SIZE 8U

static int test_case_total = 16;
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
    unsigned drop_every;
    unsigned duplicate_every;
    unsigned reorder_every;
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

static void test_lock(test_mutex_t *mutex)
{
#ifdef _WIN32
    EnterCriticalSection(mutex);
#else
    (void)pthread_mutex_lock(mutex);
#endif
}

static void test_unlock(test_mutex_t *mutex)
{
#ifdef _WIN32
    LeaveCriticalSection(mutex);
#else
    (void)pthread_mutex_unlock(mutex);
#endif
}

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

static void case_result(int fail, const char *name, const char *detail)
{
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

static void run_case(int *rc, int (*fn)(void))
{
    test_case_current++;
    *rc += fn();
}

static int expect_true(const char *name, int condition)
{
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

static void endpoint_init(endpoint_t *endpoint)
{
    memset(endpoint, 0, sizeof(*endpoint));
#ifdef _WIN32
    InitializeCriticalSection(&endpoint->mutex);
#else
    (void)pthread_mutex_init(&endpoint->mutex, NULL);
#endif
}

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

static kc_rstrm_t *endpoint_stream(endpoint_t *endpoint)
{
    kc_rstrm_t *stream;
    test_lock(&endpoint->mutex);
    stream = endpoint->stream;
    test_unlock(&endpoint->mutex);
    return stream;
}

static void endpoint_set_stream(endpoint_t *endpoint, kc_rstrm_t *stream)
{
    test_lock(&endpoint->mutex);
    endpoint->stream = stream;
    test_unlock(&endpoint->mutex);
}

static int endpoint_deliver(endpoint_t *endpoint, const void *data, size_t size)
{
    kc_rstrm_t *stream;
    int status;

    if (!endpoint || !data || size == 0) return 0;
    test_lock(&endpoint->mutex);
    stream = endpoint->stream;
    if (!stream) {
        test_unlock(&endpoint->mutex);
        return 0;
    }
    status = kc_rstrm_input(stream, data, size);
    test_unlock(&endpoint->mutex);

    if (status == KC_RSTRM_OK || status == KC_RSTRM_EFULL ||
        status == KC_RSTRM_ECLOSED)
        return 0;
    return -1;
}

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

static int datagram_cb(const void *data, size_t data_size, void *userdata)
{
    endpoint_t *endpoint = (endpoint_t *)userdata;
    unsigned number;
    int status;

    if (endpoint->fail_output) return -1;
    endpoint->tx_count++;
    number = endpoint->tx_count;
    if (data_size > endpoint->max_datagram) endpoint->max_datagram = data_size;

    if (endpoint->drop_every && number % endpoint->drop_every == 0U) {
        endpoint->dropped++;
        return 0;
    }

    if (endpoint->reorder_every && number % endpoint->reorder_every == 0U &&
        !endpoint->held)
    {
        endpoint->held = (unsigned char *)malloc(data_size);
        if (!endpoint->held) return -1;
        memcpy(endpoint->held, data, data_size);
        endpoint->held_size = data_size;
        endpoint->reordered++;
        return 0;
    }

    status = endpoint_deliver(endpoint->peer, data, data_size);
    if (status != 0) return status;

    if (endpoint->duplicate_every && number % endpoint->duplicate_every == 0U) {
        endpoint->duplicated++;
        status = endpoint_deliver(endpoint->peer, data, data_size);
        if (status != 0) return status;
    }

    if (endpoint->held) {
        unsigned char *held = endpoint->held;
        size_t held_size = endpoint->held_size;
        endpoint->held = NULL;
        endpoint->held_size = 0;
        status = endpoint_deliver(endpoint->peer, held, held_size);
        free(held);
        if (status != 0) return status;
    }
    return 0;
}

static void eof_cb(void *userdata)
{
    endpoint_t *endpoint = (endpoint_t *)userdata;
    test_lock(&endpoint->mutex);
    endpoint->eof_count++;
    endpoint->eof_received_size = endpoint->received_size;
    test_unlock(&endpoint->mutex);
}

static void drain_cb(void *userdata)
{
    endpoint_t *endpoint = (endpoint_t *)userdata;
    test_lock(&endpoint->mutex);
    endpoint->drain_count++;
    test_unlock(&endpoint->mutex);
}

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

static int pair_open(endpoint_t *a, endpoint_t *b, size_t mtu)
{
    kc_rstrm_options_t options;
    const kc_rstrm_options_t *options_ptr = NULL;
    int status;

    a->peer = b;
    b->peer = a;
    if (mtu) {
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

static void pair_close(endpoint_t *a, endpoint_t *b)
{
    kc_rstrm_t *a_stream = endpoint_stream(a);
    kc_rstrm_t *b_stream = endpoint_stream(b);
    endpoint_set_stream(a, NULL);
    endpoint_set_stream(b, NULL);
    kc_rstrm_close(a_stream);
    kc_rstrm_close(b_stream);
}

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

static int endpoint_equals(endpoint_t *endpoint, const void *data, size_t size)
{
    int equal;
    test_lock(&endpoint->mutex);
    equal = !endpoint->callback_fail && endpoint->received_size == size &&
        (size == 0 || memcmp(endpoint->received, data, size) == 0);
    test_unlock(&endpoint->mutex);
    return equal;
}

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

static void store32le(unsigned char out[4], uint32_t value)
{
    out[0] = (unsigned char)(value & 0xffU);
    out[1] = (unsigned char)((value >> 8) & 0xffU);
    out[2] = (unsigned char)((value >> 16) & 0xffU);
    out[3] = (unsigned char)((value >> 24) & 0xffU);
}

static uint32_t load32le(const unsigned char in[4])
{
    return (uint32_t)in[0] |
        ((uint32_t)in[1] << 8) |
        ((uint32_t)in[2] << 16) |
        ((uint32_t)in[3] << 24);
}

#ifdef _WIN32
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

static int case_api_validation(void)
{
    const char *name = "public API validation";
    const char *detail = "rejects invalid arguments and MTU values";
    endpoint_t endpoint;
    kc_rstrm_t *stream = (kc_rstrm_t *)(uintptr_t)1U;
    kc_rstrm_options_t options;
    int fail = 0;

    endpoint_init(&endpoint);
    fail += expect_true("open rejects NULL destination",
        kc_rstrm_open(NULL, NULL, receive_cb, datagram_cb, NULL, NULL, NULL,
            &endpoint) == KC_RSTRM_EINVAL);
    fail += expect_true("open rejects NULL receive",
        kc_rstrm_open(&stream, NULL, NULL, datagram_cb, NULL, NULL, NULL,
            &endpoint) == KC_RSTRM_EINVAL && stream == NULL);
    fail += expect_true("open rejects NULL datagram",
        kc_rstrm_open(&stream, NULL, receive_cb, NULL, NULL, NULL, NULL,
            &endpoint) == KC_RSTRM_EINVAL && stream == NULL);
    options.mtu = 511;
    fail += expect_true("open rejects MTU below minimum",
        kc_rstrm_open(&stream, &options, receive_cb, datagram_cb, NULL, NULL,
            NULL, &endpoint) == KC_RSTRM_EINVAL && stream == NULL);
    options.mtu = 65536U;
    fail += expect_true("open rejects MTU above maximum",
        kc_rstrm_open(&stream, &options, receive_cb, datagram_cb, NULL, NULL,
            NULL, &endpoint) == KC_RSTRM_EINVAL && stream == NULL);
    fail += expect_true("send rejects NULL stream",
        kc_rstrm_send(NULL, "x", 1) == KC_RSTRM_EINVAL);
    fail += expect_true("input rejects NULL stream",
        kc_rstrm_input(NULL, "x", 1) == KC_RSTRM_EINVAL);
    fail += expect_true("shutdown rejects NULL stream",
        kc_rstrm_shutdown(NULL) == KC_RSTRM_EINVAL);
    kc_rstrm_close(NULL);
    endpoint_fini(&endpoint);
    case_result(fail, name, detail);
    return fail;
}

static int case_basic_stream(void)
{
    const char *name = "basic stream";
    const char *detail = "delivers concatenated ordered bytes across send boundaries";
    endpoint_t a, b;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    fail += expect_true("opens stream pair", pair_open(&a, &b, 0) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("zero-length send is accepted while open",
            kc_rstrm_send(a.stream, NULL, 0) == KC_RSTRM_OK);
        fail += expect_true("queues abc",
            kc_rstrm_send(a.stream, "abc", 3) == KC_RSTRM_OK);
        fail += expect_true("queues def",
            kc_rstrm_send(a.stream, "def", 3) == KC_RSTRM_OK);
        fail += expect_true("receives six bytes", wait_size(&b, 6, 2000));
        fail += expect_true("preserves byte order", endpoint_equals(&b,
            "abcdef", 6));
    }
    pair_close(&a, &b);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail;
}

static int case_loss_recovery(void)
{
    const char *name = "loss recovery";
    const char *detail = "retransmits through deterministic data and ACK loss";
    endpoint_t a, b;
    unsigned char *payload = NULL;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    a.drop_every = 5;
    b.drop_every = 7;
    payload = (unsigned char *)malloc(TEST_FAULT_SIZE);
    fail += expect_true("allocates loss payload", payload != NULL);
    if (payload) fill_pattern(payload, TEST_FAULT_SIZE, 1);
    fail += expect_true("opens loss pair", pair_open(&a, &b, 900) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues loss payload",
            kc_rstrm_send(a.stream, payload, TEST_FAULT_SIZE) == KC_RSTRM_OK);
        fail += expect_true("recovers all lost bytes",
            wait_size(&b, TEST_FAULT_SIZE, TEST_TIMEOUT_MS));
        fail += expect_true("loss payload is exact",
            endpoint_equals(&b, payload, TEST_FAULT_SIZE));
        fail += expect_true("fault injector actually dropped datagrams",
            a.dropped + b.dropped > 0U);
    }
    pair_close(&a, &b);
    free(payload);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail;
}

static int case_reorder_duplicate(void)
{
    const char *name = "reorder and duplication";
    const char *detail = "deduplicates and restores order under datagram faults";
    endpoint_t a, b;
    unsigned char *payload = NULL;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    a.reorder_every = 3;
    a.duplicate_every = 4;
    b.reorder_every = 4;
    b.duplicate_every = 5;
    payload = (unsigned char *)malloc(TEST_FAULT_SIZE);
    fail += expect_true("allocates reorder payload", payload != NULL);
    if (payload) fill_pattern(payload, TEST_FAULT_SIZE, 2);
    fail += expect_true("opens reorder pair", pair_open(&a, &b, 900) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues reorder payload",
            kc_rstrm_send(a.stream, payload, TEST_FAULT_SIZE) == KC_RSTRM_OK);
        fail += expect_true("receives reordered payload",
            wait_size(&b, TEST_FAULT_SIZE, TEST_TIMEOUT_MS));
        fail += expect_true("reordered payload is exact",
            endpoint_equals(&b, payload, TEST_FAULT_SIZE));
        fail += expect_true("fault injector reordered datagrams",
            a.reordered + b.reordered > 0U);
        fail += expect_true("fault injector duplicated datagrams",
            a.duplicated + b.duplicated > 0U);
    }
    pair_close(&a, &b);
    free(payload);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail;
}

static int case_small_mtu_large_payload(void)
{
    const char *name = "MTU and large payload";
    const char *detail = "fragments a large stream without exceeding transport MTU";
    endpoint_t a, b;
    unsigned char *payload = NULL;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    payload = (unsigned char *)malloc(TEST_LARGE_SIZE);
    fail += expect_true("allocates large payload", payload != NULL);
    if (payload) fill_pattern(payload, TEST_LARGE_SIZE, 3);
    fail += expect_true("opens minimum-MTU pair", pair_open(&a, &b, 512) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues large payload",
            kc_rstrm_send(a.stream, payload, TEST_LARGE_SIZE) == KC_RSTRM_OK);
        fail += expect_true("receives large payload",
            wait_size(&b, TEST_LARGE_SIZE, TEST_TIMEOUT_MS));
        fail += expect_true("large payload is exact",
            endpoint_equals(&b, payload, TEST_LARGE_SIZE));
        fail += expect_true("all emitted datagrams honor MTU",
            a.max_datagram > 0 && a.max_datagram <= 512U &&
            b.max_datagram <= 512U);
    }
    pair_close(&a, &b);
    free(payload);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail;
}

static int case_full_duplex(void)
{
    const char *name = "full duplex";
    const char *detail = "moves independent byte streams in both directions concurrently";
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
    if (from_a) fill_pattern(from_a, size, 4);
    if (from_b) fill_pattern(from_b, size, 5);
    fail += expect_true("opens duplex pair", pair_open(&a, &b, 1000) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues A to B",
            kc_rstrm_send(a.stream, from_a, size) == KC_RSTRM_OK);
        fail += expect_true("queues B to A",
            kc_rstrm_send(b.stream, from_b, size) == KC_RSTRM_OK);
        fail += expect_true("A receives complete reverse stream",
            wait_size(&a, size, TEST_TIMEOUT_MS));
        fail += expect_true("B receives complete forward stream",
            wait_size(&b, size, TEST_TIMEOUT_MS));
        fail += expect_true("A reverse bytes are exact", endpoint_equals(&a,
            from_b, size));
        fail += expect_true("B forward bytes are exact", endpoint_equals(&b,
            from_a, size));
    }
    pair_close(&a, &b);
    free(from_a);
    free(from_b);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail;
}

static int case_concurrent_senders(void)
{
    const char *name = "concurrent senders";
    const char *detail = "serializes thread-safe sends without corrupting per-thread order";
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
            fail += expect_true("producer completed successfully",
                writers[i].status == KC_RSTRM_OK);
        }
        fail += expect_true("receives all concurrent writes",
            wait_size(&b, expected, TEST_TIMEOUT_MS));
        if (b.received_size == expected) {
            test_lock(&b.mutex);
            for (i = 0; i < expected / TEST_RECORD_SIZE; ++i) {
                const unsigned char *record = b.received + i * TEST_RECORD_SIZE;
                uint32_t id = load32le(record);
                uint32_t seq = load32le(record + 4);
                if (id >= TEST_WRITERS || seq != next_seq[id]) {
                    fail += expect_true("concurrent record ordering", 0);
                    break;
                }
                next_seq[id]++;
            }
            test_unlock(&b.mutex);
            for (i = 0; i < TEST_WRITERS; ++i)
                fail += expect_true("all producer records observed",
                    next_seq[i] == TEST_WRITES_PER_THREAD);
        }
    }
    pair_close(&a, &b);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail;
}

static int case_half_close(void)
{
    const char *name = "graceful half-close";
    const char *detail = "delivers queued bytes before EOF and keeps reverse direction open";
    endpoint_t a, b;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    fail += expect_true("opens half-close pair", pair_open(&a, &b, 0) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues bytes before shutdown",
            kc_rstrm_send(a.stream, "before", 6) == KC_RSTRM_OK);
        fail += expect_true("first shutdown succeeds",
            kc_rstrm_shutdown(a.stream) == KC_RSTRM_OK);
        fail += expect_true("repeated shutdown is harmless",
            kc_rstrm_shutdown(a.stream) == KC_RSTRM_OK);
        fail += expect_true("peer observes EOF", wait_counter(&b, 1, 1, 3000));
        fail += expect_true("all bytes precede EOF",
            endpoint_equals(&b, "before", 6) && b.eof_received_size == 6);
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
    return fail;
}

static int case_backpressure(void)
{
    const char *name = "backpressure";
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
        fail += expect_true("resumed bytes are exact", endpoint_equals(&b,
            "ok", 2));
    }
    pair_close(&a, &b);
    free(oversized);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail;
}

static int case_callback_reentrancy(void)
{
    const char *name = "callback reentrancy";
    const char *detail = "allows send from the receive callback";
    endpoint_t a, b;
    int fail = 0;

    endpoint_init(&a);
    endpoint_init(&b);
    b.echo_on_receive = 1;
    fail += expect_true("opens echo pair", pair_open(&a, &b, 0) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("sends callback echo input",
            kc_rstrm_send(a.stream, "echo", 4) == KC_RSTRM_OK);
        fail += expect_true("callback echo returns", wait_size(&a, 4, 3000));
        fail += expect_true("callback echo is exact", endpoint_equals(&a,
            "echo", 4));
    }
    pair_close(&a, &b);
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail;
}

static int case_close_from_callback(void)
{
    const char *name = "close from callback";
    const char *detail = "defers self-release safely when receive closes the stream";
    endpoint_t a, b;
    size_t received_after_close;
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
            received_after_close == 3);
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
    endpoint_fini(&a);
    endpoint_fini(&b);
    case_result(fail, name, detail);
    return fail;
}

static int case_transport_failure(void)
{
    const char *name = "transport failure";
    const char *detail = "reports one terminal ETRANSPORT and closes operations";
    endpoint_t endpoint;
    kc_rstrm_t *stream;
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
        fail += expect_true("transport failure callback arrives",
            wait_counter(&endpoint, 3, 1, 2000));
        test_lock(&endpoint.mutex);
        fail += expect_true("transport error status is terminal",
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
    return fail;
}

static int case_protocol_failure(void)
{
    const char *name = "protocol failure";
    const char *detail = "rejects malformed wire data with one terminal EPROTO";
    endpoint_t endpoint;
    kc_rstrm_t *stream;
    const unsigned char junk[3] = {1, 2, 3};
    int fail = 0;

    endpoint_init(&endpoint);
    fail += expect_true("opens malformed-input stream",
        kc_rstrm_open(&endpoint.stream, NULL, receive_cb, datagram_cb, eof_cb,
            drain_cb, error_cb, &endpoint) == KC_RSTRM_OK);
    stream = endpoint.stream;
    if (!fail) {
        fail += expect_true("malformed input is queued",
            kc_rstrm_input(stream, junk, sizeof(junk)) == KC_RSTRM_OK);
        fail += expect_true("protocol failure callback arrives",
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
    return fail;
}

static int case_error_callback_close(void)
{
    const char *name = "error callback close";
    const char *detail = "allows terminal error callback to close its own stream";
    endpoint_t endpoint;
    int fail = 0;

    endpoint_init(&endpoint);
    endpoint.fail_output = 1;
    endpoint.close_on_error = 1;
    fail += expect_true("opens self-closing error stream",
        kc_rstrm_open(&endpoint.stream, NULL, receive_cb, datagram_cb, eof_cb,
            drain_cb, error_cb, &endpoint) == KC_RSTRM_OK);
    if (!fail) {
        fail += expect_true("queues data for self-closing failure",
            kc_rstrm_send(endpoint.stream, "x", 1) == KC_RSTRM_OK);
        fail += expect_true("error callback closes stream",
            wait_counter(&endpoint, 4, 1, 2000));
        sleep_ms(100);
        test_lock(&endpoint.mutex);
        fail += expect_true("self-closing callback reports transport error",
            endpoint.error_count == 1 &&
            endpoint.error_status == KC_RSTRM_ETRANSPORT &&
            endpoint.stream == NULL);
        test_unlock(&endpoint.mutex);
    }
    endpoint_fini(&endpoint);
    case_result(fail, name, detail);
    return fail;
}

static int case_input_validation(void)
{
    const char *name = "input validation";
    const char *detail = "rejects missing, empty, and oversized datagrams synchronously";
    endpoint_t endpoint;
    unsigned char one = 0;
    unsigned char *large = NULL;
    kc_rstrm_t *stream;
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
        fail += expect_true("rejects datagram above protocol maximum",
            kc_rstrm_input(stream, large, 65536U) == KC_RSTRM_EINVAL);
    }
    endpoint_set_stream(&endpoint, NULL);
    kc_rstrm_close(stream);
    free(large);
    endpoint_fini(&endpoint);
    case_result(fail, name, detail);
    return fail;
}

static int case_status_metadata(void)
{
    const char *name = "status metadata";
    const char *detail = "exposes stable status strings and build version";
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
            kc_rstrm_strerror(statuses[i]) && kc_rstrm_strerror(statuses[i])[0]);
    (void)kc_rstrm_version();
    case_result(fail, name, detail);
    return fail;
}

int main(void)
{
    int fail = 0;

    run_case(&fail, case_api_validation);
    run_case(&fail, case_basic_stream);
    run_case(&fail, case_loss_recovery);
    run_case(&fail, case_reorder_duplicate);
    run_case(&fail, case_small_mtu_large_payload);
    run_case(&fail, case_full_duplex);
    run_case(&fail, case_concurrent_senders);
    run_case(&fail, case_half_close);
    run_case(&fail, case_backpressure);
    run_case(&fail, case_callback_reentrancy);
    run_case(&fail, case_close_from_callback);
    run_case(&fail, case_transport_failure);
    run_case(&fail, case_protocol_failure);
    run_case(&fail, case_error_callback_close);
    run_case(&fail, case_input_validation);
    run_case(&fail, case_status_metadata);

    if (fail == 0)
        printf("OK rstrm contract (%d cases)\n", test_case_total);
    else
        printf("FAIL rstrm contract: %d assertion(s) failed\n", fail);
    return fail ? 1 : 0;
}
