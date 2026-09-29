/**
 * test.c - libtrust public API contract tests.
 * Summary: Public API contract tests.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtrust.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

static int test_case_total = 0;
static int test_case_current = 0;

typedef int (*case_fn)(void);

/**
 * Prints one canonical contract-test result line.
 * @param fail Failure count.
 * @param name Name string.
 * @param description Case description.
 * @return No return value.
 */
static void case_result(int fail, const char *name, const char *description) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, description);
}

/**
 * Runs one contract test with counter tracking.
 * @param rc Result accumulator.
 * @param fn Case function.
 * @return No return value.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Checks one boolean test condition.
 * @param name Name string.
 * @param cond Condition to test.
 * @return 0 on success, or 1 on failure.
 */
static int expect_true(const char *name, int cond) {
    if (!cond) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Checks one integer test result.
 * @param name Name string.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, or 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Sets one isolated trust state directory under the system temp path.
 * @param dir Test directory name.
 * @return 0 on success, or a platform error code.
 */
static int test_set_dir(const char *dir) {
    char path[4096];
    const char *name = dir;

    if (!dir || !dir[0]) return 1;
    if (name[0] == '.') name++;

#ifdef _WIN32
    {
        char temp[MAX_PATH];
        DWORD len = GetTempPathA((DWORD)sizeof(temp), temp);

        if (len == 0 || len >= sizeof(temp)) return 1;
        if (snprintf(path, sizeof(path), "%strust-%lu-%s",
            temp, (unsigned long)GetCurrentProcessId(), name) < 0) {
            return 1;
        }
    }
    return _putenv_s("KC_TRUST_DIR", path);
#else
    if (snprintf(path, sizeof(path), "/tmp/trust-%ld-%s",
        (long)getpid(), name) < 0) {
        return 1;
    }
    return setenv("KC_TRUST_DIR", path, 1);
#endif
}

/**
 * Initializes one trust context at a test directory.
 * @param dir State directory.
 * @param out Destination output.
 * @return 0 on success, or 1 on failure.
 */
static int test_init_at(const char *dir, kc_trust_t **out) {
    if (test_set_dir(dir) != 0) return 1;
    return kc_trust_init(out) == KC_TRUST_OK ? 0 : 1;
}

/**
 * Creates one complete Bob/Alice trust relationship.
 * @param tag Test relation tag.
 * @param bob Destination Bob context.
 * @param alice Destination Alice context.
 * @param alice_uid Destination Alice UID.
 * @param bob_uid Destination Bob UID.
 * @return 0 on success, or 1 on failure.
 */
static int test_relation(const char *tag, kc_trust_t **bob,
    kc_trust_t **alice, char **alice_uid, char **bob_uid) {
    char bob_dir[256];
    char alice_dir[256];
    char *invite_uid = NULL;
    char *code = NULL;
    char *joined_uid = NULL;
    char *confirmation = NULL;
    char *confirmed_uid = NULL;
    int rc = 1;

    *bob = NULL;
    *alice = NULL;
    *alice_uid = NULL;
    *bob_uid = NULL;
    snprintf(bob_dir, sizeof(bob_dir), ".trust-test-%s-bob", tag);
    snprintf(alice_dir, sizeof(alice_dir), ".trust-test-%s-alice", tag);

    if (test_init_at(bob_dir, bob) != 0 ||
        test_init_at(alice_dir, alice) != 0) goto done;
    if (kc_trust_invite(*bob, &invite_uid, &code) != KC_TRUST_OK) goto done;
    if (kc_trust_join(*alice, code, &joined_uid,
        &confirmation) != KC_TRUST_OK) goto done;
    if (kc_trust_confirm(*bob, confirmation,
        &confirmed_uid) != KC_TRUST_OK) goto done;
    if (strcmp(invite_uid, confirmed_uid) != 0 ||
        strcmp(invite_uid, joined_uid) == 0) goto done;

    *alice_uid = invite_uid;
    invite_uid = NULL;
    *bob_uid = joined_uid;
    joined_uid = NULL;
    rc = 0;

done:
    kc_trust_free(invite_uid);
    kc_trust_free(code);
    kc_trust_free(joined_uid);
    kc_trust_free(confirmation);
    kc_trust_free(confirmed_uid);
    if (rc != 0) {
        kc_trust_close(*bob);
        kc_trust_close(*alice);
        *bob = NULL;
        *alice = NULL;
    }
    return rc;
}

/**
 * Revokes and releases one test relationship.
 * @param bob Destination Bob context.
 * @param alice Destination Alice context.
 * @param alice_uid Destination Alice UID.
 * @param bob_uid Destination Bob UID.
 * @return No return value.
 */
static void test_relation_close(kc_trust_t *bob, kc_trust_t *alice,
    char *alice_uid, char *bob_uid) {
    if (bob && alice_uid) kc_trust_revoke(bob, alice_uid);
    if (alice && bob_uid) kc_trust_revoke(alice, bob_uid);
    kc_trust_free(alice_uid);
    kc_trust_free(bob_uid);
    kc_trust_close(bob);
    kc_trust_close(alice);
}

/**
 * Tests kc_trust_version.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_trust_version(void) {
    int fail = 0;
    fail += expect_true("version is nonzero", kc_trust_version() != 0);
    case_result(fail, "kc_trust_version",
        "returns the generated build version");
    return fail ? 1 : 0;
}

/**
 * Tests kc_trust_init.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_trust_init(void) {
    kc_trust_t *a = NULL;
    kc_trust_t *b = NULL;
    int fail = 0;
    fail += expect_int("first init succeeds", 0,
        test_init_at(".trust-test-init", &a));
    fail += expect_int("second init succeeds", 0,
        test_init_at(".trust-test-init", &b));
    fail += expect_true("first context returned", a != NULL);
    fail += expect_true("second context returned", b != NULL);
    kc_trust_close(a);
    kc_trust_close(b);
    case_result(fail, "kc_trust_init",
        "creates or opens the conventional local trust store");
    return fail ? 1 : 0;
}

/**
 * Tests kc_trust_invite.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_trust_invite(void) {
    kc_trust_t *trust = NULL;
    char *alice_uid = NULL;
    char *code = NULL;
    int fail = 0;
    fail += expect_int("init succeeds", 0,
        test_init_at(".trust-test-invite", &trust));
    if (trust) {
        fail += expect_int("invite succeeds", KC_TRUST_OK,
            kc_trust_invite(trust, &alice_uid, &code));
        fail += expect_true("uid is canonical UUID length",
            alice_uid && strlen(alice_uid) == KC_TRUST_UID_SIZE);
        fail += expect_true("code is portable text",
            code && strlen(code) > 100);
        if (alice_uid)
            fail += expect_int("pending uid can be revoked", KC_TRUST_OK,
                kc_trust_revoke(trust, alice_uid));
    }
    kc_trust_free(alice_uid);
    kc_trust_free(code);
    kc_trust_close(trust);
    case_result(fail, "kc_trust_invite",
        "creates one-use invitation text and the invited endpoint UID");
    return fail ? 1 : 0;
}

/**
 * Tests kc_trust_join.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_trust_join(void) {
    kc_trust_t *bob = NULL;
    kc_trust_t *alice = NULL;
    char *alice_uid = NULL;
    char *bob_uid = NULL;
    char *code = NULL;
    char *confirmation = NULL;
    int fail = 0;
    fail += expect_int("bob init", 0,
        test_init_at(".trust-test-join-bob", &bob));
    fail += expect_int("alice init", 0,
        test_init_at(".trust-test-join-alice", &alice));
    if (bob && alice) {
        fail += expect_int("invite succeeds", KC_TRUST_OK,
            kc_trust_invite(bob, &alice_uid, &code));
        if (code) {
            fail += expect_int("join succeeds", KC_TRUST_OK,
                kc_trust_join(alice, code, &bob_uid, &confirmation));
            fail += expect_true("join returns inviter uid",
                bob_uid && strlen(bob_uid) == KC_TRUST_UID_SIZE);
            if (alice_uid && bob_uid)
                fail += expect_true("endpoint uids differ",
                    strcmp(alice_uid, bob_uid) != 0);
            fail += expect_true("confirmation returned",
                confirmation && confirmation[0]);
        }
        if (alice_uid) kc_trust_revoke(bob, alice_uid);
        if (bob_uid) kc_trust_revoke(alice, bob_uid);
    }
    kc_trust_free(alice_uid);
    kc_trust_free(bob_uid);
    kc_trust_free(code);
    kc_trust_free(confirmation);
    kc_trust_close(bob);
    kc_trust_close(alice);
    case_result(fail, "kc_trust_join",
        "joins an invitation, learns the inviter UID, and emits confirmation");
    return fail ? 1 : 0;
}

/**
 * Tests kc_trust_confirm.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_trust_confirm(void) {
    kc_trust_t *bob = NULL;
    kc_trust_t *alice = NULL;
    char *alice_uid = NULL;
    char *bob_uid = NULL;
    int fail = 0;
    fail += expect_int("relationship establishes", 0,
        test_relation("confirm", &bob, &alice, &alice_uid, &bob_uid));
    fail += expect_true("alice uid returned", alice_uid != NULL);
    fail += expect_true("bob uid returned", bob_uid != NULL);
    if (alice_uid && bob_uid)
        fail += expect_true("endpoint uids are distinct",
            strcmp(alice_uid, bob_uid) != 0);
    test_relation_close(bob, alice, alice_uid, bob_uid);
    case_result(fail, "kc_trust_confirm",
        "validates one-use confirmation and establishes the binding");
    return fail ? 1 : 0;
}

/**
 * Tests kc_trust_seal.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_trust_seal(void) {
    kc_trust_t *bob = NULL;
    kc_trust_t *alice = NULL;
    char *alice_uid = NULL;
    char *bob_uid = NULL;
    void *data = NULL;
    size_t size = 0;
    int fail = 0;
    fail += expect_int("relationship establishes", 0,
        test_relation("seal", &bob, &alice, &alice_uid, &bob_uid));
    if (bob && alice_uid) {
        fail += expect_int("seal succeeds", KC_TRUST_OK,
            kc_trust_seal(bob, alice_uid, "hello", 5, &data, &size));
        fail += expect_true("seal returns protected blob", data && size > 5);
        kc_trust_free(data);
        data = NULL;
        size = 0;
        fail += expect_int("seal enforces max message", KC_TRUST_ERROR,
            kc_trust_seal(bob, alice_uid, "x", KC_TRUST_MAX_MESSAGE + 1,
                &data, &size));
    }
    kc_trust_free(data);
    test_relation_close(bob, alice, alice_uid, bob_uid);
    case_result(fail, "kc_trust_seal",
        "protects bytes for an established scoped identity");
    return fail ? 1 : 0;
}

/**
 * Tests kc_trust_unseal.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_trust_unseal(void) {
    kc_trust_t *bob = NULL;
    kc_trust_t *alice = NULL;
    char *alice_uid = NULL;
    char *bob_uid = NULL;
    void *data = NULL;
    size_t data_size = 0;
    void *message = NULL;
    size_t message_size = 0;
    static const unsigned char binary[] = {0, 1, 2, 0, 255};
    int fail = 0;
    fail += expect_int("relationship establishes", 0,
        test_relation("unseal", &bob, &alice, &alice_uid, &bob_uid));
    if (bob && alice && alice_uid) {
        fail += expect_int("bob seals", KC_TRUST_OK,
            kc_trust_seal(bob, alice_uid, binary, sizeof(binary),
                &data, &data_size));
        if (data) {
            fail += expect_int("alice unseals", KC_TRUST_OK,
                kc_trust_unseal(alice, alice_uid, data, data_size,
                    &message, &message_size));
            fail += expect_true("binary length preserved",
                message_size == sizeof(binary));
            fail += expect_true("binary bytes preserved",
                message && memcmp(message, binary, sizeof(binary)) == 0);
        }
    }
    kc_trust_free(data);
    kc_trust_free(message);
    test_relation_close(bob, alice, alice_uid, bob_uid);
    case_result(fail, "kc_trust_unseal",
        "authenticates and restores binary plaintext");
    return fail ? 1 : 0;
}

/**
 * Tests kc_trust_revoke.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_trust_revoke(void) {
    kc_trust_t *bob = NULL;
    kc_trust_t *alice = NULL;
    char *alice_uid = NULL;
    char *bob_uid = NULL;
    void *data = NULL;
    size_t data_size = 0;
    int fail = 0;
    fail += expect_int("relationship establishes", 0,
        test_relation("revoke", &bob, &alice, &alice_uid, &bob_uid));
    if (bob && alice_uid) {
        fail += expect_int("revoke succeeds", KC_TRUST_OK,
            kc_trust_revoke(bob, alice_uid));
        fail += expect_int("seal after revoke fails", KC_TRUST_ERROR,
            kc_trust_seal(bob, alice_uid, "x", 1, &data, &data_size));
        fail += expect_int("second revoke reports missing", KC_TRUST_ERROR,
            kc_trust_revoke(bob, alice_uid));
    }
    kc_trust_free(data);
    if (alice && bob_uid) kc_trust_revoke(alice, bob_uid);
    kc_trust_free(alice_uid);
    kc_trust_free(bob_uid);
    kc_trust_close(bob);
    kc_trust_close(alice);
    case_result(fail, "kc_trust_revoke",
        "removes scoped trust without transport semantics");
    return fail ? 1 : 0;
}

/**
 * Tests kc_trust_free.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_trust_free(void) {
    kc_trust_t *trust = NULL;
    char *uid = NULL;
    char *code = NULL;
    int fail = 0;
    fail += expect_int("init succeeds", 0,
        test_init_at(".trust-test-free", &trust));
    if (trust) {
        fail += expect_int("invite allocates outputs", KC_TRUST_OK,
            kc_trust_invite(trust, &uid, &code));
        if (uid) kc_trust_revoke(trust, uid);
    }
    kc_trust_free(uid);
    kc_trust_free(code);
    kc_trust_free(NULL);
    kc_trust_close(trust);
    case_result(fail, "kc_trust_free",
        "releases public allocations and accepts NULL");
    return fail ? 1 : 0;
}

/**
 * Tests the complete directional trust protocol.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_trust_protocol(void) {
    kc_trust_t *bob = NULL;
    kc_trust_t *alice = NULL;
    char *alice_uid = NULL;
    char *bob_uid = NULL;
    void *data = NULL;
    size_t data_size = 0;
    void *message = NULL;
    size_t message_size = 0;
    int fail = 0;
    fail += expect_int("relationship establishes", 0,
        test_relation("protocol", &bob, &alice, &alice_uid, &bob_uid));
    if (bob && alice && alice_uid && bob_uid) {
        fail += expect_int("bob seals for alice uid", KC_TRUST_OK,
            kc_trust_seal(bob, alice_uid, "authenticated", 13,
                &data, &data_size));
        if (data && data_size > 0) {
            ((unsigned char *)data)[data_size - 1] ^= 1;
            fail += expect_int("tampered blob rejected", KC_TRUST_ERROR,
                kc_trust_unseal(alice, alice_uid, data, data_size,
                    &message, &message_size));
            ((unsigned char *)data)[data_size - 1] ^= 1;
            fail += expect_int("valid blob remains valid", KC_TRUST_OK,
                kc_trust_unseal(alice, alice_uid, data, data_size,
                    &message, &message_size));
            kc_trust_free(message);
            message = NULL;
            message_size = 0;
        }
        kc_trust_free(data);
        data = NULL;
        data_size = 0;

        fail += expect_int("alice seals for bob uid", KC_TRUST_OK,
            kc_trust_seal(alice, bob_uid, "reply", 5, &data, &data_size));
        if (data) {
            fail += expect_int("bob unseals on bob local uid", KC_TRUST_OK,
                kc_trust_unseal(bob, bob_uid, data, data_size,
                    &message, &message_size));
            fail += expect_true("reverse plaintext matches",
                message_size == 5 && memcmp(message, "reply", 5) == 0);
        }
    }
    kc_trust_free(data);
    kc_trust_free(message);
    test_relation_close(bob, alice, alice_uid, bob_uid);
    case_result(fail, "kc_trust_protocol",
        "binds directional UIDs and ciphertext authentication without replay policy");
    return fail ? 1 : 0;
}

/**
 * Runs all trust contract test cases.
 * @return 0 when all cases pass, or a failure count.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 10;
    test_case_current = 0;
    run_case(&rc, case_kc_trust_version);
    run_case(&rc, case_kc_trust_init);
    run_case(&rc, case_kc_trust_invite);
    run_case(&rc, case_kc_trust_join);
    run_case(&rc, case_kc_trust_confirm);
    run_case(&rc, case_kc_trust_seal);
    run_case(&rc, case_kc_trust_unseal);
    run_case(&rc, case_kc_trust_revoke);
    run_case(&rc, case_kc_trust_free);
    run_case(&rc, case_kc_trust_protocol);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one selected trust contract test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on test failure, or 2 on usage error.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_trust_version") == 0) return case_kc_trust_version();
    if (strcmp(argv[1], "kc_trust_init") == 0) return case_kc_trust_init();
    if (strcmp(argv[1], "kc_trust_invite") == 0) return case_kc_trust_invite();
    if (strcmp(argv[1], "kc_trust_join") == 0) return case_kc_trust_join();
    if (strcmp(argv[1], "kc_trust_confirm") == 0) return case_kc_trust_confirm();
    if (strcmp(argv[1], "kc_trust_seal") == 0) return case_kc_trust_seal();
    if (strcmp(argv[1], "kc_trust_unseal") == 0) return case_kc_trust_unseal();
    if (strcmp(argv[1], "kc_trust_revoke") == 0) return case_kc_trust_revoke();
    if (strcmp(argv[1], "kc_trust_free") == 0) return case_kc_trust_free();
    if (strcmp(argv[1], "kc_trust_protocol") == 0) return case_kc_trust_protocol();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
