/**
 * test.c - libstun public contract tests.
 * Summary: Validates owned and borrowed UDP, authenticated ICE-style bindings, and CLI.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */
#include "libstun.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char stun_test_key[]="example-password";
static kc_stun_client_t *stun_test_client;
static kc_stun_server_t *stun_test_server;
static int stun_test_seen_priority;
static int stun_test_role_conflict;
static int stun_test_tamper;
static int stun_test_bad_key;
static unsigned int stun_test_sent;
static unsigned int stun_test_replies;
static unsigned int stun_test_failures;
static int test_case_total;
static int test_case_current;

/**
 * Checks a contract condition and reports failures.
 * @param pass Nonzero when the assertion holds.
 * @param name Description of the assertion.
 * @return None.
 */
static void stun_test_check(int pass, const char *name)
{
    if (!pass) {
        printf("[FAIL] %s\n", name);
        stun_test_failures++;
    }
}

/**
 * Reports a numbered public contract case.
 * @param failures Number of failed assertions in the case.
 * @param name Public contract case name.
 * @param detail Description of the expected behavior.
 * @return None.
 */
static void stun_case_result(unsigned int failures, const char *name,
    const char *detail)
{
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        failures ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs one case and increments the shared progress counter.
 * @param failures Accumulated failing case count.
 * @param run Test case function.
 * @return None.
 */
static void stun_run_case(int *failures, int (*run)(void))
{
    test_case_current++;
    *failures += run();
}

/**
 * Delivers one server reply to a borrowed client.
 * @return 0 after delivering the reply.
 */
static int stun_test_server_send(void *context,const kc_stun_endpoint_t *peer,const void *bytes,size_t size)
{
    const kc_stun_endpoint_t from={"127.0.0.1",42424};
    (void)context;(void)peer;
    stun_test_replies++;
    if(stun_test_tamper&&size){
        unsigned char *copy=(unsigned char *)malloc(size);
        if(!copy)return 1;
        memcpy(copy,bytes,size);copy[size-1]^=0x10U;
        (void)kc_stun_client_receive(stun_test_client,&from,copy,size);
        free(copy);
    }else (void)kc_stun_client_receive(stun_test_client,&from,bytes,size);
    return 0;
}

/**
 * Delivers a borrowed client's datagram to the server.
 * @return Server receive status.
 */
static int stun_test_client_send(void *context,const kc_stun_endpoint_t *peer,const void *bytes,size_t size)
{
    const kc_stun_endpoint_t from={"127.0.0.1",51000};
    (void)context;(void)peer;
    stun_test_sent++;
    if(stun_test_bad_key)return 0;
    return kc_stun_server_receive(stun_test_server,&from,bytes,size);
}

/**
 * Looks up a short-term STUN credential by username.
 * @return 0 when the credential is available.
 */
static int stun_test_credentials(void *context,const char *username,kc_stun_credentials_t *out)
{
    (void)context;
    if(strcmp(username,"remote:local"))return 1;
    out->username=username;out->key=stun_test_key;out->key_size=strlen(stun_test_key);
    out->integrity=KC_STUN_HMAC_BOTH;return 0;
}

/**
 * Handles an ICE extension without interpreting it in STUN.
 * @return An optional STUN error code.
 */
static unsigned short stun_test_binding(void *context,const kc_stun_endpoint_t *peer,const kc_stun_attribute_t *attributes,size_t count)
{
    size_t i;(void)context;(void)peer;
    for(i=0;i<count;i++)if(attributes[i].type==0x0024&&attributes[i].size==4)
        stun_test_seen_priority=1;
    return stun_test_role_conflict?487U:0U;
}

/**
 * Executes a loopback Binding and reuses the mapping for application data.
 * @return void
 */
static void stun_test_owned(void)
{
    kc_stun_client_options_t options={0};kc_stun_server_options_t server_options={0};
    kc_stun_server_t *server=NULL;kc_stun_client_t *client=NULL;
    char peer[128];int rc;kc_stun_datagram_t data;const kc_stun_endpoint_t *mapped;
    server_options.host="127.0.0.1";server_options.port=0;
    rc=kc_stun_server(&server,&server_options);
    stun_test_check(rc==KC_STUN_OK&&server!=NULL,"owned server starts");
    if(rc!=KC_STUN_OK)return;
    snprintf(peer,sizeof(peer),"%s:%u","127.0.0.1",kc_stun_server_port(server));
    options.peer.host=peer;options.timeout_ms=2000;
    rc=kc_stun_client(&client,&options);
    stun_test_check(rc==KC_STUN_OK&&client!=NULL,"owned client starts");
    if(rc==KC_STUN_OK){
        do{rc=kc_stun_client_poll(client,50);}while(rc==KC_STUN_PENDING);
        mapped=kc_stun_client_mapped(client);
        stun_test_check(rc==KC_STUN_OK&&mapped&&mapped->port&&strcmp(mapped->host,"127.0.0.1")==0,"owned Mapping address");
        if(mapped){
            const char payload[]="application datagram";
            rc=kc_stun_client_send(client,mapped,payload,sizeof(payload));
            stun_test_check(rc==KC_STUN_OK,"send through preserved mapping");
            rc=kc_stun_client_read(client,&data,200);
            stun_test_check(rc==KC_STUN_OK&&data.data_size==sizeof(payload)&&memcmp(data.data,payload,sizeof(payload))==0,"receive through preserved mapping");
            kc_stun_datagram_free(&data);
        }
        rc=kc_stun_client_refresh(client,2000);
        stun_test_check(rc==KC_STUN_OK,"refresh on same socket");
        if(rc==KC_STUN_OK){
            do{rc=kc_stun_client_poll(client,50);}while(rc==KC_STUN_PENDING);
            stun_test_check(rc==KC_STUN_OK,"refreshed Mapping response");
        }
        kc_stun_client_close(client);
    }
    kc_stun_server_close(server);
}

/**
 * Checks the Binding address family and XOR decoding over IPv6 when available.
 * @return void
 */
static void stun_test_ipv6(void)
{
    kc_stun_server_options_t so={0};kc_stun_client_options_t co={0};
    kc_stun_server_t *server=NULL;kc_stun_client_t *client=NULL;
    char address[128];int rc;
    so.host="::1";so.port=0;
    rc=kc_stun_server(&server,&so);
    if(rc!=KC_STUN_OK){printf("[SKIP] IPv6 loopback unavailable\n");return;}
    snprintf(address,sizeof(address),"[::1]:%u",kc_stun_server_port(server));
    co.peer.host=address;co.host="::1";co.timeout_ms=1000;
    rc=kc_stun_client(&client,&co);
    stun_test_check(rc==KC_STUN_OK,"IPv6 Binding client starts");
    if(rc==KC_STUN_OK){
        const kc_stun_endpoint_t *mapped;
        do{rc=kc_stun_client_poll(client,50);}while(rc==KC_STUN_PENDING);
        mapped=kc_stun_client_mapped(client);
        stun_test_check(rc==KC_STUN_OK&&mapped&&
            strcmp(mapped->host,"::1")==0&&mapped->port!=0,
            "IPv6 XOR-MAPPED-ADDRESS");
        kc_stun_client_close(client);
    }
    kc_stun_server_close(server);
}

/**
 * Runs authenticated transactions through an externally owned UDP path.
 * @return void
 */
static void stun_test_borrowed(void)
{
    kc_stun_transport_t client_udp={NULL,stun_test_client_send};
    kc_stun_transport_t server_udp={NULL,stun_test_server_send};
    kc_stun_server_options_t so={0};kc_stun_client_options_t co={0};
    const unsigned char priority[]={0x7e,0,0,1};
    kc_stun_attribute_t attributes[]={{0x0024,priority,4}};
    unsigned int modes[]={KC_STUN_HMAC_SHA1,KC_STUN_HMAC_SHA256,KC_STUN_HMAC_BOTH};
    size_t i;int rc;
    so.transport=&server_udp;so.credentials=stun_test_credentials;so.binding=stun_test_binding;
    rc=kc_stun_server(&stun_test_server,&so);
    stun_test_check(rc==KC_STUN_OK,"borrowed server starts");
    if(rc!=KC_STUN_OK)return;
    co.peer.host="127.0.0.1:42424";co.transport=&client_udp;
    co.credentials.username="remote:local";co.credentials.key=stun_test_key;
    co.credentials.key_size=strlen(stun_test_key);co.attributes=attributes;co.attribute_count=1;
    for(i=0;i<sizeof(modes)/sizeof(modes[0]);i++){
        co.credentials.integrity=modes[i];
        rc=kc_stun_client(&stun_test_client,&co);
        stun_test_check(rc==KC_STUN_OK,"borrowed client starts");
        if(rc!=KC_STUN_OK)continue;
        rc=kc_stun_client_poll(stun_test_client,0);
        stun_test_check(rc==KC_STUN_OK&&kc_stun_client_mapped(stun_test_client)!=NULL,"authenticated Binding response");
        stun_test_check(stun_test_seen_priority,"opaque ICE attribute supplied to policy");
        if(i==2){
            rc=kc_stun_client_refresh(stun_test_client,500);
            stun_test_check(rc==KC_STUN_OK,"authenticated refresh starts");
            rc=kc_stun_client_poll(stun_test_client,0);
            stun_test_check(rc==KC_STUN_OK,"authenticated refresh verifies");
            {
                unsigned int before=stun_test_replies;
                rc=kc_stun_client_keepalive(stun_test_client);
                stun_test_check(rc==KC_STUN_OK&&stun_test_replies==before,
                    "Binding Indication keepalive sends no response");
            }
        }
        kc_stun_client_close(stun_test_client);
    }
    co.credentials.integrity=KC_STUN_HMAC_BOTH;
    stun_test_role_conflict=1;
    rc=kc_stun_client(&stun_test_client,&co);
    if(rc==KC_STUN_OK){
        rc=kc_stun_client_poll(stun_test_client,0);
        stun_test_check(rc==KC_STUN_EREJECTED&&kc_stun_client_error(stun_test_client)==487,"authenticated 487 role conflict");
        kc_stun_client_close(stun_test_client);
    }else stun_test_check(0,"role conflict starts");
    stun_test_role_conflict=0;
    stun_test_tamper=1;
    rc=kc_stun_client(&stun_test_client,&co);
    if(rc==KC_STUN_OK){
        rc=kc_stun_client_poll(stun_test_client,0);
        stun_test_check(rc==KC_STUN_PENDING&&kc_stun_client_mapped(stun_test_client)==NULL,"fingerprint rejects changed packet");
        kc_stun_client_close(stun_test_client);
    }else stun_test_check(0,"tamper case starts");
    stun_test_tamper=0;
    stun_test_bad_key=1;stun_test_sent=0;
    co.timeout_ms=10;
    rc=kc_stun_client(&stun_test_client,&co);
    if(rc==KC_STUN_OK){
        (void)kc_stun_client_poll(stun_test_client,0);
        do{rc=kc_stun_client_poll(stun_test_client,20);}while(rc==KC_STUN_PENDING);
        stun_test_check(rc==KC_STUN_ETIMEOUT&&stun_test_sent>0,"borrowed timeout");
        kc_stun_client_close(stun_test_client);
    }else stun_test_check(0,"timeout case starts");
    stun_test_bad_key=0;
    kc_stun_server_close(stun_test_server);stun_test_server=NULL;
    {
        kc_stun_server_options_t anonymous={0};kc_stun_client_options_t request={0};
        kc_stun_attribute_t unknown={0x0042,priority,4};
        const kc_stun_attribute_t *response;size_t count=0,k;int found=0;
        anonymous.transport=&server_udp;
        request.peer.host="127.0.0.1:42424";
        request.transport=&client_udp;
        request.attributes=&unknown;request.attribute_count=1;
        rc=kc_stun_server(&stun_test_server,&anonymous);
        if(rc==KC_STUN_OK){
            rc=kc_stun_client(&stun_test_client,&request);
            if(rc==KC_STUN_OK){
                rc=kc_stun_client_poll(stun_test_client,0);
                response=kc_stun_client_attributes(stun_test_client,&count);
                for(k=0;k<count;k++)if(response[k].type==KC_STUN_UNKNOWN_ATTRIBUTES&&
                    response[k].size>=2&&((const unsigned char *)response[k].data)[0]==0&&
                    ((const unsigned char *)response[k].data)[1]==0x42)found=1;
                stun_test_check(rc==KC_STUN_EREJECTED&&
                    kc_stun_client_error(stun_test_client)==420&&found,
                    "unknown mandatory attribute yields 420 with details");
                kc_stun_client_close(stun_test_client);
            }else stun_test_check(0,"unknown attribute client starts");
            kc_stun_server_close(stun_test_server);stun_test_server=NULL;
        }else stun_test_check(0,"anonymous server starts");
    }
}
#ifdef KC_CLI_PATH
/**
 * Executes one CLI invocation and returns its status.
 * @return Command exit status.
 */
static int stun_test_cli(const char *args)
{
    char command[4096];
#ifdef _WIN32
    snprintf(command,sizeof(command),"\"\"%s\" %s > NUL 2>&1\"",KC_CLI_PATH,args);
#else
    snprintf(command,sizeof(command),"\"%s\" %s >/dev/null 2>&1",KC_CLI_PATH,args);
#endif
    return system(command);
}

/**
 * Exercises the public command-line interface.
 * @return void
 */
static void stun_test_command_line(void)
{
    stun_test_check(stun_test_cli("--help")==0,"CLI help");
    stun_test_check(stun_test_cli("-v")==0,"CLI version");
    stun_test_check(stun_test_cli("client")==0?0:1,"CLI missing peer");
    stun_test_check(stun_test_cli("server --timeout 100")==0?0:1,"CLI client-only timeout");
    stun_test_check(stun_test_cli("client localhost --port 65536")==0?0:1,"CLI rejects invalid port");
    stun_test_check(stun_test_cli("server unexpected")==0?0:1,"CLI rejects extra arguments");
}
#endif

/**
 * Verifies owned UDP mapping and reuse.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_stun_owned(void)
{
    unsigned int before = stun_test_failures;
    stun_test_owned();
    stun_case_result(stun_test_failures - before, "kc_stun_owned",
        "creates, preserves and refreshes a UDP mapping");
    return stun_test_failures != before;
}

/**
 * Verifies IPv6 binding when loopback is supported.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_stun_ipv6(void)
{
    unsigned int before = stun_test_failures;
    stun_test_ipv6();
    stun_case_result(stun_test_failures - before, "kc_stun_ipv6",
        "decodes the IPv6 XOR-mapped address");
    return stun_test_failures != before;
}

/**
 * Verifies borrowed UDP, authentication, extensions and errors.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_stun_borrowed(void)
{
    unsigned int before = stun_test_failures;
    stun_test_borrowed();
    stun_case_result(stun_test_failures - before, "kc_stun_borrowed",
        "handles authenticated borrowed transport and protocol errors");
    return stun_test_failures != before;
}

#ifdef KC_CLI_PATH
/**
 * Verifies the public STUN command-line interface.
 * @return Zero on success, nonzero on failure.
 */
static int case_stun_cli(void)
{
    unsigned int before = stun_test_failures;
    stun_test_command_line();
    stun_case_result(stun_test_failures - before, "stun_cli",
        "validates help, version and command arguments");
    return stun_test_failures != before;
}
#endif

/**
 * Runs all public contract cases.
 * @return Zero when every case passes.
 */
static int stun_test_all(void)
{
    int failures = 0;

#ifdef KC_CLI_PATH
    test_case_total = 4;
#else
    test_case_total = 3;
#endif
    test_case_current = 0;
    stun_run_case(&failures, case_kc_stun_owned);
    stun_run_case(&failures, case_kc_stun_ipv6);
    stun_run_case(&failures, case_kc_stun_borrowed);
#ifdef KC_CLI_PATH
    stun_run_case(&failures, case_stun_cli);
#endif
    printf("%d passed, %d failed\n", test_case_total - failures, failures);
    return failures ? 1 : 0;
}

/**
 * Runs the complete suite or a named public contract case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv)
{
    int (*run)(void) = NULL;

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "all") == 0))
        return stun_test_all();
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument\n");
        return 2;
    }
    if (strcmp(argv[1], "kc_stun_owned") == 0) run = case_kc_stun_owned;
    else if (strcmp(argv[1], "kc_stun_ipv6") == 0) run = case_kc_stun_ipv6;
    else if (strcmp(argv[1], "kc_stun_borrowed") == 0) run = case_kc_stun_borrowed;
#ifdef KC_CLI_PATH
    else if (strcmp(argv[1], "stun_cli") == 0) run = case_stun_cli;
#endif
    if (!run) {
        fprintf(stderr, "unknown test case: %s\n", argv[1]);
        return 2;
    }
    test_case_current = 1;
    test_case_total = 1;
    return run();
}
