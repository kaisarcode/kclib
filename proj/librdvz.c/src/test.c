/**
 * test.c - Named rendezvous peer contract tests.
 * Summary: Verifies resident peer identity, symmetric links and CLI parity.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "librdvz.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#ifndef KC_CLI_PATH
#define KC_CLI_PATH "rdvz"
#endif

#define RDVZ_TEST_PATH_MAX 1024

static char rdvz_test_dir[RDVZ_TEST_PATH_MAX];
static int rdvz_case_current = 0;
static int rdvz_case_total = 0;

typedef struct {
    unsigned count;
    char first[128];
    char last[128];
} rdvz_test_list_t;

typedef struct {
    atomic_int count;
    char other[128];
} rdvz_test_seen_t;

/**
 * Report one canonical contract test result.
 * @param failed Nonzero when assertions failed.
 * @param name Public function or CLI behavior.
 * @param description Behavior verified by this case.
 * @return None.
 */
static void rdvz_test_result(int failed, const char *name,
    const char *description)
{
    printf("[%d/%d] [%s] %s: %s\n", rdvz_case_current,
        rdvz_case_total, failed ? "FAIL" : "PASS", name, description);
}

/**
 * Check a contract expectation and report a failed condition.
 * @param description Assertion description.
 * @param condition Nonzero when successful.
 * @return Zero for success, one for failure.
 */
static int rdvz_test_expect(const char *description, int condition)
{
    if (!condition) printf("[FAIL] %s\n", description);
    return condition ? 0 : 1;
}

/**
 * Collect one visited identifier.
 * @param id Visited peer ID.
 * @param userdata Collector state.
 * @return None.
 */
static void rdvz_test_collect(const char *id, void *userdata)
{
    rdvz_test_list_t *list = (rdvz_test_list_t *)userdata;

    if (list->count == 0U)
        (void)snprintf(list->first, sizeof(list->first), "%s", id);
    (void)snprintf(list->last, sizeof(list->last), "%s", id);
    list->count++;
}

/**
 * Record the other peer's identifier from an operation or local notification.
 * @param other Other peer ID.
 * @param userdata Callback observation state.
 * @return None.
 */
static void rdvz_test_handler(const char *other, void *userdata)
{
    rdvz_test_seen_t *seen = (rdvz_test_seen_t *)userdata;

    (void)snprintf(seen->other, sizeof(seen->other), "%s", other);
    (void)atomic_fetch_add(&seen->count, 1);
}

#ifndef __EMSCRIPTEN__
/**
 * Sleep briefly while checking asynchronous callback delivery.
 * @return None.
 */
static void rdvz_test_sleep(void)
{
#ifdef _WIN32
    Sleep(25);
#else
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = 25000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Wait for one callback to observe a named peer.
 * @param seen Callback observation state.
 * @param expected Expected other peer ID.
 * @return Nonzero when the callback was delivered.
 */
static int rdvz_test_wait(rdvz_test_seen_t *seen, const char *expected)
{
    int i;

    for (i = 0; i < 80; i++) {
        if (atomic_load(&seen->count) > 0)
            return strcmp(seen->other, expected) == 0;
        rdvz_test_sleep();
    }
    return 0;
}
#endif

/**
 * Build the isolated per-run runtime directory.
 * @return Zero on success.
 */
static int rdvz_test_setup(void)
{
#ifdef _WIN32
    char temp[MAX_PATH];
    DWORD size = GetTempPathA((DWORD)sizeof(temp), temp);
    if (!size || size >= (DWORD)sizeof(temp)) return 1;
    if ((size_t)snprintf(rdvz_test_dir, sizeof(rdvz_test_dir),
            "%skc-rdvz-test-%lu", temp,
            (unsigned long)GetCurrentProcessId()) >= sizeof(rdvz_test_dir))
        return 1;
    return _putenv_s("KC_RDVZ_DIR", rdvz_test_dir) != 0;
#else
    if ((size_t)snprintf(rdvz_test_dir, sizeof(rdvz_test_dir),
            "/tmp/kc-rdvz-test-%lu", (unsigned long)getpid()) >=
            sizeof(rdvz_test_dir)) return 1;
    return setenv("KC_RDVZ_DIR", rdvz_test_dir, 1) != 0;
#endif
}

/**
 * Construct one path inside the isolated runtime directory.
 * @param out Destination buffer.
 * @param cap Destination capacity.
 * @param filename Leaf filename.
 * @return Zero on success.
 */
static int rdvz_test_path(char *out, size_t cap, const char *filename)
{
#ifdef _WIN32
    return (size_t)snprintf(out, cap, "%s\\%s",
        rdvz_test_dir, filename) < cap ? 0 : 1;
#else
    return (size_t)snprintf(out, cap, "%s/%s",
        rdvz_test_dir, filename) < cap ? 0 : 1;
#endif
}

/**
 * Remove all state used by the contract tests.
 * @return None.
 */
static void rdvz_test_cleanup(void)
{
    const char *names[] = {
        "state.bin", "state.tmp", ".lock", "alpha.events",
        "beta.events", "gamma.events", "callback-a.events",
        "callback-b.events", "command-a.events", "command-b.events",
        "cli-a.events", "cli-b.events", "connected.txt",
        "rdvz-cli-output.txt", "rdvz-cli-event.txt",
        "rdvz-cli-action.txt"
    };
    size_t i;
    char path[RDVZ_TEST_PATH_MAX];

    for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
        if (!rdvz_test_path(path, sizeof(path), names[i]))
            (void)remove(path);
    }
#ifdef _WIN32
    (void)RemoveDirectoryA(rdvz_test_dir);
    (void)_putenv_s("KC_RDVZ_DIR", "");
#else
    (void)rmdir(rdvz_test_dir);
    (void)unsetenv("KC_RDVZ_DIR");
#endif
}

