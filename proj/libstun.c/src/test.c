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
static kc_stun_client_t *stun_test_second_client;
static kc_stun_server_t *stun_test_server;
static int stun_test_seen_priority;
static int stun_test_seen_sha1;
static int stun_test_seen_sha256;
static int stun_test_role_conflict;
static int stun_test_tamper;
static int stun_test_wrong_integrity;
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
    if(stun_test_wrong_integrity&&size){
        kc_stun_message_t decoded={0};
        kc_stun_credentials_t credentials={0};
        unsigned char forged[1024];
        size_t n=sizeof(forged),i,j=0;
        if(kc_stun_decode(bytes,size,&decoded)!=KC_STUN_OK)return 1;
        for(i=0;i<decoded.attribute_count;i++){
            if(decoded.attributes[i].type!=KC_STUN_MESSAGE_INTEGRITY&&
                decoded.attributes[i].type!=KC_STUN_MESSAGE_INTEGRITY_SHA256&&
                decoded.attributes[i].type!=KC_STUN_FINGERPRINT)
                decoded.attributes[j++]=decoded.attributes[i];
        }
        decoded.attribute_count=j;
        credentials.key="incorrect-key";
        credentials.key_size=strlen((const char *)credentials.key);
        credentials.integrity=KC_STUN_HMAC_SHA256;
        if(kc_stun_encode(&decoded,&credentials,forged,&n)!=KC_STUN_OK){
            kc_stun_message_free(&decoded);return 1;
        }
        kc_stun_message_free(&decoded);
        (void)kc_stun_client_receive(stun_test_client,&from,forged,n);
    }else if(stun_test_tamper&&size){
        unsigned char *copy=(unsigned char *)malloc(size);
        if(!copy)return 1;
        memcpy(copy,bytes,size);copy[size-1]^=0x10U;
        (void)kc_stun_client_receive(stun_test_client,&from,copy,size);
        if(stun_test_second_client)(void)kc_stun_client_receive(stun_test_second_client,&from,copy,size);
        free(copy);
    }else{
        (void)kc_stun_client_receive(stun_test_client,&from,bytes,size);
        if(stun_test_second_client)(void)kc_stun_client_receive(stun_test_second_client,&from,bytes,size);
    }
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
    for(i=0;i<count;i++){
        if(attributes[i].type==KC_STUN_PRIORITY&&attributes[i].size==4)
            stun_test_seen_priority=1;
        if(attributes[i].type==KC_STUN_MESSAGE_INTEGRITY)
            stun_test_seen_sha1=1;
        if(attributes[i].type==KC_STUN_MESSAGE_INTEGRITY_SHA256)
            stun_test_seen_sha256=1;
    }
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
            stun_test_seen_sha1=0;
            stun_test_seen_sha256=0;
            rc=kc_stun_client_refresh(stun_test_client,500);
            if(rc!=KC_STUN_OK)refresh_ok=0;
            rc=kc_stun_client_poll(stun_test_client,0);
            if(rc!=KC_STUN_OK||!stun_test_seen_sha256||stun_test_seen_sha1)
                refresh_ok=0;
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
    {
        kc_stun_client_options_t anonymous={0};
        anonymous.peer.host="127.0.0.1:42424";
        anonymous.transport=&client_udp;
        rc=kc_stun_client(&stun_test_client,&anonymous);
        if(rc==KC_STUN_OK){
            rc=kc_stun_client_poll(stun_test_client,0);
            stun_test_check(rc==KC_STUN_EREJECTED&&
                kc_stun_client_error(stun_test_client)==400,
                "missing_credentials","returns 400 for missing required STUN credentials");
            kc_stun_client_close(stun_test_client);
        }else stun_test_check(0,"missing_credentials","starts an anonymous Binding request");
    }
    stun_test_group("kc_stun_errors", "reports STUN protocol errors and authentication requirements");
    stun_test_tamper=1;
    rc=kc_stun_client(&stun_test_client,&co);
    if(rc==KC_STUN_OK){
        rc=kc_stun_client_poll(stun_test_client,0);
        stun_test_check(rc==KC_STUN_PENDING&&kc_stun_client_mapped(stun_test_client)==NULL,"fingerprint", "rejects a modified packet");
        kc_stun_client_close(stun_test_client);
    }else stun_test_check(0,"fingerprint", "starts the fingerprint rejection check");
    stun_test_tamper=0;
    stun_test_wrong_integrity=1;
    co.timeout_ms=25;
    rc=kc_stun_client(&stun_test_client,&co);
    if(rc==KC_STUN_OK){
        rc=kc_stun_client_poll(stun_test_client,0);
        while(rc==KC_STUN_PENDING)rc=kc_stun_client_poll(stun_test_client,30);
        stun_test_check(rc==KC_STUN_EAUTH,"invalid_hmac",
            "reports an integrity violation instead of a network timeout");
        kc_stun_client_close(stun_test_client);
    }else stun_test_check(0,"invalid_hmac","starts a short-term HMAC verification");
    stun_test_wrong_integrity=0;
    stun_test_group("kc_stun_integrity", "rejects invalid fingerprints and HMACs");
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
 * Verifies the standalone STUN buffer codec and short-term authentication.
 * @return void.
 */
