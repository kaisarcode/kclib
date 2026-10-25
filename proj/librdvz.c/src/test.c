/**
 * test.c - Rendezvous peer relationship contract tests.
 * Summary: Verifies independent peers, direct links and delegated events.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "librdvz.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#ifndef KC_CLI_PATH
#define KC_CLI_PATH "rdvz"
#endif
#endif

typedef struct {
    kc_rdvz_t *rdvz;
    unsigned connected;
    unsigned disconnected;
    unsigned seen_connected;
    unsigned seen_disconnected;
    int unexpected;
} rdvz_events_t;

typedef struct {
    size_t count;
    const kc_rdvz_peer_t *first;
    const kc_rdvz_peer_t *second;
    const kc_rdvz_peer_t *last;
} rdvz_list_t;

/**
 * Adds one received peer to an inspection summary.
 * @param peer Listed peer.
 * @param userdata Summary to update.
 * @return None.
 */
static void rdvz_collect(const kc_rdvz_peer_t *peer, void *userdata) {
    rdvz_list_t *list = (rdvz_list_t *)userdata;
    list->count++;
    if (list->count == 1U) list->first = peer;
    if (list->count == 2U) list->second = peer;
    list->last = peer;
}

/**
 * Checks if a peer has the expected number of direct neighbors.
 * @param peer Peer being inspected.
 * @param expected Expected direct neighbor count.
 * @return Nonzero on success.
 */
static int rdvz_expect_links(const kc_rdvz_peer_t *peer, size_t expected) {
    rdvz_list_t list = {0};
    return kc_rdvz_peer_list(peer, rdvz_collect, &list) == KC_RDVZ_OK &&
        list.count == expected;
}

/**
 * Observes a new link and verifies notification-time lookup and mutation rules.
 * @param peer Newly created peer.
 * @param other Existing peer.
 * @param userdata Test event state.
 * @return None.
 */
static void rdvz_on_connect(const kc_rdvz_peer_t *peer,
    const kc_rdvz_peer_t *other, void *userdata)
{
    rdvz_events_t *events = (rdvz_events_t *)userdata;
    kc_rdvz_peer_t *reentrant = NULL;
    rdvz_list_t list = {0};

    events->connected++;
    if (!peer || !other || !kc_rdvz_peer_id(peer) ||
        !kc_rdvz_peer_id(other)) events->unexpected++;
    if (kc_rdvz_list(events->rdvz, rdvz_collect, &list) != KC_RDVZ_OK ||
        list.count < 2U)
        events->unexpected++;
    if (kc_rdvz_connect(events->rdvz, NULL, &reentrant) != KC_RDVZ_EBUSY ||
        reentrant != NULL) events->unexpected++;
    if (kc_rdvz_close(events->rdvz) != KC_RDVZ_EBUSY)
        events->unexpected++;
    events->seen_connected++;
}

/**
 * Observes removal of one direct link after the link was detached.
 * @param peer Disconnecting peer.
 * @param other Remaining peer.
 * @param userdata Test event state.
 * @return None.
 */
static void rdvz_on_disconnect(const kc_rdvz_peer_t *peer,
    const kc_rdvz_peer_t *other, void *userdata)
{
    rdvz_events_t *events = (rdvz_events_t *)userdata;
    if (!peer || !other || !kc_rdvz_peer_id(peer) ||
        !kc_rdvz_peer_id(other)) events->unexpected++;
    if (kc_rdvz_disconnect((kc_rdvz_peer_t *)other) != KC_RDVZ_EBUSY)
        events->unexpected++;
    events->disconnected++;
    events->seen_disconnected++;
}

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints one canonical kclib contract test result.
 * @param fail Nonzero when the case failed.
 * @param name Public API function or CLI case name.
 * @param detail Behavior asserted.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs one functional case with sequential progress tracking.
 * @param rc Aggregate failure counter.
 * @param fn Functional case to run.
 * @return None.
 */
static void run_case(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
}

/**
 * Reports one failed assertion with its intended behavior.
 * @param name Assertion description.
 * @param condition Nonzero when the assertion passes.
 * @return Zero on success and one on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Tests kc_rdvz_open across independent registries and invalid arguments.
 * @return Zero when all assertions pass.
 */