/**
 * Verify named create, open, enumeration and independent handle close.
 * @return Zero on success.
 */
static int case_kc_rdvz_create(void)
{
    kc_rdvz_peer_t *created = NULL, *opened = NULL, *beta = NULL;
    rdvz_test_list_t list = {0};
    int fail = 0;

    fail += rdvz_test_expect("create validates output",
        kc_rdvz_create(NULL, "alpha", NULL) == KC_RDVZ_EINVAL);
    fail += rdvz_test_expect("create named peer",
        kc_rdvz_create(&created, "alpha", NULL) == KC_RDVZ_OK);
    fail += rdvz_test_expect("opened handle has stable name",
        created && strcmp(kc_rdvz_peer_id(created), "alpha") == 0);
    fail += rdvz_test_expect("open same resident peer",
        kc_rdvz_open(&opened, "alpha", NULL, NULL) == KC_RDVZ_OK);
    kc_rdvz_close(created);
    kc_rdvz_close(opened);
    opened = NULL;
    fail += rdvz_test_expect("peer remains after handle closes",
        kc_rdvz_open(&opened, "alpha", NULL, NULL) == KC_RDVZ_OK);
    kc_rdvz_close(opened);
    fail += rdvz_test_expect("second named peer",
        kc_rdvz_create(&beta, "beta", NULL) == KC_RDVZ_OK);
    kc_rdvz_close(beta);
    fail += rdvz_test_expect("list named peers",
        kc_rdvz_list(rdvz_test_collect, &list) == KC_RDVZ_OK &&
        list.count == 2U &&
        strcmp(list.first, "alpha") == 0 &&
        strcmp(list.last, "beta") == 0);
    fail += rdvz_test_expect("delete alpha",
        kc_rdvz_disconnect("alpha") == KC_RDVZ_OK);
    fail += rdvz_test_expect("delete beta",
        kc_rdvz_disconnect("beta") == KC_RDVZ_OK);
    rdvz_test_result(fail, "kc_rdvz_create",
        "named peers survive temporary handles and can be listed");
    return fail != 0;
}

/**
 * Verify that direct connections are undirected and never duplicate.
 * @return Zero on success.
 */
