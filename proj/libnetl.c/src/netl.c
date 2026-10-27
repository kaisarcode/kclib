/**
 * netl.c - Resident incoming network listener CLI.
 * Summary: Manages named listeners that dispatch traffic to local commands.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnetl.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#define NETL_CLI_BUF 4096
#define NETL_CLI_NAME_MAX 128
#define NETL_CLI_PATH_MAX 1024

static volatile sig_atomic_t netl_cli_stop = 0;

typedef struct netl_cli_session netl_cli_session_t;

typedef struct {
    char name[NETL_CLI_NAME_MAX];
    unsigned short port;
    int protocol;
    int outbound;
    char host[1025];
    char local_host[1025];
    unsigned short local_port;
    char command[NETL_CLI_BUF];
    long pid;
} netl_cli_registration_t;

typedef struct {
    const char *command;
    kc_netl_peer_t *outgoing;
#ifdef _WIN32
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
    netl_cli_session_t *sessions;
} netl_cli_state_t;

struct netl_cli_session {
    kc_netl_peer_t *peer;
    netl_cli_state_t *state;
    int active;
    int output_finished;
#ifdef _WIN32
    HANDLE process;
    HANDLE input_write;
    HANDLE output_read;
    HANDLE output_thread;
#else
    pid_t pid;
    int input_fd;
    int output_fd;
    pthread_t output_thread;
#endif
    netl_cli_session_t *next;
};

/**
 * Lock session state.
 * @param state CLI runtime state.
 * @return None.
 */
static void netl_cli_lock(netl_cli_state_t *state)
{
#ifdef _WIN32
    EnterCriticalSection(&state->lock);
#else
    (void)pthread_mutex_lock(&state->lock);
#endif
}

/**
 * Unlock session state.
 * @param state CLI runtime state.
 * @return None.
 */
static void netl_cli_unlock(netl_cli_state_t *state)
{
#ifdef _WIN32
    LeaveCriticalSection(&state->lock);
#else
    (void)pthread_mutex_unlock(&state->lock);
#endif
}

/**
 * Print command usage.
 * @return None.
 */
static void netl_cli_help(void)
{
    printf("Usage: netl <id> <addr:port> [command...]\n");
    printf("       netl <id>\n");
    printf("       netl -l [id]\n");
    printf("       netl <id> -d\n");
    printf("\n");
    printf("Commands:\n");
    printf("  netl <id> <addr:port> [cmd]  Create persistent network interface\n");
    printf("  netl -l [id]             List interfaces\n");
    printf("  netl <id> -d             Remove an interface\n");
    printf("  netl <id>                  Send stdin to an existing endpoint\n");
    printf("\n");
    printf("Options:\n");
    printf("  -t, --transport <type>   tcp or udp (default: tcp)\n");
    printf("  -l, --list               List interfaces\n");
    printf("  -d, --delete             Remove an interface\n");
    printf("  -h, --help               Show this help\n");
    printf("  -v, --version            Show version\n");
}

/**
 * Print linked libnetl build version.
 * @return None.
 */
static void netl_cli_version(void)
{
    printf("%llu\n", (unsigned long long)kc_netl_version());
}

/**
 * Validate one listener identifier.
 * @param name Listener identifier.
 * @return Nonzero when valid.
 */
static int netl_cli_name_valid(const char *name)
{
    const unsigned char *p;

    if (!name || !name[0] || strlen(name) >= NETL_CLI_NAME_MAX) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (p = (const unsigned char *)name; *p; p++) {
        if (isalnum(*p) || *p == '.' || *p == '_' || *p == '-') continue;
        return 0;
    }
    return 1;
}

/**
 * Validate one persisted line value.
 * @param value Value text.
 * @return Nonzero when valid.
 */
static int netl_cli_line_valid(const char *value)
{
    return value && value[0] &&
        strchr(value, '\n') == NULL &&
        strchr(value, '\r') == NULL;
}

/**
 * Parse one nonzero TCP/UDP port.
 * @param text Port text.
 * @param out Parsed port.
 * @return Nonzero on success.
 */
static int netl_cli_port(const char *text, unsigned short *out)
{
    unsigned long value;
    char *end;

    if (!text || !text[0] || !out) return 0;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end != '\0' || value == 0UL || value > 65535UL) return 0;
    *out = (unsigned short)value;
    return 1;
}

/**
 * Parses a host:port endpoint; IPv6 requires [host]:port.
 * @param endpoint Endpoint string.
 * @param host Output host buffer.
 * @param capacity Buffer size.
 * @param port Parsed port.
 * @return Nonzero on success.
 */
static int netl_cli_endpoint(const char *endpoint,char *host,
    size_t capacity,unsigned short *port)
{
    const char *start,*end,*service;
    size_t n;
    if(!endpoint||!host||!port)return 0;
    if(endpoint[0]=='['){
        start=endpoint+1;
        end=strchr(start,']');
        if(!end||end[1]!=':')return 0;
        service=end+2;
    }else{
        start=endpoint;
        end=strrchr(endpoint,':');
        if(!end||memchr(start,':',(size_t)(end-start)))return 0;
        service=end+1;
    }
    n=(size_t)(end-start);
    if(!n||n>=capacity||!netl_cli_port(service,port))return 0;
    memcpy(host,start,n);
    host[n]=0;
    return 1;
}

/**
 * Append one positional command argument.
 * @param out Command buffer.
 * @param cap Buffer capacity.
 * @param value Argument text.
 * @return Zero on success, nonzero on overflow.
 */
static int netl_cli_append(char *out, size_t cap, const char *value)
{
    size_t used;
    size_t size;

    used = strlen(out);
    size = strlen(value);
    if (used != 0U) {
        if (used + 1U >= cap) return 1;
        out[used++] = ' ';
        out[used] = '\0';
    }
    if (size >= cap - used) return 1;
    memcpy(out + used, value, size + 1U);
    return 0;
}

/**
 * Resolve the resident runtime directory.
 * @param out Output path.
 * @param cap Output capacity.
 * @return Zero on success.
 */
