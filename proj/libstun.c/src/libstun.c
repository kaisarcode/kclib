/**
 * libstun.c - STUN Binding client and server.
 * Summary: Authenticated STUN Binding transactions on owned or borrowed UDP.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "libstun.h"
#include "stun_crypto.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
typedef SOCKET stun_socket_t;
typedef int stun_socklen_t;
typedef HANDLE stun_thread_t;
#define STUN_BAD_SOCKET INVALID_SOCKET
#define stun_socket_close closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int stun_socket_t;
typedef socklen_t stun_socklen_t;
typedef pthread_t stun_thread_t;
#define STUN_BAD_SOCKET (-1)
#define stun_socket_close close
#endif
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
#ifndef KC_STUN_BUILD_VERSION
#define KC_STUN_BUILD_VERSION 0
#endif
#define STUN_MAGIC 0x2112a442U
#define STUN_MAX_PACKET 65556U
#define STUN_DEFAULT_PORT 3478U
#define STUN_DEFAULT_TIMEOUT 3000U
#define STUN_REQUEST 0x0001U
#define STUN_SUCCESS 0x0101U
#define STUN_ERROR 0x0111U
#define STUN_ATTRIBUTE_MAX 16384U
#define STUN_RTO 500U

typedef struct {
    uint16_t type;
    const uint8_t *data;
    size_t size;
    size_t offset;
} stun_attribute_view_t;
typedef struct {
    uint16_t type;
    uint8_t id[12];
    stun_attribute_view_t *attrs;
    size_t count;
} stun_packet_view_t;

struct kc_stun_client {
    stun_socket_t socket;
    int borrowed;
    kc_stun_transport_t transport;
    struct sockaddr_storage remote;
    stun_socklen_t remote_len;
    char remote_host[NI_MAXHOST];
    kc_stun_endpoint_t peer;
    char local_host[NI_MAXHOST];
    kc_stun_endpoint_t local;
    char mapped_host[NI_MAXHOST];
    kc_stun_endpoint_t mapped;
    uint8_t id[12];
    uint8_t *message;
    size_t message_size;
    uint8_t *key;
    size_t key_size;
    unsigned integrity;
    int status;
    unsigned short error;
    uint64_t deadline;
    uint64_t next_retry;
    uint64_t rto;
    uint8_t *answer;
    kc_stun_attribute_t *response;
    size_t response_count;
#ifdef _WIN32
    int wsa;
#endif
};

struct kc_stun_server {
    stun_socket_t socket4;
    stun_socket_t socket6;
    stun_thread_t thread;
    int thread_started;
    atomic_int stop;
    unsigned short port;
    int borrowed;
    kc_stun_transport_t transport;
    kc_stun_credentials_fn credentials;
    kc_stun_binding_fn binding;
    void *context;
#ifdef _WIN32
    int wsa;
#endif
};

static uint16_t stun_rd16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }
static uint32_t stun_rd32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void stun_wr16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
static void stun_wr32(uint8_t *p,uint32_t v) { p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }

static uint64_t stun_now(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec t;
    return clock_gettime(CLOCK_MONOTONIC,&t)==0 ? (uint64_t)t.tv_sec*1000U+(uint64_t)t.tv_nsec/1000000U : 0U;
#endif
}
static uint64_t stun_deadline(uint64_t duration)
{
    uint64_t n=stun_now();return duration>UINT64_MAX-n?UINT64_MAX:n+duration;
}
static uint64_t stun_remaining(uint64_t end)
{
    uint64_t n=stun_now();return n>=end?0:end-n;
}
static int stun_random(uint8_t *out,size_t size)
{
#ifdef _WIN32
    return size<=ULONG_MAX && BCryptGenRandom(NULL,out,(ULONG)size,BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;
#else
    size_t pos=0;
    int fd=open("/dev/urandom",O_RDONLY);
    if(fd<0)return 0;
    while(pos<size){ssize_t n=read(fd,out+pos,size-pos);if(n>0)pos+=(size_t)n;else if(n<0&&errno==EINTR)continue;else {close(fd);return 0;}}
    close(fd);return 1;
#endif
}
static int stun_runtime_start(void)
{
#ifdef _WIN32
    WSADATA data;return WSAStartup(MAKEWORD(2,2),&data)==0;
#else
    return 1;
#endif
}
static void stun_runtime_stop(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}
static int stun_parse_host(const char *text,char host[NI_MAXHOST],unsigned short *port)
{
    const char *begin,*end,*last=NULL,*p;
    size_t length;unsigned long value;char *tail;
    if(!text||!*text||!port)return 0;
    begin=strncmp(text,"stun:",5)==0?text+5:text;
    if(!*begin)return 0;
    *port=STUN_DEFAULT_PORT;
    if(*begin=='['){
        end=strchr(begin+1,']');if(!end||end==begin+1)return 0;
        length=(size_t)(end-begin-1);
        if(length>=NI_MAXHOST)return 0;
        memcpy(host,begin+1,length);host[length]=0;
        if(!end[1])return 1;
        if(end[1]!=':'||!end[2])return 0;
        p=end+2;
    }else{
        int colons=0;
        for(p=begin;*p;p++)if(*p==':'){colons++;last=p;}
        end=colons==1?last:begin+strlen(begin);
        length=(size_t)(end-begin);
        if(!length||length>=NI_MAXHOST)return 0;
        memcpy(host,begin,length);host[length]=0;
        if(colons!=1)return 1;
        p=last+1;
    }
    if(!*p||*p=='-')return 0;
    errno=0;value=strtoul(p,&tail,10);
    if(errno||*tail||!value||value>65535U)return 0;
    *port=(unsigned short)value;
    return 1;
}
static int stun_endpoint_text(const struct sockaddr_storage *a,stun_socklen_t len,char host[NI_MAXHOST],unsigned short *port)
{
    char service[16],*tail;unsigned long v;
    if(getnameinfo((const struct sockaddr *)a,len,host,NI_MAXHOST,service,sizeof(service),NI_NUMERICHOST|NI_NUMERICSERV)!=0)return 0;
    v=strtoul(service,&tail,10);if(*tail||v>65535U)return 0;
    *port=(unsigned short)v;return 1;
}
static int stun_endpoint_equal(const struct sockaddr_storage *a,const struct sockaddr_storage *b)
{
    if(a->ss_family!=b->ss_family)return 0;
    if(a->ss_family==AF_INET){const struct sockaddr_in *x=(const struct sockaddr_in *)a,*y=(const struct sockaddr_in *)b;return x->sin_port==y->sin_port&&x->sin_addr.s_addr==y->sin_addr.s_addr;}
    if(a->ss_family==AF_INET6){const struct sockaddr_in6 *x=(const struct sockaddr_in6 *)a,*y=(const struct sockaddr_in6 *)b;return x->sin6_port==y->sin6_port&&x->sin6_scope_id==y->sin6_scope_id&&memcmp(&x->sin6_addr,&y->sin6_addr,sizeof(x->sin6_addr))==0;}
    return 0;
}
static int stun_resolve(const char *host,unsigned short port,int family,struct sockaddr_storage *addr,stun_socklen_t *len)
{
    struct addrinfo hints,*head=NULL,*it;char service[16];int found=0;
    memset(&hints,0,sizeof(hints));hints.ai_family=family;hints.ai_socktype=SOCK_DGRAM;hints.ai_protocol=IPPROTO_UDP;
    snprintf(service,sizeof(service),"%u",port);
    if(getaddrinfo(host,service,&hints,&head)!=0)return 0;
    for(it=head;it;it=it->ai_next){if(it->ai_addrlen&&it->ai_addrlen<=sizeof(*addr)){memset(addr,0,sizeof(*addr));memcpy(addr,it->ai_addr,it->ai_addrlen);*len=(stun_socklen_t)it->ai_addrlen;found=1;break;}}
    freeaddrinfo(head);return found;
}
static stun_socket_t stun_bind(const char *host,unsigned short port,int family)
{
    struct addrinfo hints,*head=NULL,*it;char service[16];stun_socket_t socket_fd=STUN_BAD_SOCKET;
    memset(&hints,0,sizeof(hints));hints.ai_family=family;hints.ai_socktype=SOCK_DGRAM;hints.ai_protocol=IPPROTO_UDP;hints.ai_flags=AI_PASSIVE;
    snprintf(service,sizeof(service),"%u",port);
    if(getaddrinfo(host&&*host?host:NULL,service,&hints,&head)!=0)return STUN_BAD_SOCKET;
    for(it=head;it;it=it->ai_next){
        socket_fd=socket(it->ai_family,it->ai_socktype,it->ai_protocol);
        if(socket_fd==STUN_BAD_SOCKET)continue;
        if(it->ai_family==AF_INET6){int one=1;if(setsockopt(socket_fd,IPPROTO_IPV6,IPV6_V6ONLY,(const char *)&one,(stun_socklen_t)sizeof(one))!=0){stun_socket_close(socket_fd);socket_fd=STUN_BAD_SOCKET;continue;}}
        if(bind(socket_fd,it->ai_addr,(stun_socklen_t)it->ai_addrlen)==0)break;
        stun_socket_close(socket_fd);socket_fd=STUN_BAD_SOCKET;
    }
    freeaddrinfo(head);return socket_fd;
}
static int stun_socket_send(stun_socket_t fd,const struct sockaddr_storage *addr,stun_socklen_t length,const uint8_t *data,size_t size)
{
    int n;
    if(size>INT_MAX)return KC_STUN_EINVAL;
#ifdef _WIN32
    n=sendto(fd,(const char *)data,(int)size,0,(const struct sockaddr *)addr,length);
#else
    n=(int)sendto(fd,data,size,0,(const struct sockaddr *)addr,length);
#endif
    return n==(int)size?KC_STUN_OK:KC_STUN_ENET;
}
static int stun_wait(stun_socket_t fd,uint64_t ms)
{
    fd_set fds;struct timeval tv;int rc;
    FD_ZERO(&fds);FD_SET(fd,&fds);
    tv.tv_sec=(long)(ms/1000U);tv.tv_usec=(long)((ms%1000U)*1000U);
#ifdef _WIN32
    rc=select(0,&fds,NULL,NULL,&tv);
    if(rc<0&&WSAGetLastError()==WSAEINTR)return 0;
#else
    rc=select(fd+1,&fds,NULL,NULL,&tv);
    if(rc<0&&errno==EINTR)return 0;
#endif
    return rc>0?1:rc==0?0:-1;
}

/* Strictly parses the STUN wire envelope and every padded attribute. */
static int stun_unpack(const uint8_t *data,size_t size,stun_packet_view_t *out)
{
    size_t offset,n=0,capacity;
    if(!data||!out||size<20U||(data[0]&0xc0U)||stun_rd32(data+4)!=STUN_MAGIC||
        (stun_rd16(data+2)&3U)||size!=20U+(size_t)stun_rd16(data+2))return 0;
    memset(out,0,sizeof(*out));out->type=stun_rd16(data);memcpy(out->id,data+8,12);
    capacity=(size-20U)/4U;
    if(capacity>STUN_ATTRIBUTE_MAX)return 0;
    if(capacity){out->attrs=(stun_attribute_view_t *)calloc(capacity,sizeof(*out->attrs));if(!out->attrs)return 0;}
    for(offset=20U;offset<size;){
        size_t v,pad;unsigned short len;
        if(size-offset<4U)goto invalid;
        len=stun_rd16(data+offset+2);v=offset+4U;pad=((size_t)len+3U)&~3U;
        if(pad>size-v)goto invalid;
        out->attrs[n].type=stun_rd16(data+offset);
        out->attrs[n].data=data+v;
        out->attrs[n].size=len;
        out->attrs[n].offset=offset;
        n++;offset=v+pad;
    }
    out->count=n;
    return 1;
invalid:
    free(out->attrs);memset(out,0,sizeof(*out));return 0;
}
static void stun_unpack_free(stun_packet_view_t *p){free(p->attrs);memset(p,0,sizeof(*p));}
static const stun_attribute_view_t *stun_find(const stun_packet_view_t *p,uint16_t type)
{
    size_t i;for(i=0;i<p->count;i++)if(p->attrs[i].type==type)return &p->attrs[i];return NULL;
}
static int stun_fingerprint_valid(const uint8_t *data,size_t size,const stun_packet_view_t *p)
{
    const stun_attribute_view_t *fp=stun_find(p,KC_STUN_FINGERPRINT);
    if(!fp)return 1;
    if(fp->size!=4||fp->offset+8U!=size)return 0;
    return stun_rd32(fp->data)==(stun_crc32(data,fp->offset)^0x5354554eU);
}
static int stun_integrity_valid(const uint8_t *data,size_t size,const stun_packet_view_t *p,const void *key,size_t key_size)
{
    const stun_attribute_view_t *mi=stun_find(p,KC_STUN_MESSAGE_INTEGRITY_SHA256);
    uint8_t *copy,mac[32];size_t bytes;unsigned algorithm;
    (void)size;
    if(mi){algorithm=2;if(mi->size!=32)return 0;}
    else{mi=stun_find(p,KC_STUN_MESSAGE_INTEGRITY);algorithm=1;if(!mi||mi->size!=20)return 0;}
    bytes=mi->offset;
    copy=(uint8_t *)malloc(bytes);
    if(!copy)return 0;
    memcpy(copy,data,bytes);stun_wr16(copy+2,(uint16_t)(bytes+4U+mi->size-20U));
    stun_hmac(algorithm,key,key_size,copy,bytes,mac);
    free(copy);
    return stun_equal(mac,mi->data,mi->size);
}
static void stun_begin(uint8_t *out,uint16_t type,const uint8_t id[12])
{
    memset(out,0,20);stun_wr16(out,type);stun_wr32(out+4,STUN_MAGIC);memcpy(out+8,id,12);
}
static int stun_append(uint8_t *out,size_t *size,uint16_t type,const void *value,size_t n)
{
    size_t padded;
    if(n>65535U)return 0;
    padded=(n+3U)&~3U;
    if(*size+4U+padded>STUN_MAX_PACKET||*size+4U+padded-20U>65535U)return 0;
    stun_wr16(out+*size,type);stun_wr16(out+*size+2,(uint16_t)n);
    memset(out+*size+4,0,padded);
    if(n)memcpy(out+*size+4,value,n);
    *size+=4U+padded;stun_wr16(out+2,(uint16_t)(*size-20U));return 1;
}
static int stun_sign(uint8_t *out,size_t *size,const void *key,size_t key_size,unsigned algorithm)
{
    uint8_t zero[32]={0},mac[32];size_t at=*size,n=algorithm==1?20U:32U;
    if(!stun_append(out,size,algorithm==1?KC_STUN_MESSAGE_INTEGRITY:KC_STUN_MESSAGE_INTEGRITY_SHA256,zero,n))return 0;
    stun_hmac(algorithm,key,key_size,out,at,mac);
    memcpy(out+at+4,mac,n);return 1;
}
static int stun_finish(uint8_t *out,size_t *size)
{
    uint8_t zero[4]={0};size_t at=*size;
    if(!stun_append(out,size,KC_STUN_FINGERPRINT,zero,4))return 0;
    stun_wr32(out+at+4,stun_crc32(out,at)^0x5354554eU);return 1;
}
static int stun_build_request(uint8_t *data,size_t *size,const uint8_t id[12],const kc_stun_client_options_t *o)
{
    size_t n=20U,i;
    stun_begin(data,STUN_REQUEST,id);
    if(o->credentials.key){
        size_t len=strlen(o->credentials.username);
        if(!stun_append(data,&n,KC_STUN_USERNAME,o->credentials.username,len))return 0;
    }
    for(i=0;i<o->attribute_count;i++){
        const kc_stun_attribute_t *a=&o->attributes[i];
        if(a->type==KC_STUN_MESSAGE_INTEGRITY||a->type==KC_STUN_MESSAGE_INTEGRITY_SHA256||a->type==KC_STUN_FINGERPRINT||
            a->type==KC_STUN_USERNAME||a->type==KC_STUN_ERROR_CODE||(!a->data&&a->size))return 0;
        if(!stun_append(data,&n,a->type,a->data,a->size))return 0;
    }
    if((o->credentials.integrity?o->credentials.integrity:KC_STUN_HMAC_BOTH)&KC_STUN_HMAC_SHA1)
        if(!stun_sign(data,&n,o->credentials.key,o->credentials.key_size,1))return 0;
    if((o->credentials.integrity?o->credentials.integrity:KC_STUN_HMAC_BOTH)&KC_STUN_HMAC_SHA256)
        if(!stun_sign(data,&n,o->credentials.key,o->credentials.key_size,2))return 0;
    if(!stun_finish(data,&n))return 0;
    *size=n;return 1;
}
static int stun_mapped(const stun_attribute_view_t *attr,const uint8_t id[12],char host[NI_MAXHOST],unsigned short *port)
{
    uint8_t mask[16]={0x21,0x12,0xa4,0x42},ip[16];int family;size_t length,i;
    if(!attr||attr->size<8U||attr->data[0]!=0)return 0;
    family=attr->data[1]==1?AF_INET:attr->data[1]==2?AF_INET6:0;
    if(!family)return 0;
    length=family==AF_INET?4U:16U;
    if(attr->size!=length+4U)return 0;
    *port=(unsigned short)(stun_rd16(attr->data+2)^(STUN_MAGIC>>16));
    memcpy(mask+4,id,12);
    for(i=0;i<length;i++)ip[i]=(uint8_t)(attr->data[i+4]^mask[i]);
    return inet_ntop(family,ip,host,NI_MAXHOST)!=NULL;
}
static int stun_append_mapped(uint8_t *out,size_t *size,const struct sockaddr_storage *from,const uint8_t id[12])
{
    uint8_t data[20]={0},mask[16]={0x21,0x12,0xa4,0x42},ip[16];size_t n,i;unsigned short port;
    if(from->ss_family==AF_INET){const struct sockaddr_in *a=(const struct sockaddr_in *)from;memcpy(ip,&a->sin_addr,4);port=ntohs(a->sin_port);n=4;data[1]=1;}
    else if(from->ss_family==AF_INET6){const struct sockaddr_in6 *a=(const struct sockaddr_in6 *)from;memcpy(ip,&a->sin6_addr,16);port=ntohs(a->sin6_port);n=16;data[1]=2;}
    else return 0;
    stun_wr16(data+2,(unsigned short)(port^(STUN_MAGIC>>16)));memcpy(mask+4,id,12);
    for(i=0;i<n;i++)data[4+i]=(uint8_t)(ip[i]^mask[i]);
    return stun_append(out,size,KC_STUN_XOR_MAPPED_ADDRESS,data,4U+n);
}
static int stun_append_error(uint8_t *out,size_t *size,unsigned short code)
{
    uint8_t data[4];const char *phrase="Error";uint8_t value[64];size_t len;
    if(code==400)phrase="Bad Request";
    else if(code==401)phrase="Unauthenticated";
    else if(code==420)phrase="Unknown Attribute";
    else if(code==487)phrase="Role Conflict";
    memset(data,0,sizeof(data));data[2]=(uint8_t)(code/100);data[3]=(uint8_t)(code%100);
    len=strlen(phrase);memcpy(value,data,4);memcpy(value+4,phrase,len);
    return stun_append(out,size,KC_STUN_ERROR_CODE,value,len+4U);
}
static unsigned short stun_error_code(const stun_packet_view_t *p)
{
    const stun_attribute_view_t *a=stun_find(p,KC_STUN_ERROR_CODE);
    if(!a||a->size<4||a->data[2]>6||a->data[3]>99)return 0;
    return (unsigned short)(100U*a->data[2]+a->data[3]);
}
static int stun_credentials_valid(const kc_stun_credentials_t *a)
{
    if(!a->username&&!a->key&&!a->key_size&&!a->integrity)return 1;
    return a->username&&a->key&&a->key_size&&!(a->integrity&~KC_STUN_HMAC_BOTH);
}

