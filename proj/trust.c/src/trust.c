/**
 * trust.c - CLI for portable scoped trust.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libtrust.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/**
 * Prints trust CLI usage information.
 * @param name Name string.
 * @return No return value.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s <command> [arguments]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("    init                    Ensure the local trust store exists\n");
    printf("    invite                  Create a one-use trust invitation\n");
    printf("    join <code>             Join from an invitation code\n");
    printf("    confirm <confirmation>  Confirm a joined invitation\n");
    printf("    seal <remote_uid>       Encrypt stdin for a remote UID\n");
    printf("    unseal <local_uid>      Decrypt stdin addressed to a local UID\n");
    printf("    revoke <remote_uid>     Revoke an established or pending remote UID\n");
    printf("\n");
    printf("Options:\n");
    printf("    -h, --help              Show this help\n");
    printf("    -v, --version           Show version\n");
}

/**
 * Prints the trust build version.
 * @return No return value.
 */
static void kc_print_version(void) {
    printf("trust build %llu\n", (unsigned long long)kc_trust_version());
}

/**
 * Reads bounded binary input from standard input.
 * @param out Destination output.
 * @param out_size Destination byte count.
 * @param max_size Maximum byte count.
 * @return 0 on success, or 1 on failure.
 */
static int kc_read_stdin(void **out, size_t *out_size, size_t max_size) {
    unsigned char *data;
    size_t size = 0;
    size_t cap = 4096;
    if (out) *out = NULL;
    if (out_size) *out_size = 0;
    if (!out || !out_size) return 1;
    data = (unsigned char *)malloc(cap);
    if (!data) return 1;
    for (;;) {
        size_t n;
        if (size == cap) {
            size_t next;
            unsigned char *grown;
            if (cap >= max_size) {
                unsigned char extra;
                if (fread(&extra, 1, 1, stdin) == 1) {
                    free(data);
                    return 1;
                }
                break;
            }
            next = cap * 2;
            if (next > max_size) next = max_size;
            grown = (unsigned char *)realloc(data, next ? next : 1);
            if (!grown) {
                free(data);
                return 1;
            }
            data = grown;
            cap = next;
        }
        n = fread(data + size, 1, cap - size, stdin);
        size += n;
        if (n == 0) {
            if (ferror(stdin)) {
                free(data);
                return 1;
            }
            break;
        }
    }
    *out = data;
    *out_size = size;
    return 0;
}

/**
 * Writes binary output to standard output.
 * @param data Input bytes.
 * @param size Byte count.
 * @return 0 on success, or 1 on failure.
 */
static int kc_write_stdout(const void *data, size_t size) {
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 1;
#endif
    return fwrite(data, 1, size, stdout) == size && fflush(stdout) == 0 ? 0 : 1;
}

/**
 * Runs the trust command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, or 1 on command failure.
 */
int main(int argc, char **argv) {
    kc_trust_t *trust = NULL;
    const char *cmd;
    int rc = 1;

    if (argc < 2) {
        kc_print_help(argv[0]);
        return 0;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        kc_print_help(argv[0]);
        return 0;
    }
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
        kc_print_version();
        return 0;
    }

    cmd = argv[1];
    if (kc_trust_init(&trust) != KC_TRUST_OK) {
        fprintf(stderr, "trust: failed to initialize local state\n");
        return 1;
    }

    if (strcmp(cmd, "init") == 0) {
        if (argc != 2) {
            fprintf(stderr, "trust: init takes no arguments\n");
        } else {
            rc = 0;
        }
    } else if (strcmp(cmd, "invite") == 0) {
        char *uid = NULL;
        char *code = NULL;
        if (argc != 2) {
            fprintf(stderr, "trust: invite takes no arguments\n");
        } else if (kc_trust_invite(trust, &uid, &code) != KC_TRUST_OK) {
            fprintf(stderr, "trust: invite failed\n");
        } else {
            printf("{\"uid\":\"%s\",\"code\":\"%s\"}\n", uid, code);
            rc = fflush(stdout) == 0 ? 0 : 1;
        }
        kc_trust_free(uid);
        kc_trust_free(code);
    } else if (strcmp(cmd, "join") == 0) {
        char *uid = NULL;
        char *confirmation = NULL;
        if (argc != 3) {
            fprintf(stderr, "trust: join requires <code>\n");
        } else if (kc_trust_join(trust, argv[2], &uid,
            &confirmation) != KC_TRUST_OK) {
            fprintf(stderr, "trust: join failed\n");
        } else {
            printf("{\"uid\":\"%s\",\"confirmation\":\"%s\"}\n",
                uid, confirmation);
            rc = fflush(stdout) == 0 ? 0 : 1;
        }
        kc_trust_free(uid);
        kc_trust_free(confirmation);
    } else if (strcmp(cmd, "confirm") == 0) {
        char *uid = NULL;
        if (argc != 3) {
            fprintf(stderr, "trust: confirm requires <confirmation>\n");
        } else if (kc_trust_confirm(trust, argv[2], &uid) != KC_TRUST_OK) {
            fprintf(stderr, "trust: confirm failed\n");
        } else {
            printf("{\"uid\":\"%s\"}\n", uid);
            rc = fflush(stdout) == 0 ? 0 : 1;
        }
        kc_trust_free(uid);
    } else if (strcmp(cmd, "seal") == 0) {
        void *message = NULL;
        size_t message_size = 0;
        void *data = NULL;
        size_t data_size = 0;
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        if (argc != 3) {
            fprintf(stderr, "trust: seal requires <uid>\n");
        } else if (kc_read_stdin(&message, &message_size,
            KC_TRUST_MAX_MESSAGE) != 0) {
            fprintf(stderr, "trust: message too large or unreadable\n");
        } else if (kc_trust_seal(trust, argv[2], message, message_size,
            &data, &data_size) != KC_TRUST_OK) {
            fprintf(stderr, "trust: seal failed\n");
        } else {
            rc = kc_write_stdout(data, data_size);
        }
        free(message);
        kc_trust_free(data);
    } else if (strcmp(cmd, "unseal") == 0) {
        void *data = NULL;
        size_t data_size = 0;
        void *message = NULL;
        size_t message_size = 0;
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        if (argc != 3) {
            fprintf(stderr, "trust: unseal requires <uid>\n");
        } else if (kc_read_stdin(&data, &data_size,
            KC_TRUST_MAX_MESSAGE + 1024U * 1024U) != 0) {
            fprintf(stderr, "trust: protected input too large or unreadable\n");
        } else if (kc_trust_unseal(trust, argv[2], data, data_size,
            &message, &message_size) != KC_TRUST_OK) {
            fprintf(stderr, "trust: unseal failed\n");
        } else {
            rc = kc_write_stdout(message, message_size);
        }
        free(data);
        kc_trust_free(message);
    } else if (strcmp(cmd, "revoke") == 0) {
        if (argc != 3) {
            fprintf(stderr, "trust: revoke requires <uid>\n");
        } else if (kc_trust_revoke(trust, argv[2]) != KC_TRUST_OK) {
            fprintf(stderr, "trust: revoke failed\n");
        } else {
            rc = 0;
        }
    } else {
        fprintf(stderr, "trust: unknown command '%s'\n", cmd);
    }

    kc_trust_close(trust);
    return rc;
}
