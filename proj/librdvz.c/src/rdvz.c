/**
 * rdvz.c - Rendezvous registry command-line interface.
 * Summary: Runs one in-memory rendezvous operation per process.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "librdvz.h"

#include <stdio.h>
#include <string.h>

/**
 * Prints public command usage.
 * @param name Program name.
 * @return None.
 */
static void rdvz_help(const char *name) {
    printf("Usage: %s match <id> <publication> <seeker>\n", name);
    printf("       %s publish <id> <value>\n", name);
    printf("       %s seek <id> <value>\n", name);
    printf("\nOperations use one in-memory registry for this process.\n");
}

/**
 * Runs one rendezvous command.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    kc_rdvz_t *rdvz = NULL;
    const kc_rdvz_point_t *point = NULL;
    const kc_rdvz_seeker_t *seeker = NULL;
    int status;

    if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        rdvz_help(argv[0]);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version"))) {
        printf("%llu\n", (unsigned long long)kc_rdvz_version());
        return 0;
    }
    if (argc < 4 || (strcmp(argv[1], "publish") && strcmp(argv[1], "seek") &&
            strcmp(argv[1], "match"))) {
        rdvz_help(argv[0]);
        return 1;
    }
    status = kc_rdvz_open(&rdvz, NULL);
    if (status != KC_RDVZ_OK) return 1;
    if (!strcmp(argv[1], "publish")) {
        status = kc_rdvz_publish(rdvz, argv[2], argv[3], strlen(argv[3]), &point);
    } else if (!strcmp(argv[1], "seek")) {
        status = kc_rdvz_seek(rdvz, argv[2], argv[3], strlen(argv[3]), &seeker);
        point = seeker ? seeker->point : NULL;
    } else if (argc == 5) {
        status = kc_rdvz_publish(rdvz, argv[2], argv[3], strlen(argv[3]), &point);
        if (status == KC_RDVZ_OK) {
            status = kc_rdvz_seek(rdvz, argv[2], argv[4], strlen(argv[4]), &seeker);
            point = seeker ? seeker->point : NULL;
        }
    } else {
        status = KC_RDVZ_EINVAL;
    }
    if (status == KC_RDVZ_OK && point) {
        printf("%s\t%zu\t%zu\n", point->id,
            point->publication ? point->publication->data_size : 0U,
            point->seeker_count);
    } else {
        fprintf(stderr, "rdvz: %s\n", kc_rdvz_strerror(status));
    }
    kc_rdvz_close(rdvz);
    return status != KC_RDVZ_OK;
}
