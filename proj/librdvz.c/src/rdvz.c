/**
 * rdvz.c - Named rendezvous peer command-line interface.
 * Summary: Creates, lists, connects and disconnects resident peers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "librdvz.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define RDVZ_CLI_BUF 4096
#define RDVZ_CLI_ID_MAX 128

/**
 * Print public command-line usage.
 * @return None.
 */
static void rdvz_cli_help(void)
{
    printf("Usage: rdvz <id> [command...]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  rdvz <id> <cmd>              Create or update a peer\n");
    printf("  rdvz [id1] -l                List peers or direct links\n");
    printf("  rdvz <id1> -c <id2> [cmd]    Connect two peers\n");
    printf("  rdvz <id1> -d [id2] [cmd]    Disconnect a link or remove a peer\n");
    printf("\n");
    printf("Options:\n");
    printf("  -l, --list        List peers or direct links\n");
    printf("  -c, --connect     Connect two peers\n");
    printf("  -d, --disconnect  Disconnect a link or remove a peer\n");
    printf("  -h, --help        Show this help\n");
    printf("  -v, --version     Show version\n");
}

/**
 * Assemble one command from positional CLI arguments.
 * @param out Destination buffer.
 * @param cap Destination capacity.
 * @param argv Argument vector.
 * @param first First command argument.
 * @param argc Argument count.
 * @return Zero on success.
 */
static int rdvz_cli_join(char *out, size_t cap, char **argv,
    int first, int argc)
{
    size_t used = 0U;
    int i;

    if (!out || !cap || first >= argc) return 1;
    out[0] = '\0';
    for (i = first; i < argc; i++) {
        size_t size = strlen(argv[i]);

        if (strchr(argv[i], '\n') || strchr(argv[i], '\r')) return 1;
        if (i > first) {
            if (used + 1U >= cap) return 1;
            out[used++] = ' ';
        }
        if (size >= cap - used) return 1;
        memcpy(out + used, argv[i], size);
        used += size;
        out[used] = '\0';
    }
    return used ? 0 : 1;
}

/**
 * Print one named peer or linked neighbor.
 * @param id Peer identifier.
 * @param userdata Unused.
 * @return None.
 */
static void rdvz_cli_print(const char *id, void *userdata)
{
    (void)userdata;
    printf("%s\n", id);
}

/**
 * Spawn one operation-specific command with the other peer's identifier.
 * Command completion does not block subsequent CLI operations.
 * @param id Other peer identifier.
 * @param userdata Configured command string.
 * @return None.
 */