static int case_kc_rdvz_open(void) {
    kc_rdvz_t *first = NULL, *second = NULL;
    kc_rdvz_peer_t *a = NULL, *b = NULL;
    int fail = 0;

    fail += expect_true("open rejects absent output",
        kc_rdvz_open(NULL, NULL) == KC_RDVZ_EINVAL);
    fail += expect_true("open creates first registry",
        kc_rdvz_open(&first, NULL) == KC_RDVZ_OK && first != NULL);
    fail += expect_true("open creates independent registry",
        kc_rdvz_open(&second, NULL) == KC_RDVZ_OK && second != NULL);
    if (first && second) {
        fail += expect_true("first registry accepts an independent peer",
            kc_rdvz_connect(first, NULL, &a) == KC_RDVZ_OK);
        fail += expect_true("second registry accepts an independent peer",
            kc_rdvz_connect(second, NULL, &b) == KC_RDVZ_OK);
        if (a && b) {
            fail += expect_true("IDs are scoped to individual registries",
                strcmp(kc_rdvz_peer_id(a), kc_rdvz_peer_id(b)) == 0);
        }
    }
    fail += expect_true("first registry closes", kc_rdvz_close(first) == KC_RDVZ_OK);
    fail += expect_true("second registry closes", kc_rdvz_close(second) == KC_RDVZ_OK);

    case_result(fail, "kc_rdvz_open",
        "creates independent registries and rejects invalid output");
    return fail != 0;
}

/**
 * Tests kc_rdvz_connect, direct linking, validation and lookup growth.
 * @return Zero when all assertions pass.
 */
static int case_kc_rdvz_connect(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *a = NULL, *b = NULL, *c = NULL, *extra = NULL;
    int fail = 0;
    unsigned i;

    fail += expect_true("open registry",
        kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK);
    if (!rdvz) {
        case_result(fail, "kc_rdvz_connect",
            "creates independent and linked peers with strict ID validation");
        return 1;
    }
    fail += expect_true("connect rejects absent output",
        kc_rdvz_connect(rdvz, NULL, NULL) == KC_RDVZ_EINVAL);
    fail += expect_true("connect rejects zero ID",
        kc_rdvz_connect(rdvz, "0", &extra) == KC_RDVZ_EINVAL);
    fail += expect_true("connect rejects leading zero",
        kc_rdvz_connect(rdvz, "01", &extra) == KC_RDVZ_EINVAL);
    fail += expect_true("connect rejects invalid ID",
        kc_rdvz_connect(rdvz, "abc", &extra) == KC_RDVZ_EINVAL);
    fail += expect_true("connect rejects overflowing ID",
        kc_rdvz_connect(rdvz, "18446744073709551616", &extra) == KC_RDVZ_EINVAL);
    fail += expect_true("connect distinguishes missing peer",
        kc_rdvz_connect(rdvz, "100", &extra) == KC_RDVZ_ENOENT);
    fail += expect_true("failed connect clears output", extra == NULL);

    fail += expect_true("connect creates an independent peer",
        kc_rdvz_connect(rdvz, NULL, &a) == KC_RDVZ_OK);
    if (a) {
        fail += expect_true("connect links to an existing peer",
            kc_rdvz_connect(rdvz, kc_rdvz_peer_id(a), &b) == KC_RDVZ_OK);
    }
    if (b) {
        fail += expect_true("connect supports chains",
            kc_rdvz_connect(rdvz, kc_rdvz_peer_id(b), &c) == KC_RDVZ_OK);
    }
    if (a && b && c) {
        fail += expect_true("first endpoint has one link",
            rdvz_expect_links(a, 1U));
        fail += expect_true("middle peer has two links",
            rdvz_expect_links(b, 2U));
        fail += expect_true("last endpoint has one link",
            rdvz_expect_links(c, 1U));
        for (i = 0U; i < 1024U; i++) {
            if (kc_rdvz_connect(rdvz, kc_rdvz_peer_id(a), &extra) !=
                KC_RDVZ_OK) break;
        }
        fail += expect_true("connect preserves lookup through hash growth",
            i == 1024U && rdvz_expect_links(a, 1025U));
    }
    fail += expect_true("close frees created peers",
        kc_rdvz_close(rdvz) == KC_RDVZ_OK);

    case_result(fail, "kc_rdvz_connect",
        "creates fresh peers and symmetric direct links, rejects invalid IDs");
    return fail != 0;
}

/**
 * Tests kc_rdvz_peer_id uniqueness and lifetime across reconnects.
 * @return Zero when all assertions pass.
 */