static int netl_cli_default_dir(char *out, size_t cap)
{
    const char *override = getenv("KC_NETL_DIR");

    if (override && override[0]) {
        return (size_t)snprintf(out, cap, "%s", override) < cap ? 0 : 1;
    }
#ifdef _WIN32
    {
        char temp[MAX_PATH];
        DWORD size = GetTempPathA((DWORD)sizeof(temp), temp);

        if (size == 0 || size >= (DWORD)sizeof(temp)) return 1;
        return (size_t)snprintf(
            out,
            cap,
            "%skaisarcode\\netl.c",
            temp
        ) < cap ? 0 : 1;
    }
#else
    {
        const char *xdg = getenv("XDG_RUNTIME_DIR");
        char path[NETL_CLI_PATH_MAX];
        struct stat st;

        if (xdg && xdg[0]) {
            return (size_t)snprintf(
                out,
                cap,
                "%s/kaisarcode/netl.c",
                xdg
            ) < cap ? 0 : 1;
        }
        if ((size_t)snprintf(
                path,
                sizeof(path),
                "/run/user/%lu",
                (unsigned long)getuid()
            ) < sizeof(path) &&
                stat(path, &st) == 0 &&
                S_ISDIR(st.st_mode)) {
            return (size_t)snprintf(
                out,
                cap,
                "%s/kaisarcode/netl.c",
                path
            ) < cap ? 0 : 1;
        }
        return (size_t)snprintf(
            out,
            cap,
            "/tmp/kaisarcode/netl.c-%lu",
            (unsigned long)getuid()
        ) < cap ? 0 : 1;
    }
#endif
}

/**
 * Ensure one runtime directory exists.
 * @param dir Runtime directory.
 * @return Zero on success.
 */
static int netl_cli_ensure_dir(const char *dir)
{
#ifdef _WIN32
    char buffer[NETL_CLI_PATH_MAX];
    char *p;

    if ((size_t)snprintf(buffer, sizeof(buffer), "%s", dir) >=
            sizeof(buffer)) {
        return 1;
    }
    for (p = buffer + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;

            *p = '\0';
            if (!CreateDirectoryA(buffer, NULL) &&
                    GetLastError() != ERROR_ALREADY_EXISTS) {
                return 1;
            }
            *p = saved;
        }
    }
    if (!CreateDirectoryA(buffer, NULL) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
        return 1;
    }
    return 0;
#else
    char buffer[NETL_CLI_PATH_MAX];
    char *p;

    if ((size_t)snprintf(buffer, sizeof(buffer), "%s", dir) >=
            sizeof(buffer)) {
        return 1;
    }
    for (p = buffer + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buffer, 0700) != 0 && errno != EEXIST) return 1;
            *p = '/';
        }
    }
    return mkdir(buffer, 0700) == 0 || errno == EEXIST ? 0 : 1;
#endif
}

/**
 * Compose one state path.
 * @param out Output path.
 * @param cap Output capacity.
 * @param dir Runtime directory.
 * @param name Listener identifier.
 * @param suffix File suffix.
 * @return Zero on success.
 */
static int netl_cli_state_path(
    char *out,
    size_t cap,
    const char *dir,
    const char *name,
    const char *suffix
)
{
#ifdef _WIN32
    return (size_t)snprintf(
        out,
        cap,
        "%s\\%s%s",
        dir,
        name,
        suffix
    ) < cap ? 0 : 1;
#else
    return (size_t)snprintf(
        out,
        cap,
        "%s/%s%s",
        dir,
        name,
        suffix
    ) < cap ? 0 : 1;
#endif
}

/**
 * Persist one listener registration.
 * @param dir Runtime directory.
 * @param registration Registration data.
 * @return Zero on success.
 */
static int netl_cli_write_registration(
    const char *dir,
    const netl_cli_registration_t *registration
)
{
    char path[NETL_CLI_PATH_MAX];
    FILE *file;

    if (netl_cli_state_path(
            path,
            sizeof(path),
            dir,
            registration->name,
            ".meta"
        ) != 0) {
        return 1;
    }
    file = fopen(path, "w");
    if (!file) return 1;
    fprintf(file, "%u\n", (unsigned int)registration->port);
    fprintf(
        file,
        "%s\n",
        registration->protocol == KC_NETL_UDP ? "udp" : "tcp"
    );
    fprintf(file, "%s\n", registration->command);
    fprintf(file,"%d\n",registration->outbound);
    fprintf(file,"%s\n",registration->host);
    fprintf(file,"%s\n",registration->local_host);
    fprintf(file,"%u\n",(unsigned)registration->local_port);
    fclose(file);
#ifndef _WIN32
    (void)chmod(path, 0600);
#endif
    return 0;
}

/**
 * Read one listener registration.
 * @param dir Runtime directory.
 * @param name Listener identifier.
 * @param registration Output registration.
 * @return Zero on success.
 */
static int netl_cli_read_registration(
    const char *dir,
    const char *name,
    netl_cli_registration_t *registration
)
{
    char path[NETL_CLI_PATH_MAX];
    char port_text[32];
    char transport[16];
    char mode_text[16],local_port_text[32];
    FILE *file;
    size_t size;

    if (!registration || !netl_cli_name_valid(name)) return 1;
    if (netl_cli_state_path(
            path,
            sizeof(path),
            dir,
            name,
            ".meta"
        ) != 0) {
        return 1;
    }
    file = fopen(path, "r");
    if (!file) return 1;
    memset(registration, 0, sizeof(*registration));
    if (!fgets(port_text, sizeof(port_text), file) ||
            !fgets(transport, sizeof(transport), file) ||
            !fgets(
                registration->command,
                sizeof(registration->command),
                file
            ) ||
            !fgets(mode_text,sizeof(mode_text),file)||
            !fgets(registration->host,sizeof(registration->host),file)||
            !fgets(registration->local_host,sizeof(registration->local_host),file)||
            !fgets(local_port_text,sizeof(local_port_text),file)) {
        fclose(file);
        return 1;
    }
    fclose(file);

    size = strlen(port_text);
    if (size && port_text[size - 1U] == '\n') port_text[size - 1U] = '\0';
    size = strlen(transport);
    if (size && transport[size - 1U] == '\n') transport[size - 1U] = '\0';
    size = strlen(registration->command);
    if (size && registration->command[size - 1U] == '\n') {
        registration->command[size - 1U] = '\0';
    }

    if (!netl_cli_port(port_text, &registration->port)) return 1;
    if (strcmp(transport, "tcp") == 0) {
        registration->protocol = KC_NETL_TCP;
    } else if (strcmp(transport, "udp") == 0) {
        registration->protocol = KC_NETL_UDP;
    } else {
        return 1;
    }
    registration->outbound=mode_text[0]=='1';
    if(mode_text[0]!='0'&&mode_text[0]!='1')return 1;
    {
        char *fields[]={registration->host,registration->local_host};
        size_t i;
        for(i=0;i<2;i++){
            size_t n=strlen(fields[i]);
            if(n&&fields[i][n-1U]=='\n')fields[i][n-1U]=0;
        }
    }
    {
        char *end=NULL;
        unsigned long n=strtoul(local_port_text,&end,10);
        if(!end||(*end!='\n'&&*end!='\0')||n>65535UL)return 1;
        registration->local_port=(unsigned short)n;
    }
    if(registration->command[0]&&
        !netl_cli_line_valid(registration->command))return 1;
    snprintf(registration->name, sizeof(registration->name), "%s", name);
    return 0;
}

