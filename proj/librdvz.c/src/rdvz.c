/**
 * rdvz.c - Rendezvous peer relationship CLI.
 * Summary: Creates one in-memory peer per invocation.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "librdvz.h"

#include <stdio.h>
#include <string.h>

/**
 * Prints available CLI commands.
 * @param name Executable name.
 * @return None.
 */
static void rdvz_help(const char *name) {
    printf("Usage: %s connect\n", name);
    printf("       %s --help|--version\n", name);
    printf("\nEach command operates on a private in-memory registry.\n");
}

/**
 * Runs one demonstration of the rendezvous relationship contract.
 * @param argc Command argument count.
 * @param argv Command argument vector.
 * @return Zero on success, nonzero on failure.
 */
int main(int argc, char **argv) {
    kc_rdvz_t *rdvz = NULL;
    kc_rdvz_peer_t *peer = NULL;
    int status;

    if (argc != 2) {
        rdvz_help(argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
        rdvz_help(argv[0]);
        return 0;
    }
    if (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v")) {
        printf("%llu\n", (unsigned long long)kc_rdvz_version());
        return 0;
    }
    if (strcmp(argv[1], "connect") != 0) {
        rdvz_help(argv[0]);
        return 2;
    }

    status = kc_rdvz_open(&rdvz, NULL);
    if (status == KC_RDVZ_OK)
        status = kc_rdvz_connect(rdvz, NULL, &peer);

    if (status == KC_RDVZ_OK) {
        printf("%s\n", kc_rdvz_peer_id(peer));
    } else {
        fprintf(stderr, "rdvz: %s\n", kc_rdvz_strerror(status));
    }
    (void)kc_rdvz_close(rdvz);
    return status == KC_RDVZ_OK ? 0 : 1;
}
