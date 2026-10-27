/**
 * nets.c - Persistent outbound network relay command-line interface.
 * Summary: Relays binary standard streams through one TCP or UDP endpoint.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnets.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <conio.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#endif

#define NETS_CLI_BUFFER_SIZE 16384U
#define NETS_CLI_STDIN_NONE 0
#define NETS_CLI_STDIN_READY 1
#define NETS_CLI_STDIN_CLOSED 2

typedef struct {
    const char *host;
    unsigned short port;
    int protocol;
    const char *local_host;
    unsigned short local_port;
} nets_cli_options_t;

typedef struct {
    atomic_int terminal;
    atomic_int error_status;
    atomic_int io_failed;
} nets_cli_state_t;

static volatile sig_atomic_t nets_cli_stop;

/**
 * Prints command usage.
 * @param name Program name.
 * @return void.
 */
static void nets_cli_help(const char *name)
{
    printf("Usage: %s <host> <port> [options]\n", name);
    printf("       %s <id> -s [host port]\\n",name);
    printf("       %s <id> -d\\n",name);
    printf("       %s -l [id]\\n",name);
    printf("\n");
    printf("Options:\n");
    printf("  -t, --transport <type>  tcp or udp (default: tcp)\n");
    printf("  --local-host <host>     UDP source bind address\n");
    printf("  --local-port <port>     UDP source port (default: 0)\n");
    printf("  --id <id>                Register resident UDP sender\\n");
    printf("  --receive <cmd>          Run command for incoming datagrams\\n");
    printf("  -s, --send               Send stdin through resident endpoint\\n");
    printf("  -l, --list               List resident endpoints\\n");
    printf("  -d, --delete             Remove resident endpoint\\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --version           Show version\n");
}

/**
 * Prints the linked libnets build version.
 * @return void.
 */
static void nets_cli_version(void)
{
    printf("%llu\n", (unsigned long long)kc_nets_version());
}

/**
 * Prints one CLI error.
 * @param message Error message.
 * @return One.
 */
static int nets_cli_error(const char *message)
{
    fprintf(stderr, "nets: %s\n", message);
    return 1;
}

/**
 * Prints one CLI error with a value.
 * @param message Error prefix.
 * @param value Error value.
 * @return One.
 */
static int nets_cli_error_value(const char *message, const char *value)
{
    fprintf(stderr, "nets: %s%s\n", message, value ? value : "");
    return 1;
}

/**
 * Parses one nonzero network port.
 * @param text Port text.
 * @param out Parsed port.
 * @return Nonzero on success.
 */
static int nets_cli_parse_port(const char *text, unsigned short *out)
{
    unsigned long value;
    char *end;

    if (!text || !text[0] || !out || text[0] == '-') return 0;
    errno = 0;
    end = NULL;
    value = strtoul(text, &end, 10);
    if (errno || !end || *end != '\0' || value == 0UL || value > 65535UL) {
        return 0;
    }
    *out = (unsigned short)value;
    return 1;
}

/**
 * Reads one required option value.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param index Current option index, advanced on success.
 * @param option Option name for diagnostics.
 * @param out Destination value.
 * @return Zero on success.
 */
static int nets_cli_option_value(
    int argc,
    char **argv,
    int *index,
    const char *option,
    const char **out
)
{
    if (*index + 1 >= argc || argv[*index + 1][0] == '\0') {
        fprintf(stderr, "nets: %s requires a value\n", option);
        return 1;
    }
    *index += 1;
    *out = argv[*index];
    return 0;
}

/**
 * Parses the outbound endpoint command contract.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param options Parsed options.
 * @return Zero on success.
 */
static int nets_cli_parse(
    int argc,
    char **argv,
    nets_cli_options_t *options
)
{
    const char *positionals[2];
    size_t positional_count;
    int index;

    if (!options) return nets_cli_error("invalid parser state");
    memset(options, 0, sizeof(*options));
    options->protocol = KC_NETS_TCP;
    positional_count = 0U;

    for (index = 1; index < argc; index++) {
        const char *argument;

        argument = argv[index];
        if (
            strcmp(argument, "-t") == 0 ||
            strcmp(argument, "--transport") == 0
        ) {
            const char *value;

            value = NULL;
            if (
                nets_cli_option_value(
                    argc,
                    argv,
                    &index,
                    argument,
                    &value
                ) != 0
            ) {
                return 1;
            }
            if (strcmp(value, "tcp") == 0) {
                options->protocol = KC_NETS_TCP;
            } else if (strcmp(value, "udp") == 0) {
                options->protocol = KC_NETS_UDP;
            } else {
                return nets_cli_error_value("invalid transport: ", value);
            }
        } else if (strcmp(argument,"--local-host")==0) {
            if(nets_cli_option_value(argc,argv,&index,argument,
                &options->local_host)!=0)return 1;
        } else if (strcmp(argument,"--local-port")==0) {
            const char *value=NULL;
            char *end=NULL;
            unsigned long port;
            if(nets_cli_option_value(argc,argv,&index,argument,&value)!=0)return 1;
            errno=0;
            port=strtoul(value,&end,10);
            if(errno||!end||!*value||*end||port>65535UL)
                return nets_cli_error_value("invalid local port: ",value);
            options->local_port=(unsigned short)port;
        } else if (argument[0] == '-') {
            return nets_cli_error_value("unknown option: ", argument);
        } else {
            if (
                positional_count >=
                sizeof(positionals) / sizeof(positionals[0])
            ) {
                return nets_cli_error("too many positional arguments");
            }
            positionals[positional_count++] = argument;
        }
    }

    if (positional_count != 2U) {
        return nets_cli_error("requires <host> <port>");
    }
    if (!positionals[0][0]) return nets_cli_error("host must not be empty");
    if (!nets_cli_parse_port(positionals[1], &options->port)) {
        return nets_cli_error_value("invalid port: ", positionals[1]);
    }
    options->host = positionals[0];
    if(options->protocol!=KC_NETS_UDP &&
        (options->local_host||options->local_port))
        return nets_cli_error("local UDP options require -t udp");
    return 0;
}