/**
 * Begins an asynchronous STUN Binding client transaction.
 * @return KC_STUN_OK or a negative error.
 */
int kc_stun_client(kc_stun_client_t **out,const kc_stun_client_options_t *o)
{
    kc_stun_client_t *c;char host[NI_MAXHOST];unsigned short port;struct sockaddr_storage addr;stun_socklen_t addr_len;
    if(!out||!o||!o->peer.host||(!o->attributes&&o->attribute_count)||
        !stun_credentials_valid(&o->credentials)||
        (o->transport&&!o->transport->send))return KC_STUN_EINVAL;
    *out=NULL;
    if(!stun_parse_host(o->peer.host,host,&port))return KC_STUN_EINVAL;
    if(o->peer.port)port=o->peer.port;
    if(!stun_runtime_start())return KC_STUN_ENET;
    c=(kc_stun_client_t *)calloc(1,sizeof(*c));
    if(!c){stun_runtime_stop();return KC_STUN_ENOMEM;}
    c->socket=STUN_BAD_SOCKET;
#ifdef _WIN32
    c->wsa=1;
#endif
    c->borrowed=o->transport!=NULL;
    if(o->transport)c->transport=*o->transport;
    if(!stun_resolve(host,port,AF_UNSPEC,&addr,&addr_len)){kc_stun_client_close(c);return KC_STUN_ENET;}
    if(!stun_endpoint_text(&addr,addr_len,c->remote_host,&port)){kc_stun_client_close(c);return KC_STUN_ENET;}
    c->peer.host=c->remote_host;c->peer.port=port;c->remote=addr;c->remote_len=addr_len;
    if(!c->borrowed){
        c->socket=stun_bind(o->host,o->port,addr.ss_family);
        if(c->socket==STUN_BAD_SOCKET){kc_stun_client_close(c);return KC_STUN_ENET;}
        {struct sockaddr_storage local;stun_socklen_t len=(stun_socklen_t)sizeof(local);
         if(getsockname(c->socket,(struct sockaddr *)&local,&len)!=0||
            !stun_endpoint_text(&local,len,c->local_host,&c->local.port)){kc_stun_client_close(c);return KC_STUN_ENET;}
         c->local.host=c->local_host;}
    }
    if(o->credentials.key){
        c->key=(uint8_t *)malloc(o->credentials.key_size);
        if(!c->key){kc_stun_client_close(c);return KC_STUN_ENOMEM;}
        memcpy(c->key,o->credentials.key,o->credentials.key_size);
        c->key_size=o->credentials.key_size;
        c->integrity=o->credentials.integrity?o->credentials.integrity:KC_STUN_HMAC_BOTH;
    }
    c->message=(uint8_t *)malloc(STUN_MAX_PACKET);
    if(!c->message){kc_stun_client_close(c);return KC_STUN_ENOMEM;}
    if(!stun_random(c->id,12)||!stun_build_request(c->message,&c->message_size,c->id,o)){
        kc_stun_client_close(c);return KC_STUN_EINVAL;
    }
    c->status=KC_STUN_PENDING;
    c->rto=STUN_RTO;c->deadline=stun_deadline(o->timeout_ms?o->timeout_ms:STUN_DEFAULT_TIMEOUT);
    c->next_retry=stun_now();
    *out=c;
    return KC_STUN_OK;
}
static int stun_client_send(kc_stun_client_t *c)
{
    if(c->borrowed){int rc=c->transport.send(c->transport.context,&c->peer,c->message,c->message_size);return rc==0?KC_STUN_OK:KC_STUN_ENET;}
    return stun_socket_send(c->socket,&c->remote,c->remote_len,c->message,c->message_size);
}