static int case_kc_rdvz_connect(void)
{
    kc_rdvz_peer_t *a = NULL, *b = NULL, *c = NULL;
    rdvz_test_list_t a_links = {0}, b_links = {0}, c_links = {0};
    int fail = 0;

    fail += rdvz_test_expect("create alpha",
        kc_rdvz_create(&a, "alpha", NULL) == KC_RDVZ_OK);
    fail += rdvz_test_expect("create beta",
        kc_rdvz_create(&b, "beta", NULL) == KC_RDVZ_OK);
    fail += rdvz_test_expect("create independent gamma",
        kc_rdvz_create(&c, "gamma", NULL) == KC_RDVZ_OK);
    if (a && b && c) {
        fail += rdvz_test_expect("link alpha and beta",
            kc_rdvz_connect(a, "beta", NULL, NULL) == KC_RDVZ_OK);
        fail += rdvz_test_expect("reverse link is the same link",
            kc_rdvz_connect(b, "alpha", NULL, NULL) == KC_RDVZ_EEXIST);
        fail += rdvz_test_expect("link gamma through beta",
            kc_rdvz_connect(c, "beta", NULL, NULL) == KC_RDVZ_OK);
        fail += rdvz_test_expect("reject self-link",
            kc_rdvz_connect(a, "alpha", NULL, NULL) == KC_RDVZ_EINVAL);
        fail += rdvz_test_expect("no implicit peer creation",
            kc_rdvz_connect(a, "unknown", NULL, NULL) == KC_RDVZ_ENOENT);
        fail += rdvz_test_expect("alpha has only beta",
            kc_rdvz_peer_list(a, rdvz_test_collect, &a_links)
                == KC_RDVZ_OK && a_links.count == 1U &&
                strcmp(a_links.first, "beta") == 0);
        fail += rdvz_test_expect("beta has two neighbors",
            kc_rdvz_peer_list(b, rdvz_test_collect, &b_links)
                == KC_RDVZ_OK && b_links.count == 2U);
        fail += rdvz_test_expect("gamma has only beta; no transitivity",
            kc_rdvz_peer_list(c, rdvz_test_collect, &c_links)
                == KC_RDVZ_OK && c_links.count == 1U &&
                strcmp(c_links.first, "beta") == 0);
    }
    kc_rdvz_close(a);
    kc_rdvz_close(b);
    kc_rdvz_close(c);
    fail += rdvz_test_expect("delete alpha",
        kc_rdvz_disconnect("alpha") == KC_RDVZ_OK);
    fail += rdvz_test_expect("delete beta",
        kc_rdvz_disconnect("beta") == KC_RDVZ_OK);
    fail += rdvz_test_expect("delete gamma",
        kc_rdvz_disconnect("gamma") == KC_RDVZ_OK);
    rdvz_test_result(fail, "kc_rdvz_connect",
        "symmetric unique links between existing peers");
    return fail != 0;
}

/**
 * Verify either endpoint can remove one relationship without deleting peers.
 * @return Zero on success.
 */
static int case_kc_rdvz_peer_disconnect(void)
{
    kc_rdvz_peer_t *a = NULL, *b = NULL, *c = NULL;
    rdvz_test_list_t links = {0};
    int fail = 0;

    fail += rdvz_test_expect("create alpha",
        kc_rdvz_create(&a, "alpha", NULL) == KC_RDVZ_OK);
    fail += rdvz_test_expect("create beta",
        kc_rdvz_create(&b, "beta", NULL) == KC_RDVZ_OK);
    fail += rdvz_test_expect("create gamma",
        kc_rdvz_create(&c, "gamma", NULL) == KC_RDVZ_OK);
    if (a && b && c) {
        fail += rdvz_test_expect("connect alpha-beta",
            kc_rdvz_connect(a, "beta", NULL, NULL) == KC_RDVZ_OK);
        fail += rdvz_test_expect("connect beta-gamma",
            kc_rdvz_connect(b, "gamma", NULL, NULL) == KC_RDVZ_OK);
        fail += rdvz_test_expect("remove alpha-beta from beta side",
            kc_rdvz_peer_disconnect(b, "alpha", NULL, NULL) == KC_RDVZ_OK);
        fail += rdvz_test_expect("alpha is now isolated",
            kc_rdvz_peer_list(a, rdvz_test_collect, &links)
                == KC_RDVZ_OK && links.count == 0U);
        memset(&links, 0, sizeof(links));
        fail += rdvz_test_expect("gamma-beta remains",
            kc_rdvz_peer_list(c, rdvz_test_collect, &links)
                == KC_RDVZ_OK && links.count == 1U &&
                strcmp(links.first, "beta") == 0);
        fail += rdvz_test_expect("reverse disconnect finds no duplicate",
            kc_rdvz_peer_disconnect(a, "beta", NULL, NULL) == KC_RDVZ_ENOENT);
    }
    kc_rdvz_close(a);
    kc_rdvz_close(b);
    kc_rdvz_close(c);
    fail += rdvz_test_expect("delete alpha",
        kc_rdvz_disconnect("alpha") == KC_RDVZ_OK);
    fail += rdvz_test_expect("delete beta",
        kc_rdvz_disconnect("beta") == KC_RDVZ_OK);
    fail += rdvz_test_expect("delete gamma",
        kc_rdvz_disconnect("gamma") == KC_RDVZ_OK);
    rdvz_test_result(fail, "kc_rdvz_peer_disconnect",
        "removes only one direct link from either endpoint");
    return fail != 0;
}

