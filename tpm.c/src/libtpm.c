/**
 * libtpm.c - Text profile matcher.
 * Summary: Core implementation for the tpm library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#endif
#include <signal.h>

#include "libtpm.h"

#if !defined(KC_TPM_BUILD_VERSION) || KC_TPM_BUILD_VERSION + 0 == 0
#undef KC_TPM_BUILD_VERSION
#define KC_TPM_BUILD_VERSION 0ULL
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KC_TPM_MAX_GRAMS 8192
#define KC_TPM_RAW_MAX   16384
#define KC_TPM_NG_MAX    8

typedef struct {
    char gram[12];
    int count;
} kc_tpm_gram_t;

struct kc_tpm {
    kc_tpm_gram_t profile[KC_TPM_MAX_GRAMS];
    int profile_size;
    long total;
    int ngram_size;

    kc_tpm_options_t opts;
    volatile sig_atomic_t stop_requested;
};

/**
 * Normalizes text: lowercase A-Z, collapse whitespace runs to single space,
 * trim leading/trailing spaces.
 * @param input Input text to normalize.
 * @param out_len Output length (bytes).
 * @return Allocated string the caller must free, or NULL on error.
 */
static char *kc_tpm_norm(const char *input, size_t *out_len) {
    size_t i, j;
    size_t len;
    char *out;
    int in_space;

    if (!input || !out_len) {
        return NULL;
    }

    len = strlen(input);
    out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }

    in_space = 1;
    j = 0;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)input[i];
        if (isspace((int)c)) {
            if (!in_space) {
                out[j++] = ' ';
                in_space = 1;
            }
        } else {
            if (c >= 'A' && c <= 'Z') {
                out[j++] = (char)(c + ('a' - 'A'));
            } else {
                out[j++] = (char)c;
            }
            in_space = 0;
        }
    }

    if (j > 0 && out[j - 1] == ' ') {
        j--;
    }

    out[j] = '\0';
    *out_len = j;
    return out;
}

/**
 * Compare two raw gram strings for qsort (memcmp by first 8 bytes).
 * @param a Left element.
 * @param b Right element.
 * @return Negative, zero, or positive.
 */
static int kc_tpm_raw_cmp(const void *a, const void *b) {
    return memcmp(a, b, 8);
}

/**
 * Build an n-gram profile: extract raw n-grams, sort, uniq count,
 * strip leading/trailing spaces.
 *
 * This replicates sort|uniq -c which groups by RAW bytes, then strips spaces
 * (as the shell's read -r strips IFS whitespace), potentially producing
 * duplicate entries for the same stripped gram (e.g. " el" and "el " both
 * become "el" with separate counts).
 *
 * @param text Normalized text.
 * @param len Length of text.
 * @param n N-gram size.
 * @param profile Output profile array.
 * @param out_size Number of profile entries written.
 * @param out_total Total number of n-gram windows.
 * @return KC_TPM_OK on success, KC_TPM_ERROR on overflow.
 */
static int kc_tpm_grams(
    const char *text,
    size_t len,
    int n,
    kc_tpm_gram_t *profile,
    int *out_size,
    long *out_total
) {
    char raw[KC_TPM_RAW_MAX][12];
    int raw_count;
    size_t i;
    int size;
    long total;
    int idx;

    raw_count = 0;

    for (i = 0; i + (size_t)n <= len; i++) {
        if (raw_count >= KC_TPM_RAW_MAX) {
            return KC_TPM_ERROR;
        }
        memcpy(raw[raw_count], text + i, (size_t)n);
        raw[raw_count][n] = '\0';
        raw_count++;
    }

    total = (long)raw_count;

    qsort(raw, (size_t)raw_count, sizeof(raw[0]), kc_tpm_raw_cmp);

    size = 0;
    idx = 0;
    while (idx < raw_count) {
        int dup_count;
        char *gram_raw;
        int start;
        int end;
        int gram_len;

        gram_raw = raw[idx];
        dup_count = 1;
        idx++;

        while (idx < raw_count &&
            memcmp(gram_raw, raw[idx], (size_t)n + 1) == 0) {
            dup_count++;
            idx++;
        }

        start = 0;
        while (start < n && gram_raw[start] == ' ') {
            start++;
        }

        end = n - 1;
        while (end >= start && gram_raw[end] == ' ') {
            end--;
        }

        if (start > end) {
            continue;
        }

        gram_len = end - start + 1;

        if (size >= KC_TPM_MAX_GRAMS) {
            return KC_TPM_ERROR;
        }

        memcpy(profile[size].gram, gram_raw + start, (size_t)gram_len);
        profile[size].gram[gram_len] = '\0';
        profile[size].count = dup_count;
        size++;
    }

    *out_size = size;
    *out_total = total;
    return KC_TPM_OK;
}

