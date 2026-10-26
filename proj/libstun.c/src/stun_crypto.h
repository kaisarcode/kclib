#ifndef STUN_CRYPTO_H
#define STUN_CRYPTO_H
#include <stddef.h>
#include <stdint.h>
void stun_hmac(unsigned algorithm, const void *key, size_t key_size, const void *data, size_t size, uint8_t out[32]);
uint32_t stun_crc32(const void *data, size_t size);
int stun_equal(const void *a, const void *b, size_t size);
#endif