static int case_kc_rdvz_peer_id(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *a = NULL, *b = NULL, *rejoined = NULL;
    char previous[32] = {0};
    int fail = 0;

    fail += expect_true("NULL peer has no ID", kc_rdvz_peer_id(NULL) == NULL);
    fail += expect_true("open registry", kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK);
    if (rdvz) {
        fail += expect_true("create peer with ID",
            kc_rdvz_connect(rdvz, NULL, &a) == KC_RDVZ_OK);
        if (a) {
            fail += expect_true("first peer has canonical ID",
                strcmp(kc_rdvz_peer_id(a), "1") == 0);
            snprintf(previous, sizeof(previous), "%s", kc_rdvz_peer_id(a));
            fail += expect_true("second peer gets an independent identity",
                kc_rdvz_connect(rdvz, kc_rdvz_peer_id(a), &b) == KC_RDVZ_OK);
        }
        if (a && b) {
            fail += expect_true("second peer has distinct ID",
                strcmp(kc_rdvz_peer_id(a), kc_rdvz_peer_id(b)) != 0);
            fail += expect_true("disconnect original peer",
                kc_rdvz_disconnect(a) == KC_RDVZ_OK);
            fail += expect_true("other peer retains its ID",
                strcmp(kc_rdvz_peer_id(b), "2") == 0);
            fail += expect_true("old ID cannot be reused as a target",
                kc_rdvz_connect(rdvz, previous, &rejoined) == KC_RDVZ_ENOENT);
            fail += expect_true("rejoining creates a fresh peer",
                kc_rdvz_connect(rdvz, kc_rdvz_peer_id(b), &rejoined) == KC_RDVZ_OK);
            if (rejoined) {
                fail += expect_true("rejoining peer gets a new ID",
                    strcmp(kc_rdvz_peer_id(rejoined), "3") == 0);
            }
        }
        fail += expect_true("close registry", kc_rdvz_close(rdvz) == KC_RDVZ_OK);
    }
    case_result(fail, "kc_rdvz_peer_id",
        "exposes registry-scoped unique IDs and fresh identity on rejoin");
    return fail != 0;
}

/**
 * Tests kc_rdvz_list and stable insertion-order enumeration.
 * @return Zero when all assertions pass.
 */
static int case_kc_rdvz_list(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *a = NULL, *b = NULL, *c = NULL;
    rdvz_list_t list = {0};
    int fail = 0;

    fail += expect_true("list rejects NULL registry",
        kc_rdvz_list(NULL, rdvz_collect, &list) == KC_RDVZ_EINVAL);
    fail += expect_true("open registry", kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK);
    if (rdvz) {
        fail += expect_true("list rejects NULL callback",
            kc_rdvz_list(rdvz, NULL, &list) == KC_RDVZ_EINVAL);
        fail += expect_true("list accepts an empty registry",
            kc_rdvz_list(rdvz, rdvz_collect, &list) == KC_RDVZ_OK &&
            list.count == 0U);
        fail += expect_true("create first peer",
            kc_rdvz_connect(rdvz, NULL, &a) == KC_RDVZ_OK);
        fail += expect_true("create second peer",
            kc_rdvz_connect(rdvz, NULL, &b) == KC_RDVZ_OK);
        fail += expect_true("create third peer",
            kc_rdvz_connect(rdvz, NULL, &c) == KC_RDVZ_OK);
        if (a && b && c) {
            memset(&list, 0, sizeof(list));
            fail += expect_true("list reports insertion order",
                kc_rdvz_list(rdvz, rdvz_collect, &list) == KC_RDVZ_OK &&
                list.count == 3U && list.first == a &&
                list.second == b && list.last == c);
            fail += expect_true("disconnect middle peer",
                kc_rdvz_disconnect(b) == KC_RDVZ_OK);
            memset(&list, 0, sizeof(list));
            fail += expect_true("list retains remaining peers in order",
                kc_rdvz_list(rdvz, rdvz_collect, &list) == KC_RDVZ_OK &&
                list.count == 2U && list.first == a && list.last == c);
        }
        fail += expect_true("close registry", kc_rdvz_close(rdvz) == KC_RDVZ_OK);
    }
    case_result(fail, "kc_rdvz_list",
        "enumerates live peers in insertion order without including removed peers");
    return fail != 0;
}

/**
 * Tests kc_rdvz_peer_list and nontransitive symmetric neighbors.
 * @return Zero when all assertions pass.
 */