/**
 * Persist one resident process identifier.
 * @param dir Runtime directory.
 * @param name Listener identifier.
 * @param pid Process identifier.
 * @return Zero on success.
 */
static int netl_cli_write_pid(const char *dir, const char *name, long pid)
{
    char path[NETL_CLI_PATH_MAX];
    FILE *file;

    if (netl_cli_state_path(
            path,
            sizeof(path),
            dir,
            name,
            ".pid"
        ) != 0) {
        return 1;
    }
    file = fopen(path, "w");
    if (!file) return 1;
    fprintf(file, "%ld\n", pid);
    fclose(file);
#ifndef _WIN32
    (void)chmod(path, 0600);
#endif
    return 0;
}

/**
 * Read one resident process identifier.
 * @param dir Runtime directory.
 * @param name Listener identifier.
 * @param pid Output process identifier.
 * @return Zero on success.
 */
static int netl_cli_read_pid(
    const char *dir,
    const char *name,
    long *pid
)
{
    char path[NETL_CLI_PATH_MAX];
    FILE *file;
    long value;

    if (!pid || netl_cli_state_path(
            path,
            sizeof(path),
            dir,
            name,
            ".pid"
        ) != 0) {
        return 1;
    }
    file = fopen(path, "r");
    if (!file) return 1;
    if (fscanf(file, "%ld", &value) != 1) {
        fclose(file);
        return 1;
    }
    fclose(file);
    if (value <= 0) return 1;
    *pid = value;
    return 0;
}

/**
 * Test whether one resident process is alive.
 * @param pid Process identifier.
 * @return Nonzero when alive.
 */
static int netl_cli_pid_alive(long pid)
{
#ifdef _WIN32
    HANDLE process;
    DWORD code;

    if (pid <= 0) return 0;
    process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        (DWORD)pid
    );
    if (!process) return 0;
    if (!GetExitCodeProcess(process, &code)) {
        CloseHandle(process);
        return 0;
    }
    CloseHandle(process);
    return code == STILL_ACTIVE;
#else
    if (pid <= 0) return 0;
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}

/**
 * Stop one resident process.
 * @param pid Process identifier.
 * @return None.
 */
static void netl_cli_stop_pid(long pid)
{
#ifdef _WIN32
    HANDLE process;

    if (pid <= 0) return;
    process = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (process) {
        (void)TerminateProcess(process, 0);
        CloseHandle(process);
    }
#else
    if (pid > 0) (void)kill((pid_t)pid, SIGTERM);
#endif
}

/**
 * Wait briefly for one resident process to exit.
 * @param pid Process identifier.
 * @return None.
 */
static void netl_cli_wait_stopped(long pid)
{
    int i;

    for (i = 0; i < 50 && netl_cli_pid_alive(pid); i++) {
#ifdef _WIN32
        Sleep(20);
#else
        struct timespec delay;

        delay.tv_sec = 0;
        delay.tv_nsec = 20000000L;
        (void)nanosleep(&delay, NULL);
#endif
    }
}

/**
 * Remove registration state.
 * @param dir Runtime directory.
 * @param name Listener identifier.
 * @return None.
 */
static void netl_cli_remove_state(const char *dir, const char *name)
{
    static const char *suffixes[] = {".meta", ".pid"};
    char path[NETL_CLI_PATH_MAX];
    size_t i;

    for (i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        if (netl_cli_state_path(
                path,
                sizeof(path),
                dir,
                name,
                suffixes[i]
            ) == 0) {
            (void)remove(path);
        }
    }
}

/**
 * Record a termination signal.
 * @param sig Signal number.
 * @return None.
 */
static void netl_cli_signal(int sig)
{
    (void)sig;
    netl_cli_stop = 1;
}

#ifndef _WIN32
/**
 * Reap completed command processes.
 * @param sig Signal number.
 * @return None.
 */
static void netl_cli_reap(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}
#endif

/**
 * Sleep briefly while the listener remains active.
 * @return None.
 */
static void netl_cli_sleep(void)
{
#ifdef _WIN32
    Sleep(200);
#else
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 200000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Find one TCP command session.
 * Caller must hold the state lock.
 * @param state CLI runtime state.
 * @param peer Network peer.
 * @return Session pointer, or NULL.
 */
static netl_cli_session_t *netl_cli_find(
    netl_cli_state_t *state,
    kc_netl_peer_t *peer
)
{
    netl_cli_session_t *session;

    for (session = state->sessions; session; session = session->next) {
        if (session->peer == peer) return session;
    }
    return NULL;
}

/**
 * Remove one session from the state list.
 * Caller must hold the state lock.
 * @param state CLI runtime state.
 * @param target Session to remove.
 * @return None.
 */
static void netl_cli_unlink(
    netl_cli_state_t *state,
    netl_cli_session_t *target
)
{
    netl_cli_session_t **cursor;

    cursor = &state->sessions;
    while (*cursor) {
        if (*cursor == target) {
            *cursor = target->next;
            return;
        }
        cursor = &(*cursor)->next;
    }
}

#ifdef _WIN32
/**
 * Relay command stdout to one TCP peer.
 * @param arg Session pointer.
 * @return Thread result.
 */
static DWORD WINAPI netl_cli_tcp_output(LPVOID arg)
{
    netl_cli_session_t *session;
    char buffer[8192];
    DWORD amount;
    int release;

    session = (netl_cli_session_t *)arg;
    while (ReadFile(
            session->output_read,
            buffer,
            (DWORD)sizeof(buffer),
            &amount,
            NULL
        ) && amount != 0U) {
        netl_cli_lock(session->state);
        if (!session->active ||
                kc_netl_send(session->peer, buffer, (size_t)amount) !=
                KC_NETL_OK) {
            netl_cli_unlock(session->state);
            break;
        }
        netl_cli_unlock(session->state);
    }

    WaitForSingleObject(session->process, INFINITE);
    netl_cli_lock(session->state);
    session->output_finished = 1;
    release = !session->active;
    if (release) netl_cli_unlink(session->state, session);
    netl_cli_unlock(session->state);

    CloseHandle(session->output_read);
    CloseHandle(session->process);
    if (release) free(session);
    return 0;
}

/**
 * Start one command for one TCP peer.
 * @param state CLI runtime state.
 * @param peer Network peer.
 * @return Session pointer, or NULL.
 */
static netl_cli_session_t *netl_cli_tcp_start(
    netl_cli_state_t *state,
    kc_netl_peer_t *peer
)
{
    SECURITY_ATTRIBUTES attributes;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    HANDLE input_read;
    HANDLE input_write;
    HANDLE output_read;
    HANDLE output_write;
    netl_cli_session_t *session;
    char command[NETL_CLI_BUF + 256];

    memset(&attributes, 0, sizeof(attributes));
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    input_read = NULL;
    input_write = NULL;
    output_read = NULL;
    output_write = NULL;

    if (!CreatePipe(&input_read, &input_write, &attributes, 0)) return NULL;
    if (!CreatePipe(&output_read, &output_write, &attributes, 0)) {
        CloseHandle(input_read);
        CloseHandle(input_write);
        return NULL;
    }
    SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_read;
    startup.hStdOutput = output_write;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if ((size_t)snprintf(command, sizeof(command),
            "cmd.exe /c %s", state->command) >= sizeof(command)) {
        CloseHandle(input_read);
        CloseHandle(input_write);
        CloseHandle(output_read);
        CloseHandle(output_write);
        return NULL;
    }

    if (!CreateProcessA(
            NULL,
            command,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &startup,
            &process
        )) {
        CloseHandle(input_read);
        CloseHandle(input_write);
        CloseHandle(output_read);
        CloseHandle(output_write);
        return NULL;
    }

    CloseHandle(process.hThread);
    CloseHandle(input_read);
    CloseHandle(output_write);
    session = (netl_cli_session_t *)calloc(1, sizeof(*session));
    if (!session) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        CloseHandle(input_write);
        CloseHandle(output_read);
        return NULL;
    }

    session->peer = peer;
    session->state = state;
    session->active = 1;
    session->process = process.hProcess;
    session->input_write = input_write;
    session->output_read = output_read;
    session->output_thread = CreateThread(
        NULL,
        0,
        netl_cli_tcp_output,
        session,
        0,
        NULL
    );
    if (!session->output_thread) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        CloseHandle(input_write);
        CloseHandle(output_read);
        free(session);
        return NULL;
    }
    CloseHandle(session->output_thread);
    session->output_thread = NULL;

    netl_cli_lock(state);
    session->next = state->sessions;
    state->sessions = session;
    netl_cli_unlock(state);
    return session;
}

