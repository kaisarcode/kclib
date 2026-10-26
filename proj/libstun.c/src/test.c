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
static int stun_group_failures;
static int stun_group_checks;

/**
 * Prints one test result in the canonical kclib format.
 * @param fail Nonzero when the test failed.
 * @param name Test case identifier.
 * @param detail Behavior verified by the case.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail)
{
    test_case_current++;
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
    if (fail) stun_test_failures++;
}

/**
 * Verifies an assertion with a named public contract.
 * @param pass Nonzero if the assertion succeeded.
 * @param name Test identifier.
 * @param detail Description of the verified behavior.
 * @return None.
 */
static void stun_test_check(int pass, const char *name, const char *detail)
{
    stun_group_checks++;
    if (!pass) {
        fprintf(stderr, "    FAIL %s: %s\n", name, detail);
        stun_group_failures++;
    }
}

/**
 * Reports one public behavior case containing several assertions.
 * @param name Public capability name.
 * @param detail Contract exercised by the case.
 * @return None.
 */
static void stun_test_group(const char *name, const char *detail)
{
    case_result(stun_group_failures != 0 || stun_group_checks == 0, name, detail);
    stun_group_failures = 0;
    stun_group_checks = 0;
}

/**
 * Delivers one server reply to a borrowed client.
 * @return 0 after delivering the reply.
 */
