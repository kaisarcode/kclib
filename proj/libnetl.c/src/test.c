/**
 * test.c - Public libnetl contract tests.
 * Summary: Exercises transport only through the exported API.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "libnetl.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define TEST_CLOSE closesocket
typedef SOCKET test_socket_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define TEST_CLOSE close
typedef int test_socket_t;
#endif

typedef struct {
    atomic_int received;
    atomic_int errors;
    unsigned short last_port;
    char data[64];
    size_t size;
    int reply;
} test_state_t;

/**
 * Receive bytes and optionally echo them to the originating peer.
 * @param peer Callback peer.
 * @param data Received bytes.
 * @param size Received byte count.
 * @param userdata Test state.
 */
static void test_receive(kc_netl_peer_t *peer,const void *data,
    size_t size,void *userdata)
{
    test_state_t *state=(test_state_t *)userdata;
    if(size>sizeof(state->data))size=sizeof(state->data);
    memcpy(state->data,data,size);
    state->size=size;
    state->last_port=kc_netl_peer_port(peer);
    if(state->reply && size!=4U) (void)kc_netl_peer_send(peer,"pong",4U);
    atomic_fetch_add(&state->received,1);
}

/**
 * Register asynchronous failures.
 * @param status Failure status.
 * @param userdata Test state.
 */
static void test_error(int status,void *userdata)
{
    test_state_t *state=(test_state_t *)userdata;
    (void)status;
    atomic_fetch_add(&state->errors,1);
}

/**
 * Delay briefly while callbacks run.
 * @param milliseconds Wait interval.
 */
static void test_sleep(unsigned milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec duration;
    duration.tv_sec=(time_t)(milliseconds/1000U);
    duration.tv_nsec=(long)((milliseconds%1000U)*1000000UL);
    (void)nanosleep(&duration,NULL);
#endif
}

/**
 * Wait for a callback count.
 * @param state Test state.
 * @param count Expected minimum.
 * @return One when reached, zero otherwise.
 */
static int test_wait(test_state_t *state,int count)
{
    unsigned i;
    for(i=0;i<200U;i++){
        if(atomic_load(&state->received)>=count)return 1;
        test_sleep(10U);
    }
    return 0;
}

/**
 * Obtain an unused local port without relying on nonpublic libnetl accessors.
 * @param protocol Socket transport.
 * @return Available port, or zero on failure.
 */
static unsigned short test_port(int protocol)
{
    test_socket_t fd;
    struct sockaddr_in address;
    #ifdef _WIN32
    int length=(int)sizeof(address);
    #else
    socklen_t length=(socklen_t)sizeof(address);
    #endif
    unsigned short port=0;
    fd=socket(AF_INET,protocol==KC_NETL_TCP?SOCK_STREAM:SOCK_DGRAM,0);
#ifdef _WIN32
    if(fd==INVALID_SOCKET)return 0;
#else
    if(fd<0)return 0;
#endif
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(fd,(struct sockaddr *)&address,(socklen_t)sizeof(address))==0 &&
        getsockname(fd,(struct sockaddr *)&address,&length)==0)
        port=ntohs(address.sin_port);
    TEST_CLOSE(fd);
    return port;
}

/**
 * Verify validation and status helpers with public symbols only.
 * @return Zero on success.
 */
static int case_open(void)
{
    kc_netl_t *interface=NULL;
    kc_netl_options_t options;
    test_state_t state={0};
    int fail=0;
    memset(&options,0,sizeof(options));
    options.host="127.0.0.1";
    options.port=12345U;
    options.protocol=KC_NETL_TCP;
    fail+=(kc_netl_open(NULL,&options,test_receive,NULL,test_error,&state)!=KC_NETL_EINVAL);
    fail+=(kc_netl_open(&interface,NULL,test_receive,NULL,test_error,&state)!=KC_NETL_EINVAL);
    fail+=(kc_netl_open(&interface,&options,NULL,NULL,test_error,&state)!=KC_NETL_EINVAL);
    options.protocol=99;
    fail+=(kc_netl_open(&interface,&options,test_receive,NULL,test_error,&state)!=KC_NETL_EINVAL);
    fail+=(interface!=NULL);
    kc_netl_close(NULL);
    fail+=(kc_netl_strerror(KC_NETL_OK)==NULL);
    fail+=(kc_netl_version()==0U);
    printf("[public_open] %s\n",fail?"FAIL":"PASS");
    return fail?1:0;
}

/**
 * Verify persistent UDP, directed datagrams, and callback replies.
 * @return Zero on success.
 */