/**
 * Dispatch one UDP datagram to a command.
 * @param state CLI runtime state.
 * @param data Datagram bytes.
 * @param size Datagram size.
 * @return Zero on success, nonzero on failure.
 */
static int netl_cli_udp_start(
    netl_cli_state_t *state,
    const void *data,
    size_t size,
    const char *host,
    unsigned short port
)
{
    SECURITY_ATTRIBUTES attributes;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    HANDLE input_read;
    HANDLE input_write;
    char command[NETL_CLI_BUF + 256];
    DWORD written;

    memset(&attributes, 0, sizeof(attributes));
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    input_read = NULL;
    input_write = NULL;
    written = 0U;

    if (!CreatePipe(&input_read, &input_write, &attributes, 0)) return 1;
    SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_read;
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if ((size_t)snprintf(command, sizeof(command),
            "cmd.exe /c %s %s %u", state->command, host,
            (unsigned)port) >= sizeof(command)) {
        CloseHandle(input_read);
        CloseHandle(input_write);
        return 1;
    }

    if (!CreateProcessA(
            NULL,
            command,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &startup,
            &process
        )) {
        CloseHandle(input_read);
        CloseHandle(input_write);
        return 1;
    }

    CloseHandle(process.hThread);
    CloseHandle(input_read);
    if (size != 0U) {
        if (!WriteFile(input_write, data, (DWORD)size, &written, NULL) ||
                written != (DWORD)size) {
            CloseHandle(input_write);
            CloseHandle(process.hProcess);
            return 1;
        }
    }
    CloseHandle(input_write);
    CloseHandle(process.hProcess);
    return 0;
}
#else
/**
 * Relay command stdout to one TCP peer.
 * @param arg Session pointer.
 * @return Thread result.
 */
static void *netl_cli_tcp_output(void *arg)
{
    netl_cli_session_t *session;
    char buffer[8192];
    ssize_t amount;
    int release;

    session = (netl_cli_session_t *)arg;
    while ((amount = read(session->output_fd, buffer, sizeof(buffer))) > 0) {
        netl_cli_lock(session->state);
        if (!session->active ||
                kc_netl_send(session->peer, buffer, (size_t)amount) !=
                KC_NETL_OK) {
            netl_cli_unlock(session->state);
            break;
        }
        netl_cli_unlock(session->state);
    }

    (void)waitpid(session->pid, NULL, 0);
    netl_cli_lock(session->state);
    session->output_finished = 1;
    release = !session->active;
    if (release) netl_cli_unlink(session->state, session);
    netl_cli_unlock(session->state);

    (void)close(session->output_fd);
    if (release) free(session);
    return NULL;
}

/**
 * Start one command for one TCP peer.
 * @param state CLI runtime state.
 * @param peer Network peer.
 * @return Session pointer, or NULL.
 */
static netl_cli_session_t *netl_cli_tcp_start(
    netl_cli_state_t *state,
    kc_netl_peer_t *peer
)
{
    int input_pipe[2];
    int output_pipe[2];
    pid_t pid;
    netl_cli_session_t *session;

    if (pipe(input_pipe) != 0) return NULL;
    if (pipe(output_pipe) != 0) {
        (void)close(input_pipe[0]);
        (void)close(input_pipe[1]);
        return NULL;
    }

    pid = fork();
    if (pid < 0) {
        (void)close(input_pipe[0]);
        (void)close(input_pipe[1]);
        (void)close(output_pipe[0]);
        (void)close(output_pipe[1]);
        return NULL;
    }
    if (pid == 0) {
        (void)close(input_pipe[1]);
        (void)close(output_pipe[0]);
        if (dup2(input_pipe[0], STDIN_FILENO) < 0 ||
                dup2(output_pipe[1], STDOUT_FILENO) < 0) {
            _exit(1);
        }
        if (input_pipe[0] > STDERR_FILENO) (void)close(input_pipe[0]);
        if (output_pipe[1] > STDERR_FILENO) (void)close(output_pipe[1]);
        execl("/bin/sh", "sh", "-c", state->command, (char *)NULL);
        _exit(1);
    }

    (void)close(input_pipe[0]);
    (void)close(output_pipe[1]);
    session = (netl_cli_session_t *)calloc(1, sizeof(*session));
    if (!session) {
        (void)close(input_pipe[1]);
        (void)close(output_pipe[0]);
        (void)kill(pid, SIGTERM);
        (void)waitpid(pid, NULL, 0);
        return NULL;
    }

    session->peer = peer;
    session->state = state;
    session->active = 1;
    session->pid = pid;
    session->input_fd = input_pipe[1];
    session->output_fd = output_pipe[0];
    if (pthread_create(
            &session->output_thread,
            NULL,
            netl_cli_tcp_output,
            session
        ) != 0) {
        (void)close(session->input_fd);
        (void)close(session->output_fd);
        (void)kill(pid, SIGTERM);
        (void)waitpid(pid, NULL, 0);
        free(session);
        return NULL;
    }
    (void)pthread_detach(session->output_thread);

    netl_cli_lock(state);
    session->next = state->sessions;
    state->sessions = session;
    netl_cli_unlock(state);
    return session;
}