static int stun_test_server_send(void *context, const kc_stun_endpoint_t *peer,
    const void *bytes, size_t size)
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
    stun_test_check(rc==KC_STUN_OK&&server!=NULL,"kc_stun_server", "starts an owned UDP server");
    if(rc!=KC_STUN_OK){stun_test_group("kc_stun_binding", "serves Binding and discovers the mapped IPv4 address");test_case_total -= 2;return;}
    snprintf(peer,sizeof(peer),"%s:%u","127.0.0.1",kc_stun_server_port(server));
    options.peer.host=peer;options.timeout_ms=2000;
    rc=kc_stun_client(&client,&options);
    stun_test_check(rc==KC_STUN_OK&&client!=NULL,"kc_stun_client", "starts an owned UDP client");
    if(rc==KC_STUN_OK){
        const kc_stun_endpoint_t *local=kc_stun_client_local(client);
        const char early[]="datagram during Binding";
        kc_stun_endpoint_t destination={"127.0.0.1",local?local->port:0};
        int sent=local&&kc_stun_client_send(client,&destination,early,sizeof(early))==KC_STUN_OK;
        int early_valid=0;

        do{rc=kc_stun_client_poll(client,50);}while(rc==KC_STUN_PENDING);
        if(sent){
            int received=kc_stun_client_read(client,&data,200)==KC_STUN_OK;
            int valid=received&&data.data_size==sizeof(early)&&
                memcmp(data.data,early,sizeof(early))==0;
            early_valid=valid;
            if(received)kc_stun_datagram_free(&data);
        }
        mapped=kc_stun_client_mapped(client);
        stun_test_check(rc==KC_STUN_OK&&mapped&&mapped->port&&strcmp(mapped->host,"127.0.0.1")==0,"kc_stun_client_mapped", "returns the IPv4 mapped endpoint");
        stun_test_group("kc_stun_binding", "serves Binding and discovers the mapped IPv4 address");
        stun_test_check(sent&&early_valid,"pending_udp_receive","preserves data received during Binding polling");
        if(mapped){
            const char payload[]="application datagram";
            rc=kc_stun_client_send(client,mapped,payload,sizeof(payload));
            stun_test_check(rc==KC_STUN_OK,"kc_stun_client_send", "sends through the preserved mapping");
            rc=kc_stun_client_read(client,&data,200);
            stun_test_check(rc==KC_STUN_OK&&data.data_size==sizeof(payload)&&memcmp(data.data,payload,sizeof(payload))==0,"kc_stun_client_read", "receives application data through the mapping");
            if(rc==KC_STUN_OK)kc_stun_datagram_free(&data);
        }
        stun_test_group("kc_stun_datagrams", "sends and receives through the preserved UDP mapping");
        rc=kc_stun_client_refresh(client,2000);
        stun_test_check(rc==KC_STUN_OK,"kc_stun_client_refresh", "refreshes on the existing socket");
        if(rc==KC_STUN_OK){
            do{rc=kc_stun_client_poll(client,50);}while(rc==KC_STUN_PENDING);
            stun_test_check(rc==KC_STUN_OK,"kc_stun_client_poll", "receives the refreshed mapping response");
        }
        stun_test_group("kc_stun_refresh", "renews Binding on the existing UDP mapping");
        kc_stun_client_close(client);
    } else {
        stun_test_group("kc_stun_binding", "serves Binding and discovers the mapped IPv4 address");
        test_case_total -= 2;
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
    if(rc!=KC_STUN_OK){test_case_total -= 1; printf("[SKIP] IPv6 loopback unavailable\n"); return;}
    snprintf(address,sizeof(address),"[::1]:%u",kc_stun_server_port(server));
    co.peer.host=address;co.host="::1";co.timeout_ms=1000;
    rc=kc_stun_client(&client,&co);
    stun_test_check(rc==KC_STUN_OK,"ipv6_client", "starts an IPv6 Binding client");
    if(rc==KC_STUN_OK){
        const kc_stun_endpoint_t *mapped;
        do{rc=kc_stun_client_poll(client,50);}while(rc==KC_STUN_PENDING);
        mapped=kc_stun_client_mapped(client);
        stun_test_check(rc==KC_STUN_OK&&mapped&&
            strcmp(mapped->host,"::1")==0&&mapped->port!=0,
            "ipv6_mapped", "decodes an IPv6 XOR-MAPPED-ADDRESS");
        kc_stun_client_close(client);
    }
    kc_stun_server_close(server);
    stun_test_group("kc_stun_ipv6", "discovers an IPv6 mapped endpoint");
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
    int attributes_ok=1, refresh_ok=1, keepalive_ok=1;
    so.transport=&server_udp;so.credentials=stun_test_credentials;so.binding=stun_test_binding;
    rc=kc_stun_server(&stun_test_server,&so);
    stun_test_check(rc==KC_STUN_OK,"borrowed_server", "starts with external UDP transport");
    if(rc!=KC_STUN_OK){stun_test_group("kc_stun_borrowed", "uses externally owned UDP for Binding");test_case_total -= 7;return;}
    stun_test_group("kc_stun_borrowed", "uses externally owned UDP for Binding");
    co.peer.host="127.0.0.1:42424";co.transport=&client_udp;
    co.credentials.username="remote:local";co.credentials.key=stun_test_key;
    co.credentials.key_size=strlen(stun_test_key);co.attributes=attributes;co.attribute_count=1;
    for(i=0;i<sizeof(modes)/sizeof(modes[0]);i++){
        co.credentials.integrity=modes[i];
        stun_test_seen_priority=0;
        rc=kc_stun_client(&stun_test_client,&co);
        stun_test_check(rc == KC_STUN_OK,
            i == 0 ? "hmac_sha1_client" :
            i == 1 ? "hmac_sha256_client" : "hmac_both_client",
            "starts a borrowed client with the selected integrity mode");
        if(rc!=KC_STUN_OK)continue;
        rc=kc_stun_client_poll(stun_test_client,0);
        stun_test_check(rc == KC_STUN_OK && kc_stun_client_mapped(stun_test_client) != NULL,
            i == 0 ? "hmac_sha1_binding" :
            i == 1 ? "hmac_sha256_binding" : "hmac_both_binding",
            "verifies the authenticated Binding response");
        if(!stun_test_seen_priority)attributes_ok=0;
        if(i==2){
            rc=kc_stun_client_refresh(stun_test_client,500);
            if(rc!=KC_STUN_OK)refresh_ok=0;
            rc=kc_stun_client_poll(stun_test_client,0);
            if(rc!=KC_STUN_OK)refresh_ok=0;
            {
                unsigned int before=stun_test_replies;
                rc=kc_stun_client_keepalive(stun_test_client);
                if(rc!=KC_STUN_OK||stun_test_replies!=before)keepalive_ok=0;
            }
        }
        kc_stun_client_close(stun_test_client);
    }
    stun_test_group("kc_stun_authenticated", "verifies authenticated Binding with SHA-1 and SHA-256");
    stun_test_check(attributes_ok,"extension_attributes","forwards extension attributes without ICE policy");
    stun_test_group("kc_stun_attributes", "makes STUN extension attributes available to consumers");
    stun_test_check(refresh_ok&&keepalive_ok,"borrowed_refresh_keepalive",
        "refreshes authenticated Binding and sends a Binding Indication");
    stun_test_group("kc_stun_maintenance", "refreshes transactions and sends Binding Indications");
    co.credentials.integrity=KC_STUN_HMAC_BOTH;
    stun_test_role_conflict=1;
    rc=kc_stun_client(&stun_test_client,&co);
    if(rc==KC_STUN_OK){
        rc=kc_stun_client_poll(stun_test_client,0);
        stun_test_check(rc==KC_STUN_EREJECTED&&kc_stun_client_error(stun_test_client)==487,"role_conflict", "reports an authenticated 487 role conflict");
        kc_stun_client_close(stun_test_client);
    }else stun_test_check(0,"role_conflict", "starts the role conflict check");
    stun_test_role_conflict=0;
    stun_test_group("kc_stun_errors", "returns authenticated STUN protocol errors");
    stun_test_tamper=1;
    rc=kc_stun_client(&stun_test_client,&co);
    if(rc==KC_STUN_OK){
        rc=kc_stun_client_poll(stun_test_client,0);
        stun_test_check(rc==KC_STUN_PENDING&&kc_stun_client_mapped(stun_test_client)==NULL,"fingerprint", "rejects a modified packet");
        kc_stun_client_close(stun_test_client);
    }else stun_test_check(0,"fingerprint", "starts the fingerprint rejection check");
    stun_test_tamper=0;
    stun_test_group("kc_stun_integrity", "rejects responses with an invalid fingerprint");
    stun_test_bad_key=1;stun_test_sent=0;
    co.timeout_ms=10;
    rc=kc_stun_client(&stun_test_client,&co);
    if(rc==KC_STUN_OK){
        (void)kc_stun_client_poll(stun_test_client,0);
        do{rc=kc_stun_client_poll(stun_test_client,20);}while(rc==KC_STUN_PENDING);
        stun_test_check(rc==KC_STUN_ETIMEOUT&&stun_test_sent>0,"timeout", "times out on borrowed UDP transport");
        kc_stun_client_close(stun_test_client);
    }else stun_test_check(0,"timeout", "starts a borrowed transport timeout check");
    stun_test_bad_key=0;
    stun_test_group("kc_stun_timeout", "expires an unanswered Binding transaction");
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
                    "unknown_attribute", "returns 420 with unknown mandatory attribute details");
                kc_stun_client_close(stun_test_client);
            }else stun_test_check(0,"unknown_attribute", "starts an unknown attribute request");
            kc_stun_server_close(stun_test_server);stun_test_server=NULL;
        }else stun_test_check(0,"unknown_attribute", "starts an anonymous server for unknown attribute handling");
    }
    stun_test_group("kc_stun_unknown_attributes", "reports unsupported comprehension-required attributes");
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
    stun_test_check(stun_test_cli("--help")==0,"help", "--help succeeds");
    stun_test_check(stun_test_cli("-v")==0,"version", "-v succeeds");
    stun_test_group("stun_cli", "provides help and version commands");
}
#endif

