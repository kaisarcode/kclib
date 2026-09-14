/**
 * test.c - libnetl public API tests.
 * Summary: Tests each public libnetl function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnetl.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#define getpid _getpid
#else
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define mkdir_one(path) mkdir(path, 0700)
#endif

/**
 * Describes list callback observations.
 */
typedef struct {
    const char *expected_key;
    int count;
    int exact_count;
    char first_addrport[256];
} list_state_t;

/**
 * Records one list callback entry.
 * @param key Listener key name.
 * @param addrport Address and port string.
 * @param userdata List state pointer.
 * @return None.
 */
static void record_list(const char *key, const char *addrport, void *userdata) {
    list_state_t *state;

    state = (list_state_t *)userdata;
    if (state == NULL || key == NULL || addrport == NULL) return;
    state->count++;
    if (state->count == 1) {
        snprintf(state->first_addrport, sizeof(state->first_addrport), "%s", addrport);
    }
    if (state->expected_key != NULL && strcmp(key, state->expected_key) == 0) {
        state->exact_count++;
    }
}

/**
 * Verifies one integer result.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Non-zero when the check passed.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Verifies one string result.
 * @param name Check description.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual != NULL ? actual : "NULL");
        return 1;
    }
    return 0;
}

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case name.
 * @param detail Test behavior detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

typedef int (*case_fn)(void);

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

#ifndef _WIN32
/**
 * Sleeps briefly to let processes reach readiness.
 * @return None.
 */
static void short_sleep(void) {
    struct timespec ts;

    ts.tv_sec = 0;
    ts.tv_nsec = 200000000L;
    nanosleep(&ts, NULL);
}
#endif

#ifndef _WIN32
/**
 * Finds a free TCP port by binding to port 0.
 * @return Port number, or 0 on failure.
 */
static unsigned short find_free_port(void) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return 0;
    }
    if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
        close(fd);
        return 0;
    }
    close(fd);
    return ntohs(addr.sin_port);
}

/**
 * Kills a process and reaps it to avoid zombies.
 * @param pid Process ID.
 * @return None.
 */
static void kill_and_reap(pid_t pid) {
    if (pid <= 0) return;
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}
#endif