/**
 * Dispatch one UDP datagram to a command.
 * @param state CLI runtime state.
 * @param data Datagram bytes.
 * @param size Datagram size.
 * @return Zero on success, nonzero on failure.
 */
static int netl_cli_udp_start(
    netl_cli_state_t *state,
    const void *data,
    size_t size,
    const char *host,
    unsigned short port
)
{
    int pipefd[2];
    pid_t pid;
    size_t offset;
    char shell_command[NETL_CLI_BUF+256];

    if((size_t)snprintf(shell_command,sizeof(shell_command),
        "%s %s %u",state->command,host,(unsigned)port)>=sizeof(shell_command))
        return 1;
    if (pipe(pipefd) != 0) return 1;
    pid = fork();
    if (pid < 0) {
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        return 1;
    }
    if (pid == 0) {
        (void)close(pipefd[1]);
        if (dup2(pipefd[0], STDIN_FILENO) < 0) _exit(1);
        if (pipefd[0] > STDERR_FILENO) (void)close(pipefd[0]);
        execl("/bin/sh", "sh", "-c", shell_command, (char *)NULL);
        _exit(1);
    }

    (void)close(pipefd[0]);
    offset = 0U;
    while (offset < size) {
        ssize_t written;

        written = write(
            pipefd[1],
            (const unsigned char *)data + offset,
            size - offset
        );
        if (written <= 0) break;
        offset += (size_t)written;
    }
    (void)close(pipefd[1]);
    return offset == size ? 0 : 1;
}
#endif

/**
 * Deliver incoming network data to the configured command.
 * @param input Network input.
 * @param userdata CLI runtime state.
 * @return None.
 */
static void netl_cli_receive(kc_netl_peer_t *peer, const void *data,
    size_t data_size, void *userdata)
{
    netl_cli_state_t *state;
    netl_cli_session_t *session;

    state = (netl_cli_state_t *)userdata;
    if (!peer || !state || !state->command || !*state->command) return;
    if (kc_netl_peer_protocol(peer) == KC_NETL_UDP) {
        (void)netl_cli_udp_start(state, data, data_size,
            kc_netl_peer_host(peer), kc_netl_peer_port(peer));
        return;
    }

    netl_cli_lock(state);
    session = netl_cli_find(state, peer);
    netl_cli_unlock(state);
    if (!session) session = netl_cli_tcp_start(state, peer);
    if (!session) {
        kc_netl_peer_close(peer);
        return;
    }

#ifdef _WIN32
    {
        DWORD written;
        size_t offset = 0U;

        while (offset < data_size) {
            if (!WriteFile(
                    session->input_write,
                    (const unsigned char *)data + offset,
                    (DWORD)(data_size - offset),
                    &written,
                    NULL
                ) || written == 0U) {
                kc_netl_peer_close(peer);
                return;
            }
            offset += (size_t)written;
        }
    }
#else
    {
        size_t offset = 0U;

        while (offset < data_size) {
            ssize_t written;

            written = write(
                session->input_fd,
                (const unsigned char *)data + offset,
                data_size - offset
            );
            if (written <= 0) {
                kc_netl_peer_close(peer);
                return;
            }
            offset += (size_t)written;
        }
    }
#endif
}

/**
 * Close the command input associated with one TCP peer.
 * @param peer Closed network peer.
 * @param userdata CLI runtime state.
 * @return None.
 */
static void netl_cli_disconnect(kc_netl_peer_t *peer, void *userdata)
{
    netl_cli_state_t *state;
    netl_cli_session_t *session;
    int release;

    state = (netl_cli_state_t *)userdata;
    if (!state) return;
    if(state->outgoing==peer){
        netl_cli_stop=1;
        return;
    }
    netl_cli_lock(state);
    session = netl_cli_find(state, peer);
    if (!session) {
        netl_cli_unlock(state);
        return;
    }
    session->active = 0;
#ifdef _WIN32
    if (session->input_write) {
        CloseHandle(session->input_write);
        session->input_write = NULL;
    }
#else
    if (session->input_fd >= 0) {
        (void)close(session->input_fd);
        session->input_fd = -1;
    }
#endif
    release = session->output_finished;
    if (release) netl_cli_unlink(state, session);
    netl_cli_unlock(state);
    if (release) free(session);
}

/**
 * Stop after one asynchronous listener error.
 * @param status libnetl status.
 * @param userdata Unused CLI state.
 * @return None.
 */
static void netl_cli_error(int status, void *userdata)
{
    (void)status;
    (void)userdata;
    netl_cli_stop = 1;
}

/**
 * Release remaining command sessions after listener close.
 * @param state CLI runtime state.
 * @return None.
 */
static void netl_cli_cleanup(netl_cli_state_t *state)
{
    netl_cli_session_t *session;

    netl_cli_lock(state);
    session = state->sessions;
    state->sessions = NULL;
    while (session) {
        netl_cli_session_t *next = session->next;

        session->active = 0;
#ifdef _WIN32
        if (session->input_write) CloseHandle(session->input_write);
        if (session->process) TerminateProcess(session->process, 0);
#else
        if (session->input_fd >= 0) (void)close(session->input_fd);
        if (session->pid > 0) (void)kill(session->pid, SIGTERM);
#endif
        session = next;
    }
    netl_cli_unlock(state);
}

/**
 * Drains queued outgoing send requests for one resident endpoint.
 * @param dir Runtime directory.
 * @param name Listener identifier.
 * @param outgoing Outbound peer.
 * @param protocol Protocol of the endpoint.
 * @return void.
 */
static void netl_cli_drain_sends(
    const char *dir,
    const char *name,
    kc_netl_peer_t *outgoing,
    int protocol
);

/**
 * Run one resident listener.
 * @param dir Runtime directory.
 * @param name Listener identifier.
 * @return Process status.
 */