/** Consumes one correlated Binding response without owning UDP receive. */
int kc_stun_client_receive(kc_stun_client_t *c,const kc_stun_endpoint_t *peer,const void *bytes,size_t size)
{
    stun_packet_view_t parsed;const uint8_t *raw=(const uint8_t *)bytes;const stun_attribute_view_t *mapped;
    size_t i;
    if(!c||!peer||!peer->host||!bytes)return KC_STUN_EINVAL;
    if(c->status!=KC_STUN_PENDING)return c->status;
    if(peer->port!=c->peer.port||strcmp(peer->host,c->peer.host)!=0)return KC_STUN_PENDING;
    if(!stun_unpack(raw,size,&parsed))return KC_STUN_PENDING;
    if(memcmp(parsed.id,c->id,12)||
       (parsed.type!=STUN_SUCCESS&&parsed.type!=STUN_ERROR)||
       !stun_fingerprint_valid(raw,size,&parsed)||
       (c->integrity&&!stun_integrity_valid(raw,size,&parsed,c->key,c->key_size))){
        stun_unpack_free(&parsed);return KC_STUN_PENDING;
    }
    if(parsed.type==STUN_ERROR){
        c->error=stun_error_code(&parsed);
        c->status=c->error==401?KC_STUN_EAUTH:KC_STUN_EREJECTED;
    }else{
        mapped=stun_find(&parsed,KC_STUN_XOR_MAPPED_ADDRESS);
        if(!stun_mapped(mapped,c->id,c->mapped_host,&c->mapped.port)){
            c->status=KC_STUN_EPROTO;
        }else{
            c->mapped.host=c->mapped_host;c->status=KC_STUN_OK;
        }
    }
    if(c->status==KC_STUN_OK||c->status==KC_STUN_EREJECTED||c->status==KC_STUN_EAUTH){
        c->answer=(uint8_t *)malloc(size);
        c->response=(kc_stun_attribute_t *)calloc(parsed.count?parsed.count:1,sizeof(*c->response));
        if(!c->answer||!c->response){free(c->answer);free(c->response);c->answer=NULL;c->response=NULL;c->status=KC_STUN_ENOMEM;}
        else{
            memcpy(c->answer,raw,size);c->response_count=parsed.count;
            for(i=0;i<parsed.count;i++){
                c->response[i].type=parsed.attrs[i].type;c->response[i].data=c->answer+(parsed.attrs[i].data-raw);
                c->response[i].size=parsed.attrs[i].size;
            }
        }
    }
    stun_unpack_free(&parsed);return c->status;
}

