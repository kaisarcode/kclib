/**
 * libngram.h
 * Summary: Public API for descending sliding-window n-gram traversal.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_NGRAM_H
#define KC_NGRAM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_NGRAM_OK      0
#define KC_NGRAM_ERROR  -1
#define KC_NGRAM_EABORT -2

typedef struct {
    const char *input;
    size_t byte_start;
    size_t byte_end;
    size_t start;
    size_t end;
    size_t size;
} kc_ngram_chunk_t;

typedef struct {
    size_t max_tokens;
    size_t min_tokens;
    const char *separators;
} kc_ngram_options_t;

typedef int (*kc_ngram_visit_fn)(
    const kc_ngram_chunk_t *chunk,
    void *userdata
);

kc_ngram_options_t kc_ngram_options_default(void);

int kc_ngram_execute(const char *input, const kc_ngram_options_t *options,
    kc_ngram_visit_fn visit, void *userdata, size_t *out_count);

uint64_t kc_ngram_version(void);

#ifdef __cplusplus
}
#endif

#endif