static int netl_cli_serve(const char *dir, const char *name)
{
    netl_cli_registration_t registration;
    kc_netl_options_t options;
    kc_netl_t *listener;
    netl_cli_state_t state;
    int rc;

    if (netl_cli_read_registration(dir, name, &registration) != 0) return 1;

    memset(&state, 0, sizeof(state));
    state.command = registration.command;
#ifdef _WIN32
    InitializeCriticalSection(&state.lock);
#else
    if (pthread_mutex_init(&state.lock, NULL) != 0) return 1;
    (void)signal(SIGINT, netl_cli_signal);
    (void)signal(SIGTERM, netl_cli_signal);
    (void)signal(SIGPIPE, SIG_IGN);
    (void)signal(SIGCHLD, netl_cli_reap);
#endif

    memset(&options, 0, sizeof(options));
    options.host=registration.host;
    options.port = registration.port;
    options.protocol = registration.protocol;
    listener = NULL;
    if(registration.outbound){
        kc_netl_connect_options_t remote;
        memset(&remote,0,sizeof(remote));
        remote.host=registration.host;
        remote.port=registration.port;
        remote.protocol=registration.protocol;
        remote.local_host=registration.local_host[0]?
            registration.local_host:NULL;
        remote.local_port=registration.local_port;
        rc=kc_netl_connect(&listener,&state.outgoing,&remote,
            netl_cli_receive,netl_cli_disconnect,netl_cli_error,&state);
    }else{
        rc=kc_netl_open(&listener,&options,netl_cli_receive,
            netl_cli_disconnect,netl_cli_error,&state);
    }
    if (rc != KC_NETL_OK) {
#ifdef _WIN32
        DeleteCriticalSection(&state.lock);
#else
        (void)pthread_mutex_destroy(&state.lock);
#endif
        return 1;
    }

#ifdef _WIN32
    (void)signal(SIGINT, netl_cli_signal);
    (void)signal(SIGTERM, netl_cli_signal);
#endif
    while (!netl_cli_stop) {
        if(registration.outbound&&!netl_cli_stop)
            netl_cli_drain_sends(dir,name,state.outgoing,registration.protocol);
        netl_cli_sleep();
    }

    kc_netl_close(listener);
    netl_cli_cleanup(&state);
#ifdef _WIN32
    DeleteCriticalSection(&state.lock);
#else
    (void)pthread_mutex_destroy(&state.lock);
#endif
    return 0;
}

/**
 * Submits stdin through private IPC to an already resident outbound endpoint.
 * The worker owns the persistent socket and each IPC file carries exactly one
 * UDP datagram or the complete TCP byte stream.
 * @param dir Runtime directory.
 * @param name Listener identifier.
 * @return Zero on success, non-zero on failure.
 */
static int netl_cli_send(const char *dir, const char *name)
{
    char path[NETL_CLI_PATH_MAX],pending[NETL_CLI_PATH_MAX];
    char suffix[64],partial[64];
    netl_cli_registration_t registration;
    unsigned char buffer[8192];
    FILE *file;
    size_t size,total=0U;
    long worker;
    unsigned long process_id;
    if(netl_cli_read_registration(dir,name,&registration)!=0||
        !registration.outbound||
        netl_cli_read_pid(dir,name,&worker)!=0||
        !netl_cli_pid_alive(worker))return 1;
#ifdef _WIN32
    process_id=(unsigned long)GetCurrentProcessId();
    if(_setmode(_fileno(stdin),_O_BINARY)==-1)return 1;
#else
    process_id=(unsigned long)getpid();
#endif
    snprintf(suffix,sizeof(suffix),".send.%lu",process_id);
    snprintf(partial,sizeof(partial),".pending.%lu",process_id);
    if(netl_cli_state_path(path,sizeof(path),dir,name,suffix)!=0||
        netl_cli_state_path(pending,sizeof(pending),dir,name,partial)!=0)
        return 1;
    file=fopen(pending,"wb");
    if(!file)return 1;
#ifndef _WIN32
    (void)chmod(pending,0600);
#endif
    while((size=fread(buffer,1,sizeof(buffer),stdin))!=0U){
        if((registration.protocol==KC_NETL_UDP&&
            (total>65507U||size>65507U-total))||
            fwrite(buffer,1,size,file)!=size){
            fclose(file);remove(pending);return 1;
        }
        total+=size;
    }
    if(ferror(stdin)||fflush(file)!=0||fclose(file)!=0||
        rename(pending,path)!=0){
        remove(pending);return 1;
    }
    return 0;
}

/**
 * Delivers a complete local IPC request through the existing outbound peer.
 * UDP maps each file to one datagram, TCP queues its ordered byte stream.
 * @param outgoing Outbound peer.
 * @param protocol Protocol of the endpoint.
 * @param path IPC file path.
 * @return Zero on success, non-zero on failure.
 */
static int netl_cli_process_send(
    kc_netl_peer_t *outgoing,
    int protocol,
    const char *path
)
{
    FILE *file=fopen(path,"rb");
    unsigned char buffer[65508];
    size_t size;
    int rc=0;
    if(!file||!outgoing){if(file)fclose(file);return 1;}
    if(protocol==KC_NETL_UDP){
        size=fread(buffer,1,sizeof(buffer),file);
        if(ferror(file)||!feof(file)||size>65507U||
            kc_netl_send(outgoing,buffer,size)!=KC_NETL_OK)rc=1;
    }else{
        while((size=fread(buffer,1,sizeof(buffer),file))!=0U){
            if(kc_netl_send(outgoing,buffer,size)!=KC_NETL_OK){
                rc=1;break;
            }
        }
        if(ferror(file))rc=1;
    }
    fclose(file);
    return rc;
}

/**
 * Drains complete outgoing send requests for one resident endpoint.
 * @param dir Runtime directory.
 * @param name Listener identifier.
 * @param outgoing Outbound peer.
 * @param protocol Protocol of the endpoint.
 * @return void.
 */
static void netl_cli_drain_sends(
    const char *dir,
    const char *name,
    kc_netl_peer_t *outgoing,
    int protocol
)
{
    char prefix[NETL_CLI_NAME_MAX+16];
    size_t prefix_size;
    snprintf(prefix,sizeof(prefix),"%s.send.",name);
    prefix_size=strlen(prefix);
#ifdef _WIN32
    {
        char pattern[NETL_CLI_PATH_MAX];
        WIN32_FIND_DATAA entry;
        HANDLE scan;
        if (
            (size_t)snprintf(pattern, sizeof(pattern), "%s\\%s*", dir, prefix) >=
            sizeof(pattern)
        ) {
            return;
        }
        scan=FindFirstFileA(pattern,&entry);
        if(scan==INVALID_HANDLE_VALUE)return;
        do{
            char path[NETL_CLI_PATH_MAX];
            if(strncmp(entry.cFileName,prefix,prefix_size)!=0)continue;
            if((size_t)snprintf(path,sizeof(path),"%s\\%s",
                dir,entry.cFileName)>=sizeof(path))continue;
            (void)netl_cli_process_send(outgoing,protocol,path);
            (void)remove(path);
        }while(FindNextFileA(scan,&entry));
        FindClose(scan);
    }
#else
    {
        DIR *scan=opendir(dir);
        struct dirent *entry;
        if(!scan)return;
        while((entry=readdir(scan))!=NULL){
            char path[NETL_CLI_PATH_MAX];
            if(strncmp(entry->d_name,prefix,prefix_size)!=0)continue;
            if((size_t)snprintf(path,sizeof(path),"%s/%s",
                dir,entry->d_name)>=sizeof(path))continue;
            (void)netl_cli_process_send(outgoing,protocol,path);
            (void)remove(path);
        }
        closedir(scan);
    }
#endif
}

