#include "librstrm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

typedef struct endpoint endpoint_t;
struct endpoint {
    kc_rstrm_t *stream;
    endpoint_t *peer;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
    unsigned char received[4096];
    size_t received_size;
    int eof_count;
    int drain_count;
    int error_status;
    int fail_output;
};
static void lock(endpoint_t*e){
#ifdef _WIN32
EnterCriticalSection(&e->mutex);
#else
pthread_mutex_lock(&e->mutex);
#endif
}
static void unlock(endpoint_t*e){
#ifdef _WIN32
LeaveCriticalSection(&e->mutex);
#else
pthread_mutex_unlock(&e->mutex);
#endif
}
static void sleep_ms(unsigned ms){
#ifdef _WIN32
Sleep(ms);
#else
struct timespec t={(time_t)(ms/1000U),(long)(ms%1000U)*1000000L};nanosleep(&t,NULL);
#endif
}
static void received(const void*d,size_t n,void*u){endpoint_t*e=u;lock(e);if(e->received_size+n<=sizeof(e->received)){memcpy(e->received+e->received_size,d,n);e->received_size+=n;}unlock(e);}
static int datagram(const void*d,size_t n,void*u){endpoint_t*e=u;if(e->fail_output)return -1;return e->peer&&e->peer->stream?kc_rstrm_input(e->peer->stream,d,n):0;}
static void eof_cb(void*u){endpoint_t*e=u;lock(e);e->eof_count++;unlock(e);}
static void drain_cb(void*u){endpoint_t*e=u;lock(e);e->drain_count++;unlock(e);}
static void error_cb(int s,void*u){endpoint_t*e=u;lock(e);e->error_status=s;unlock(e);}
static void init(endpoint_t*e){memset(e,0,sizeof(*e));
#ifdef _WIN32
InitializeCriticalSection(&e->mutex);
#else
pthread_mutex_init(&e->mutex,NULL);
#endif
}
static void fini(endpoint_t*e){
#ifdef _WIN32
DeleteCriticalSection(&e->mutex);
#else
pthread_mutex_destroy(&e->mutex);
#endif
}
static int wait_received(endpoint_t*e,size_t n,unsigned timeout){unsigned elapsed=0;while(elapsed<timeout){size_t got;lock(e);got=e->received_size;unlock(e);if(got>=n)return 1;sleep_ms(10);elapsed+=10;}return 0;}
static int wait_int(endpoint_t*e,int which,int value,unsigned timeout){unsigned elapsed=0;while(elapsed<timeout){int got;lock(e);got=which==1?e->eof_count:e->error_status;unlock(e);if(got==value)return 1;sleep_ms(10);elapsed+=10;}return 0;}
static int expect(const char*name,int ok){if(!ok){fprintf(stderr,"FAIL %s\n",name);return 1;}return 0;}
int main(void){int fail=0;endpoint_t a,b,bad;kc_rstrm_options_t o={.mtu=1200};unsigned char junk[3]={1,2,3};
    fail+=expect("open null out",kc_rstrm_open(NULL,NULL,received,datagram,eof_cb,drain_cb,error_cb,NULL)==KC_RSTRM_EINVAL);
    init(&a);init(&b);a.peer=&b;b.peer=&a;
    fail+=expect("open a",kc_rstrm_open(&a.stream,&o,received,datagram,eof_cb,drain_cb,error_cb,&a)==KC_RSTRM_OK);
    fail+=expect("open b",kc_rstrm_open(&b.stream,&o,received,datagram,eof_cb,drain_cb,error_cb,&b)==KC_RSTRM_OK);
    fail+=expect("send abc",kc_rstrm_send(a.stream,"abc",3)==KC_RSTRM_OK);
    fail+=expect("send def",kc_rstrm_send(a.stream,"def",3)==KC_RSTRM_OK);
    fail+=expect("receive stream",wait_received(&b,6,1000));
    lock(&b);fail+=expect("ordered bytes",b.received_size==6&&memcmp(b.received,"abcdef",6)==0);unlock(&b);
    fail+=expect("shutdown",kc_rstrm_shutdown(a.stream)==KC_RSTRM_OK);
    fail+=expect("peer eof",wait_int(&b,1,1,1000));
    fail+=expect("send after shutdown",kc_rstrm_send(a.stream,"x",1)==KC_RSTRM_ECLOSED);
    fail+=expect("half close reverse send",kc_rstrm_send(b.stream,"reply",5)==KC_RSTRM_OK);
    fail+=expect("reverse receive",wait_received(&a,5,1000));
    kc_rstrm_close(a.stream);kc_rstrm_close(b.stream);fini(&a);fini(&b);

    init(&bad);bad.fail_output=1;
    fail+=expect("open failing transport",kc_rstrm_open(&bad.stream,NULL,received,datagram,eof_cb,drain_cb,error_cb,&bad)==KC_RSTRM_OK);
    fail+=expect("queue failing output",kc_rstrm_send(bad.stream,"x",1)==KC_RSTRM_OK);
    fail+=expect("transport error",wait_int(&bad,2,KC_RSTRM_ETRANSPORT,1000));
    kc_rstrm_close(bad.stream);fini(&bad);

    init(&bad);
    fail+=expect("open malformed",kc_rstrm_open(&bad.stream,NULL,received,datagram,eof_cb,drain_cb,error_cb,&bad)==KC_RSTRM_OK);
    fail+=expect("input malformed queued",kc_rstrm_input(bad.stream,junk,sizeof(junk))==KC_RSTRM_OK);
    fail+=expect("protocol error",wait_int(&bad,2,KC_RSTRM_EPROTO,1000));
    kc_rstrm_close(bad.stream);fini(&bad);

    fail+=expect("strerror",strcmp(kc_rstrm_strerror(KC_RSTRM_EFULL),"stream buffer full")==0);
    (void)kc_rstrm_version();
    if(!fail)puts("OK rstrm contract");
    return fail?1:0;
}