/**
 * Verify deleting one peer removes its incident edges, not other peers.
 * @return Zero on success.
 */
static int case_kc_rdvz_disconnect(void)
{
    kc_rdvz_peer_t *alpha = NULL, *beta = NULL, *gamma = NULL;
    rdvz_test_list_t links = {0};
    int fail = 0;

    fail += rdvz_test_expect("create alpha",
        kc_rdvz_create(&alpha, "alpha", NULL) == KC_RDVZ_OK);
    fail += rdvz_test_expect("create beta",
        kc_rdvz_create(&beta, "beta", NULL) == KC_RDVZ_OK);
    fail += rdvz_test_expect("create gamma",
        kc_rdvz_create(&gamma, "gamma", NULL) == KC_RDVZ_OK);
    if (alpha && beta && gamma)
        fail += rdvz_test_expect("connect beta-gamma",
            kc_rdvz_connect(beta, "gamma", NULL, NULL) == KC_RDVZ_OK);
    fail += rdvz_test_expect("delete beta",
        kc_rdvz_disconnect("beta") == KC_RDVZ_OK);
    {
        kc_rdvz_peer_t *missing = NULL;
        fail += rdvz_test_expect("deleted peer cannot reopen",
            kc_rdvz_open(&missing, "beta", NULL, NULL) == KC_RDVZ_ENOENT);
        kc_rdvz_close(missing);
    }
    fail += rdvz_test_expect("stale handle cannot connect",
        beta && kc_rdvz_connect(beta, "alpha", NULL, NULL)
            == KC_RDVZ_ENOENT);
    fail += rdvz_test_expect("gamma loses deleted neighbor",
        gamma && kc_rdvz_peer_list(gamma, rdvz_test_collect, &links)
            == KC_RDVZ_OK && links.count == 0U);
    kc_rdvz_close(alpha);
    kc_rdvz_close(beta);
    kc_rdvz_close(gamma);
    fail += rdvz_test_expect("delete alpha",
        kc_rdvz_disconnect("alpha") == KC_RDVZ_OK);
    fail += rdvz_test_expect("delete gamma",
        kc_rdvz_disconnect("gamma") == KC_RDVZ_OK);
    rdvz_test_result(fail, "kc_rdvz_disconnect",
        "deletes one peer with its edges and invalidates old handles");
    return fail != 0;
}

/**
 * Verify creation callbacks receive the other peer's ID as a plain string.
 * @return Zero on success.
 */