/**
 * Tests kc_netl_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_version(void) {
    const char *name = "kc_netl_version";
    const char *detail = "returns non-zero build timestamp";
    int fail = expect_true("kc_netl_version returns non-zero build timestamp", kc_netl_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_strerror.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_strerror(void) {
    const char *name = "kc_netl_strerror";
    const char *detail = "maps codes to messages";
    int fail;

    fail = 0;
    fail += expect_string("strerror(OK)", "ok", kc_netl_strerror(KC_NETL_OK));
    fail += expect_string("strerror(ERROR)", "error", kc_netl_strerror(KC_NETL_ERROR));
    fail += expect_string("strerror(ENET)", "network error", kc_netl_strerror(KC_NETL_ENET));
    fail += expect_string("strerror(unknown)", "unknown error", kc_netl_strerror(999));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_options_default(void) {
    const char *name = "kc_netl_options_default";
    const char *detail = "returns zeroed options";
    kc_netl_options_t opts;

    opts = kc_netl_options_default();
    int fail = expect_true("kc_netl_options_default returns zeroed options", opts.reserved == 0);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_options_load_env(void) {
    const char *name = "kc_netl_options_load_env";
    const char *detail = "loads environment options";
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    opts = kc_netl_options_default();
    kc_netl_options_load_env(&opts);
    fail += expect_true("load_env does not crash on valid opts", 1);
    kc_netl_options_load_env(NULL);
    fail += expect_true("load_env(NULL) does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_options_free(void) {
    const char *name = "kc_netl_options_free";
    const char *detail = "frees without crashing";
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    opts = kc_netl_options_default();
    kc_netl_options_free(&opts);
    fail += expect_true("options_free does not crash", 1);
    kc_netl_options_free(NULL);
    fail += expect_true("options_free(NULL) does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_request_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_request_stop(void) {
    const char *name = "kc_netl_request_stop";
    const char *detail = "is idempotent";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    fail += expect_int("request_stop(NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_request_stop(NULL));
    opts = kc_netl_options_default();
    if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) return 1;
    fail += expect_int("request_stop(ctx) returns OK", KC_NETL_OK,
        kc_netl_request_stop(ctx));
    fail += expect_int("request_stop is idempotent", KC_NETL_OK,
        kc_netl_request_stop(ctx));
    kc_netl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_open(void) {
    const char *name = "kc_netl_open";
    const char *detail = "validates arguments and allocates context";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    ctx = NULL;
    opts = kc_netl_options_default();
    fail += expect_int("open(NULL, opts) returns ERROR", KC_NETL_ERROR,
        kc_netl_open(NULL, &opts));
    fail += expect_int("open(out, NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_open(&ctx, NULL));
    fail += expect_true("open error leaves output NULL", ctx == NULL);
    fail += expect_int("open valid opts returns OK", KC_NETL_OK,
        kc_netl_open(&ctx, &opts));
    fail += expect_true("open creates valid context", ctx != NULL);
    kc_netl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_close(void) {
    const char *name = "kc_netl_close";
    const char *detail = "releases context";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    fail += expect_int("close(NULL) returns ERROR", KC_NETL_ERROR, kc_netl_close(NULL));
    opts = kc_netl_options_default();
    if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) return 1;
    fail += expect_int("close releases context", KC_NETL_OK, kc_netl_close(ctx));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_path.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_path(void) {
    const char *name = "kc_netl_path";
    const char *detail = "returns metadata file location";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    fail += expect_true("path(NULL) returns NULL", kc_netl_path(NULL) == NULL);
    opts = kc_netl_options_default();
    if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) return 1;
    fail += expect_true("path returns non-NULL string", kc_netl_path(ctx) != NULL);
    fail += expect_true("path contains 'netl'", strstr(kc_netl_path(ctx), "netl") != NULL);
    kc_netl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_update.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_update(void) {
    const char *name = "kc_netl_update";
    const char *detail = "validates and replaces registrations";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    fail += expect_int("update(NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_update(NULL, "key", "127.0.0.1", 8080, KC_NETL_TCP, "cat"));
    opts = kc_netl_options_default();
    if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) return 1;
    fail += expect_int("update(ctx, NULL, ...) returns ERROR", KC_NETL_ERROR,
        kc_netl_update(ctx, NULL, "127.0.0.1", 8080, KC_NETL_TCP, "cat"));
    fail += expect_int("update(ctx, key, NULL, ...) returns ERROR", KC_NETL_ERROR,
        kc_netl_update(ctx, "key", NULL, 8080, KC_NETL_TCP, "cat"));
    fail += expect_int("update(ctx, key, host, port, proto, NULL) returns OK (cmd ignored)", KC_NETL_OK,
        kc_netl_update(ctx, "key", "127.0.0.1", 8080, KC_NETL_TCP, NULL));
    fail += expect_int("update(ctx, key, host, port, bad_proto, cmd) returns ERROR", KC_NETL_ERROR,
        kc_netl_update(ctx, "key", "127.0.0.1", 8080, 99, "cat"));
    fail += expect_int("update with valid TCP returns OK", KC_NETL_OK,
        kc_netl_update(ctx, "testupd", "127.0.0.1", 9001, KC_NETL_TCP, "cat"));
    fail += expect_int("update with valid UDP returns OK", KC_NETL_OK,
        kc_netl_update(ctx, "testupd2", "127.0.0.1", 9002, KC_NETL_UDP, "cat"));
    fail += expect_int("replace key returns OK", KC_NETL_OK,
        kc_netl_update(ctx, "testupd", "127.0.0.1", 9003, KC_NETL_TCP, "echo"));
    kc_netl_delete(ctx, "key");
    kc_netl_delete(ctx, "testupd");
    kc_netl_delete(ctx, "testupd2");
    kc_netl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_list.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_list(void) {
    const char *name = "kc_netl_list";
    const char *detail = "enumerates registrations via callback";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    list_state_t state;
    int fail;

    fail = 0;
    fail += expect_int("list(NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_list(NULL, NULL, NULL, NULL));
    opts = kc_netl_options_default();
    if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) return 1;
    fail += expect_int("list missing key returns ERROR", KC_NETL_ERROR,
        kc_netl_list(ctx, "missing", NULL, NULL));
    fail += expect_int("list with NULL callback returns OK", KC_NETL_OK,
        kc_netl_list(ctx, NULL, NULL, NULL));
    fail += expect_int("update key for list test", KC_NETL_OK,
        kc_netl_update(ctx, "listed", "127.0.0.1", 9010, KC_NETL_TCP, "cat"));
    memset(&state, 0, sizeof(state));
    state.expected_key = "listed";
    fail += expect_int("list exact key returns OK", KC_NETL_OK,
        kc_netl_list(ctx, "listed", record_list, &state));
    fail += expect_int("list exact key invokes callback", 1, state.exact_count);
    fail += expect_true("list callback provides addrport", state.first_addrport[0] != '\0');
    fail += expect_string("list addrport matches", "127.0.0.1:9010", state.first_addrport);
    memset(&state, 0, sizeof(state));
    state.expected_key = "listed";
    fail += expect_int("list all returns OK", KC_NETL_OK,
        kc_netl_list(ctx, NULL, record_list, &state));
    fail += expect_true("list all includes listed key", state.exact_count >= 1);
    kc_netl_delete(ctx, "listed");
    kc_netl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_delete.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_delete(void) {
    const char *name = "kc_netl_delete";
    const char *detail = "removes registrations";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    list_state_t state;
    int fail;

    fail = 0;
    fail += expect_int("delete(NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_delete(NULL, "key"));
    opts = kc_netl_options_default();
    if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) return 1;
    fail += expect_int("delete(ctx, NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_delete(ctx, NULL));
    fail += expect_int("delete(ctx, empty) returns ERROR", KC_NETL_ERROR,
        kc_netl_delete(ctx, ""));
    fail += expect_int("delete missing key returns OK (no-op)", KC_NETL_OK,
        kc_netl_delete(ctx, "missing"));
    fail += expect_int("update key for delete test", KC_NETL_OK,
        kc_netl_update(ctx, "delme", "127.0.0.1", 9020, KC_NETL_TCP, "cat"));
    fail += expect_int("delete existing key returns OK", KC_NETL_OK,
        kc_netl_delete(ctx, "delme"));
    memset(&state, 0, sizeof(state));
    state.expected_key = "delme";
    fail += expect_int("list deleted key returns ERROR", KC_NETL_ERROR,
        kc_netl_list(ctx, "delme", record_list, &state));
    fail += expect_int("deleted key is not listed", 0, state.exact_count);
    kc_netl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_set_pid.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_set_pid(void) {
    const char *name = "kc_netl_set_pid";
    const char *detail = "records listener process";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    fail += expect_int("set_pid(NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_set_pid(NULL, "key", 1234));
    opts = kc_netl_options_default();
    if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) return 1;
    fail += expect_int("set_pid(ctx, NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_set_pid(ctx, NULL, 1234));
    fail += expect_int("set_pid(ctx, key, pid) on missing key returns ERROR", KC_NETL_ERROR,
        kc_netl_set_pid(ctx, "missing", 1234));
    fail += expect_int("update key for set_pid test", KC_NETL_OK,
        kc_netl_update(ctx, "setpid", "127.0.0.1", 9030, KC_NETL_TCP, "cat"));
    fail += expect_int("set_pid on valid key returns OK", KC_NETL_OK,
        kc_netl_set_pid(ctx, "setpid", 99999));
    kc_netl_delete(ctx, "setpid");
    kc_netl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_exec error paths.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_exec(void) {
    const char *name = "kc_netl_exec";
    const char *detail = "rejects invalid arguments";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    fail += expect_int("exec(NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_exec(NULL, "key"));
    opts = kc_netl_options_default();
    if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) return 1;
    fail += expect_int("exec(ctx, NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_exec(ctx, NULL));
    fail += expect_int("exec(ctx, empty) returns ERROR", KC_NETL_ERROR,
        kc_netl_exec(ctx, ""));
    fail += expect_int("exec(ctx, missing) returns ERROR", KC_NETL_ERROR,
        kc_netl_exec(ctx, "nonexistent"));
    kc_netl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_stop(void) {
    const char *name = "kc_netl_stop";
    const char *detail = "rejects invalid arguments";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    fail += expect_int("stop(NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_stop(NULL, "key"));
    opts = kc_netl_options_default();
    if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) return 1;
    fail += expect_int("stop(ctx, NULL) returns ERROR", KC_NETL_ERROR,
        kc_netl_stop(ctx, NULL));
    fail += expect_int("stop(ctx, missing) returns ERROR", KC_NETL_ERROR,
        kc_netl_stop(ctx, "nonexistent"));
    kc_netl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_serve error paths.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_serve(void) {
    const char *name = "kc_netl_serve";
    const char *detail = "rejects invalid arguments";
    int fail;

    fail = 0;
    fail += expect_int("serve(NULL, port, TCP, cmd) returns ERROR", KC_NETL_ERROR,
        kc_netl_serve(NULL, 0, KC_NETL_TCP, NULL));
    fail += expect_int("serve(host, port, TCP, empty_cmd) returns ERROR", KC_NETL_ERROR,
        kc_netl_serve("127.0.0.1", 1, KC_NETL_TCP, ""));
    fail += expect_int("serve(host, port, bad_proto, cmd) returns ERROR", KC_NETL_ERROR,
        kc_netl_serve("127.0.0.1", 1, 99, "cat"));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

#ifndef _WIN32
/**
 * Tests kc_netl_exec success by forking a listener, connecting, and killing it.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_exec_success(void) {
    const char *name = "kc_netl_exec_success";
    const char *detail = "bridges tcp stream";
    kc_netl_t *ctx;
    kc_netl_options_t opts;
    unsigned short port;
    pid_t pid;
    int fail;

    fail = 0;
    port = find_free_port();
    if (port == 0) {
        fail += expect_true("find_free_port succeeded", 0);
        case_result(fail, name, detail);
        return 1;
    }
    opts = kc_netl_options_default();
    if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) return 1;
    fail += expect_int("update for exec test", KC_NETL_OK,
        kc_netl_update(ctx, "execsrv", "127.0.0.1", port, KC_NETL_TCP, "cat"));
    pid = fork();
    if (pid == 0) {
        kc_netl_exec(ctx, "execsrv");
        _exit(1);
    }
    short_sleep();
    {
        struct sockaddr_in addr;
        int fd;
        char buf[64];
        ssize_t n;

        fd = socket(AF_INET, SOCK_STREAM, 0);
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        fail += expect_int("connect to exec listener", 0,
            connect(fd, (struct sockaddr *)&addr, sizeof(addr)));
        write(fd, "ping\n", 5);
        shutdown(fd, SHUT_WR);
        memset(buf, 0, sizeof(buf));
        n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) buf[n] = '\0';
        fail += expect_string("exec listener echoes back", "ping\n", buf);
        close(fd);
    }
    kill_and_reap(pid);
    kc_netl_delete(ctx, "execsrv");
    kc_netl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_netl_serve success path with TCP listener.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_serve_success(void) {
    const char *name = "kc_netl_serve_success";
    const char *detail = "dispatches to command";
    unsigned short port;
    pid_t pid;
    int fail;

    fail = 0;
    port = find_free_port();
    if (port == 0) {
        fail += expect_true("find_free_port succeeded", 0);
        case_result(fail, name, detail);
        return 1;
    }
    pid = fork();
    if (pid == 0) {
        kc_netl_serve("127.0.0.1", port, KC_NETL_TCP, "cat");
        _exit(1);
    }
    short_sleep();
    {
        struct sockaddr_in addr;
        int fd;
        char buf[64];
        ssize_t n;

        fd = socket(AF_INET, SOCK_STREAM, 0);
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        fail += expect_int("connect to serve listener", 0,
            connect(fd, (struct sockaddr *)&addr, sizeof(addr)));
        write(fd, "hello\n", 6);
        shutdown(fd, SHUT_WR);
        memset(buf, 0, sizeof(buf));
        n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) buf[n] = '\0';
        fail += expect_string("serve listener echoes back", "hello\n", buf);
        close(fd);
    }
    kill_and_reap(pid);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests two contexts coexist with isolated state.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_multictx(void) {
    const char *name = "kc_netl_multictx";
    const char *detail = "keeps contexts independent";
    kc_netl_t *a;
    kc_netl_t *b;
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    opts = kc_netl_options_default();
    if (kc_netl_open(&a, &opts) != KC_NETL_OK) return 1;
    if (kc_netl_open(&b, &opts) != KC_NETL_OK) {
        kc_netl_close(a);
        return 1;
    }
    fail += expect_int("stop a returns OK", KC_NETL_OK, kc_netl_request_stop(a));
    fail += expect_int("stop b returns OK", KC_NETL_OK, kc_netl_request_stop(b));
    fail += expect_int("stop a again returns OK", KC_NETL_OK, kc_netl_request_stop(a));
    fail += expect_true("a path still valid", kc_netl_path(a) != NULL);
    fail += expect_true("b path still valid", kc_netl_path(b) != NULL);
    fail += expect_int("close a returns OK", KC_NETL_OK, kc_netl_close(a));
    fail += expect_int("close b returns OK", KC_NETL_OK, kc_netl_close(b));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}
#else
/**
 * POSIX-only exec success test stub for Windows.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_exec_success(void) {
    const char *name = "kc_netl_exec_success";
    const char *detail = "bridges tcp stream";
    case_result(0, name, detail);
    return 0;
}

/**
 * POSIX-only serve success test stub for Windows.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_serve_success(void) {
    const char *name = "kc_netl_serve_success";
    const char *detail = "dispatches to command";
    case_result(0, name, detail);
    return 0;
}

/**
 * Tests two contexts coexist with isolated state on Windows.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_netl_multictx(void) {
    const char *name = "kc_netl_multictx";
    const char *detail = "keeps contexts independent";
    kc_netl_t *a;
    kc_netl_t *b;
    kc_netl_options_t opts;
    int fail;

    fail = 0;
    opts = kc_netl_options_default();
    if (kc_netl_open(&a, &opts) != KC_NETL_OK) return 1;
    if (kc_netl_open(&b, &opts) != KC_NETL_OK) {
        kc_netl_close(a);
        return 1;
    }
    fail += expect_int("stop a returns OK", KC_NETL_OK, kc_netl_request_stop(a));
    fail += expect_int("stop b returns OK", KC_NETL_OK, kc_netl_request_stop(b));
    fail += expect_int("stop a again returns OK", KC_NETL_OK, kc_netl_request_stop(a));
    fail += expect_int("close a returns OK", KC_NETL_OK, kc_netl_close(a));
    fail += expect_int("close b returns OK", KC_NETL_OK, kc_netl_close(b));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}
#endif

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 19;
    test_case_current = 0;
    run_case(&rc, case_kc_netl_version);
    run_case(&rc, case_kc_netl_strerror);
    run_case(&rc, case_kc_netl_options_default);
    run_case(&rc, case_kc_netl_options_load_env);
    run_case(&rc, case_kc_netl_options_free);
    run_case(&rc, case_kc_netl_request_stop);
    run_case(&rc, case_kc_netl_open);
    run_case(&rc, case_kc_netl_close);
    run_case(&rc, case_kc_netl_path);
    run_case(&rc, case_kc_netl_update);
    run_case(&rc, case_kc_netl_list);
    run_case(&rc, case_kc_netl_delete);
    run_case(&rc, case_kc_netl_set_pid);
    run_case(&rc, case_kc_netl_exec);
    run_case(&rc, case_kc_netl_stop);
    run_case(&rc, case_kc_netl_serve);
    run_case(&rc, case_kc_netl_exec_success);
    run_case(&rc, case_kc_netl_serve_success);
    run_case(&rc, case_kc_netl_multictx);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one named test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_netl_version") == 0) return case_kc_netl_version();
    if (strcmp(argv[1], "kc_netl_strerror") == 0) return case_kc_netl_strerror();
    if (strcmp(argv[1], "kc_netl_options_default") == 0) return case_kc_netl_options_default();
    if (strcmp(argv[1], "kc_netl_options_load_env") == 0) return case_kc_netl_options_load_env();
    if (strcmp(argv[1], "kc_netl_options_free") == 0) return case_kc_netl_options_free();
    if (strcmp(argv[1], "kc_netl_request_stop") == 0) return case_kc_netl_request_stop();
    if (strcmp(argv[1], "kc_netl_open") == 0) return case_kc_netl_open();
    if (strcmp(argv[1], "kc_netl_close") == 0) return case_kc_netl_close();
    if (strcmp(argv[1], "kc_netl_path") == 0) return case_kc_netl_path();
    if (strcmp(argv[1], "kc_netl_update") == 0) return case_kc_netl_update();
    if (strcmp(argv[1], "kc_netl_list") == 0) return case_kc_netl_list();
    if (strcmp(argv[1], "kc_netl_delete") == 0) return case_kc_netl_delete();
    if (strcmp(argv[1], "kc_netl_set_pid") == 0) return case_kc_netl_set_pid();
    if (strcmp(argv[1], "kc_netl_exec") == 0) return case_kc_netl_exec();
    if (strcmp(argv[1], "kc_netl_exec_success") == 0) return case_kc_netl_exec_success();
    if (strcmp(argv[1], "kc_netl_stop") == 0) return case_kc_netl_stop();
    if (strcmp(argv[1], "kc_netl_serve") == 0) return case_kc_netl_serve();
    if (strcmp(argv[1], "kc_netl_serve_success") == 0) return case_kc_netl_serve_success();
    if (strcmp(argv[1], "kc_netl_multictx") == 0) return case_kc_netl_multictx();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