static int case_udp(void)
{
    kc_netl_t *sender=NULL,*receiver=NULL;
    kc_netl_options_t left,right;
    kc_netl_endpoint_t destination;
    test_state_t sent={0},received={0};
    unsigned short a=test_port(KC_NETL_UDP),b=test_port(KC_NETL_UDP);
    int fail=0;
    if(!a||!b||a==b)return 1;
    memset(&left,0,sizeof(left));
    memset(&right,0,sizeof(right));
    left.host="127.0.0.1";left.port=a;left.protocol=KC_NETL_UDP;
    right.host="127.0.0.1";right.port=b;right.protocol=KC_NETL_UDP;
    received.reply=1;
    if(kc_netl_open(&receiver,&right,test_receive,NULL,test_error,&received)!=KC_NETL_OK)
        return 1;
    if(kc_netl_open(&sender,&left,test_receive,NULL,test_error,&sent)!=KC_NETL_OK){
        kc_netl_close(receiver);return 1;
    }
    destination.host="127.0.0.1";destination.port=b;
    fail+=(kc_netl_send(sender,"ping",4U,&destination)!=KC_NETL_OK);
    fail+=!test_wait(&received,1);
    fail+=!test_wait(&sent,1);
    fail+=(received.size!=4U||memcmp(received.data,"ping",4U)!=0);
    fail+=(sent.size!=4U||memcmp(sent.data,"pong",4U)!=0);
    fail+=(atomic_load(&sent.errors)!=0);
    fail+=(atomic_load(&received.errors)!=0);
    kc_netl_close(sender);
    kc_netl_close(receiver);
    printf("[public_udp] %s\n",fail?"FAIL":"PASS");
    return fail?1:0;
}

/**
 * Verify persistent TCP and callback replies through the unified interface.
 * @return Zero on success.
 */
static int case_tcp(void)
{
    kc_netl_t *interface=NULL;
    kc_netl_options_t options;
    test_state_t state={0};
    unsigned short port=test_port(KC_NETL_TCP);
    int fail=0;
    if(!port)return 1;
    memset(&options,0,sizeof(options));
    options.host="127.0.0.1";
    options.port=port;
    options.protocol=KC_NETL_TCP;
    state.reply=1;
    if(kc_netl_open(&interface,&options,test_receive,NULL,test_error,&state)!=KC_NETL_OK)
        return 1;
    fail+=(kc_netl_send(interface,"hello",5U,NULL)!=KC_NETL_OK);
    fail+=!test_wait(&state,2);
    fail+=(atomic_load(&state.errors)!=0);
    kc_netl_close(interface);
    printf("[public_tcp] %s\n",fail?"FAIL":"PASS");
    return fail?1:0;
}

#if defined(KC_CLI_PATH) && !defined(__EMSCRIPTEN__)

/**
 * Run the netl CLI with output discarded.
 * @param args Arguments appended after the executable.
 * @return 0 on success, 1 on failure.
 */
static int netl_cli_run(const char *args) {
    char command[4096];

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > NUL 2>&1\"",
        KC_CLI_PATH, args);
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        KC_CLI_PATH, args);
#endif
    return system(command) == 0 ? 0 : 1;
}

/**
 * Test CLI argument handling for listener administration.
 * @return 0 on success, 1 on failure.
 */
static int case_netl_cli(void) {
    int fail = 0;

    fail += netl_cli_run("--help");
    fail += netl_cli_run("--version");
    fail += netl_cli_run("") == 0;
    fail += netl_cli_run("sample invalid-endpoint") == 0;
    fail += netl_cli_run("sample 0.0.0.0:8080 true --unknown") == 0;
    fail += netl_cli_run("bad/name -d") == 0;
    fail += netl_cli_run("nonexistent") == 0;
    fail += netl_cli_run("sender 127.0.0.1:invalid -t udp") == 0;
    fail += netl_cli_run("sender badendpoint -t udp") == 0;
    {
        char create[256],listed[256],remove[256];
        unsigned short port=(unsigned short)(20000U+
            ((unsigned)kc_netl_version()%15000U));
        snprintf(create,sizeof(create),
            "contract_%u 127.0.0.1:%u -t udp",
            (unsigned)port,(unsigned)port);
        snprintf(listed,sizeof(listed),"-l contract_%u",(unsigned)port);
        snprintf(remove,sizeof(remove),"contract_%u -d",(unsigned)port);
        fail += netl_cli_run(create);
        fail += netl_cli_run(listed);
        fail += netl_cli_run(remove);
    }
    printf("[netl_cli] %s\n",fail?"FAIL":"PASS");
    return fail != 0;
}
#endif

/**
 * Run the public contract test suite.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Failure count.
 */
int main(int argc,char **argv)
{
    int fails=0,total=0;
    if(argc!=2){fprintf(stderr,"expected test case\n");return 2;}
    if(strcmp(argv[1],"all")==0){
        fails+=case_open();total++;
        fails+=case_udp();total++;
        fails+=case_tcp();total++;
#if defined(KC_CLI_PATH) && !defined(__EMSCRIPTEN__)
        fails+=case_netl_cli();total++;
#endif
        printf("%d passed, %d failed\n",total-fails,fails);
        return fails;
    }
    if(strcmp(argv[1],"kc_netl_open")==0)return case_open();
    if(strcmp(argv[1],"kc_netl_udp")==0)return case_udp();
    if(strcmp(argv[1],"kc_netl_tcp")==0)return case_tcp();
#if defined(KC_CLI_PATH) && !defined(__EMSCRIPTEN__)
    if(strcmp(argv[1],"netl_cli")==0)return case_netl_cli();
#endif
    fprintf(stderr,"unknown test case: %s\n",argv[1]);return 2;
}
