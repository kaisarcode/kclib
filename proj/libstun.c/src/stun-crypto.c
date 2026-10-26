/**
 * stun-crypto.c - Private STUN authentication and fingerprint primitives.
 * Summary: Portable SHA-1, SHA-256, HMAC and CRC-32 without runtime dependencies.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */
#include "stun-crypto.h"
#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t block[64];
    unsigned algorithm;
} stun_digest_t;

static uint32_t rotl32(uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }
static uint32_t rotr32(uint32_t v, unsigned n) { return (v >> n) | (v << (32 - n)); }
static uint32_t load32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void store32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static const uint32_t sha256_k[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};

/**
 * Mixes one 64-byte block into a SHA-1 or SHA-256 state.
 * @param d Digest state to update.
 * @param bytes Sixty-four input bytes.
 * @return void.
 */
static void digest_block(stun_digest_t *d, const uint8_t bytes[64])
{
    uint32_t w[80];
    uint32_t a,b,c,e,f,g,h,t1,t2;
    unsigned i;
    for (i=0;i<16;i++) w[i] = load32(bytes + i*4);
    if (d->algorithm == 1) {
        for (i=16;i<80;i++) w[i]=rotl32(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        a=d->state[0]; b=d->state[1]; c=d->state[2]; e=d->state[3]; f=d->state[4];
        for (i=0;i<80;i++) {
            uint32_t x,k;
            if (i<20) { x=(b&c)|(~b&e); k=0x5a827999U; }
            else if(i<40) { x=b^c^e; k=0x6ed9eba1U; }
            else if(i<60) { x=(b&c)|(b&e)|(c&e); k=0x8f1bbcdcU; }
            else { x=b^c^e; k=0xca62c1d6U; }
            t1=rotl32(a,5)+x+f+k+w[i]; f=e; e=c; c=rotl32(b,30); b=a; a=t1;
        }
        d->state[0]+=a;d->state[1]+=b;d->state[2]+=c;d->state[3]+=e;d->state[4]+=f;
    } else {
        for (i=16;i<64;i++) {
            uint32_t s0=rotr32(w[i-15],7)^rotr32(w[i-15],18)^(w[i-15]>>3);
            uint32_t s1=rotr32(w[i-2],17)^rotr32(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        a=d->state[0]; b=d->state[1]; c=d->state[2]; e=d->state[3]; f=d->state[4]; g=d->state[5]; h=d->state[6]; t2=d->state[7];
        for (i=0;i<64;i++) {
            uint32_t s1=rotr32(f,6)^rotr32(f,11)^rotr32(f,25);
            uint32_t ch=(f&g)^(~f&h);
            uint32_t s0=rotr32(a,2)^rotr32(a,13)^rotr32(a,22);
            uint32_t maj=(a&b)^(a&c)^(b&c);
            t1=t2+s1+ch+sha256_k[i]+w[i];
            t2=h;h=g;g=f;f=e+t1;e=c;c=b;b=a;a=t1+s0+maj;
        }
        d->state[0]+=a;d->state[1]+=b;d->state[2]+=c;d->state[3]+=e;
        d->state[4]+=f;d->state[5]+=g;d->state[6]+=h;d->state[7]+=t2;
    }
}

/**
 * Initializes a digest state for SHA-1 or SHA-256.
 * @param d Digest state to initialize.
 * @param algorithm Algorithm selector: 1 for SHA-1.
 * @return void.
 */
static void digest_init(stun_digest_t *d, unsigned algorithm)
{
    static const uint32_t sha1[5] = {0x67452301U,0xefcdab89U,0x98badcfeU,0x10325476U,0xc3d2e1f0U};
    static const uint32_t sha256[8] = {0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    memset(d,0,sizeof(*d)); d->algorithm=algorithm;
    memcpy(d->state,algorithm==1?sha1:sha256,algorithm==1?sizeof(sha1):sizeof(sha256));
}

/**
 * Feeds input bytes into a digest state.
 * @param d Digest state to update.
 * @param data Input bytes.
 * @param size Input byte count.
 * @return void.
 */
static void digest_update(stun_digest_t *d, const void *data, size_t size)
{
    const uint8_t *p=(const uint8_t *)data;
    size_t off=(size_t)(d->count&63U),take;
    while(size) {
        take=64U-off;
        if(take>size)take=size;
        memcpy(d->block+off,p,take);
        d->count+=take; p+=take;size-=take;off+=take;
        if(off==64U){digest_block(d,d->block);off=0;}
    }
}

/**
 * Finishes a digest and writes its raw output.
 * @param d Digest state to finish.
 * @param out Destination for the digest bytes.
 * @return void.
 */
static void digest_final(stun_digest_t *d, uint8_t out[32])
{
    uint64_t bits=d->count*8U;
    uint8_t pad[72]={0x80};
    uint8_t len[8];
    unsigned i;
    size_t rem=(size_t)(d->count&63U);
    for(i=0;i<8;i++)len[7-i]=(uint8_t)(bits>>(8*i));
    digest_update(d,pad,rem<56?56-rem:120-rem);
    digest_update(d,len,8);
    for(i=0;i<(d->algorithm==1?5U:8U);i++)store32(out+i*4,d->state[i]);
}

/**
 * Computes an HMAC with SHA-1 or SHA-256.
 * @param algorithm HMAC algorithm selector.
 * @param key HMAC key.
 * @param key_size Key length.
 * @param data Input bytes.
 * @param size Input byte count.
 * @param out Destination for the MAC bytes.
 * @return void.
 */
void stun_hmac(unsigned algorithm,const void *key,size_t key_size,const void *data,size_t size,uint8_t out[32])
{
    uint8_t k[64]={0},inner[32],ipad[64],opad[64];
    stun_digest_t d;
    size_t i,n=algorithm==1?20U:32U;
    if(key_size>64U){digest_init(&d,algorithm);digest_update(&d,key,key_size);digest_final(&d,k);}
    else if(key_size)memcpy(k,key,key_size);
    for(i=0;i<64;i++){ipad[i]=(uint8_t)(k[i]^0x36U);opad[i]=(uint8_t)(k[i]^0x5cU);}
    digest_init(&d,algorithm);digest_update(&d,ipad,64);digest_update(&d,data,size);digest_final(&d,inner);
    digest_init(&d,algorithm);digest_update(&d,opad,64);digest_update(&d,inner,n);digest_final(&d,out);
    memset(&d,0,sizeof(d));memset(k,0,sizeof(k));memset(inner,0,sizeof(inner));
}

/**
 * Computes the CRC-32 value of a byte range.
 * @param data Input bytes.
 * @param size Input byte count.
 * @return CRC-32 value.
 */
uint32_t stun_crc32(const void *data,size_t size)
{
    const uint8_t *p=(const uint8_t *)data;
    uint32_t crc=~(uint32_t)0;
    size_t i;unsigned j;
    for(i=0;i<size;i++){
        crc^=p[i];
        for(j=0;j<8;j++)crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));
    }
    return ~crc;
}

/**
 * Compares two byte ranges in constant time.
 * @param a First byte range.
 * @param b Second byte range.
 * @param n Byte count to compare.
 * @return 1 when both ranges are equal.
 */
int stun_equal(const void *a,const void *b,size_t n)
{
    const uint8_t *x=(const uint8_t *)a,*y=(const uint8_t *)b;
    uint8_t diff=0;size_t i;
    for(i=0;i<n;i++)diff|=(uint8_t)(x[i]^y[i]);
    return diff==0;
}
