/**
 * redp2p.c - REDP2P command-line interface.
 * Summary: CLI utilities and local-port adapters over the REDP2P engine.
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


typedef struct {
    redp2p_t *ctx;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
    int thread_started;
    _Atomic int done;
    int result;
    int consumer;
    char index_host[256];
    uint16_t index_port;
    char id[KC_REDP2P_ID_MAX + 1];
    char self_id[KC_REDP2P_ID_MAX + 1];
    uint16_t local_port;
} redp2p_cli_runtime_t;

/**
 * Parses one index endpoint with the default REDP2P port.
 * @param text Index endpoint.
 * @param host Destination host.
 * @param port Destination port.
 * @return 1 on success, 0 on invalid input.
 */
static int redp2p_cli_index(const char *text, char host[256], uint16_t *port)
{
    const char *colon;
    size_t len;

    if (!text || !text[0] || !host || !port) return 0;
    *port = KC_REDP2P_PORT_DEFAULT;

    if (text[0] == '[') {
        const char *end = strchr(text + 1, ']');
        if (!end || end == text + 1) return 0;
        len = (size_t)(end - text - 1);
        if (len >= 256) return 0;
        memcpy(host, text + 1, len);
        host[len] = '\0';
        if (end[1] == '\0') return 1;
        return end[1] == ':' && redp2p_cli_u16(end + 2, port);
    }

    colon = strrchr(text, ':');
    if (colon && strchr(text, ':') == colon) {
        len = (size_t)(colon - text);
        if (len == 0 || len >= 256 || !redp2p_cli_u16(colon + 1, port))
            return 0;
        memcpy(host, text, len);
        host[len] = '\0';
        return 1;
    }

    len = strlen(text);
    if (len == 0 || len >= 256) return 0;
    memcpy(host, text, len + 1);
    return 1;
}

/**
 * Generates one private consumer identifier.
 * @param out Destination identifier.
 * @return 1 on success, 0 on random-source failure.
 */
static int redp2p_cli_self_id(char out[KC_REDP2P_ID_MAX + 1])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char random[8];
    size_t i;

    if (redp2p_fill_random(random, sizeof(random)) != 0) return 0;
    out[0] = 'c';
    for (i = 0; i < sizeof(random); i++) {
        out[1 + i * 2] = hex[random[i] >> 4];
        out[2 + i * 2] = hex[random[i] & 15];
    }
    out[17] = '\0';
    memset(random, 0, sizeof(random));
    return 1;
}

/**
 * Applies the public runtime defaults used by the native API.
 * @param ctx Runtime context.
 * @return None.
 */
static void redp2p_cli_defaults(redp2p_t *ctx)
{
    ctx->sweep = REDP2P_SWEEP_DEFAULT;
    ctx->prune_interval_s = 60;
    ctx->etimeout_sec = 120;
    ctx->heartbeat_s = 15;
    ctx->punch_poll_ms = 500;
    ctx->pending_ttl_s = 30;
    ctx->max_consumers_per_publisher =
        REDP2P_MAX_CONSUMERS_PER_PUBLISHER;
}

#ifdef _WIN32
/**
 * Runs one CLI pub/con local-port adapter.
 * @param arg CLI runtime.
 * @return Thread status.
 */
static DWORD WINAPI redp2p_cli_runtime_worker(LPVOID arg)
#else
/**
 * Runs one CLI pub/con local-port adapter.
 * @param arg CLI runtime.
 * @return NULL after exit.
 */
