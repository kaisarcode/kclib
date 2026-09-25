/**
 * redp2p.c - REDP2P command-line interface.
 * Summary: Thin CLI over the idx/pub/con public capability API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libredp2p.h"
#include "libredp2p-core.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static volatile sig_atomic_t redp2p_cli_stop = 0;

/**
 * Records a CLI termination signal.
 * @param sig Signal number.
 * @return None.
 */
static void redp2p_cli_signal(int sig)
{
    (void)sig;
    redp2p_cli_stop = 1;
}

/**
 * Sleeps briefly while a long-running CLI command remains active.
 * @return None.
 */
static void redp2p_cli_sleep(void)
{
#ifdef _WIN32
    Sleep(250);
#else
    usleep(250000);
#endif
}

/**
 * Parses one nonzero 16-bit unsigned integer.
 * @param text Input text.
 * @param out Destination value.
 * @return 1 on success, 0 on invalid input.
 */
static int redp2p_cli_u16(const char *text, uint16_t *out)
{
    unsigned long value;
    char *end;

    if (!text || !text[0] || !out) return 0;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end != '\0' || value == 0 || value > 65535) return 0;
    *out = (uint16_t)value;
    return 1;
}

/**
 * Parses one nonnegative size value.
 * @param text Input text.
 * @param out Destination value.
 * @return 1 on success, 0 on invalid input.
 */
static int redp2p_cli_size(const char *text, size_t *out)
{
    unsigned long long value;
    char *end;

    if (!text || !text[0] || !out) return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno || *end != '\0' || value > SIZE_MAX) return 0;
    *out = (size_t)value;
    return 1;
}

/**
 * Parses one bounded unsigned integer.
 * @param text Input text.
 * @param max Maximum accepted value.
 * @param out Destination value.
 * @return 1 on success, 0 on invalid input.
 */
static int redp2p_cli_uint(const char *text, unsigned int max,
    unsigned int *out)
{
    unsigned long value;
    char *end;

    if (!text || !text[0] || !out) return 0;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end != '\0' || value > max) return 0;
    *out = (unsigned int)value;
    return 1;
}

/**
 * Parses one publisher specification in id@index form.
 * @param text Specification text.
 * @param id Destination publisher identifier.
 * @param index Destination index endpoint.
 * @return 1 on success, 0 on invalid input.
 */
static int redp2p_cli_spec(const char *text, char id[KC_REDP2P_ID_MAX + 1],
    char index[320])
{
    const char *at;
    size_t id_len;
    size_t index_len;

    if (!text || !id || !index) return 0;
    at = strchr(text, '@');
    if (!at || at == text || !at[1]) return 0;
    id_len = (size_t)(at - text);
    index_len = strlen(at + 1);
    if (id_len > KC_REDP2P_ID_MAX || index_len >= 320) return 0;
    memcpy(id, text, id_len);
    id[id_len] = '\0';
    memcpy(index, at + 1, index_len + 1);
    return redp2p_is_valid_id(id);
}

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void redp2p_cli_usage(const char *name)
{
    printf("Usage: %s <command> [options]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("  idx <port> [--seats <N>] [--pow <N>] [--max-consumers <N>]\n");
    printf("  idx <port> --list\n");
    printf("  pub <id>@<index[:port]> --tcp <port> [--stun <url>]\n");
    printf("  pub <id>@<index[:port]> --udp <port> [--stun <url>]\n");
    printf("  con <id>@<index[:port]> <local-port> [--stun <url>]\n");
    printf("\n");
    printf("Environment:\n");
    printf("  REDP2P_SEATS          Index publisher capacity; unset means unlimited\n");
    printf("  REDP2P_POW            Index registration PoW bits (0..32)\n");
    printf("  REDP2P_PASS           Index/pub registration password\n");
    printf("  REDP2P_VIP            Reserved index IDs as '<id> <pass> ...'\n");
    printf("  REDP2P_MAX_CONSUMERS_PER_PUBLISHER\n");
    printf("  REDP2P_STUN           Optional STUN URL for pub/con\n");
}

/**
 * Parses the REDP2P_VIP environment value into structured entries.
 * @param text VIP token text.
 * @param out Destination VIP array.
 * @param out_count Destination VIP count.
 * @param out_storage Destination mutable backing storage.
 * @return 1 on success, 0 on invalid input or allocation failure.
 */
static int redp2p_cli_vips(const char *text, kc_redp2p_vip_t **out,
    size_t *out_count, char **out_storage)
{
    char *copy;
    char *p;
    size_t tokens;
    size_t count;
    kc_redp2p_vip_t *vips;

    *out = NULL;
    *out_count = 0;
    *out_storage = NULL;
    if (!text || !text[0]) return 1;

    copy = (char *)malloc(strlen(text) + 1);
    if (!copy) return 0;
    memcpy(copy, text, strlen(text) + 1);

    tokens = 0;
    p = copy;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        tokens++;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            p++;
    }
    if (tokens == 0 || (tokens & 1u)) {
        free(copy);
        return 0;
    }
    count = tokens / 2;
    vips = (kc_redp2p_vip_t *)calloc(count, sizeof(*vips));
    if (!vips) {
        free(copy);
        return 0;
    }

    p = copy;
    for (size_t i = 0; i < count; i++) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        vips[i].id = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            p++;
        if (*p) *p++ = '\0';
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        vips[i].pass = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            p++;
        if (*p) *p++ = '\0';
    }
    *out = vips;
    *out_count = count;
    *out_storage = copy;
    return 1;
}