static int case_kc_rdvz_peer_list(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *a = NULL, *b = NULL, *c = NULL;
    rdvz_list_t list = {0};
    int fail = 0;

    fail += expect_true("peer_list rejects NULL peer",
        kc_rdvz_peer_list(NULL, rdvz_collect, &list) == KC_RDVZ_EINVAL);
    fail += expect_true("open registry", kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK);
    if (rdvz) {
        fail += expect_true("create first peer",
            kc_rdvz_connect(rdvz, NULL, &a) == KC_RDVZ_OK);
        if (a) fail += expect_true("create second peer linked to first",
            kc_rdvz_connect(rdvz, kc_rdvz_peer_id(a), &b) == KC_RDVZ_OK);
        if (b) fail += expect_true("create third peer linked to second",
            kc_rdvz_connect(rdvz, kc_rdvz_peer_id(b), &c) == KC_RDVZ_OK);
        if (a && b && c) {
            fail += expect_true("peer_list rejects NULL visitor",
                kc_rdvz_peer_list(a, NULL, &list) == KC_RDVZ_EINVAL);
            fail += expect_true("only direct neighbors are included",
                rdvz_expect_links(a, 1U) && rdvz_expect_links(c, 1U));
            fail += expect_true("middle peer lists both direct neighbors",
                kc_rdvz_peer_list(b, rdvz_collect, &list) == KC_RDVZ_OK &&
                list.count == 2U && list.first == a && list.last == c);
            fail += expect_true("disconnect first endpoint",
                kc_rdvz_disconnect(a) == KC_RDVZ_OK);
            memset(&list, 0, sizeof(list));
            fail += expect_true("removing an endpoint updates neighbors",
                kc_rdvz_peer_list(b, rdvz_collect, &list) == KC_RDVZ_OK &&
                list.count == 1U && list.first == c);
        }
        fail += expect_true("close registry", kc_rdvz_close(rdvz) == KC_RDVZ_OK);
    }
    case_result(fail, "kc_rdvz_peer_list",
        "enumerates direct neighbors only and reflects link removals");
    return fail != 0;
}

/**
 * Tests kc_rdvz_disconnect and one callback per removed link.
 * @return Zero when all assertions pass.
 */
static int case_kc_rdvz_disconnect(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *a = NULL, *b = NULL, *c = NULL, *rejoined = NULL;
    kc_rdvz_options_t options = {0};
    rdvz_events_t events = {0};
    int fail = 0;

    fail += expect_true("disconnect rejects NULL peer",
        kc_rdvz_disconnect(NULL) == KC_RDVZ_EINVAL);
    options.on_connect = rdvz_on_connect;
    options.on_disconnect = rdvz_on_disconnect;
    options.userdata = &events;
    fail += expect_true("open registry with link callbacks",
        kc_rdvz_open(&rdvz, &options) == KC_RDVZ_OK);
    if (rdvz) {
        events.rdvz = rdvz;
        fail += expect_true("create first peer",
            kc_rdvz_connect(rdvz, NULL, &a) == KC_RDVZ_OK);
        if (a) fail += expect_true("link second peer",
            kc_rdvz_connect(rdvz, kc_rdvz_peer_id(a), &b) == KC_RDVZ_OK);
        if (b) fail += expect_true("link third peer",
            kc_rdvz_connect(rdvz, kc_rdvz_peer_id(b), &c) == KC_RDVZ_OK);
        if (a && b && c) {
            fail += expect_true("one connect notification per link",
                events.connected == 2U && events.unexpected == 0);
            fail += expect_true("disconnect middle peer",
                kc_rdvz_disconnect(b) == KC_RDVZ_OK);
            fail += expect_true("both links trigger disconnect notification",
                events.disconnected == 2U && events.unexpected == 0);
            fail += expect_true("remaining peers survive independently",
                rdvz_expect_links(a, 0U) && rdvz_expect_links(c, 0U));
            fail += expect_true("rejoin surviving peer with new identity",
                kc_rdvz_connect(rdvz, kc_rdvz_peer_id(c), &rejoined) ==
                KC_RDVZ_OK);
            if (rejoined) {
                fail += expect_true("rejoined peer has symmetric link",
                    rdvz_expect_links(rejoined, 1U) && rdvz_expect_links(c, 1U));
                fail += expect_true("new link gets a connect notification",
                    events.connected == 3U && events.unexpected == 0);
                fail += expect_true("disconnect rejoined peer",
                    kc_rdvz_disconnect(rejoined) == KC_RDVZ_OK);
            }
            fail += expect_true("disconnect callback counts new removal",
                events.disconnected == 3U && events.unexpected == 0);
        }
        fail += expect_true("close registry", kc_rdvz_close(rdvz) == KC_RDVZ_OK);
    }
    case_result(fail, "kc_rdvz_disconnect",
        "removes links without removing neighbors and delegates link events");
    return fail != 0;
}