static void rdvz_cli_command(const char *id, void *userdata)
{
    const char *command = (const char *)userdata;
    char text[RDVZ_CLI_BUF + RDVZ_CLI_ID_MAX + 8];

    if (!command || !command[0] || !id) return;
    if ((size_t)snprintf(text, sizeof(text), "%s %s",
            command, id) >= sizeof(text)) return;
#ifdef _WIN32
    {
        char shell[sizeof(text) + 16];
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;

        if ((size_t)snprintf(shell, sizeof(shell),
                "cmd.exe /C %s", text) >= sizeof(shell)) return;
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        if (CreateProcessA(NULL, shell, NULL, NULL, FALSE,
                DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL,
                &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
#else
    {
        pid_t pid = fork();
        if (pid < 0) return;
        if (pid == 0) {
            pid_t child = fork();
            if (child < 0) _exit(1);
            if (child > 0) _exit(0);
            (void)setsid();
            execl("/bin/sh", "sh", "-c", text, (char *)NULL);
            _exit(127);
        }
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
    }
#endif
}

/**
 * Print an unsuccessful library operation.
 * @param action User-visible operation.
 * @param status Library error status.
 * @return Nonzero process status.
 */
static int rdvz_cli_error(const char *action, int status)
{
    fprintf(stderr, "rdvz: %s: %s\n", action, kc_rdvz_strerror(status));
    return 1;
}

/**
 * Create or update a named peer and its persistent connect command.
 * @param name Peer identifier.
 * @param command Shell command executed on new direct connections.
 * @return Zero on success.
 */
static int rdvz_cli_create(const char *name, const char *command)
{
    kc_rdvz_options_t options;
    kc_rdvz_peer_t *peer = NULL;
    int rc;

    memset(&options, 0, sizeof(options));
    options.cmd = command;
    rc = kc_rdvz_create(&peer, name, &options);
    if (rc != KC_RDVZ_OK) return rdvz_cli_error("create", rc);
    kc_rdvz_close(peer);
    return 0;
}

/**
 * List every registered peer or the connections of one named peer.
 * @param name Optional peer identifier for linked-neighbor listing.
 * @return Zero on success.
 */
static int rdvz_cli_list(const char *name)
{
    kc_rdvz_peer_t *peer = NULL;
    int rc;

    if (!name) rc = kc_rdvz_list(rdvz_cli_print, NULL);
    else {
        rc = kc_rdvz_open(&peer, name, NULL, NULL);
        if (rc == KC_RDVZ_OK)
            rc = kc_rdvz_peer_list(peer, rdvz_cli_print, NULL);
        kc_rdvz_close(peer);
    }
    return rc == KC_RDVZ_OK ? 0 : rdvz_cli_error("list", rc);
}

/**
 * Create one symmetric relation between named peers.
 * @param name Initiating peer ID.
 * @param other Other peer ID.
 * @param command Optional operation-specific command.
 * @return Zero on success.
 */
static int rdvz_cli_connect(const char *name, const char *other,
    const char *command)
{
    kc_rdvz_peer_t *peer = NULL;
    int rc = kc_rdvz_open(&peer, name, NULL, NULL);

    if (rc == KC_RDVZ_OK)
        rc = kc_rdvz_connect(peer, other,
            command ? rdvz_cli_command : NULL, (void *)command);
    kc_rdvz_close(peer);
    return rc == KC_RDVZ_OK ? 0 : rdvz_cli_error("connect", rc);
}

/**
 * Remove one link without removing either endpoint.
 * @param name Initiating peer ID.
 * @param other Other peer ID.
 * @param command Optional operation-specific command.
 * @return Zero on success.
 */
static int rdvz_cli_unlink(const char *name, const char *other,
    const char *command)
{
    kc_rdvz_peer_t *peer = NULL;
    int rc = kc_rdvz_open(&peer, name, NULL, NULL);

    if (rc == KC_RDVZ_OK)
        rc = kc_rdvz_peer_disconnect(peer, other,
            command ? rdvz_cli_command : NULL, (void *)command);
    kc_rdvz_close(peer);
    return rc == KC_RDVZ_OK ? 0 : rdvz_cli_error("disconnect", rc);
}

/**
 * Dispatch CLI commands to the public library.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    const char *name;
    char command[RDVZ_CLI_BUF];

    if (argc == 2 &&
            (strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "--help") == 0)) {
        rdvz_cli_help();
        return 0;
    }
    if (argc == 2 &&
            (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0)) {
        printf("%llu\n", (unsigned long long)kc_rdvz_version());
        return 0;
    }
    if (argc == 2 &&
            (strcmp(argv[1], "-l") == 0 ||
            strcmp(argv[1], "--list") == 0)) return rdvz_cli_list(NULL);

    if (argc < 2 || argv[1][0] == '-') {
        fprintf(stderr, "rdvz: missing or invalid peer id\n");
        return 1;
    }
    name = argv[1];
    if (argc < 3) {
        fprintf(stderr, "rdvz: missing operation or command\n");
        return 1;
    }
    if (strcmp(argv[2], "-l") == 0 ||
            strcmp(argv[2], "--list") == 0) {
        if (argc != 3) return 1;
        return rdvz_cli_list(name);
    }
    if (strcmp(argv[2], "-c") == 0 ||
            strcmp(argv[2], "--connect") == 0) {
        if (argc < 4) return 1;
        if (argc > 4 && rdvz_cli_join(command, sizeof(command),
                argv, 4, argc)) return 1;
        return rdvz_cli_connect(name, argv[3],
            argc > 4 ? command : NULL);
    }
    if (strcmp(argv[2], "-d") == 0 ||
            strcmp(argv[2], "--disconnect") == 0) {
        int rc;

        if (argc == 3) {
            rc = kc_rdvz_disconnect(name);
            return rc == KC_RDVZ_OK ? 0 : rdvz_cli_error("disconnect", rc);
        }
        if (argc > 4 && rdvz_cli_join(command, sizeof(command),
                argv, 4, argc)) return 1;
        return rdvz_cli_unlink(name, argv[3],
            argc > 4 ? command : NULL);
    }
    if (argv[2][0] == '-' ||
            rdvz_cli_join(command, sizeof(command), argv, 2, argc))
        return 1;
    return rdvz_cli_create(name, command);
}