/**
 * Allocate and initialize a new tpm context.
 * Prepares one inference context.
 * @param out Pointer to receive the context pointer.
 * @param opts Options.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_open(kc_tpm_t **out, const kc_tpm_options_t *opts) {
    kc_tpm_t *tpm;
    if (!out || !opts) return KC_TPM_ERROR;
    tpm = (kc_tpm_t *)calloc(1, sizeof(kc_tpm_t));
    if (!tpm) return KC_TPM_ERROR;
    tpm->opts = *opts;
    *out = tpm;
    return KC_TPM_OK;
}

/**
 * Build an n-gram profile from map text.
 * @param tpm Context pointer.
 * @param map_text Text to build profile from.
 * @param ngram_size N-gram size.
 * @return KC_TPM_OK on success, KC_TPM_ERROR on failure.
 */
int kc_tpm_build(kc_tpm_t *tpm, const char *map_text, int ngram_size) {
    char *norm;
    size_t len;
    int rc;

    if (!tpm || !map_text || ngram_size < 1 || ngram_size > KC_TPM_NG_MAX) {
        return KC_TPM_ERROR;
    }

    tpm->profile_size = 0;
    tpm->total = 0;
    tpm->ngram_size = ngram_size;

    norm = kc_tpm_norm(map_text, &len);
    if (!norm) {
        return KC_TPM_ERROR;
    }

    rc = kc_tpm_grams(
        norm, len, ngram_size,
        tpm->profile, &tpm->profile_size, &tpm->total
    );

    free(norm);
    return rc;
}

/**
 * Score input text against the built profile.
 * @param tpm Context pointer with built profile.
 * @param input_text Text to score.
 * @return Score in [0.0, 1.0].
 */
double kc_tpm_score(kc_tpm_t *tpm, const char *input_text) {
    char *norm;
    size_t len;
    kc_tpm_gram_t input_profile[KC_TPM_MAX_GRAMS];
    int input_size;
    long input_total;
    int i;
    double log_sum;

    if (!tpm || !input_text || tpm->profile_size <= 0 || tpm->total <= 0) {
        return 0.0;
    }

    norm = kc_tpm_norm(input_text, &len);
    if (!norm) {
        return 0.0;
    }

    if (kc_tpm_grams(norm, len, tpm->ngram_size,
            input_profile, &input_size, &input_total) != KC_TPM_OK) {
        free(norm);
        return 0.0;
    }

    free(norm);

    if (input_total <= 0) {
        return 0.0;
    }

    log_sum = 0.0;
    for (i = 0; i < input_size; i++) {
        int map_count;
        int j;
        double numerator;
        double denominator;

        map_count = 0;
        for (j = 0; j < tpm->profile_size; j++) {
            if (strcmp(input_profile[i].gram, tpm->profile[j].gram) == 0) {
                map_count = tpm->profile[j].count;
            }
        }

        numerator = (double)map_count + 1.0;
        denominator = (double)tpm->total + (double)tpm->profile_size;
        log_sum += (double)input_profile[i].count *
            log(numerator / denominator);
    }

    if (log_sum == 0.0 && input_total > 0) {
        double denominator = (double)tpm->total + (double)tpm->profile_size;
        log_sum = (double)input_total * log(1.0 / denominator);
    }

    double avg_log = log_sum / (double)input_total;
    double score = 1.0 / (1.0 + exp(-8.0 * (avg_log + 5.25)));

    if (score < 0.0) score = 0.0;
    if (score > 1.0) score = 1.0;

    return score;
}

/**
 * Release a tpm context.
 * @param tpm Context pointer.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_close(kc_tpm_t *tpm) {
    if (!tpm) return KC_TPM_ERROR;
    kc_tpm_options_free(&tpm->opts);
    free(tpm);
    return KC_TPM_OK;
}

/**
 * Create an options struct initialized with default values.
 * @param none Unused.
 * @return Default-initialized options.
 */
kc_tpm_options_t kc_tpm_options_default(void) {
    kc_tpm_options_t opts;
    memset(&opts, 0, sizeof(opts));
    return opts;
}

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_tpm_options_load_env(kc_tpm_options_t *opts) {
    (void)opts;
}

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_tpm_options_free(kc_tpm_options_t *opts) {
    (void)opts;
}

/**
 * Request stop for a specific tpm context.
 * @param tpm Context pointer.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_stop(kc_tpm_t *tpm) {
    if (!tpm) return KC_TPM_ERROR;
    tpm->stop_requested = 1;
    return KC_TPM_OK;
}

/**
 * Returns whether stop was requested on a specific tpm context.
 * @param tpm Context pointer.
 * @return 1 if stop was requested, or 0 otherwise.
 */
int kc_tpm_stop_requested(kc_tpm_t *tpm) {
    if (!tpm) return 0;
    return tpm->stop_requested ? 1 : 0;
}

/**
 * Retrieves the library build version as a Unix timestamp.
 * @return Build version timestamp.
 */
uint64_t kc_tpm_version(void) {
    return (uint64_t)KC_TPM_BUILD_VERSION;
}
