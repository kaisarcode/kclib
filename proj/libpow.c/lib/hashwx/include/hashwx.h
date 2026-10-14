/*
  Copyright (c) 2020-2026 tevador <tevador@gmail.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the License,
  or (at your option) any later version.
*/

#ifndef HASHWX_H
#define HASHWX_H

#include <stdint.h>
#include <stddef.h>

typedef struct hashwx_ctx hashwx_ctx;

typedef enum hashwx_type {
    HASHWX_INTERPRETED,
    HASHWX_COMPILED
} hashwx_type;

#define HASHWX_NOTSUPP ((hashwx_ctx*)-1)
#define HASHWX_SEED_SIZE 32

#if defined(_WIN32) || defined(__CYGWIN__)
#define HASHWX_WIN
#endif

#ifdef HASHWX_WIN
    #ifdef HASHWX_SHARED
        #define HASHWX_API __declspec(dllexport)
    #elif !defined(HASHWX_STATIC)
        #define HASHWX_API __declspec(dllimport)
    #else
        #define HASHWX_API
    #endif
    #define HASHWX_PRIVATE
#elif defined(__wasm__)
    #define HASHWX_API extern
    #define HASHWX_PRIVATE
#else
    #ifdef HASHWX_SHARED
        #define HASHWX_API __attribute__ ((visibility ("default")))
    #else
        #define HASHWX_API __attribute__ ((visibility ("hidden")))
    #endif
    #define HASHWX_PRIVATE __attribute__ ((visibility ("hidden")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

HASHWX_API hashwx_ctx* hashwx_alloc(hashwx_type type);
HASHWX_API void hashwx_make(hashwx_ctx* ctx, const uint8_t seed[HASHWX_SEED_SIZE]);
HASHWX_API uint64_t hashwx_exec(const hashwx_ctx* ctx, uint64_t input);
HASHWX_API void hashwx_free(hashwx_ctx* ctx);

#ifdef __cplusplus
}
#endif

#endif