static int case_kc_rdvz_callbacks(void)
{
    kc_rdvz_peer_t *a = NULL, *b = NULL;
    kc_rdvz_options_t options;
    rdvz_test_seen_t incoming, connected, disconnected;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    memset(&incoming, 0, sizeof(incoming));
    memset(&connected, 0, sizeof(connected));
    memset(&disconnected, 0, sizeof(disconnected));
    atomic_init(&incoming.count, 0);
    atomic_init(&connected.count, 0);
    atomic_init(&disconnected.count, 0);
    options.on_connect = rdvz_test_handler;
    options.userdata = &incoming;
    fail += rdvz_test_expect("create callback peer",
        kc_rdvz_create(&a, "callback-a", &options) == KC_RDVZ_OK);
    fail += rdvz_test_expect("create independent peer",
        kc_rdvz_create(&b, "callback-b", NULL) == KC_RDVZ_OK);
    if (a && b) {
        fail += rdvz_test_expect("connect from other side",
            kc_rdvz_connect(b, "callback-a",
                rdvz_test_handler, &connected) == KC_RDVZ_OK);
        fail += rdvz_test_expect("initiator receives literal other ID",
            atomic_load(&connected.count) == 1 &&
            strcmp(connected.other, "callback-a") == 0);
#ifdef __EMSCRIPTEN__
        fail += rdvz_test_expect("created peer receives literal remote ID",
            atomic_load(&incoming.count) == 1 &&
            strcmp(incoming.other, "callback-b") == 0);
#else
        fail += rdvz_test_expect("created peer observes remote ID",
            rdvz_test_wait(&incoming, "callback-b"));
#endif
        fail += rdvz_test_expect("disconnect from either endpoint",
            kc_rdvz_peer_disconnect(a, "callback-b",
                rdvz_test_handler, &disconnected) == KC_RDVZ_OK);
        fail += rdvz_test_expect("disconnect callback receives other ID",
            atomic_load(&disconnected.count) == 1 &&
            strcmp(disconnected.other, "callback-b") == 0);
    }
    kc_rdvz_close(a);
    kc_rdvz_close(b);
    fail += rdvz_test_expect("delete callback-a",
        kc_rdvz_disconnect("callback-a") == KC_RDVZ_OK);
    fail += rdvz_test_expect("delete callback-b",
        kc_rdvz_disconnect("callback-b") == KC_RDVZ_OK);
    rdvz_test_result(fail, "kc_rdvz_callbacks",
        "creation and operation callbacks receive only the other ID");
    return fail != 0;
}

#ifndef __EMSCRIPTEN__
/**
 * Check whether a text file contains an expected peer identifier.
 * @param filename Filename within test directory.
 * @param expected Expected substring.
 * @return Nonzero when found.
 */
static int rdvz_test_file_contains(const char *filename,
    const char *expected)
{
    char path[RDVZ_TEST_PATH_MAX], buffer[256];
    FILE *file;

    if (rdvz_test_path(path, sizeof(path), filename)) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    if (!fgets(buffer, sizeof(buffer), file)) {
        (void)fclose(file);
        return 0;
    }
    (void)fclose(file);
    return strstr(buffer, expected) != NULL;
}

/**
 * Wait for an asynchronous configured command to write its other-peer ID.
 * @param filename Test output filename.
 * @param expected Expected peer identifier.
 * @return Nonzero when the ID appears.
 */
static int rdvz_test_wait_file(const char *filename,
    const char *expected)
{
    int i;

    for (i = 0; i < 80; i++) {
        if (rdvz_test_file_contains(filename, expected)) return 1;
        rdvz_test_sleep();
    }
    return 0;
}
#endif

/**
 * Verify a resident command runs after the creating handle closes.
 * @return Zero on success.
 */
static int case_kc_rdvz_command(void)
{
#ifdef __EMSCRIPTEN__
    rdvz_test_result(0, "kc_rdvz_command",
        "native resident commands are not required in WASM");
    return 0;
#else
    kc_rdvz_peer_t *a = NULL, *b = NULL;
    kc_rdvz_options_t options;
    char output[RDVZ_TEST_PATH_MAX], command[RDVZ_TEST_PATH_MAX + 32];
    int fail = 0;

    fail += rdvz_test_expect("output path",
        rdvz_test_path(output, sizeof(output), "connected.txt") == 0);
    memset(&options, 0, sizeof(options));
    (void)snprintf(command, sizeof(command), "echo > \"%s\"", output);
    options.cmd = command;
    fail += rdvz_test_expect("register persistent connect command",
        kc_rdvz_create(&a, "command-a", &options) == KC_RDVZ_OK);
    kc_rdvz_close(a);
    a = NULL;
    fail += rdvz_test_expect("create other peer",
        kc_rdvz_create(&b, "command-b", NULL) == KC_RDVZ_OK);
    if (b) {
        fail += rdvz_test_expect("connect without creator handle",
            kc_rdvz_connect(b, "command-a", NULL, NULL) == KC_RDVZ_OK);
        fail += rdvz_test_expect("command receives command-b ID",
            rdvz_test_wait_file("connected.txt", "command-b"));
    }
    kc_rdvz_close(b);
    fail += rdvz_test_expect("remove command-a",
        kc_rdvz_disconnect("command-a") == KC_RDVZ_OK);
    fail += rdvz_test_expect("remove command-b",
        kc_rdvz_disconnect("command-b") == KC_RDVZ_OK);
    rdvz_test_result(fail, "kc_rdvz_command",
        "persisted commands run with other peer IDs after local close");
    return fail != 0;
#endif
}