/**
 * Exercises STUN message identification independently of UDP transport.
 * @return void.
 */
static void stun_test_messages(void)
{
    unsigned char valid[20]={0x00,0x01,0x00,0x00,0x21,0x12,0xa4,0x42};
    unsigned char invalid[20];
    stun_test_check(kc_stun_is_message(valid,sizeof(valid)),
        "stun_message","recognizes a complete STUN message envelope");
    stun_test_check(!kc_stun_is_message(valid,19),
        "short_message","does not classify incomplete datagrams as STUN");
    memcpy(invalid,valid,sizeof(invalid));
    invalid[0]=0x80;
    stun_test_check(!kc_stun_is_message(invalid,sizeof(invalid)),
        "message_bits","requires the STUN header's leading bits");
    memcpy(invalid,valid,sizeof(invalid));
    invalid[4]=0;
    stun_test_check(!kc_stun_is_message(invalid,sizeof(invalid)),
        "message_cookie","requires the STUN magic cookie");
    memcpy(invalid,valid,sizeof(invalid));
    invalid[3]=4;
    stun_test_check(!kc_stun_is_message(invalid,sizeof(invalid)),
        "message_length","requires the complete declared length");
    stun_test_group("kc_stun_messages",
        "identifies STUN messages in caller-owned datagram buffers");
}

/**
 * Runs the full numbered STUN contract suite.
 * @return Zero when all assertions pass.
 */
static int stun_test_all(void)
{
#ifdef KC_CLI_PATH
    test_case_total = 14;
#else
    test_case_total = 13;
#endif
    test_case_current = 0;
    stun_test_failures = 0;
    stun_group_failures = 0;
    stun_group_checks = 0;
    stun_test_messages();
    stun_test_owned();
    stun_test_ipv6();
    stun_test_borrowed();
#ifdef KC_CLI_PATH
    stun_test_command_line();
#endif
    printf("%d passed, %u failed\n",
        test_case_current - (int)stun_test_failures, stun_test_failures);
    return stun_test_failures ? 1 : 0;
}

/**
 * Runs STUN public contract tests.
 * @return Process exit status.
 */
int main(void)
{
    return stun_test_all();
}