static void stun_test_codec(void)
{
    unsigned char encoded[1024],id[12]={1,2,3,4,5,6,7,8,9,10,11,12};
    unsigned char priority[4]={0,0,0,42};
    kc_stun_attribute_t attributes[]={
        {KC_STUN_USERNAME,"local:remote",12},
        {KC_STUN_PRIORITY,priority,4},
        {KC_STUN_USE_CANDIDATE,NULL,0}
    };
    kc_stun_message_t message={0},decoded={0};
    kc_stun_credentials_t credentials={0};
    size_t size=sizeof(encoded),i;
    int rc,found=0;
    message.type=KC_STUN_REQUEST;
    memcpy(message.transaction_id,id,12);
    message.attributes=attributes;
    message.attribute_count=3;
    credentials.key=stun_test_key;
    credentials.key_size=strlen(stun_test_key);
    credentials.integrity=KC_STUN_HMAC_BOTH;
    rc=kc_stun_encode(&message,&credentials,encoded,&size);
    stun_test_check(rc==KC_STUN_OK,"encode","encodes an authenticated STUN request");
    if(rc==KC_STUN_OK){
        stun_test_check(kc_stun_is_message(encoded,size),
            "classification","recognizes an encoded STUN packet");
        stun_test_check(kc_stun_verify_fingerprint(encoded,size)==KC_STUN_OK,
            "codec_fingerprint","verifies the generated fingerprint");
        stun_test_check(kc_stun_verify_integrity(encoded,size,
            stun_test_key,strlen(stun_test_key))==KC_STUN_OK,
            "codec_hmac","verifies generated HMAC");
        rc=kc_stun_decode(encoded,size,&decoded);
        stun_test_check(rc==KC_STUN_OK&&decoded.type==KC_STUN_REQUEST&&
            kc_stun_transaction_matches(&decoded,id),
            "codec_decode","decodes a request with its transaction identifier");
        for(i=0;i<decoded.attribute_count;i++)
            if(decoded.attributes[i].type==KC_STUN_PRIORITY&&
                decoded.attributes[i].size==4&&
                memcmp(decoded.attributes[i].data,priority,4)==0)found=1;
        stun_test_check(found,"codec_attributes","preserves opaque extension attributes");
        kc_stun_message_free(&decoded);
        encoded[size-1]^=1U;
        stun_test_check(kc_stun_verify_fingerprint(encoded,size)!=KC_STUN_OK,
            "codec_tamper","detects a modified fingerprint");
    }
    stun_test_group("kc_stun_codec",
        "encodes and decodes caller-owned STUN buffers with integrity and attributes");
}