/**
 * Tests kc_rdvz_close, cleanup and absence of implicit departure callbacks.
 * @return Zero when all assertions pass.
 */
static int case_kc_rdvz_close(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *a = NULL, *b = NULL;
    kc_rdvz_options_t options = {0};
    rdvz_events_t events = {0};
    int fail = 0;

    fail += expect_true("close accepts NULL", kc_rdvz_close(NULL) == KC_RDVZ_OK);
    options.on_connect = rdvz_on_connect;
    options.on_disconnect = rdvz_on_disconnect;
    options.userdata = &events;
    fail += expect_true("open registry with callbacks",
        kc_rdvz_open(&rdvz, &options) == KC_RDVZ_OK);
    if (rdvz) {
        events.rdvz = rdvz;
        fail += expect_true("create first peer",
            kc_rdvz_connect(rdvz, NULL, &a) == KC_RDVZ_OK);
        if (a) fail += expect_true("create linked peer",
            kc_rdvz_connect(rdvz, kc_rdvz_peer_id(a), &b) == KC_RDVZ_OK);
        fail += expect_true("link notifies on connect",
            events.connected == 1U && events.unexpected == 0);
        fail += expect_true("close frees all remaining peers and links",
            kc_rdvz_close(rdvz) == KC_RDVZ_OK);
        fail += expect_true("close does not emit disconnect events",
            events.disconnected == 0U);
    }
    case_result(fail, "kc_rdvz_close",
        "releases registry resources without synthetic disconnect notifications");
    return fail != 0;
}

/**
 * Tests kc_rdvz_strerror on all statuses and an unknown error.
 * @return Zero when all assertions pass.
 */
static int case_kc_rdvz_strerror(void) {
    int fail = 0;

    fail += expect_true("success has stable message",
        strcmp(kc_rdvz_strerror(KC_RDVZ_OK), "success") == 0);
    fail += expect_true("unknown peer has stable message",
        strcmp(kc_rdvz_strerror(KC_RDVZ_ENOENT), "peer not found") == 0);
    fail += expect_true("invalid argument has stable message",
        strcmp(kc_rdvz_strerror(KC_RDVZ_EINVAL), "invalid argument") == 0);
    fail += expect_true("memory failure has stable message",
        strcmp(kc_rdvz_strerror(KC_RDVZ_ENOMEM), "out of memory") == 0);
    fail += expect_true("busy has stable message",
        strcmp(kc_rdvz_strerror(KC_RDVZ_EBUSY), "registry busy") == 0);
    fail += expect_true("ID limit has stable message",
        strcmp(kc_rdvz_strerror(KC_RDVZ_ELIMIT), "peer ID limit reached") == 0);
    fail += expect_true("unknown error has fallback message",
        strcmp(kc_rdvz_strerror(-999), "unknown error") == 0);

    case_result(fail, "kc_rdvz_strerror",
        "returns stable descriptions for known and unknown statuses");
    return fail != 0;
}

/**
 * Tests kc_rdvz_version generated-build metadata.
 * @return Zero when all assertions pass.
 */
static int case_kc_rdvz_version(void) {
    int fail = expect_true("version is nonzero", kc_rdvz_version() != 0U);

    case_result(fail, "kc_rdvz_version",
        "returns a nonzero generated build version");
    return fail != 0;
}

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
#define KC_LINE_END "\r\n"
#else
#define KC_LINE_END "\n"
#endif

/**
 * Executes one CLI command, discarding output.
 * @param args Optional CLI arguments.
 * @return Process status from system.
 */
static int rdvz_cli_run(const char *args) {
    char command[4096];

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > NUL 2>&1\"",
        KC_CLI_PATH, args ? args : "");
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        KC_CLI_PATH, args ? args : "");
#endif
    return system(command);
}

/**
 * Runs a CLI command and checks its exact stdout.
 * @param args Required CLI command arguments.
 * @param expected Expected output including line endings.
 * @return Nonzero when the command succeeds and stdout matches.
 */