/** Drives client retransmission deadlines and owned UDP reception. */
int kc_stun_client_poll(kc_stun_client_t *c,uint64_t wait_ms)
{
    uint64_t now,next,remaining;
    int ready;
    if(!c)return KC_STUN_EINVAL;
    if(c->status!=KC_STUN_PENDING)return c->status;
    now=stun_now();
    if(now>=c->deadline){c->status=KC_STUN_ETIMEOUT;return c->status;}
    if(now>=c->next_retry){
        int rc=stun_client_send(c);
        if(rc!=KC_STUN_OK){c->status=rc;return rc;}
        c->next_retry=stun_deadline(c->rto);
        if(c->rto<1600U)c->rto*=2U;
    }
    if(c->borrowed)return c->status;
    remaining=stun_remaining(c->deadline);
    next=stun_remaining(c->next_retry);
    if(wait_ms<remaining)remaining=wait_ms;
    if(next<remaining)remaining=next;
    ready=stun_wait(c->socket,remaining);
    if(ready<0){c->status=KC_STUN_ENET;return c->status;}
    if(ready>0){
        uint8_t packet[STUN_MAX_PACKET];struct sockaddr_storage from;
        stun_socklen_t flen=(stun_socklen_t)sizeof(from);char host[NI_MAXHOST];unsigned short port;int got;
#ifdef _WIN32
        got=recvfrom(c->socket,(char *)packet,(int)sizeof(packet),0,(struct sockaddr *)&from,&flen);
#else
        got=(int)recvfrom(c->socket,packet,sizeof(packet),0,(struct sockaddr *)&from,&flen);
#endif
        if(got<0){c->status=KC_STUN_ENET;return c->status;}
        if(stun_endpoint_equal(&from,&c->remote)&&stun_endpoint_text(&from,flen,host,&port)){
            kc_stun_endpoint_t source={host,port};
            return kc_stun_client_receive(c,&source,packet,(size_t)got);
        }
    }
    return KC_STUN_PENDING;
}