/**
 * Verifies the reusable XOR-MAPPED-ADDRESS codec.
 * @return void.
 */
static void stun_test_mapped_codec(void)
{
    kc_stun_endpoint_t endpoint={"127.0.0.1",48912};
    unsigned char id[12]={1,2,3,4,5,6,7,8,9,10,11,12};
    unsigned char data[32];
    kc_stun_attribute_t attribute;
    char host[64];
    unsigned short port=0;
    size_t size=sizeof(data);
    int rc=kc_stun_encode_mapped(&endpoint,id,data,&size);
    stun_test_check(rc==KC_STUN_OK&&size==8,
        "mapped_ipv4_encode","encodes IPv4 XOR-MAPPED-ADDRESS");
    attribute.type=KC_STUN_XOR_MAPPED_ADDRESS;
    attribute.data=data;attribute.size=size;
    rc=kc_stun_decode_mapped(&attribute,id,host,sizeof(host),&port);
    stun_test_check(rc==KC_STUN_OK&&strcmp(host,endpoint.host)==0&&port==endpoint.port,
        "mapped_ipv4_decode","decodes IPv4 XOR-MAPPED-ADDRESS");
    endpoint.host="::1";
    size=sizeof(data);
    rc=kc_stun_encode_mapped(&endpoint,id,data,&size);
    stun_test_check(rc==KC_STUN_OK&&size==20,
        "mapped_ipv6_encode","encodes IPv6 XOR-MAPPED-ADDRESS");
    attribute.size=size;
    rc=kc_stun_decode_mapped(&attribute,id,host,sizeof(host),&port);
    stun_test_check(rc==KC_STUN_OK&&strcmp(host,endpoint.host)==0&&port==endpoint.port,
        "mapped_ipv6_decode","decodes IPv6 XOR-MAPPED-ADDRESS");
    {
        kc_stun_message_t response={0},parsed={0};
        kc_stun_attribute_t mapped_attribute={KC_STUN_XOR_MAPPED_ADDRESS,data,size};
        unsigned char encoded[256];
        size_t encoded_size=sizeof(encoded);
        response.type=KC_STUN_SUCCESS_RESPONSE;
        memcpy(response.transaction_id,id,12);
        response.attributes=&mapped_attribute;
        response.attribute_count=1;
        rc=kc_stun_encode(&response,NULL,encoded,&encoded_size);
        stun_test_check(rc==KC_STUN_OK,
            "mapped_response_encode","builds a Binding success response");
        if(rc==KC_STUN_OK){
            rc=kc_stun_decode(encoded,encoded_size,&parsed);
            stun_test_check(rc==KC_STUN_OK&&parsed.type==KC_STUN_SUCCESS_RESPONSE&&
                parsed.attribute_count>=1&&
                kc_stun_decode_mapped(&parsed.attributes[0],id,host,sizeof(host),&port)==KC_STUN_OK&&
                strcmp(host,"::1")==0,
                "mapped_response_decode","parses the mapped endpoint from a response");
            kc_stun_message_free(&parsed);
        }
    }
    stun_test_group("kc_stun_mapped_codec",
        "encodes and decodes IPv4 and IPv6 mapped endpoint attributes");
}

/**
 * Exercises concurrent independent STUN transactions over the same
 * borrowed UDP.
 * @return void.
 */
