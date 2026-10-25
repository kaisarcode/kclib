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
        list.count != events->seen_connected + 2U)
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

/**
 * Tests independent peers and symmetric direct links.
 * @return Number of failed contract checks.
 */
static int case_connect_and_list(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *a = NULL, *b = NULL, *c = NULL;
    rdvz_list_t list = {0};
    int fail = 0;

    fail += kc_rdvz_open(&rdvz, NULL) != KC_RDVZ_OK;
    if (!rdvz) return 1;
    fail += kc_rdvz_connect(rdvz, NULL, &a) != KC_RDVZ_OK;
    fail += !a || strcmp(kc_rdvz_peer_id(a), "1") != 0;
    fail += kc_rdvz_connect(rdvz, kc_rdvz_peer_id(a), &b) != KC_RDVZ_OK;
    fail += !b || strcmp(kc_rdvz_peer_id(b), "2") != 0;
    fail += kc_rdvz_connect(rdvz, kc_rdvz_peer_id(b), &c) != KC_RDVZ_OK;
    fail += !c || strcmp(kc_rdvz_peer_id(c), "3") != 0;
    if (a && b && c) {
        fail += !rdvz_expect_links(a, 1U);
        fail += !rdvz_expect_links(b, 2U);
        fail += !rdvz_expect_links(c, 1U);
        fail += kc_rdvz_list(rdvz, rdvz_collect, &list) != KC_RDVZ_OK;
        fail += list.count != 3U || list.last != c;
        fail += kc_rdvz_disconnect(a) != KC_RDVZ_OK;
        fail += !rdvz_expect_links(b, 1U);
        fail += !rdvz_expect_links(c, 1U);
        fail += kc_rdvz_disconnect(c) != KC_RDVZ_OK;
        fail += !rdvz_expect_links(b, 0U);
        fail += kc_rdvz_disconnect(b) != KC_RDVZ_OK;
    }
    fail += kc_rdvz_close(rdvz) != KC_RDVZ_OK;
    return fail;
}

/**
 * Tests departure, continued membership and a later new encounter.
 * @return Number of failed contract checks.
 */
static int case_reconnect(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *a = NULL, *b = NULL;
    char previous[32];
    int fail = 0;

    fail += kc_rdvz_open(&rdvz, NULL) != KC_RDVZ_OK;
    if (!rdvz) return 1;
    fail += kc_rdvz_connect(rdvz, NULL, &a) != KC_RDVZ_OK;
    fail += kc_rdvz_connect(rdvz, kc_rdvz_peer_id(a), &b) != KC_RDVZ_OK;
    if (a && b) {
        snprintf(previous, sizeof(previous), "%s", kc_rdvz_peer_id(a));
        fail += kc_rdvz_disconnect(a) != KC_RDVZ_OK;
        fail += !rdvz_expect_links(b, 0U);
        fail += kc_rdvz_connect(rdvz, previous, &a) != KC_RDVZ_ENOENT;
        fail += a != NULL;
        fail += kc_rdvz_connect(rdvz, kc_rdvz_peer_id(b), &a) != KC_RDVZ_OK;
        if (a) {
            fail += strcmp(kc_rdvz_peer_id(a), previous) == 0;
            fail += !rdvz_expect_links(b, 1U);
            fail += kc_rdvz_disconnect(a) != KC_RDVZ_OK;
            fail += !rdvz_expect_links(b, 0U);
        }
        fail += kc_rdvz_disconnect(b) != KC_RDVZ_OK;
    }
    fail += kc_rdvz_close(rdvz) != KC_RDVZ_OK;
    return fail;
}

/**
 * Tests one notification per actual edge and close without disconnect events.
 * @return Number of failed contract checks.
 */
static int case_notifications(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *a = NULL, *b = NULL, *c = NULL;
    rdvz_events_t events = {0};
    kc_rdvz_options_t options = {0};
    int fail = 0;

    options.on_connect = rdvz_on_connect;
    options.on_disconnect = rdvz_on_disconnect;
    options.userdata = &events;
    fail += kc_rdvz_open(&rdvz, &options) != KC_RDVZ_OK;
    if (!rdvz) return 1;
    events.rdvz = rdvz;
    fail += kc_rdvz_connect(rdvz, NULL, &a) != KC_RDVZ_OK;
    fail += kc_rdvz_connect(rdvz, kc_rdvz_peer_id(a), &b) != KC_RDVZ_OK;
    fail += kc_rdvz_connect(rdvz, kc_rdvz_peer_id(b), &c) != KC_RDVZ_OK;
    fail += events.connected != 2U;
    fail += events.unexpected != 0;
    if (b) fail += kc_rdvz_disconnect(b) != KC_RDVZ_OK;
    fail += events.disconnected != 2U;
    fail += events.unexpected != 0;
    if (a) fail += !rdvz_expect_links(a, 0U);
    if (c) fail += !rdvz_expect_links(c, 0U);
    fail += kc_rdvz_close(rdvz) != KC_RDVZ_OK;
    fail += events.disconnected != 2U;
    return fail;
}

/**
 * Tests malformed IDs, missing peers and invalid arguments.
 * @return Number of failed contract checks.
 */