/** Sends arbitrary UDP data through a preserved STUN client mapping. */
int kc_stun_client_send(kc_stun_client_t *c,const kc_stun_endpoint_t *peer,const void *bytes,size_t size)
{
    struct sockaddr_storage dest;stun_socklen_t len;
    if(!c||!peer||!peer->host||!peer->port||(!bytes&&size))return KC_STUN_EINVAL;
    if(c->borrowed)return c->transport.send(c->transport.context,peer,bytes,size)==0?KC_STUN_OK:KC_STUN_ENET;
    if(c->socket==STUN_BAD_SOCKET)return KC_STUN_ECLOSED;
    if(!stun_resolve(peer->host,peer->port,c->remote.ss_family,&dest,&len))return KC_STUN_ENET;
    return stun_socket_send(c->socket,&dest,len,(const uint8_t *)bytes,size);
}
/** Reads one non-STUN datagram from an owned client mapping. */
int kc_stun_client_read(kc_stun_client_t *c,kc_stun_datagram_t *out,uint64_t timeout_ms)
{
    uint8_t packet[STUN_MAX_PACKET];struct sockaddr_storage from;
    stun_socklen_t len=(stun_socklen_t)sizeof(from);int ready,got;
    char host[NI_MAXHOST];unsigned short port;
    if(!c||!out||c->borrowed)return KC_STUN_EINVAL;
    memset(out,0,sizeof(*out));
    if(c->status==KC_STUN_PENDING)return KC_STUN_PENDING;
    ready=stun_wait(c->socket,timeout_ms);
    if(ready<=0)return ready==0?KC_STUN_ETIMEOUT:KC_STUN_ENET;
#ifdef _WIN32
    got=recvfrom(c->socket,(char *)packet,(int)sizeof(packet),0,(struct sockaddr *)&from,&len);
#else
    got=(int)recvfrom(c->socket,packet,sizeof(packet),0,(struct sockaddr *)&from,&len);
#endif
    if(got<0||!stun_endpoint_text(&from,len,host,&port))return KC_STUN_ENET;
    out->host=(char *)malloc(strlen(host)+1U);
    out->data=malloc(got?(size_t)got:1U);
    if(!out->host||!out->data){kc_stun_datagram_free(out);return KC_STUN_ENOMEM;}
    memcpy(out->host,host,strlen(host)+1U);out->port=port;
    if(got)memcpy(out->data,packet,(size_t)got);
    out->data_size=(size_t)got;
    return KC_STUN_OK;
}
void kc_stun_datagram_free(kc_stun_datagram_t *d)
{
    if(!d)return;
    free(d->host);free(d->data);memset(d,0,sizeof(*d));
}
/** Starts a fresh transaction through the existing mapping and credentials. */
int kc_stun_client_refresh(kc_stun_client_t *c,uint64_t timeout_ms)
{
    stun_packet_view_t p;size_t i;uint8_t mac[32];
    if(!c||c->status==KC_STUN_PENDING)return KC_STUN_EINVAL;
    if(!stun_random(c->id,12))return KC_STUN_ENET;
    memcpy(c->message+8,c->id,12);
    if(!stun_unpack(c->message,c->message_size,&p))return KC_STUN_EPROTO;
    for(i=0;i<p.count;i++){
        stun_attribute_view_t *a=&p.attrs[i];unsigned algorithm=0;
        if(a->type==KC_STUN_MESSAGE_INTEGRITY)algorithm=1;
        else if(a->type==KC_STUN_MESSAGE_INTEGRITY_SHA256)algorithm=2;
        if(algorithm){
            size_t header_length=a->offset+4U+a->size-20U;
            stun_wr16(c->message+2,(uint16_t)header_length);
            stun_hmac(algorithm,c->key,c->key_size,c->message,a->offset,mac);
            memcpy(c->message+a->offset+4,mac,a->size);
        }
    }
    stun_wr16(c->message+2,(uint16_t)(c->message_size-20U));
    for(i=0;i<p.count;i++)if(p.attrs[i].type==KC_STUN_FINGERPRINT){
        stun_wr32(c->message+p.attrs[i].offset+4,stun_crc32(c->message,p.attrs[i].offset)^0x5354554eU);
    }
    stun_unpack_free(&p);
    free(c->answer);free(c->response);c->answer=NULL;c->response=NULL;c->response_count=0;
    c->mapped.host=NULL;c->error=0;c->status=KC_STUN_PENDING;
    c->deadline=stun_deadline(timeout_ms?timeout_ms:STUN_DEFAULT_TIMEOUT);
    c->next_retry=stun_now();c->rto=STUN_RTO;
    return KC_STUN_OK;
}