/**
 * Removes stale queued UDP reply files when a listener is deleted.
 * @param dir Private resident directory.
 * @param name Listener identifier.
 * @return void.
 */
static void netl_cli_purge_sends(const char *dir,const char *name)
{
    char first[NETL_CLI_NAME_MAX+16],second[NETL_CLI_NAME_MAX+16];
    size_t a,b;
    snprintf(first,sizeof(first),"%s.send.",name);
    snprintf(second,sizeof(second),"%s.pending.",name);
    a=strlen(first);b=strlen(second);
#ifdef _WIN32
    {
        char pattern[NETL_CLI_PATH_MAX];
        WIN32_FIND_DATAA entry;
        HANDLE scan;
        if (
            (size_t)snprintf(pattern, sizeof(pattern), "%s\\*", dir) >=
            sizeof(pattern)
        ) {
            return;
        }
        scan=FindFirstFileA(pattern,&entry);
        if(scan==INVALID_HANDLE_VALUE)return;
        do{
            char path[NETL_CLI_PATH_MAX];
            if (
                strncmp(entry.cFileName, first, a) != 0 &&
                strncmp(entry.cFileName, second, b) != 0
            ) {
                continue;
            }
            if((size_t)snprintf(path,sizeof(path),"%s\\%s",
                dir,entry.cFileName)>=sizeof(path))continue;
            (void)remove(path);
        }while(FindNextFileA(scan,&entry));
        FindClose(scan);
    }
#else
    {
        DIR *scan=opendir(dir);
        struct dirent *entry;
        if(!scan)return;
        while((entry=readdir(scan))!=NULL){
            char path[NETL_CLI_PATH_MAX];
            if(strncmp(entry->d_name,first,a)!=0&&
                strncmp(entry->d_name,second,b)!=0)continue;
            if((size_t)snprintf(path,sizeof(path),"%s/%s",
                dir,entry->d_name)>=sizeof(path))continue;
            (void)remove(path);
        }
        closedir(scan);
    }
#endif
}

/**
 * Delete one registration and stop its resident process.
 * @param dir Runtime directory.
 * @param name Listener identifier.
 * @return Zero on success.
 */
static int netl_cli_delete(const char *dir, const char *name)
{
    long pid;

    if (netl_cli_read_pid(dir, name, &pid) == 0) {
        netl_cli_stop_pid(pid);
        netl_cli_wait_stopped(pid);
    }
    netl_cli_remove_state(dir, name);
    netl_cli_purge_sends(dir,name);
    return 0;
}

/**
 * Start or replace one resident listener.
 * @param dir Runtime directory.
 * @param registration Registration data.
 * @return Zero on success.
 */
static int netl_cli_create(
    const char *dir,
    const netl_cli_registration_t *registration
)
{
    long old_pid;

    if (netl_cli_ensure_dir(dir) != 0) return 1;
    if (netl_cli_read_pid(dir, registration->name, &old_pid) == 0) {
        netl_cli_stop_pid(old_pid);
        netl_cli_wait_stopped(old_pid);
    }
    if (netl_cli_write_registration(dir, registration) != 0) return 1;

#ifdef _WIN32
    {
        char executable[NETL_CLI_PATH_MAX];
        char command[NETL_CLI_PATH_MAX + 8];
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        DWORD size;

        size = GetModuleFileNameA(
            NULL,
            executable,
            (DWORD)sizeof(executable)
        );
        if (size == 0 || size >= (DWORD)sizeof(executable)) {
            netl_cli_remove_state(dir, registration->name);
            return 1;
        }
        if ((size_t)snprintf(
                command,
                sizeof(command),
                "\"%s\"",
                executable
            ) >= sizeof(command)) {
            netl_cli_remove_state(dir, registration->name);
            return 1;
        }

        if (!SetEnvironmentVariableA(
                "KC_NETL_WORKER",
                registration->name
            )) {
            netl_cli_remove_state(dir, registration->name);
            return 1;
        }
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);

        if (!CreateProcessA(
                NULL,
                command,
                NULL,
                NULL,
                FALSE,
                DETACHED_PROCESS | CREATE_NO_WINDOW,
                NULL,
                NULL,
                &startup,
                &process
            )) {
            (void)SetEnvironmentVariableA("KC_NETL_WORKER", NULL);
            netl_cli_remove_state(dir, registration->name);
            return 1;
        }
        (void)SetEnvironmentVariableA("KC_NETL_WORKER", NULL);
        CloseHandle(process.hThread);
        if (netl_cli_write_pid(
                dir,
                registration->name,
                (long)process.dwProcessId
            ) != 0) {
            (void)TerminateProcess(process.hProcess, 0);
            CloseHandle(process.hProcess);
            netl_cli_remove_state(dir, registration->name);
            return 1;
        }
        CloseHandle(process.hProcess);
    }
#else
    {
        pid_t pid = fork();

        if (pid < 0) {
            netl_cli_remove_state(dir, registration->name);
            return 1;
        }
        if (pid == 0) {
            int null_fd;

            if (setsid() < 0) _exit(1);
            null_fd = open("/dev/null", O_RDWR);
            if (null_fd >= 0) {
                (void)dup2(null_fd, STDIN_FILENO);
                (void)dup2(null_fd, STDOUT_FILENO);
                (void)dup2(null_fd, STDERR_FILENO);
                if (null_fd > STDERR_FILENO) (void)close(null_fd);
            }
            _exit(netl_cli_serve(dir, registration->name));
        }
        if (netl_cli_write_pid(
                dir,
                registration->name,
                (long)pid
            ) != 0) {
            (void)kill(pid, SIGTERM);
            netl_cli_remove_state(dir, registration->name);
            return 1;
        }
    }
#endif
    return 0;
}