/**
 * Prints one publisher identifier returned by an index list operation.
 * @param id Publisher identifier.
 * @param userdata Pointer to an output failure flag.
 * @return None.
 */
static void redp2p_cli_print_id(const char *id, void *userdata)
{
    int *failed = (int *)userdata;
    if (printf("%s\n", id) < 0) *failed = 1;
}

/**
 * Lists publishers announced on one local index port.
 * @param port Index port.
 * @return Process exit code.
 */
static int redp2p_cli_list(uint16_t port)
{
    redp2p_t *ctx = NULL;
    int failed = 0;
    int status;

    status = redp2p_context_create(&ctx);
    if (status != REDP2P_OK) return 1;
    status = redp2p_idx_query_publishers(ctx, "127.0.0.1", port,
        redp2p_cli_print_id, &failed);
    redp2p_context_destroy(ctx);
    if (status != REDP2P_OK) {
        fprintf(stderr, "redp2p: list failed: %s\n", redp2p_strerror(status));
        return 1;
    }
    return failed;
}

/**
 * Runs the idx command.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
static int redp2p_cli_idx(int argc, char **argv)
{
    kc_redp2p_idx_options_t options;
    kc_redp2p_idx_t *idx = NULL;
    kc_redp2p_vip_t *vips = NULL;
    char *vip_storage = NULL;
    size_t vip_storage_len = 0;
    size_t seats_value = 0;
    const size_t *seats = NULL;
    size_t max_consumers = 0;
    uint16_t port;
    unsigned int pow = 0;
    const char *value;
    int list = 0;
    int status;

    if (argc < 3 || !redp2p_cli_u16(argv[2], &port)) {
        fprintf(stderr, "redp2p: idx requires a port\n");
        return 1;
    }

    value = getenv("REDP2P_SEATS");
    if (value) {
        if (!redp2p_cli_size(value, &seats_value)) {
            fprintf(stderr, "redp2p: invalid REDP2P_SEATS\n");
            return 1;
        }
        seats = &seats_value;
    }
    value = getenv("REDP2P_POW");
    if (value && !redp2p_cli_uint(value, 32, &pow)) {
        fprintf(stderr, "redp2p: invalid REDP2P_POW\n");
        return 1;
    }
    value = getenv("REDP2P_MAX_CONSUMERS_PER_PUBLISHER");
    if (value && !redp2p_cli_size(value, &max_consumers)) {
        fprintf(stderr, "redp2p: invalid REDP2P_MAX_CONSUMERS_PER_PUBLISHER\n");
        return 1;
    }

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "-l") == 0) {
            list = 1;
        } else if (strcmp(argv[i], "--seats") == 0) {
            if (++i >= argc || !redp2p_cli_size(argv[i], &seats_value)) {
                fprintf(stderr, "redp2p: --seats requires a nonnegative integer\n");
                return 1;
            }
            seats = &seats_value;
        } else if (strcmp(argv[i], "--pow") == 0) {
            if (++i >= argc || !redp2p_cli_uint(argv[i], 32, &pow)) {
                fprintf(stderr, "redp2p: --pow requires 0..32\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--max-consumers") == 0) {
            if (++i >= argc || !redp2p_cli_size(argv[i], &max_consumers)) {
                fprintf(stderr, "redp2p: --max-consumers requires a nonnegative integer\n");
                return 1;
            }
        } else {
            fprintf(stderr, "redp2p: unknown idx option '%s'\n", argv[i]);
            return 1;
        }
    }
    if (list) return redp2p_cli_list(port);

    memset(&options, 0, sizeof(options));
    options.port = port;
    options.seats = seats;
    options.pow = pow;
    options.pass = getenv("REDP2P_PASS");
    options.max_consumers = max_consumers;
    value = getenv("REDP2P_VIP");
    if (value) vip_storage_len = strlen(value) + 1;
    if (!redp2p_cli_vips(value, &vips,
        &options.vip_count, &vip_storage))
    {
        fprintf(stderr, "redp2p: invalid REDP2P_VIP\n");
        return 1;
    }
    options.vips = vips;

    status = kc_redp2p_idx(&idx, &options);
    free(vips);
    if (vip_storage) {
        memset(vip_storage, 0, vip_storage_len);
        free(vip_storage);
    }
    if (status != KC_REDP2P_OK) {
        fprintf(stderr, "redp2p: idx failed: %s\n",
            kc_redp2p_strerror(status));
        return 1;
    }

    fprintf(stderr, "redp2p: idx listening on port %u\n", (unsigned)port);
    redp2p_cli_stop = 0;
    signal(SIGINT, redp2p_cli_signal);
    signal(SIGTERM, redp2p_cli_signal);
    while (!redp2p_cli_stop) redp2p_cli_sleep();
    kc_redp2p_idx_close(idx);
    return 0;
}

/**
 * Runs the pub command.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
static int redp2p_cli_pub(int argc, char **argv)
{
    kc_redp2p_pub_options_t options;
    kc_redp2p_pub_t *pub = NULL;
    char id[KC_REDP2P_ID_MAX + 1];
    char index[320];
    uint16_t port = 0;
    int protocol = 0;
    const char *stun = getenv("REDP2P_STUN");
    int status;

    if (argc < 5 || !redp2p_cli_spec(argv[2], id, index)) {
        fprintf(stderr,
            "redp2p: usage: %s pub <id>@<index[:port]> --tcp|--udp <port>\n",
            argv[0]);
        return 1;
    }
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--tcp") == 0 || strcmp(argv[i], "--udp") == 0) {
            if (protocol || ++i >= argc || !redp2p_cli_u16(argv[i], &port)) {
                fprintf(stderr, "redp2p: choose one protocol and valid port\n");
                return 1;
            }
            protocol = strcmp(argv[i - 1], "--tcp") == 0 ?
                KC_REDP2P_TCP : KC_REDP2P_UDP;
        } else if (strcmp(argv[i], "--stun") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "redp2p: --stun requires a URL\n");
                return 1;
            }
            stun = argv[i];
        } else {
            fprintf(stderr, "redp2p: unknown pub option '%s'\n", argv[i]);
            return 1;
        }
    }
    if (!protocol || !port) {
        fprintf(stderr, "redp2p: pub requires --tcp <port> or --udp <port>\n");
        return 1;
    }

    memset(&options, 0, sizeof(options));
    options.id = id;
    options.index = index;
    options.protocol = protocol;
    options.port = port;
    options.pass = getenv("REDP2P_PASS");
    options.stun = stun;

    status = kc_redp2p_pub(&pub, &options);
    if (status != KC_REDP2P_OK) {
        fprintf(stderr, "redp2p: pub failed: %s\n",
            kc_redp2p_strerror(status));
        return 1;
    }

    fprintf(stderr, "redp2p: '%s' published from local port %u\n",
        id, (unsigned)port);
    redp2p_cli_stop = 0;
    signal(SIGINT, redp2p_cli_signal);
    signal(SIGTERM, redp2p_cli_signal);
    while (!redp2p_cli_stop) redp2p_cli_sleep();
    kc_redp2p_pub_close(pub);
    return 0;
}

/**
 * Runs the con command.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
static int redp2p_cli_con(int argc, char **argv)
{
    kc_redp2p_con_options_t options;
    kc_redp2p_con_t *con = NULL;
    char id[KC_REDP2P_ID_MAX + 1];
    char index[320];
    uint16_t port;
    const char *stun = getenv("REDP2P_STUN");
    int status;

    if (argc < 4 || !redp2p_cli_spec(argv[2], id, index) ||
        !redp2p_cli_u16(argv[3], &port))
    {
        fprintf(stderr,
            "redp2p: usage: %s con <id>@<index[:port]> <local-port> [--stun <url>]\n",
            argv[0]);
        return 1;
    }
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--stun") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "redp2p: --stun requires a URL\n");
                return 1;
            }
            stun = argv[i];
        } else {
            fprintf(stderr, "redp2p: unknown con option '%s'\n", argv[i]);
            return 1;
        }
    }

    memset(&options, 0, sizeof(options));
    options.id = id;
    options.index = index;
    options.port = port;
    options.stun = stun;

    status = kc_redp2p_con(&con, &options);
    if (status != KC_REDP2P_OK) {
        fprintf(stderr, "redp2p: con failed: %s\n",
            kc_redp2p_strerror(status));
        return 1;
    }

    fprintf(stderr, "redp2p: tunnel to '%s' available on 127.0.0.1:%u\n",
        id, (unsigned)port);
    redp2p_cli_stop = 0;
    signal(SIGINT, redp2p_cli_signal);
    signal(SIGTERM, redp2p_cli_signal);
    while (!redp2p_cli_stop) redp2p_cli_sleep();
    kc_redp2p_con_close(con);
    return 0;
}

/**
 * Runs the REDP2P command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0)
    {
        redp2p_cli_usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("%llu\n", (unsigned long long)kc_redp2p_version());
        return 0;
    }
    if (strcmp(argv[1], "idx") == 0) return redp2p_cli_idx(argc, argv);
    if (strcmp(argv[1], "pub") == 0) return redp2p_cli_pub(argc, argv);
    if (strcmp(argv[1], "con") == 0) return redp2p_cli_con(argc, argv);

    fprintf(stderr, "redp2p: unknown command '%s'\n", argv[1]);
    return 1;
}