/**
 * Writes all received bytes to standard output.
 * @param data Network bytes.
 * @param size Byte count.
 * @return Zero on success.
 */
static int nets_cli_write_stdout(const void *data, size_t size)
{
    const unsigned char *bytes;
    size_t offset;

    bytes = (const unsigned char *)data;
    offset = 0U;
    while (offset < size) {
        size_t part;
#ifdef _WIN32
        int result;
#else
        ssize_t result;
#endif

        part = size - offset;
        if (part > (size_t)INT_MAX) part = (size_t)INT_MAX;
#ifdef _WIN32
        result = _write(_fileno(stdout), bytes + offset, (unsigned int)part);
        if (result < 0) return 1;
#else
        result = write(STDOUT_FILENO, bytes + offset, part);
        if (result < 0) {
            if (errno == EINTR) continue;
            return 1;
        }
#endif
        if (result <= 0) return 1;
        offset += (size_t)result;
    }
    return 0;
}

/**
 * Reads one binary chunk from standard input.
 * @param data Destination buffer.
 * @param size Destination size.
 * @return Byte count, zero on EOF, or negative on error.
 */
static int nets_cli_read_stdin(void *data, size_t size)
{
    for (;;) {
#ifdef _WIN32
        int result;

        result = _read(_fileno(stdin), data, (unsigned int)size);
        return result < 0 ? -1 : result;
#else
        ssize_t result;

        result = read(STDIN_FILENO, data, size);
        if (result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        return (int)result;
#endif
    }
}

/**
 * Configures binary standard streams where required.
 * @return Zero on success.
 */
static int nets_cli_prepare_stdio(void)
{
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1) return 1;
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 1;
#endif
    return 0;
}

#ifdef _WIN32
/**
 * Checks whether Windows standard input can be read without blocking.
 * @param closed Set when redirected standard input reached EOF.
 * @return Nonzero when input is ready.
 */
static int nets_cli_windows_stdin_ready(int *closed)
{
    HANDLE handle;
    DWORD type;

    *closed = 0;
    handle = (HANDLE)_get_osfhandle(_fileno(stdin));
    if (handle == INVALID_HANDLE_VALUE || handle == NULL) {
        *closed = 1;
        return 0;
    }
    type = GetFileType(handle);
    if (type == FILE_TYPE_PIPE) {
        DWORD available;

        available = 0;
        if (!PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) {
            DWORD error;

            error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF) {
                *closed = 1;
            }
            return 0;
        }
        return available != 0U;
    }
    if (type == FILE_TYPE_DISK) return 1;
    if (type == FILE_TYPE_CHAR) {
        DWORD mode;

        if (!GetConsoleMode(handle, &mode)) return 1;
        return _kbhit() != 0;
    }
    return 0;
}
#endif

/**
 * Waits briefly for standard-input activity.
 * @return NETS_CLI_STDIN_* state, or negative on failure.
 */
static int nets_cli_wait_stdin(void)
{
#ifdef _WIN32
    int closed;

    if (nets_cli_windows_stdin_ready(&closed)) return NETS_CLI_STDIN_READY;
    if (closed) return NETS_CLI_STDIN_CLOSED;
    Sleep(100U);
    return NETS_CLI_STDIN_NONE;
#else
    fd_set readable;
    struct timeval timeout;
    int result;

    for (;;) {
        FD_ZERO(&readable);
        FD_SET(STDIN_FILENO, &readable);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        result = select(
            STDIN_FILENO + 1,
            &readable,
            NULL,
            NULL,
            &timeout
        );
        if (result >= 0) break;
        if (errno != EINTR) return -1;
        if (nets_cli_stop) return NETS_CLI_STDIN_NONE;
    }
    if (result == 0) return NETS_CLI_STDIN_NONE;
    return FD_ISSET(STDIN_FILENO, &readable)
        ? NETS_CLI_STDIN_READY
        : NETS_CLI_STDIN_NONE;
#endif
}

/**
 * Waits briefly while standard input is closed.
 * @return void.
 */
