/**
 * stun.c - STUN command-line interface.
 * Summary: Runs a STUN Binding client or server.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "libstun.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static volatile sig_atomic_t stun_cli_stop;

/** Prints all public CLI operations and options. */
static void stun_cli_help(void)
{
    printf("Usage: stun <client|server> [arguments] [options]\n");
    printf("\nCommands:\n");
    printf("  stun client <server>    Discover a public UDP endpoint\n");
    printf("  stun server             Serve STUN Binding requests\n");
    printf("\nOptions:\n");
    printf("  --host <host>           Local bind host\n");
    printf("  -p, --port <port>       Local UDP port (client: 0, server: 3478)\n");
    printf("  -t, --timeout <ms>      Client timeout in ms (default: library)\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --version           Show version\n");
}
/** Parses nonnegative decimal input without truncation. */
static int stun_cli_u64(const char *text,uint64_t *out)
{
    unsigned long long n;char *end=NULL;
    if(!text||!*text||*text=='-'||!out)return 0;
    errno=0;n=strtoull(text,&end,10);
    if(errno||!end||*end)return 0;
    *out=(uint64_t)n;return 1;
}
/** Parses a UDP port. */
static int stun_cli_port(const char *text,unsigned short *out)
{
    uint64_t n;if(!stun_cli_u64(text,&n)||n>65535U)return 0;
    *out=(unsigned short)n;return 1;
}
/** Emits an error to stderr. */
static int stun_cli_error(const char *message){fprintf(stderr,"stun: %s\n",message);return 1;}
/** Requests graceful server shutdown. */
static void stun_cli_signal(int signum){(void)signum;stun_cli_stop=1;}
/** Runs one STUN Binding transaction. */
static int stun_cli_client(const kc_stun_client_options_t *options)
{
    kc_stun_client_t *client=NULL;const kc_stun_endpoint_t *mapped;
    int status=kc_stun_client(&client,options);
    if(status!=KC_STUN_OK){fprintf(stderr,"stun: client: %s\n",kc_stun_strerror(status));return 1;}
    do{status=kc_stun_client_poll(client,250);}while(status==KC_STUN_PENDING);
    mapped=kc_stun_client_mapped(client);
    if(status==KC_STUN_OK&&mapped){printf("%s\t%u\n",mapped->host,mapped->port);}
    else{
        fprintf(stderr,"stun: client: %s",kc_stun_strerror(status));
        if(status==KC_STUN_EREJECTED)fprintf(stderr," (%u)",kc_stun_client_error(client));
        fputc('\n',stderr);
    }
    kc_stun_client_close(client);
    return status==KC_STUN_OK?0:1;
}
/** Runs a Binding server until signaled. */
static int stun_cli_server(const kc_stun_server_options_t *options)
{
    kc_stun_server_t *server=NULL;int status=kc_stun_server(&server,options);
    if(status!=KC_STUN_OK){fprintf(stderr,"stun: server: %s\n",kc_stun_strerror(status));return 1;}
    printf("%u\n",kc_stun_server_port(server));fflush(stdout);
    stun_cli_stop=0;(void)signal(SIGINT,stun_cli_signal);(void)signal(SIGTERM,stun_cli_signal);
    while(!stun_cli_stop){
#ifdef _WIN32
        Sleep(100);
#else
        struct timespec pause_time={0,100000000L};nanosleep(&pause_time,NULL);
#endif
    }
    kc_stun_server_close(server);return 0;
}
/** Dispatches the STUN CLI. */
int main(int argc,char **argv)
{
    kc_stun_client_options_t client;kc_stun_server_options_t server;
    int is_client,is_server,i;
    if(argc==2&&(!strcmp(argv[1],"-h")||!strcmp(argv[1],"--help"))){stun_cli_help();return 0;}
    if(argc==2&&(!strcmp(argv[1],"-v")||!strcmp(argv[1],"--version"))){printf("%llu\n",(unsigned long long)kc_stun_version());return 0;}
    if(argc<2){stun_cli_help();return 1;}
    is_client=strcmp(argv[1],"client")==0;is_server=strcmp(argv[1],"server")==0;
    if(!is_client&&!is_server)return stun_cli_error("invalid command");
    memset(&client,0,sizeof(client));memset(&server,0,sizeof(server));
    server.port=3478;
    for(i=2;i<argc;i++){
        const char *arg=argv[i];
        if(strcmp(arg,"--host")==0){
            if(++i>=argc||!*argv[i])return stun_cli_error("--host requires a host");
            if(is_client)client.host=argv[i];else server.host=argv[i];
        }else if(strcmp(arg,"-p")==0||strcmp(arg,"--port")==0){
            unsigned short port;
            if(++i>=argc||!stun_cli_port(argv[i],&port))return stun_cli_error("invalid port");
            if(is_client)client.port=port;else server.port=port;
        }else if(strcmp(arg,"-t")==0||strcmp(arg,"--timeout")==0){
            if(!is_client)return stun_cli_error("--timeout is client-only");
            if(++i>=argc||!stun_cli_u64(argv[i],&client.timeout_ms))return stun_cli_error("invalid timeout");
        }else if(is_client&&!client.peer.host&&arg[0]!='-')client.peer.host=arg;
        else return stun_cli_error("unexpected argument");
    }
    if(is_client){if(!client.peer.host)return stun_cli_error("client requires a server");return stun_cli_client(&client);}
    return stun_cli_server(&server);
}