static int case_errors(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *a = NULL, *invalid = NULL;
    int fail = 0;

    fail += kc_rdvz_open(NULL, NULL) != KC_RDVZ_EINVAL;
    fail += kc_rdvz_open(&rdvz, NULL) != KC_RDVZ_OK;
    if (!rdvz) return 1;
    fail += kc_rdvz_connect(rdvz, NULL, NULL) != KC_RDVZ_EINVAL;
    fail += kc_rdvz_connect(rdvz, "0", &invalid) != KC_RDVZ_EINVAL;
    fail += kc_rdvz_connect(rdvz, "01", &invalid) != KC_RDVZ_EINVAL;
    fail += kc_rdvz_connect(rdvz, "abc", &invalid) != KC_RDVZ_EINVAL;
    fail += kc_rdvz_connect(rdvz, "18446744073709551616", &invalid) !=
        KC_RDVZ_EINVAL;
    fail += kc_rdvz_connect(rdvz, "10", &invalid) != KC_RDVZ_ENOENT;
    fail += invalid != NULL;
    fail += kc_rdvz_list(rdvz, NULL, NULL) != KC_RDVZ_EINVAL;
    fail += kc_rdvz_peer_list(NULL, rdvz_collect, NULL) != KC_RDVZ_EINVAL;
    fail += kc_rdvz_connect(rdvz, NULL, &a) != KC_RDVZ_OK;
    if (a) fail += kc_rdvz_disconnect(a) != KC_RDVZ_OK;
    fail += strcmp(kc_rdvz_strerror(KC_RDVZ_ENOENT), "peer not found") != 0;
    fail += kc_rdvz_close(rdvz) != KC_RDVZ_OK;
    fail += kc_rdvz_close(NULL) != KC_RDVZ_OK;
    return fail;
}

/**
 * Tests hash growth, neighbor cleanup and independent retained peers.
 * @return Number of failed contract checks.
 */
static int case_many_peers(void) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *anchor = NULL, *peer = NULL;
    rdvz_list_t list = {0};
    int fail = 0;
    unsigned i;

    fail += kc_rdvz_open(&rdvz, NULL) != KC_RDVZ_OK;
    if (!rdvz) return 1;
    fail += kc_rdvz_connect(rdvz, NULL, &anchor) != KC_RDVZ_OK;
    if (!anchor) {
        (void)kc_rdvz_close(rdvz);
        return 1;
    }
    for (i = 0U; i < 2048U; ++i) {
        if (kc_rdvz_connect(rdvz, kc_rdvz_peer_id(anchor), &peer) !=
            KC_RDVZ_OK) {
            fail++;
            break;
        }
    }
    fail += kc_rdvz_list(rdvz, rdvz_collect, &list) != KC_RDVZ_OK;
    fail += list.count != (size_t)i + 1U;
    fail += !rdvz_expect_links(anchor, (size_t)i);
    fail += kc_rdvz_disconnect(anchor) != KC_RDVZ_OK;
    fail += !rdvz_expect_links(peer, 0U);
    fail += kc_rdvz_close(rdvz) != KC_RDVZ_OK;
    return fail;
}

#ifndef __EMSCRIPTEN__
/**
 * Runs one native rdvz CLI command.
 * @param args CLI arguments.
 * @return Command exit status.
 */
static int rdvz_cli_run(const char *args) {
    char command[4096];
#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > NUL 2>&1\"",
        KC_CLI_PATH, args);
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        KC_CLI_PATH, args);
#endif
    return system(command);
}

/**
 * Tests the native CLI demonstration commands.
 * @return Number of failed checks.
 */
static int case_cli(void) {
    int fail = 0;
    fail += rdvz_cli_run("--help") != 0;
    fail += rdvz_cli_run("--version") != 0;
    fail += rdvz_cli_run("connect") != 0;
    fail += rdvz_cli_run("match") != 0;
    fail += rdvz_cli_run("seek") == 0;
    return fail;
}
#endif

/**
 * Runs one named test case and reports the result.
 * @param name Test case name.
 * @param fn Test implementation.
 * @return Number of failures.
 */
static int rdvz_run(const char *name, int (*fn)(void)) {
    int fail = fn();
    printf("%s: %s\n", name, fail ? "FAIL" : "PASS");
    return fail;
}

/**
 * Runs every portable contract case and optional native CLI case.
 * @return Nonzero if any case fails.
 */
static int case_all(void) {
    int fail = 0;
    fail += rdvz_run("connect_and_list", case_connect_and_list);
    fail += rdvz_run("reconnect", case_reconnect);
    fail += rdvz_run("notifications", case_notifications);
    fail += rdvz_run("errors", case_errors);
    fail += rdvz_run("many_peers", case_many_peers);
#ifndef __EMSCRIPTEN__
    fail += rdvz_run("cli", case_cli);
#endif
    return fail != 0;
}

/**
 * Dispatches a named contract test case.
 * @param argc Argument count.
 * @param argv Command arguments.
 * @return Zero on success and nonzero on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s all|connect_and_list|reconnect|notifications|errors|many_peers|cli\n",
            argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "connect_and_list") == 0)
        return rdvz_run(argv[1], case_connect_and_list) != 0;
    if (strcmp(argv[1], "reconnect") == 0)
        return rdvz_run(argv[1], case_reconnect) != 0;
    if (strcmp(argv[1], "notifications") == 0)
        return rdvz_run(argv[1], case_notifications) != 0;
    if (strcmp(argv[1], "errors") == 0)
        return rdvz_run(argv[1], case_errors) != 0;
    if (strcmp(argv[1], "many_peers") == 0)
        return rdvz_run(argv[1], case_many_peers) != 0;
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "cli") == 0)
        return rdvz_run(argv[1], case_cli) != 0;
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