/**
 * Verify stable errors and the generated build version.
 * @return Zero on success.
 */
static int case_kc_rdvz_metadata(void)
{
    int fail = 0;

    fail += rdvz_test_expect("build version is nonzero",
        kc_rdvz_version() != 0U);
    fail += rdvz_test_expect("known success description",
        strcmp(kc_rdvz_strerror(KC_RDVZ_OK), "success") == 0);
    fail += rdvz_test_expect("known missing peer description",
        strstr(kc_rdvz_strerror(KC_RDVZ_ENOENT), "not found") != NULL);
    fail += rdvz_test_expect("unknown status description",
        strcmp(kc_rdvz_strerror(-999), "unknown error") == 0);
    rdvz_test_result(fail, "kc_rdvz_metadata",
        "version and public error messages are available");
    return fail != 0;
}

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
#define KC_LINE_END "\r\n"
#else
#define KC_LINE_END "\n"
#endif

/**
 * Run one native CLI command and discard its output.
 * @param args Command arguments.
 * @return Process exit status from system().
 */
static int rdvz_cli_run(const char *args)
{
    char line[RDVZ_TEST_PATH_MAX * 4];

#ifdef _WIN32
    (void)snprintf(line, sizeof(line),
        "\"\"%s\" %s > NUL 2>&1\"", KC_CLI_PATH, args ? args : "");
#else
    (void)snprintf(line, sizeof(line),
        "\"%s\" %s > /dev/null 2>&1", KC_CLI_PATH, args ? args : "");
#endif
    return system(line);
}

/**
 * Run the native CLI and compare exact stdout.
 * @param args CLI arguments.
 * @param expected Expected stdout.
 * @return Nonzero when stdout matches.
 */
static int rdvz_cli_equals(const char *args, const char *expected)
{
    char line[RDVZ_TEST_PATH_MAX * 4];
    char path[RDVZ_TEST_PATH_MAX], text[2048];
    FILE *file;
    size_t count;

    if (rdvz_test_path(path, sizeof(path), "rdvz-cli-output.txt")) return 0;
#ifdef _WIN32
    (void)snprintf(line, sizeof(line),
        "\"\"%s\" %s > \"%s\" 2>NUL\"",
        KC_CLI_PATH, args, path);
#else
    (void)snprintf(line, sizeof(line),
        "\"%s\" %s > \"%s\" 2>/dev/null",
        KC_CLI_PATH, args, path);
#endif
    if (system(line) != 0) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    count = fread(text, 1U, sizeof(text) - 1U, file);
    (void)fclose(file);
    text[count] = '\0';
    return strcmp(text, expected) == 0;
}

/**
 * Verify help and version are functional native CLI commands.
 * @return Zero on success.
 */
static int case_rdvz_cli_help(void)
{
    const char *expected =
        "Usage: rdvz <id> [command...]" KC_LINE_END
        KC_LINE_END
        "Commands:" KC_LINE_END
        "  rdvz <id> <cmd>              Create or update a peer" KC_LINE_END
        "  rdvz [id1] -l                List peers or direct links" KC_LINE_END
        "  rdvz <id1> -c <id2> [cmd]    Connect two peers" KC_LINE_END
        "  rdvz <id1> -d [id2] [cmd]    Disconnect a link or remove a peer" KC_LINE_END
        KC_LINE_END
        "Options:" KC_LINE_END
        "  -l, --list        List peers or direct links" KC_LINE_END
        "  -c, --connect     Connect two peers" KC_LINE_END
        "  -d, --disconnect  Disconnect a link or remove a peer" KC_LINE_END
        "  -h, --help        Show this help" KC_LINE_END
        "  -v, --version     Show version" KC_LINE_END;
    int fail = 0;

    fail += rdvz_test_expect("short help",
        rdvz_cli_equals("-h", expected));
    fail += rdvz_test_expect("long help",
        rdvz_cli_equals("--help", expected));
    fail += rdvz_test_expect("short version",
        rdvz_cli_run("-v") == 0);
    fail += rdvz_test_expect("long version",
        rdvz_cli_run("--version") == 0);
    rdvz_test_result(fail, "rdvz_cli_help",
        "help and version follow the kclib convention");
    return fail != 0;
}