static void nets_cli_idle(void)
{
#ifdef _WIN32
    Sleep(100U);
#else
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 100000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Handles process interruption.
 * @param signal_number Signal number.
 * @return void.
 */
static void nets_cli_signal(int signal_number)
{
    (void)signal_number;
    nets_cli_stop = 1;
}

/**
 * Writes one incoming network chunk to standard output.
 * @param data Received bytes.
 * @param data_size Byte count.
 * @param userdata Relay state.
 * @return void.
 */
static void nets_cli_receive(
    const void *data,
    size_t data_size,
    void *userdata
)
{
    nets_cli_state_t *state;

    state = (nets_cli_state_t *)userdata;
    if (nets_cli_write_stdout(data, data_size) != 0) {
        atomic_store(&state->io_failed, 1);
        atomic_store(&state->terminal, 1);
    }
}

/**
 * Marks one TCP remote disconnect as terminal.
 * @param userdata Relay state.
 * @return void.
 */
static void nets_cli_disconnect(void *userdata)
{
    nets_cli_state_t *state;

    state = (nets_cli_state_t *)userdata;
    atomic_store(&state->terminal, 1);
}

/**
 * Stores one asynchronous network failure.
 * @param status Public network status.
 * @param userdata Relay state.
 * @return void.
 */
static void nets_cli_network_error(int status, void *userdata)
{
    nets_cli_state_t *state;

    state = (nets_cli_state_t *)userdata;
    atomic_store(&state->error_status, status);
    atomic_store(&state->terminal, 1);
}

/**
 * Runs one persistent endpoint relay.
 * @param options Parsed command options.
 * @return Process exit status.
 */
static int nets_cli_run(const nets_cli_options_t *options)
{
    unsigned char buffer[NETS_CLI_BUFFER_SIZE];
    kc_nets_options_t network_options;
    kc_nets_udp_options_t udp_options;
    kc_nets_t *network;
    nets_cli_state_t state;
    int stdin_open;
    int local_failure;
    int status;

    memset(&state, 0, sizeof(state));
    atomic_init(&state.terminal, 0);
    atomic_init(&state.error_status, KC_NETS_OK);
    atomic_init(&state.io_failed, 0);
    memset(&network_options, 0, sizeof(network_options));
    network_options.host = options->host;
    network_options.port = options->port;
    network_options.protocol = options->protocol;
    network = NULL;

    if(options->protocol==KC_NETS_UDP &&
        (options->local_host||options->local_port)){
        memset(&udp_options,0,sizeof(udp_options));
        udp_options.endpoint=network_options;
        udp_options.local_host=options->local_host;
        udp_options.local_port=options->local_port;
        status=kc_nets_open_udp(&network,&udp_options,nets_cli_receive,
            nets_cli_network_error,&state);
    }else{
        status=kc_nets_open(&network,&network_options,nets_cli_receive,
            nets_cli_disconnect,nets_cli_network_error,&state);
    }
    if (status != KC_NETS_OK) {
        return nets_cli_error_value("open failed: ", kc_nets_strerror(status));
    }

    stdin_open = 1;
    local_failure = 0;
    while (!nets_cli_stop && !atomic_load(&state.terminal)) {
        if (stdin_open) {
            int event;

            event = nets_cli_wait_stdin();
            if (event < 0) {
                local_failure = nets_cli_error("standard input wait failed");
                break;
            }
            if (event == NETS_CLI_STDIN_CLOSED) {
                stdin_open = 0;
                continue;
            }
            if (event == NETS_CLI_STDIN_READY) {
                int size;

                size = nets_cli_read_stdin(buffer, sizeof(buffer));
                if (size < 0) {
                    local_failure = nets_cli_error("standard input read failed");
                    break;
                }
                if (size == 0) {
                    stdin_open = 0;
                    continue;
                }
                status = kc_nets_send(network, buffer, (size_t)size);
                if (status != KC_NETS_OK) {
                    if (
                        status == KC_NETS_ECLOSED &&
                        atomic_load(&state.terminal)
                    ) {
                        break;
                    }
                    local_failure = nets_cli_error_value(
                        "send failed: ",
                        kc_nets_strerror(status)
                    );
                    break;
                }
            }
        } else {
            nets_cli_idle();
        }
    }

    kc_nets_close(network);
    if (local_failure != 0) return local_failure;
    if (atomic_load(&state.io_failed)) {
        return nets_cli_error("standard output write failed");
    }
    status = atomic_load(&state.error_status);
    if (status != KC_NETS_OK) {
        return nets_cli_error_value("network error: ", kc_nets_strerror(status));
    }
    return 0;
}

#define NETS_RESIDENT_PATH_MAX 2048
#define NETS_RESIDENT_ID_MAX 128
#define NETS_RESIDENT_CMD_MAX 2048

typedef struct {
    char name[NETS_RESIDENT_ID_MAX];
    char host[1025];
    char local_host[1025];
    char command[NETS_RESIDENT_CMD_MAX];
    unsigned short port;
    unsigned short local_port;
} nets_resident_info_t;

/**
 * Checks a resident endpoint identifier without accepting path separators.
 * @param id Endpoint identifier.
 * @return 1 when valid.
 */
static int nets_resident_id_valid(const char *id)
{
    const unsigned char *p;
    if(!id||!*id||strlen(id)>=NETS_RESIDENT_ID_MAX||
        strcmp(id,".")==0||strcmp(id,"..")==0)return 0;
    for(p=(const unsigned char *)id;*p;p++)
        if(!isalnum(*p)&&*p!='.'&&*p!='_'&&*p!='-')return 0;
    return 1;
}

/**
 * Resolves the per-user resident endpoint directory.
 * @param path Destination.
 * @param capacity Path capacity.
 * @return Zero on success.
 */
static int nets_resident_directory(char *path,size_t capacity)
{
    const char *override=getenv("KC_NETS_DIR");
    if(override&&*override)
        return (size_t)snprintf(path,capacity,"%s",override)<capacity?0:1;
#ifdef _WIN32
    {
        char temporary[MAX_PATH];
        DWORD n=GetTempPathA((DWORD)sizeof(temporary),temporary);
        if(!n||n>=sizeof(temporary))return 1;
        return (size_t)snprintf(path,capacity,"%skaisarcode\\nets.c",
            temporary)<capacity?0:1;
    }
#else
    {
        const char *xdg=getenv("XDG_RUNTIME_DIR");
        if(xdg&&*xdg)
            return (size_t)snprintf(path,capacity,"%s/kaisarcode/nets.c",
                xdg)<capacity?0:1;
        return (size_t)snprintf(path,capacity,"/tmp/kaisarcode/nets.c-%lu",
            (unsigned long)getuid())<capacity?0:1;
    }
#endif
}

/**
 * Creates an endpoint directory using private ownership permissions.
 * @param path Directory to create.
 * @return Zero on success.
 */
static int nets_resident_mkdir(const char *path)
{
    char buffer[NETS_RESIDENT_PATH_MAX];
    char *p;
    if((size_t)snprintf(buffer,sizeof(buffer),"%s",path)>=sizeof(buffer))return 1;
    for(p=buffer+1;*p;p++){
#ifdef _WIN32
        if(*p!='\\'&&*p!='/')continue;
#else
        if(*p!='/')continue;
#endif
        {
            char old=*p;
            *p=0;
#ifdef _WIN32
            if(!CreateDirectoryA(buffer,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)
                return 1;
#else
            if(mkdir(buffer,0700)!=0&&errno!=EEXIST)return 1;
#endif
            *p=old;
        }
    }
#ifdef _WIN32
    return CreateDirectoryA(buffer,NULL)||GetLastError()==ERROR_ALREADY_EXISTS?0:1;
#else
    return mkdir(buffer,0700)==0||errno==EEXIST?0:1;
#endif
}

/**
 * Constructs the path of one resident endpoint state file.
 * @param path Destination.
 * @param capacity Destination capacity.
 * @param directory Runtime directory.
 * @param id Validated endpoint name.
 * @param suffix State file suffix.
 * @return Zero on success.
 */
static int nets_resident_path(char *path,size_t capacity,
    const char *directory,const char *id,const char *suffix)
{
#ifdef _WIN32
    return (size_t)snprintf(path,capacity,"%s\\%s%s",directory,id,suffix)
        <capacity?0:1;
#else
    return (size_t)snprintf(path,capacity,"%s/%s%s",directory,id,suffix)
        <capacity?0:1;
#endif
}

/**
 * Parses a port, optionally permitting zero for ephemeral local binds.
 * @param text Port text.
 * @param allow_zero Whether zero is accepted.
 * @param port Destination port.
 * @return 1 when valid.
 */
static int nets_resident_port(const char *text,int allow_zero,
    unsigned short *port)
{
    char *end=NULL;
    unsigned long n;
    if(!text||!*text||!port||*text=='-')return 0;
    errno=0;n=strtoul(text,&end,10);
    if(errno||!end||*end||n>65535UL||(!allow_zero&&!n))return 0;
    *port=(unsigned short)n;
    return 1;
}

/**
 * Saves one resident endpoint configuration.
 * @param directory Runtime directory.
 * @param info Endpoint configuration.
 * @return Zero on success.
 */
static int nets_resident_write(const char *directory,
    const nets_resident_info_t *info)
{
    char path[NETS_RESIDENT_PATH_MAX];
    FILE *f;
    if(nets_resident_path(path,sizeof(path),directory,info->name,".meta"))return 1;
    f=fopen(path,"wb");
    if(!f)return 1;
    if(fprintf(f,"%s\n%u\n%s\n%u\n%s\n",
        info->host,(unsigned)info->port,info->local_host,
        (unsigned)info->local_port,info->command)<0){
        fclose(f);return 1;
    }
    if(fclose(f)!=0)return 1;
#ifndef _WIN32
    (void)chmod(path,0600);
#endif
    return 0;
}

/**
 * Loads a resident endpoint configuration.
 * @param directory Runtime directory.
 * @param id Endpoint identifier.
 * @param info Destination.
 * @return Zero on success.
 */
static int nets_resident_read(const char *directory,const char *id,
    nets_resident_info_t *info)
{
    char path[NETS_RESIDENT_PATH_MAX],port[32],local[32];
    FILE *f;
    char *fields[3];
    size_t i;
    if(!nets_resident_id_valid(id)||!info||
        nets_resident_path(path,sizeof(path),directory,id,".meta"))return 1;
    f=fopen(path,"rb");
    if(!f)return 1;
    memset(info,0,sizeof(*info));
    fields[0]=info->host;fields[1]=info->local_host;fields[2]=info->command;
    if(!fgets(info->host,sizeof(info->host),f)||
       !fgets(port,sizeof(port),f)||
       !fgets(info->local_host,sizeof(info->local_host),f)||
       !fgets(local,sizeof(local),f)||
       !fgets(info->command,sizeof(info->command),f)){
        fclose(f);return 1;
    }
    fclose(f);
    for(i=0;i<3;i++){
        size_t length=strlen(fields[i]);
        if(length&&fields[i][length-1U]=='\n')fields[i][length-1U]=0;
    }
    {
        size_t length=strlen(port);
        if(length&&port[length-1U]=='\n')port[length-1U]=0;
        length=strlen(local);
        if(length&&local[length-1U]=='\n')local[length-1U]=0;
    }
    if(!nets_resident_port(port,0,&info->port)||
       !nets_resident_port(local,1,&info->local_port)||
       !*info->host)return 1;
    snprintf(info->name,sizeof(info->name),"%s",id);
    return 0;
}

/**
 * Reads one endpoint's resident PID.
 * @param directory Runtime directory.
 * @param id Endpoint identifier.
 * @param pid Destination PID.
 * @return Zero on success.
 */
static int nets_resident_read_pid(const char *directory,const char *id,long *pid)
{
    char path[NETS_RESIDENT_PATH_MAX];
    FILE *f;
    if(nets_resident_path(path,sizeof(path),directory,id,".pid"))return 1;
    f=fopen(path,"rb");
    if(!f)return 1;
    if(fscanf(f,"%ld",pid)!=1||*pid<=0){fclose(f);return 1;}
    fclose(f);
    return 0;
}

/**
 * Checks whether one resident process is still running.
 * @param pid Process identifier.
 * @return One when alive.
 */
static int nets_resident_alive(long pid)
{
#ifdef _WIN32
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,(DWORD)pid);
    DWORD state=0;
    if(!h)return 0;
    if(!GetExitCodeProcess(h,&state)){CloseHandle(h);return 0;}
    CloseHandle(h);
    return state==STILL_ACTIVE;
#else
    return pid>0&&(kill((pid_t)pid,0)==0||errno==EPERM);
#endif
}

/**
 * Stops a resident endpoint by PID.
 * @param pid Resident process identifier.
 * @return void.
 */
static void nets_resident_stop(long pid)
{
#ifdef _WIN32
    HANDLE h=OpenProcess(PROCESS_TERMINATE,FALSE,(DWORD)pid);
    if(h){(void)TerminateProcess(h,0);CloseHandle(h);}
#else
    if(pid>0)(void)kill((pid_t)pid,SIGTERM);
#endif
}

/**
 * Saves the process ID for one resident endpoint.
 * @param directory Runtime directory.
 * @param id Endpoint identifier.
 * @param pid Process identifier.
 * @return Zero on success.
 */
static int nets_resident_write_pid(const char *directory,const char *id,long pid)
{
    char path[NETS_RESIDENT_PATH_MAX];
    FILE *f;
    if(nets_resident_path(path,sizeof(path),directory,id,".pid"))return 1;
    f=fopen(path,"wb");
    if(!f)return 1;
    if(fprintf(f,"%ld\n",pid)<0){fclose(f);return 1;}
    if(fclose(f)!=0)return 1;
#ifndef _WIN32
    (void)chmod(path,0600);
#endif
    return 0;
}

/**
 * Executes the configured callback command for one UDP datagram.
 * @param command Registered command line.
 * @param host Numeric source host.
 * @param port Source port.
 * @param bytes Borrowed datagram.
 * @param size Datagram size.
 * @return Zero on successful command handoff.
 */
static int nets_resident_callback(const char *command,const char *host,
    unsigned short port,const void *bytes,size_t size)
{
    char invocation[NETS_RESIDENT_CMD_MAX+1150];
    if((size_t)snprintf(invocation,sizeof(invocation),"%s %s %u",
        command,host,(unsigned)port)>=sizeof(invocation))return 1;
#ifdef _WIN32
    {
        SECURITY_ATTRIBUTES attributes;
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        HANDLE input_read=NULL,input_write=NULL;
        char shell[NETS_RESIDENT_CMD_MAX+1180];
        DWORD sent;
        size_t offset=0;
        memset(&attributes,0,sizeof(attributes));
        attributes.nLength=sizeof(attributes);
        attributes.bInheritHandle=TRUE;
        if(!CreatePipe(&input_read,&input_write,&attributes,0))return 1;
        SetHandleInformation(input_write,HANDLE_FLAG_INHERIT,0);
        memset(&startup,0,sizeof(startup));
        startup.cb=sizeof(startup);
        startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdInput=input_read;
        startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
        memset(&process,0,sizeof(process));
        snprintf(shell,sizeof(shell),"cmd.exe /c %s",invocation);
        if(!CreateProcessA(NULL,shell,NULL,NULL,TRUE,CREATE_NO_WINDOW,
            NULL,NULL,&startup,&process)){
            CloseHandle(input_read);CloseHandle(input_write);return 1;
        }
        CloseHandle(process.hThread);
        CloseHandle(input_read);
        while(offset<size){
            if(!WriteFile(input_write,(const unsigned char *)bytes+offset,
                (DWORD)(size-offset),&sent,NULL)||!sent)break;
            offset+=(size_t)sent;
        }
        CloseHandle(input_write);
        CloseHandle(process.hProcess);
        return offset==size?0:1;
    }
#else
    {
        int pipefd[2];
        pid_t child;
        size_t offset=0;
        if(pipe(pipefd)!=0)return 1;
        child=fork();
        if(child<0){close(pipefd[0]);close(pipefd[1]);return 1;}
        if(child==0){
            close(pipefd[1]);
            if(dup2(pipefd[0],STDIN_FILENO)<0)_exit(1);
            if(pipefd[0]>STDERR_FILENO)close(pipefd[0]);
            execl("/bin/sh","sh","-c",invocation,(char *)NULL);
            _exit(1);
        }
        close(pipefd[0]);
        while(offset<size){
            ssize_t sent=write(pipefd[1],(const unsigned char *)bytes+offset,
                size-offset);
            if(sent<0&&errno==EINTR)continue;
            if(sent<=0)break;
            offset+=(size_t)sent;
        }
        close(pipefd[1]);
        return offset==size?0:1;
    }
#endif
}

/**
 * Delivers inbound UDP bytes to an external command with source arguments.
 * @param host Source host.
 * @param port Source port.
 * @param bytes Datagram.
 * @param size Datagram size.
 * @param userdata Resident configuration.
 * @return void.
 */
static void nets_resident_receive(const char *host,unsigned short port,
    const void *bytes,size_t size,void *userdata)
{
    const nets_resident_info_t *info=(const nets_resident_info_t *)userdata;
    if(*info->command)
        (void)nets_resident_callback(info->command,host,port,bytes,size);
}

/**
 * Reads one resident send request and queues it through the existing socket.
 * @param network Persistent network endpoint.
 * @param path Completed request path.
 * @return Zero on successful handoff.
 */
static int nets_resident_process_send(kc_nets_t *network,const char *path)
{
    FILE *file=fopen(path,"rb");
    char host[1025],port_text[32];
    unsigned short port;
    unsigned char buffer[65508];
    size_t size;
    int result=1;
    if(!file)return 1;
    if(fgets(host,sizeof(host),file)&&fgets(port_text,sizeof(port_text),file)){
        size_t len=strlen(host);
        if(len&&host[len-1U]=='\n'){
            host[len-1U]=0;
            if(nets_resident_port(port_text,0,&port)){
                size=fread(buffer,1,sizeof(buffer),file);
                if(!ferror(file)&&feof(file)&&size<=65507U)
                    result=kc_nets_send_to(network,host,port,buffer,size)==KC_NETS_OK?0:1;
            }
        }
    }
    fclose(file);
    return result;
}

/**
 * Processes queued binary send requests for one resident endpoint.
 * @param directory Resident state directory.
 * @param info Registered endpoint.
 * @param network Open persistent endpoint.
 * @return void.
 */
static void nets_resident_drain(const char *directory,
    const nets_resident_info_t *info,kc_nets_t *network)
{
    char prefix[NETS_RESIDENT_ID_MAX+16];
    size_t len;
    snprintf(prefix,sizeof(prefix),"%s.send.",info->name);
    len=strlen(prefix);
#ifdef _WIN32
    {
        WIN32_FIND_DATAA entry;
        HANDLE listing;
        char pattern[NETS_RESIDENT_PATH_MAX];
        if((size_t)snprintf(pattern,sizeof(pattern),"%s\\%s*",
            directory,prefix)>=sizeof(pattern))return;
        listing=FindFirstFileA(pattern,&entry);
        if(listing==INVALID_HANDLE_VALUE)return;
        do{
            char path[NETS_RESIDENT_PATH_MAX];
            if(strncmp(entry.cFileName,prefix,len)!=0)continue;
            if((size_t)snprintf(path,sizeof(path),"%s\\%s",
                directory,entry.cFileName)>=sizeof(path))continue;
            (void)nets_resident_process_send(network,path);
            (void)remove(path);
        }while(FindNextFileA(listing,&entry));
        FindClose(listing);
    }
#else
    {
        DIR *listing=opendir(directory);
        struct dirent *entry;
        if(!listing)return;
        while((entry=readdir(listing))!=NULL){
            char path[NETS_RESIDENT_PATH_MAX];
            if(strncmp(entry->d_name,prefix,len)!=0)continue;
            if((size_t)snprintf(path,sizeof(path),"%s/%s",
                directory,entry->d_name)>=sizeof(path))continue;
            (void)nets_resident_process_send(network,path);
            (void)remove(path);
        }
        closedir(listing);
    }
#endif
}

/**
 * Runs one resident UDP endpoint with the same socket for every destination.
 * @param directory Per-user resident state directory.
 * @param name Registered endpoint ID.
 * @return Process exit status.
 */
static int nets_resident_worker(const char *directory,const char *name)
{
    nets_resident_info_t info;
    kc_nets_t *network=NULL;
    kc_nets_udp_options_t options;
    int result;
    if(nets_resident_read(directory,name,&info)!=0)return 1;
    memset(&options,0,sizeof(options));
    options.endpoint.host=info.host;
    options.endpoint.port=info.port;
    options.endpoint.protocol=KC_NETS_UDP;
    options.local_host=*info.local_host?info.local_host:NULL;
    options.local_port=info.local_port;
    options.receive_from=nets_resident_receive;
    result=kc_nets_open_udp(&network,&options,NULL,NULL,&info);
    if(result!=KC_NETS_OK)return 1;
#ifdef _WIN32
    (void)signal(SIGINT,nets_cli_signal);
    (void)signal(SIGTERM,nets_cli_signal);
#else
    (void)signal(SIGINT,nets_cli_signal);
    (void)signal(SIGTERM,nets_cli_signal);
    (void)signal(SIGPIPE,SIG_IGN);
    (void)signal(SIGCHLD,SIG_IGN);
#endif
    while(!nets_cli_stop){
        nets_resident_drain(directory,&info,network);
        nets_cli_idle();
    }
    kc_nets_close(network);
    return 0;
}

/**
 * Removes persistent state files for a named endpoint.
 * @param directory State directory.
 * @param name Endpoint ID.
 * @return void.
 */
static void nets_resident_remove_state(const char *directory,const char *name)
{
    char path[NETS_RESIDENT_PATH_MAX];
    if(nets_resident_path(path,sizeof(path),directory,name,".meta")==0)
        (void)remove(path);
    if(nets_resident_path(path,sizeof(path),directory,name,".pid")==0)
        (void)remove(path);
}

/**
 * Stops a resident network endpoint and removes its registration.
 * @param directory State directory.
 * @param name Endpoint identifier.
 * @return Zero on success.
 */
static int nets_resident_delete(const char *directory,const char *name)
{
    long pid;
    if(nets_resident_read_pid(directory,name,&pid)==0&&nets_resident_alive(pid)){
        nets_resident_stop(pid);
#ifdef _WIN32
        Sleep(100U);
#else
        {
            struct timespec delay={0,100000000L};
            (void)nanosleep(&delay,NULL);
        }
#endif
    }
    nets_resident_remove_state(directory,name);
    return 0;
}

/**
 * Creates one detached resident UDP sender with its endpoint metadata.
 * @param directory Per-user state directory.
 * @param info Endpoint and callback configuration.
 * @return Zero on successful process startup.
 */
static int nets_resident_create(const char *directory,
    const nets_resident_info_t *info)
{
    long old_pid;
    if(nets_resident_mkdir(directory)!=0)return 1;
    if(nets_resident_read_pid(directory,info->name,&old_pid)==0&&
        nets_resident_alive(old_pid))nets_resident_stop(old_pid);
    if(nets_resident_write(directory,info)!=0)return 1;
#ifdef _WIN32
    {
        char executable[NETS_RESIDENT_PATH_MAX],command[NETS_RESIDENT_PATH_MAX+8];
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        DWORD n=GetModuleFileNameA(NULL,executable,(DWORD)sizeof(executable));
        if(!n||n>=sizeof(executable))goto failed;
        if((size_t)snprintf(command,sizeof(command),"\"%s\"",executable)
            >=sizeof(command))goto failed;
        if(!SetEnvironmentVariableA("KC_NETS_WORKER",info->name))goto failed;
        memset(&startup,0,sizeof(startup));
        memset(&process,0,sizeof(process));
        startup.cb=sizeof(startup);
        if(!CreateProcessA(NULL,command,NULL,NULL,FALSE,
            DETACHED_PROCESS|CREATE_NO_WINDOW,NULL,NULL,&startup,&process)){
            (void)SetEnvironmentVariableA("KC_NETS_WORKER",NULL);
            goto failed;
        }
        (void)SetEnvironmentVariableA("KC_NETS_WORKER",NULL);
        CloseHandle(process.hThread);
        if(nets_resident_write_pid(directory,info->name,
            (long)process.dwProcessId)!=0){
            TerminateProcess(process.hProcess,0);
            CloseHandle(process.hProcess);
            goto failed;
        }
        CloseHandle(process.hProcess);
    }
#else
    {
        pid_t pid=fork();
        if(pid<0)goto failed;
        if(pid==0){
            (void)setsid();
            nets_cli_stop=0;
            _exit(nets_resident_worker(directory,info->name));
        }
        if(nets_resident_write_pid(directory,info->name,(long)pid)!=0){
            (void)kill(pid,SIGTERM);
            goto failed;
        }
    }
#endif
    return 0;
failed:
    nets_resident_remove_state(directory,info->name);
    return 1;
}

/**
 * Queues one binary stdin datagram to a live resident endpoint.
 * @param directory State directory.
 * @param name Endpoint ID.
 * @param destination Optional host, or NULL to use the registered default.
 * @param port Destination port, or zero to use the registered default.
 * @return Zero when delivered to the resident IPC queue.
 */
static int nets_resident_send(const char *directory,const char *name,
    const char *destination,unsigned short port)
{
    nets_resident_info_t info;
    char path[NETS_RESIDENT_PATH_MAX],temporary[NETS_RESIDENT_PATH_MAX];
    char final_suffix[64],temp_suffix[64];
    unsigned long pid;
    long worker_pid;
    FILE *file;
    unsigned char data[65508];
    size_t size;
    if(nets_resident_read(directory,name,&info)!=0||
        nets_resident_read_pid(directory,name,&worker_pid)!=0||
        !nets_resident_alive(worker_pid))return 1;
    if(!destination){destination=info.host;port=info.port;}
    if(!destination||!*destination||strchr(destination,'\n')||
        strchr(destination,'\r')||!port)return 1;
#ifdef _WIN32
    pid=(unsigned long)GetCurrentProcessId();
#else
    pid=(unsigned long)getpid();
#endif
    snprintf(final_suffix,sizeof(final_suffix),".send.%lu",pid);
    snprintf(temp_suffix,sizeof(temp_suffix),".pending.%lu",pid);
    if(nets_resident_path(path,sizeof(path),directory,name,final_suffix)!=0||
        nets_resident_path(temporary,sizeof(temporary),directory,name,temp_suffix)!=0)
        return 1;
    file=fopen(temporary,"wb");
    if(!file)return 1;
#ifndef _WIN32
    (void)chmod(temporary,0600);
#endif
    size=fread(data,1,sizeof(data),stdin);
    if(ferror(stdin)||size>65507U||
        fprintf(file,"%s\n%u\n",destination,(unsigned)port)<0||
        (size&&fwrite(data,1,size,file)!=size)||
        fflush(file)!=0){
        fclose(file);remove(temporary);return 1;
    }
    if(fclose(file)!=0||rename(temporary,path)!=0){
        remove(temporary);return 1;
    }
    return 0;
}

/**
 * Prints registered resident endpoints or one selected endpoint.
 * @param directory State directory.
 * @param id Optional specific endpoint.
 * @return Zero on success.
 */
static int nets_resident_list(const char *directory,const char *id)
{
    if(id){
        nets_resident_info_t info;
        long pid=0;
        if(nets_resident_read(directory,id,&info)!=0)return 1;
        (void)nets_resident_read_pid(directory,id,&pid);
        printf("%s\t%s:%u\tudp\t%s\n",info.name,info.host,
            (unsigned)info.port,nets_resident_alive(pid)?"running":"stopped");
        return 0;
    }
#ifdef _WIN32
    {
        WIN32_FIND_DATAA item;
        HANDLE listing;
        char pattern[NETS_RESIDENT_PATH_MAX];
        if((size_t)snprintf(pattern,sizeof(pattern),"%s\\*.meta",directory)
            >=sizeof(pattern))return 1;
        listing=FindFirstFileA(pattern,&item);
        if(listing==INVALID_HANDLE_VALUE)return 0;
        do{
            size_t n=strlen(item.cFileName);
            if(n>5U&&strcmp(item.cFileName+n-5U,".meta")==0){
                char name[NETS_RESIDENT_ID_MAX];
                if(n-5U>=sizeof(name))continue;
                memcpy(name,item.cFileName,n-5U);
                name[n-5U]=0;
                (void)nets_resident_list(directory,name);
            }
        }while(FindNextFileA(listing,&item));
        FindClose(listing);
    }
#else
    {
        DIR *listing=opendir(directory);
        struct dirent *item;
        if(!listing)return 0;
        while((item=readdir(listing))!=NULL){
            size_t n=strlen(item->d_name);
            if(n>5U&&strcmp(item->d_name+n-5U,".meta")==0){
                char name[NETS_RESIDENT_ID_MAX];
                if(n-5U>=sizeof(name))continue;
                memcpy(name,item->d_name,n-5U);
                name[n-5U]=0;
                (void)nets_resident_list(directory,name);
            }
        }
        closedir(listing);
    }
#endif
    return 0;
}

/**
 * Dispatches optional persistent UDP sender commands.
 * Ordinary nets host port relay invocations are returned to the old parser.
 * @param argc CLI argument count.
 * @param argv CLI arguments.
 * @return -1 for legacy mode or a resident command's process exit status.
 */
static int nets_resident_dispatch(int argc,char **argv)
{
    char directory[NETS_RESIDENT_PATH_MAX];
    const char *worker=getenv("KC_NETS_WORKER");
    int i,create=0;
    if(nets_resident_directory(directory,sizeof(directory))!=0)return 1;
    if(worker&&*worker){
        if(!nets_resident_id_valid(worker))return 1;
        nets_cli_stop=0;
        return nets_resident_worker(directory,worker);
    }
    if(argc>=2&&(strcmp(argv[1],"-l")==0||
        strcmp(argv[1],"--list")==0)){
        if(argc>3|| (argc==3&&!nets_resident_id_valid(argv[2])))return 1;
        return nets_resident_list(directory,argc==3?argv[2]:NULL);
    }
    if(argc>=3&&nets_resident_id_valid(argv[1])){
        if(strcmp(argv[2],"-d")==0||strcmp(argv[2],"--delete")==0){
            if(argc!=3)return 1;
            return nets_resident_delete(directory,argv[1]);
        }
        if(strcmp(argv[2],"-s")==0||strcmp(argv[2],"--send")==0){
            unsigned short port=0;
            if(argc!=3&&argc!=5)return 1;
            if(argc==5&&!nets_resident_port(argv[4],0,&port))return 1;
            if(nets_cli_prepare_stdio()!=0)return 1;
            return nets_resident_send(directory,argv[1],
                argc==5?argv[3]:NULL,port);
        }
    }
    for(i=1;i<argc;i++)
        if(strcmp(argv[i],"--id")==0)create=1;
    if(!create)return -1;
    {
        nets_resident_info_t info;
        int transport=KC_NETS_TCP;
        if(argc<5||!nets_resident_port(argv[2],0,&info.port))return 1;
        memset(&info,0,sizeof(info));
        if(!nets_resident_port(argv[2],0,&info.port)||
            !argv[1][0]||strlen(argv[1])>=sizeof(info.host))return 1;
        snprintf(info.host,sizeof(info.host),"%s",argv[1]);
        for(i=3;i<argc;i++){
            const char *arg=argv[i],*value;
            if(i+1>=argc)return 1;
            value=argv[++i];
            if(strcmp(arg,"--id")==0){
                if(!nets_resident_id_valid(value))return 1;
                snprintf(info.name,sizeof(info.name),"%s",value);
            }else if(strcmp(arg,"-t")==0||strcmp(arg,"--transport")==0){
                if(strcmp(value,"udp")!=0)return 1;
                transport=KC_NETS_UDP;
            }else if(strcmp(arg,"--local-host")==0){
                if(strlen(value)>=sizeof(info.local_host)||
                    strchr(value,'\n')||strchr(value,'\r'))return 1;
                snprintf(info.local_host,sizeof(info.local_host),"%s",value);
            }else if(strcmp(arg,"--local-port")==0){
                if(!nets_resident_port(value,1,&info.local_port))return 1;
            }else if(strcmp(arg,"--receive")==0){
                if(strlen(value)>=sizeof(info.command)||
                    strchr(value,'\n')||strchr(value,'\r'))return 1;
                snprintf(info.command,sizeof(info.command),"%s",value);
            }else return 1;
        }
        if(transport!=KC_NETS_UDP||!*info.name)return 1;
        return nets_resident_create(directory,&info);
    }
}

/**
 * Runs metadata handling or one endpoint relay.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
    nets_cli_options_t options;
    int index;

    {
        int resident=nets_resident_dispatch(argc,argv);
        if(resident>=0)return resident;
    }

    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "-h") == 0 || strcmp(argv[index], "--help") == 0) {
            nets_cli_help(argv[0]);
            return 0;
        }
        if (
            strcmp(argv[index], "-v") == 0 ||
            strcmp(argv[index], "--version") == 0
        ) {
            nets_cli_version();
            return 0;
        }
    }

    if (nets_cli_parse(argc, argv, &options) != 0) return 1;
    if (nets_cli_prepare_stdio() != 0) {
        return nets_cli_error("standard stream setup failed");
    }

    nets_cli_stop = 0;
    (void)signal(SIGINT, nets_cli_signal);
    (void)signal(SIGTERM, nets_cli_signal);
    return nets_cli_run(&options);
}