static void stun_test_shared_udp(void)
{
    kc_stun_transport_t client_udp={NULL,stun_test_client_send};
    kc_stun_transport_t server_udp={NULL,stun_test_server_send};
    kc_stun_server_options_t server_options={0};
    kc_stun_client_options_t options={0};
    kc_stun_client_t *first=NULL,*second=NULL;
    kc_stun_server_t *server=NULL;
    int first_rc,second_rc;
    server_options.transport=&server_udp;
    first_rc=kc_stun_server(&server,&server_options);
    stun_test_check(first_rc==KC_STUN_OK,"shared_server","creates a borrowed UDP Binding responder");
    if(first_rc==KC_STUN_OK){
        stun_test_server=server;
        options.transport=&client_udp;
        options.peer.host="127.0.0.1:42424";
        first_rc=kc_stun_client(&first,&options);
        second_rc=kc_stun_client(&second,&options);
        stun_test_check(first_rc==KC_STUN_OK&&second_rc==KC_STUN_OK,
            "shared_transactions","creates independent transactions on one transport");
        if(first&&second){
            stun_test_client=first;
            stun_test_second_client=second;
            first_rc=kc_stun_client_poll(first,0);
            second_rc=kc_stun_client_poll(second,0);
            stun_test_check(first_rc==KC_STUN_OK&&second_rc==KC_STUN_OK&&
                kc_stun_client_mapped(first)&&kc_stun_client_mapped(second),
                "shared_binding","correlates each Binding reply independently");
        }
        stun_test_second_client=NULL;
        stun_test_client=NULL;
        kc_stun_client_close(first);
        kc_stun_client_close(second);
        kc_stun_server_close(server);
        stun_test_server=NULL;
    }
    stun_test_group("kc_stun_shared_udp",
        "correlates concurrent Binding transactions on caller-owned UDP");
}

/**
 * Verifies public STUN error attribute encoding and decoding.
 * @return void.
 */
static void stun_test_error_codec(void)
{
    unsigned char data[128];
    kc_stun_attribute_t attribute;
    unsigned short code=0;
    char reason[128];
    size_t size=sizeof(data);
    int rc=kc_stun_encode_error(487,"Role Conflict",data,&size);
    stun_test_check(rc==KC_STUN_OK&&size==17,
        "error_encode","encodes an RFC STUN error code and reason");
    attribute.type=KC_STUN_ERROR_CODE;
    attribute.data=data;attribute.size=size;
    rc=kc_stun_decode_error(&attribute,&code,reason,sizeof(reason));
    stun_test_check(rc==KC_STUN_OK&&code==487&&
        strcmp(reason,"Role Conflict")==0,
        "error_decode","decodes STUN error code and reason");
    {
        kc_stun_message_t message={0},parsed={0};
        unsigned char encoded[256];
        kc_stun_attribute_t error_attribute={KC_STUN_ERROR_CODE,data,size};
        size_t encoded_size=sizeof(encoded);
        message.type=KC_STUN_ERROR_RESPONSE;
        message.attributes=&error_attribute;
        message.attribute_count=1;
        rc=kc_stun_encode(&message,NULL,encoded,&encoded_size);
        stun_test_check(rc==KC_STUN_OK,
            "error_response_encode","builds a complete STUN error response");
        if(rc==KC_STUN_OK){
            rc=kc_stun_decode(encoded,encoded_size,&parsed);
            stun_test_check(rc==KC_STUN_OK&&parsed.type==KC_STUN_ERROR_RESPONSE&&
                parsed.attribute_count>=1&&
                kc_stun_decode_error(&parsed.attributes[0],&code,reason,sizeof(reason))==KC_STUN_OK&&
                code==487,"error_response_decode","parses a complete STUN error response");
            kc_stun_message_free(&parsed);
        }
    }
    stun_test_group("kc_stun_error_codec",
        "serializes and parses STUN protocol error attributes");
}

/**
 * Runs the full numbered STUN contract suite.
 * @return Zero when all assertions pass.
 */
static int stun_test_all(void)
{
#ifdef KC_CLI_PATH
    test_case_total = 18;
#else
    test_case_total = 17;
#endif
    test_case_current = 0;
    stun_test_failures = 0;
    stun_group_failures = 0;
    stun_group_checks = 0;
    stun_test_codec();
    stun_test_mapped_codec();
    stun_test_error_codec();
    stun_test_shared_udp();
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
