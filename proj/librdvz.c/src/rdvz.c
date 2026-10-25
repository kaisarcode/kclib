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
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void rdvz_cli_usage(const char *name)
{
    printf("Usage: %s connect\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
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

    if (argc == 2 && (!strcmp(argv[1], "-h") ||
        !strcmp(argv[1], "--help")))
    {
        rdvz_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "-v") ||
        !strcmp(argv[1], "--version")))
    {
        printf("%llu\n", (unsigned long long)kc_rdvz_version());
        return 0;
    }
    if (argc == 1) {
        fprintf(stderr, "rdvz: missing command\n");
        return 1;
    }
    if (argc != 2) {
        fprintf(stderr, "rdvz: expected one command\n");
        return 1;
    }
    if (strcmp(argv[1], "connect") != 0) {
        fprintf(stderr, "rdvz: unknown command: %s\n", argv[1]);
        return 1;
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