/**
 * Print one listener registration.
 * @param registration Registration data.
 * @return None.
 */
static void netl_cli_print(const netl_cli_registration_t *registration)
{
    printf(
        "%s\t%u\t%s\t%s\n",
        registration->name,
        (unsigned int)registration->port,
        registration->protocol == KC_NETL_UDP ? "udp" : "tcp",
        registration->command
    );
}

/**
 * List one listener or all registered listeners.
 * @param dir Runtime directory.
 * @param filter Optional listener identifier.
 * @return Zero on success.
 */
static int netl_cli_list(const char *dir, const char *filter)
{
    netl_cli_registration_t registration;

    if (filter) {
        if (netl_cli_read_registration(
                dir,
                filter,
                &registration
            ) != 0) {
            return 0;
        }
        netl_cli_print(&registration);
        return 0;
    }

#ifdef _WIN32
    {
        WIN32_FIND_DATAA data;
        HANDLE find;
        char pattern[NETL_CLI_PATH_MAX];

        if ((size_t)snprintf(
                pattern,
                sizeof(pattern),
                "%s\\*.meta",
                dir
            ) >= sizeof(pattern)) {
            return 1;
        }
        find = FindFirstFileA(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) return 0;
        do {
            char name[NETL_CLI_NAME_MAX];
            size_t size = strlen(data.cFileName);

            if (size <= 5U ||
                    strcmp(data.cFileName + size - 5U, ".meta") != 0) {
                continue;
            }
            if (size - 5U >= sizeof(name)) continue;
            memcpy(name, data.cFileName, size - 5U);
            name[size - 5U] = '\0';
            if (netl_cli_read_registration(
                    dir,
                    name,
                    &registration
                ) == 0) {
                netl_cli_print(&registration);
            }
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
#else
    {
        DIR *directory;
        struct dirent *entry;

        directory = opendir(dir);
        if (!directory) return 0;
        while ((entry = readdir(directory)) != NULL) {
            char name[NETL_CLI_NAME_MAX];
            size_t size = strlen(entry->d_name);

            if (size <= 5U ||
                    strcmp(entry->d_name + size - 5U, ".meta") != 0) {
                continue;
            }
            if (size - 5U >= sizeof(name)) continue;
            memcpy(name, entry->d_name, size - 5U);
            name[size - 5U] = '\0';
            if (netl_cli_read_registration(
                    dir,
                    name,
                    &registration
                ) == 0) {
                netl_cli_print(&registration);
            }
        }
        closedir(directory);
    }
#endif
    return 0;
}

/**
 * Execute the command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv)
{
    netl_cli_registration_t registration;
    char dir[NETL_CLI_PATH_MAX];
    const char *worker;
    const char *name;
    const char *port_text;
    char command[NETL_CLI_BUF];
    int protocol;
    int list;
    int remove;
    int positional;
    int i;

    if (netl_cli_default_dir(dir, sizeof(dir)) != 0) return 1;
    worker = getenv("KC_NETL_WORKER");
    if (worker && worker[0]) return netl_cli_serve(dir, worker);

    if (argc == 2 &&
            (strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "--help") == 0)) {
        netl_cli_help();
        return 0;
    }
    if (argc == 2 &&
            (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0)) {
        netl_cli_version();
        return 0;
    }

    name = NULL;
    port_text = NULL;
    command[0] = '\0';
    protocol = KC_NETL_TCP;
    list = 0;
    remove = 0;
    positional = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 ||
                strcmp(argv[i], "--help") == 0) {
            netl_cli_help();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 ||
                strcmp(argv[i], "--version") == 0) {
            netl_cli_version();
            return 0;
        } else if (strcmp(argv[i], "-l") == 0 ||
                strcmp(argv[i], "--list") == 0) {
            list = 1;
        } else if (strcmp(argv[i], "-d") == 0 ||
                strcmp(argv[i], "--delete") == 0) {
            remove = 1;
        } else if (strcmp(argv[i], "-t") == 0 ||
                strcmp(argv[i], "--transport") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "netl: --transport requires tcp or udp\n");
                return 1;
            }
            if (strcmp(argv[i], "tcp") == 0) {
                protocol = KC_NETL_TCP;
            } else if (strcmp(argv[i], "udp") == 0) {
                protocol = KC_NETL_UDP;
            } else {
                fprintf(stderr, "netl: --transport requires tcp or udp\n");
                return 1;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "netl: unknown option: %s\n", argv[i]);
            return 1;
        } else if (positional == 0) {
            name = argv[i];
            positional++;
        } else if (positional == 1) {
            port_text = argv[i];
            positional++;
        } else {
            if (netl_cli_append(command, sizeof(command), argv[i]) != 0) {
                fprintf(stderr, "netl: command is too long\n");
                return 1;
            }
            positional++;
        }
    }

    if (list) {
        if (remove || port_text || command[0] || protocol != KC_NETL_TCP) {
            fprintf(stderr, "netl: invalid list arguments\n");
            return 1;
        }
        if (name && !netl_cli_name_valid(name)) {
            fprintf(stderr, "netl: invalid id: %s\n", name);
            return 1;
        }
        return netl_cli_list(dir, name);
    }

    if (!name) {
        fprintf(stderr, "netl: missing id\n");
        return 1;
    }
    if (!netl_cli_name_valid(name)) {
        fprintf(stderr, "netl: invalid id: %s\n", name);
        return 1;
    }

    if (remove) {
        if (port_text || command[0] || protocol != KC_NETL_TCP) {
            fprintf(stderr, "netl: invalid delete arguments\n");
            return 1;
        }
        return netl_cli_delete(dir, name);
    }

    if(!port_text){
        if(command[0]||protocol!=KC_NETL_TCP)return 1;
        return netl_cli_send(dir,name);
    }
    memset(&registration,0,sizeof(registration));
    snprintf(registration.name,sizeof(registration.name),"%s",name);
    if(!netl_cli_endpoint(port_text,registration.host,
        sizeof(registration.host),&registration.port)){
        fprintf(stderr,"netl: invalid endpoint: %s\n",port_text);
        return 1;
    }
    registration.protocol=protocol;
    /* Wildcard interfaces bind locally; specific remote endpoints connect. */
    /* UDP owns one bidirectional socket regardless of destination address. */
    registration.outbound=protocol==KC_NETL_UDP||
        (strcmp(registration.host,"0.0.0.0")!=0&&
         strcmp(registration.host,"::")!=0);
    if(command[0]){
        if(!netl_cli_line_valid(command)){
            fprintf(stderr,"netl: invalid command\n");return 1;
        }
        snprintf(registration.command,sizeof(registration.command),"%s",command);
    }
    return netl_cli_create(dir, &registration);
}