const kc_stun_endpoint_t *kc_stun_client_local(const kc_stun_client_t *c){return c&&c->local.host?&c->local:NULL;}
const kc_stun_endpoint_t *kc_stun_client_mapped(const kc_stun_client_t *c){return c&&c->status==KC_STUN_OK?&c->mapped:NULL;}
unsigned short kc_stun_client_error(const kc_stun_client_t *c){return c?c->error:0;}
const kc_stun_attribute_t *kc_stun_client_attributes(const kc_stun_client_t *c,size_t *count){if(count)*count=c?c->response_count:0;return c?c->response:NULL;}

void kc_stun_client_close(kc_stun_client_t *c)
{
    if(!c)return;
    if(c->socket!=STUN_BAD_SOCKET)stun_socket_close(c->socket);
    if(c->key){memset(c->key,0,c->key_size);free(c->key);}
    free(c->message);free(c->answer);free(c->response);
#ifdef _WIN32
    if(c->wsa)stun_runtime_stop();
#else
    stun_runtime_stop();
#endif
    free(c);
}

static int stun_server_send(kc_stun_server_t *s,stun_socket_t fd,const struct sockaddr_storage *addr,stun_socklen_t len,const kc_stun_endpoint_t *peer,const uint8_t *data,size_t size)
{
    if(s->borrowed)return s->transport.send(s->transport.context,peer,data,size)==0?KC_STUN_OK:KC_STUN_ENET;
    return stun_socket_send(fd,addr,len,data,size);
}
static int stun_server_process(kc_stun_server_t *s,stun_socket_t fd,const struct sockaddr_storage *addr,stun_socklen_t len,const kc_stun_endpoint_t *peer,const uint8_t *data,size_t size)
{
    stun_packet_view_t p;uint8_t out[STUN_MAX_PACKET];size_t used=20,i;
    unsigned short error=0;kc_stun_credentials_t auth={0};int authenticated=0;
    kc_stun_attribute_t *attrs=NULL;const stun_attribute_view_t *user,*mi;
    if(!stun_unpack(data,size,&p))return KC_STUN_EPROTO;
    if(p.type!=STUN_REQUEST||!stun_fingerprint_valid(data,size,&p)){stun_unpack_free(&p);return KC_STUN_EPROTO;}
    user=stun_find(&p,KC_STUN_USERNAME);
    mi=stun_find(&p,KC_STUN_MESSAGE_INTEGRITY_SHA256);
    if(!mi)mi=stun_find(&p,KC_STUN_MESSAGE_INTEGRITY);
    if(s->credentials){
        char *name;
        if(!user||!mi||user->size>512U){error=401;goto response;}
        name=(char *)malloc(user->size+1U);
        if(!name){stun_unpack_free(&p);return KC_STUN_ENOMEM;}
        memcpy(name,user->data,user->size);name[user->size]=0;
        if(memchr(name,0,user->size)||s->credentials(s->context,name,&auth)!=0||
           !auth.key||!auth.key_size||!stun_integrity_valid(data,size,&p,auth.key,auth.key_size))error=401;
        else authenticated=1;
        free(name);
        if(error)goto response;
    }
    if(!s->binding){
        for(i=0;i<p.count;i++){
            unsigned short type=p.attrs[i].type;
            if(type<0x8000U&&type!=KC_STUN_USERNAME&&
               type!=KC_STUN_MESSAGE_INTEGRITY&&
               type!=KC_STUN_MESSAGE_INTEGRITY_SHA256&&
               type!=KC_STUN_REALM&&type!=KC_STUN_NONCE&&
               type!=KC_STUN_ERROR_CODE&&type!=KC_STUN_UNKNOWN_ATTRIBUTES&&
               type!=KC_STUN_XOR_MAPPED_ADDRESS){
                error=420;break;
            }
        }
        if(error)goto response;
    }
    if(s->binding){
        attrs=(kc_stun_attribute_t *)calloc(p.count?p.count:1U,sizeof(*attrs));
        if(!attrs){stun_unpack_free(&p);return KC_STUN_ENOMEM;}
        for(i=0;i<p.count;i++){attrs[i].type=p.attrs[i].type;attrs[i].data=p.attrs[i].data;attrs[i].size=p.attrs[i].size;}
        error=s->binding(s->context,peer,attrs,p.count);
        free(attrs);
        if(error&&((error<300)||(error>699)))error=400;
    }
response:
    stun_begin(out,error?STUN_ERROR:STUN_SUCCESS,p.id);
    if(error){if(!stun_append_error(out,&used,error))goto invalid;}
    else if(!addr||!stun_append_mapped(out,&used,addr,p.id)){
        /* Borrowed transports do not expose sockaddr structures; resolve the
           peer's numeric endpoint for XOR-MAPPED-ADDRESS. */
        struct sockaddr_storage endpoint;stun_socklen_t elen;
        if(!stun_resolve(peer->host,peer->port,AF_UNSPEC,&endpoint,&elen)||
           !stun_append_mapped(out,&used,&endpoint,p.id))goto invalid;
    }
    if(authenticated){
        unsigned alg=stun_find(&p,KC_STUN_MESSAGE_INTEGRITY_SHA256)?2U:1U;
        if(!stun_sign(out,&used,auth.key,auth.key_size,alg))goto invalid;
    }
    if(!stun_finish(out,&used))goto invalid;
    stun_unpack_free(&p);
    return stun_server_send(s,fd,addr,len,peer,out,used);
invalid:
    stun_unpack_free(&p);return KC_STUN_EPROTO;
}