static int rdvz_cli_output_equals(const char *args, const char *expected) {
    const char *output_path = "rdvz-cli-output.txt";
    char command[4096];
    char output[256];
    FILE *file;
    size_t size;

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > %s 2>NUL\"",
        KC_CLI_PATH, args, output_path);
#else
    snprintf(command, sizeof(command), "\"%s\" %s > %s 2>/dev/null",
        KC_CLI_PATH, args, output_path);
#endif
    if (system(command) != 0) {
        remove(output_path);
        return 0;
    }
    file = fopen(output_path, "rb");
    if (!file) {
        remove(output_path);
        return 0;
    }
    size = fread(output, 1U, sizeof(output) - 1U, file);
    fclose(file);
    remove(output_path);
    output[size] = '\0';
    return strcmp(output, expected) == 0;
}

/**
 * Prints one CLI result using the shared functional test counter.
 * @param name CLI test name.
 * @param detail CLI behavior under test.
 * @param ok Nonzero if the behavior matches the contract.
 * @return Zero on success and one on failure.
 */
static int rdvz_cli_result(const char *name, const char *detail, int ok) {
    test_case_current++;
    case_result(!ok, name, detail);
    return ok ? 0 : 1;
}

/**
 * Tests each supported CLI operation and invalid invocations.
 * @return Number of failed CLI cases.
 */
static int case_rdvz_cli(void) {
    int failures = 0;

    failures += rdvz_cli_result("help", "--help succeeds",
        rdvz_cli_run("--help") == 0);
    failures += rdvz_cli_result("version", "--version succeeds",
        rdvz_cli_run("--version") == 0);
    failures += rdvz_cli_result("connect", "connect prints a fresh peer ID",
        rdvz_cli_output_equals("connect", "1" KC_LINE_END));
    failures += rdvz_cli_result("missing", "command is required",
        rdvz_cli_run(NULL) != 0);
    failures += rdvz_cli_result("unknown", "unknown command fails",
        rdvz_cli_run("--unknown") != 0);

    return failures;
}
#endif

/**
 * Runs every public API contract and native CLI case.
 * @return Zero on success and nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 9;
#else
    test_case_total = 14;
#endif
    test_case_current = 0;
    run_case(&rc, case_kc_rdvz_open);
    run_case(&rc, case_kc_rdvz_connect);
    run_case(&rc, case_kc_rdvz_peer_id);
    run_case(&rc, case_kc_rdvz_list);
    run_case(&rc, case_kc_rdvz_peer_list);
    run_case(&rc, case_kc_rdvz_disconnect);
    run_case(&rc, case_kc_rdvz_close);
    run_case(&rc, case_kc_rdvz_strerror);
    run_case(&rc, case_kc_rdvz_version);
#ifndef __EMSCRIPTEN__
    rc += case_rdvz_cli();
#endif

    printf("%d passed, %d failed\n", test_case_total - rc, rc);
    return rc ? 1 : 0;
}

/**
 * Runs one named functional test or the complete contract suite.
 * @param argc Argument count.
 * @param argv Test name as one argument.
 * @return Zero on success, one on failure or two for invalid usage.
 */
int main(int argc, char **argv) {
    typedef struct {
        const char *name;
        int (*fn)(void);
    } rdvz_test_case_t;
    static const rdvz_test_case_t cases[] = {
        {"kc_rdvz_open", case_kc_rdvz_open},
        {"kc_rdvz_connect", case_kc_rdvz_connect},
        {"kc_rdvz_peer_id", case_kc_rdvz_peer_id},
        {"kc_rdvz_list", case_kc_rdvz_list},
        {"kc_rdvz_peer_list", case_kc_rdvz_peer_list},
        {"kc_rdvz_disconnect", case_kc_rdvz_disconnect},
        {"kc_rdvz_close", case_kc_rdvz_close},
        {"kc_rdvz_strerror", case_kc_rdvz_strerror},
        {"kc_rdvz_version", case_kc_rdvz_version}
    };
    size_t i;

    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            test_case_total = 1;
            test_case_current = 1;
            return cases[i].fn();
        }
    }
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "rdvz_cli") == 0) {
        int failures;
        test_case_total = 5;
        test_case_current = 0;
        failures = case_rdvz_cli();
        printf("%d passed, %d failed\n", test_case_total - failures, failures);
        return failures ? 1 : 0;
    }
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