static void *redp2p_cli_runtime_worker(void *arg)
#endif
{
    redp2p_cli_runtime_t *runtime = (redp2p_cli_runtime_t *)arg;

    if (runtime->consumer) {
        runtime->result = redp2p_con_run(runtime->ctx,
            runtime->index_host, runtime->index_port, runtime->self_id,
            runtime->id, runtime->local_port);
    } else {
        runtime->result = redp2p_pub_run(runtime->ctx,
            runtime->index_host, runtime->index_port, runtime->id,
            runtime->local_port);
    }
    atomic_store(&runtime->done, 1);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Starts one CLI local-port adapter worker.
 * @param runtime Initialized runtime.
 * @return REDP2P_OK on success or an error code.
 */
static int redp2p_cli_runtime_start(redp2p_cli_runtime_t *runtime)
{
    atomic_store(&runtime->done, 0);
    runtime->result = REDP2P_ERROR;
    atomic_store(&runtime->ctx->ready_state, 0);
    atomic_store(&runtime->ctx->ready_status, REDP2P_ERROR);
#ifdef _WIN32
    runtime->thread = CreateThread(NULL, 0, redp2p_cli_runtime_worker,
        runtime, 0, NULL);
    if (!runtime->thread) return REDP2P_ERROR;
#else
    if (pthread_create(&runtime->thread, NULL, redp2p_cli_runtime_worker,
        runtime) != 0) return REDP2P_ERROR;
#endif
    runtime->thread_started = 1;
    return REDP2P_OK;
}

/**
 * Waits until a CLI pub/con runtime is ready.
 * @param runtime Started runtime.
 * @return Runtime status.
 */
static int redp2p_cli_runtime_ready(redp2p_cli_runtime_t *runtime)
{
    uint64_t deadline = redp2p_now_ms() + 10000U;

    for (;;) {
        int state = atomic_load(&runtime->ctx->ready_state);
        if (state > 0) return REDP2P_OK;
        if (state < 0) return atomic_load(&runtime->ctx->ready_status);
        if (atomic_load(&runtime->done))
            return runtime->result == REDP2P_OK ?
                REDP2P_ERROR : runtime->result;
        if (redp2p_now_ms() >= deadline) return REDP2P_ETIMEOUT;
#ifdef _WIN32
        Sleep(1);
#else
        {
            struct timespec ts = {0, 1000000L};
            nanosleep(&ts, NULL);
        }
#endif
    }
}

/**
 * Stops and releases one CLI pub/con runtime.
 * @param runtime Runtime to close.
 * @return Final worker result.
 */
static int redp2p_cli_runtime_close(redp2p_cli_runtime_t *runtime)
{
    int result;

    if (!runtime) return REDP2P_EINVAL;
    if (runtime->ctx) redp2p_context_request_stop(runtime->ctx);
    if (runtime->thread_started) {
#ifdef _WIN32
        WaitForSingleObject(runtime->thread, INFINITE);
        CloseHandle(runtime->thread);
        runtime->thread = NULL;
#else
        pthread_join(runtime->thread, NULL);
#endif
        runtime->thread_started = 0;
    }
    result = runtime->result;
    if (runtime->ctx) {
        redp2p_context_destroy(runtime->ctx);
        runtime->ctx = NULL;
    }
    return result;
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
    printf("Options:\n");
    printf("  -h, --help      Show this help\n");
    printf("  -v, --version   Show version\n");
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
    redp2p_cli_runtime_t runtime;
    char id[KC_REDP2P_ID_MAX + 1];
    char index[320];
    uint16_t port = 0;
    int protocol = 0;
    const char *stun = getenv("REDP2P_STUN");
    const char *pass = getenv("REDP2P_PASS");
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

    memset(&runtime, 0, sizeof(runtime));
    if (!redp2p_cli_index(index, runtime.index_host, &runtime.index_port)) {
        fprintf(stderr, "redp2p: invalid index endpoint\n");
        return 1;
    }
    memcpy(runtime.id, id, strlen(id) + 1);
    runtime.local_port = port;

    status = redp2p_context_create(&runtime.ctx);
    if (status != REDP2P_OK) return 1;
    redp2p_cli_defaults(runtime.ctx);
    status = redp2p_pub_set_protocol(runtime.ctx, protocol);
    if (status == REDP2P_OK)
        status = redp2p_set_local_port(runtime.ctx, port);
    if (status == REDP2P_OK && pass)
        status = redp2p_set_registration_pass(runtime.ctx, pass);
    if (status == REDP2P_OK && stun)
        status = redp2p_set_stun_server(runtime.ctx, stun);
    if (status != REDP2P_OK) {
        fprintf(stderr, "redp2p: pub failed: %s\n", redp2p_strerror(status));
        redp2p_context_destroy(runtime.ctx);
        return 1;
    }

    status = redp2p_cli_runtime_start(&runtime);
    if (status == REDP2P_OK) status = redp2p_cli_runtime_ready(&runtime);
    if (status != REDP2P_OK) {
        fprintf(stderr, "redp2p: pub failed: %s\n", redp2p_strerror(status));
        redp2p_cli_runtime_close(&runtime);
        return 1;
    }

    fprintf(stderr, "redp2p: '%s' published from local port %u\n",
        id, (unsigned)port);
    redp2p_cli_stop = 0;
    signal(SIGINT, redp2p_cli_signal);
    signal(SIGTERM, redp2p_cli_signal);
    while (!redp2p_cli_stop && !atomic_load(&runtime.done))
        redp2p_cli_sleep();

    status = redp2p_cli_runtime_close(&runtime);
    return status == REDP2P_OK ? 0 : 1;
}

/**
 * Runs the con command.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
static int redp2p_cli_con(int argc, char **argv)
{
    redp2p_cli_runtime_t runtime;
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

    memset(&runtime, 0, sizeof(runtime));
    runtime.consumer = 1;
    if (!redp2p_cli_index(index, runtime.index_host, &runtime.index_port) ||
        !redp2p_cli_self_id(runtime.self_id))
    {
        fprintf(stderr, "redp2p: invalid consumer configuration\n");
        return 1;
    }
    memcpy(runtime.id, id, strlen(id) + 1);
    runtime.local_port = port;

    status = redp2p_context_create(&runtime.ctx);
    if (status != REDP2P_OK) return 1;
    redp2p_cli_defaults(runtime.ctx);
    status = redp2p_set_local_port(runtime.ctx, port);
    if (status == REDP2P_OK && stun)
        status = redp2p_set_stun_server(runtime.ctx, stun);
    if (status != REDP2P_OK) {
        fprintf(stderr, "redp2p: con failed: %s\n", redp2p_strerror(status));
        redp2p_context_destroy(runtime.ctx);
        return 1;
    }

    status = redp2p_cli_runtime_start(&runtime);
    if (status == REDP2P_OK) status = redp2p_cli_runtime_ready(&runtime);
    if (status != REDP2P_OK) {
        fprintf(stderr, "redp2p: con failed: %s\n", redp2p_strerror(status));
        redp2p_cli_runtime_close(&runtime);
        return 1;
    }

    fprintf(stderr, "redp2p: tunnel to '%s' available on 127.0.0.1:%u\n",
        id, (unsigned)port);
    redp2p_cli_stop = 0;
    signal(SIGINT, redp2p_cli_signal);
    signal(SIGTERM, redp2p_cli_signal);
    while (!redp2p_cli_stop && !atomic_load(&runtime.done))
        redp2p_cli_sleep();

    status = redp2p_cli_runtime_close(&runtime);
    return status == REDP2P_OK ? 0 : 1;
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