int kc_stun_server_receive(kc_stun_server_t *s,const kc_stun_endpoint_t *peer,const void *bytes,size_t size)
{
    if(!s||!s->borrowed||!peer||!peer->host||!peer->port||!bytes)return KC_STUN_EINVAL;
    return stun_server_process(s,STUN_BAD_SOCKET,NULL,0,peer,(const uint8_t *)bytes,size);
}

static void stun_server_read(kc_stun_server_t *s,stun_socket_t fd)
{
    uint8_t packet[STUN_MAX_PACKET];struct sockaddr_storage addr;stun_socklen_t len=(stun_socklen_t)sizeof(addr);
    char host[NI_MAXHOST];unsigned short port;int got;
#ifdef _WIN32
    got=recvfrom(fd,(char *)packet,(int)sizeof(packet),0,(struct sockaddr *)&addr,&len);
#else
    got=(int)recvfrom(fd,packet,sizeof(packet),0,(struct sockaddr *)&addr,&len);
#endif
    if(got<20||!stun_endpoint_text(&addr,len,host,&port))return;
    {kc_stun_endpoint_t peer={host,port};(void)stun_server_process(s,fd,&addr,len,&peer,packet,(size_t)got);}
}
#ifdef _WIN32
static DWORD WINAPI stun_server_thread(LPVOID arg)
#else
static void *stun_server_thread(void *arg)
#endif
{
    kc_stun_server_t *s=(kc_stun_server_t *)arg;
    while(!atomic_load(&s->stop)){
        fd_set fds;struct timeval tv;int ready;
#ifndef _WIN32
        stun_socket_t maxfd=0;
#endif
        FD_ZERO(&fds);
        if(s->socket4!=STUN_BAD_SOCKET){FD_SET(s->socket4,&fds);
#ifndef _WIN32
            if(s->socket4>maxfd)maxfd=s->socket4;
#endif
        }
        if(s->socket6!=STUN_BAD_SOCKET){FD_SET(s->socket6,&fds);
#ifndef _WIN32
            if(s->socket6>maxfd)maxfd=s->socket6;
#endif
        }
        tv.tv_sec=0;tv.tv_usec=100000;
#ifdef _WIN32
        ready=select(0,&fds,NULL,NULL,&tv);
#else
        ready=select(maxfd+1,&fds,NULL,NULL,&tv);
#endif
        if(ready<=0)continue;
        if(s->socket4!=STUN_BAD_SOCKET&&FD_ISSET(s->socket4,&fds))stun_server_read(s,s->socket4);
        if(s->socket6!=STUN_BAD_SOCKET&&FD_ISSET(s->socket6,&fds))stun_server_read(s,s->socket6);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int kc_stun_server(kc_stun_server_t **out,const kc_stun_server_options_t *options)
{
    kc_stun_server_t *s;unsigned short port;struct sockaddr_storage addr;stun_socklen_t len;
    if(!out||!options||(options->transport&&!options->transport->send))return KC_STUN_EINVAL;
    *out=NULL;
    if(!stun_runtime_start())return KC_STUN_ENET;
    s=(kc_stun_server_t *)calloc(1,sizeof(*s));if(!s){stun_runtime_stop();return KC_STUN_ENOMEM;}
    s->socket4=STUN_BAD_SOCKET;s->socket6=STUN_BAD_SOCKET;
#ifdef _WIN32
    s->wsa=1;
#endif
    atomic_init(&s->stop,0);s->borrowed=options->transport!=NULL;
    s->port=options->port;s->credentials=options->credentials;s->binding=options->binding;s->context=options->context;
    if(s->borrowed){s->transport=*options->transport;*out=s;return KC_STUN_OK;}
    port=options->port;
    if(!options->host||!*options->host){
        s->socket4=stun_bind(NULL,port,AF_INET);
        if(s->socket4!=STUN_BAD_SOCKET){len=(stun_socklen_t)sizeof(addr);
            if(getsockname(s->socket4,(struct sockaddr *)&addr,&len)==0)s->port=ntohs(((struct sockaddr_in *)&addr)->sin_port);}
        s->socket6=stun_bind(NULL,s->port?s->port:port,AF_INET6);
    }else{
        s->socket4=stun_bind(options->host,port,AF_INET);
        s->socket6=stun_bind(options->host,port,AF_INET6);
    }
    if(s->socket4==STUN_BAD_SOCKET&&s->socket6==STUN_BAD_SOCKET){kc_stun_server_close(s);return KC_STUN_ENET;}
    if(!s->port){stun_socket_t fd=s->socket4!=STUN_BAD_SOCKET?s->socket4:s->socket6;len=(stun_socklen_t)sizeof(addr);
        if(getsockname(fd,(struct sockaddr *)&addr,&len)!=0){kc_stun_server_close(s);return KC_STUN_ENET;}
        s->port=addr.ss_family==AF_INET?ntohs(((struct sockaddr_in *)&addr)->sin_port):ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    }
#ifdef _WIN32
    s->thread=CreateThread(NULL,0,stun_server_thread,s,0,NULL);
    s->thread_started=s->thread!=NULL;
#else
    s->thread_started=pthread_create(&s->thread,NULL,stun_server_thread,s)==0;
#endif
    if(!s->thread_started){kc_stun_server_close(s);return KC_STUN_ENET;}
    *out=s;return KC_STUN_OK;
}
unsigned short kc_stun_server_port(const kc_stun_server_t *s){return s?s->port:0;}
void kc_stun_server_close(kc_stun_server_t *s)
{
    if(!s)return;
    atomic_store(&s->stop,1);
    if(s->thread_started){
#ifdef _WIN32
        WaitForSingleObject(s->thread,INFINITE);CloseHandle(s->thread);
#else
        pthread_join(s->thread,NULL);
#endif
    }
    if(s->socket4!=STUN_BAD_SOCKET)stun_socket_close(s->socket4);
    if(s->socket6!=STUN_BAD_SOCKET)stun_socket_close(s->socket6);
#ifdef _WIN32
    if(s->wsa)stun_runtime_stop();
#else
    stun_runtime_stop();
#endif
    free(s);
}
const char *kc_stun_strerror(int status)
{
    switch(status){
    case KC_STUN_OK:return "ok";case KC_STUN_PENDING:return "pending";
    case KC_STUN_EINVAL:return "invalid argument";case KC_STUN_ENET:return "network error";
    case KC_STUN_ETIMEOUT:return "timeout";case KC_STUN_EPROTO:return "invalid STUN message";
    case KC_STUN_ENOMEM:return "out of memory";case KC_STUN_ECLOSED:return "closed";
    case KC_STUN_EAUTH:return "unauthenticated";case KC_STUN_EREJECTED:return "STUN request rejected";
    default:return "unknown STUN status";
    }
}
uint64_t kc_stun_version(void){return (uint64_t)KC_STUN_BUILD_VERSION;}