/**
 * Verify peer creation and listing across independent CLI processes.
 * @return Zero on success.
 */
static int case_rdvz_cli_create(void)
{
    char command[RDVZ_TEST_PATH_MAX * 2];
    char path[RDVZ_TEST_PATH_MAX];
    int fail = 0;

    fail += rdvz_test_expect("list starts empty",
        rdvz_cli_equals("-l", ""));
    fail += rdvz_test_expect("create cli-a",
        rdvz_test_path(path, sizeof(path), "rdvz-cli-event.txt") == 0);
    (void)snprintf(command, sizeof(command),
        "cli-a \"echo > '%s'\"", path);
    fail += rdvz_test_expect("create command is persisted",
        rdvz_cli_run(command) == 0);
    fail += rdvz_test_expect("create cli-b",
        rdvz_cli_run("cli-b \"echo\"") == 0);
    fail += rdvz_test_expect("list sees both CLI peers",
        rdvz_cli_equals("-l",
            "cli-a" KC_LINE_END "cli-b" KC_LINE_END));
    fail += rdvz_test_expect("remove cli-a",
        rdvz_cli_run("cli-a -d") == 0);
    fail += rdvz_test_expect("remove cli-b",
        rdvz_cli_run("cli-b -d") == 0);
    rdvz_test_result(fail, "rdvz_cli_create",
        "CLI-created peers survive separate invocations");
    return fail != 0;
}

/**
 * Verify the CLI link and unlink operations are symmetric.
 * @return Zero on success.
 */
static int case_rdvz_cli_links(void)
{
    char command[RDVZ_TEST_PATH_MAX * 2];
    char path[RDVZ_TEST_PATH_MAX], action[RDVZ_TEST_PATH_MAX];
    int fail = 0;

    fail += rdvz_test_expect("resident event output path",
        rdvz_test_path(path, sizeof(path), "rdvz-cli-event.txt") == 0);
    fail += rdvz_test_expect("operation output path",
        rdvz_test_path(action, sizeof(action), "rdvz-cli-action.txt") == 0);
    (void)snprintf(command, sizeof(command),
        "cli-a \"echo > '%s'\"", path);
    fail += rdvz_test_expect("create cli-a with resident command",
        rdvz_cli_run(command) == 0);
    fail += rdvz_test_expect("create cli-b",
        rdvz_cli_run("cli-b \"echo\"") == 0);
    (void)snprintf(command, sizeof(command),
        "cli-b -c cli-a \"echo > '%s'\"", action);
    fail += rdvz_test_expect("connect cli-b to cli-a",
        rdvz_cli_run(command) == 0);
    fail += rdvz_test_expect("cli-a sees cli-b",
        rdvz_cli_equals("cli-a -l", "cli-b" KC_LINE_END));
    fail += rdvz_test_expect("cli-b sees cli-a",
        rdvz_cli_equals("cli-b -l", "cli-a" KC_LINE_END));
    fail += rdvz_test_expect("persistent command receives cli-b",
        rdvz_test_wait_file("rdvz-cli-event.txt", "cli-b"));
    fail += rdvz_test_expect("connect operation command receives cli-a",
        rdvz_test_wait_file("rdvz-cli-action.txt", "cli-a"));
    (void)remove(action);
    (void)snprintf(command, sizeof(command),
        "cli-a -d cli-b \"echo > '%s'\"", action);
    fail += rdvz_test_expect("remove from reverse endpoint",
        rdvz_cli_run(command) == 0);
    fail += rdvz_test_expect("disconnect command receives cli-b",
        rdvz_test_wait_file("rdvz-cli-action.txt", "cli-b"));
    fail += rdvz_test_expect("both peers remain without links",
        rdvz_cli_equals("cli-a -l", "") &&
        rdvz_cli_equals("cli-b -l", ""));
    fail += rdvz_test_expect("delete cli-a",
        rdvz_cli_run("cli-a -d") == 0);
    fail += rdvz_test_expect("delete cli-b",
        rdvz_cli_run("cli-b -d") == 0);
    rdvz_test_result(fail, "rdvz_cli_links",
        "CLI connects and disconnects the same symmetric relation");
    return fail != 0;
}
/**
 * Verify deleting a peer affects neither other identities nor CLI usability.
 * @return Zero on success.
 */
static int case_rdvz_cli_disconnect(void)
{
    int fail = 0;

    fail += rdvz_test_expect("create cli-a",
        rdvz_cli_run("cli-a \"echo\"") == 0);
    fail += rdvz_test_expect("create cli-b",
        rdvz_cli_run("cli-b \"echo\"") == 0);
    fail += rdvz_test_expect("delete cli-a",
        rdvz_cli_run("cli-a -d") == 0);
    fail += rdvz_test_expect("cli-b remains",
        rdvz_cli_equals("cli-b -l", ""));
    fail += rdvz_test_expect("delete cli-b",
        rdvz_cli_run("cli-b --disconnect") == 0);
    fail += rdvz_test_expect("missing CLI peer fails",
        rdvz_cli_run("cli-a -l") != 0);
    fail += rdvz_test_expect("unknown command fails",
        rdvz_cli_run("cli-a --unknown") != 0);
    rdvz_test_result(fail, "rdvz_cli_disconnect",
        "deleting a peer removes only that named identity");
    return fail != 0;
}
#endif

/**
 * Run one contract case and advance the shared progress counter.
 * @param failures Accumulated failing case count.
 * @param test One zero-on-success contract case.
 * @return None.
 */
static void rdvz_run_case(int *failures, int (*test)(void))
{
    rdvz_case_current++;
    *failures += test();
}

/**
 * Run the entire native or WASM contract.
 * @return Zero when all cases passed.
 */
static int rdvz_test_all(void)
{
    int failures = 0;

#ifdef __EMSCRIPTEN__
    rdvz_case_total = 7;
#else
    rdvz_case_total = 11;
#endif
    rdvz_case_current = 0;
    rdvz_run_case(&failures, case_kc_rdvz_create);
    rdvz_run_case(&failures, case_kc_rdvz_connect);
    rdvz_run_case(&failures, case_kc_rdvz_peer_disconnect);
    rdvz_run_case(&failures, case_kc_rdvz_disconnect);
    rdvz_run_case(&failures, case_kc_rdvz_callbacks);
    rdvz_run_case(&failures, case_kc_rdvz_command);
    rdvz_run_case(&failures, case_kc_rdvz_metadata);
#ifndef __EMSCRIPTEN__
    rdvz_run_case(&failures, case_rdvz_cli_help);
    rdvz_run_case(&failures, case_rdvz_cli_create);
    rdvz_run_case(&failures, case_rdvz_cli_links);
    rdvz_run_case(&failures, case_rdvz_cli_disconnect);
#endif
    printf("%d passed, %d failed\n",
        rdvz_case_total - failures, failures);
    return failures ? 1 : 0;
}

/**
 * Execute all tests or one named public contract case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Zero when successful.
 */
int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        int (*run)(void);
    } cases[] = {
        {"kc_rdvz_create", case_kc_rdvz_create},
        {"kc_rdvz_connect", case_kc_rdvz_connect},
        {"kc_rdvz_peer_disconnect", case_kc_rdvz_peer_disconnect},
        {"kc_rdvz_disconnect", case_kc_rdvz_disconnect},
        {"kc_rdvz_callbacks", case_kc_rdvz_callbacks},
        {"kc_rdvz_command", case_kc_rdvz_command},
        {"kc_rdvz_metadata", case_kc_rdvz_metadata}
    };
    size_t i;
    int rc = 2;

    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument\n");
        return 2;
    }
    if (rdvz_test_setup() != 0) return 2;
    if (strcmp(argv[1], "all") == 0) rc = rdvz_test_all();
    else {
        for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
            if (strcmp(argv[1], cases[i].name) == 0) {
                rdvz_case_current = 1;
                rdvz_case_total = 1;
                rc = cases[i].run();
                break;
            }
        }
        if (rc == 2) fprintf(stderr, "unknown test case: %s\n", argv[1]);
    }
    rdvz_test_cleanup();
    return rc;
}
