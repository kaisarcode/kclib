/**
 * libngram.c
 * Summary: Descending sliding-window n-gram traversal library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libngram.h"

#if !defined(KC_NGRAM_BUILD_VERSION) || KC_NGRAM_BUILD_VERSION + 0 == 0
#undef KC_NGRAM_BUILD_VERSION
#define KC_NGRAM_BUILD_VERSION 0ULL
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t byte_start;
    size_t byte_end;
} kc_ngram_token_t;

typedef struct {
    kc_ngram_token_t *items;
    size_t count;
    size_t cap;
} kc_ngram_token_list_t;

typedef struct {
    size_t start;
    size_t end;
} kc_ngram_span_t;

/**
 * Returns whether one byte belongs to the configured separator set.
 * @param ch Byte to inspect.
 * @param separators Separator byte set.
 * @return 1 when the byte is a separator, or 0 otherwise.
 */
static int kc_ngram_is_separator(char ch, const char *separators) {
    if (separators == NULL || *separators == '\0') {
        return 0;
    }

    return strchr(separators, (unsigned char)ch) != NULL;
}

/**
 * Releases token storage.
 * @param tokens Token list to release.
 * @return No return value.
 */
static void kc_ngram_free_tokens(kc_ngram_token_list_t *tokens) {
    if (tokens == NULL) {
        return;
    }

    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
    tokens->cap = 0;
}

/**
 * Ensures token storage capacity for at least one more element.
 * @param tokens Destination token list.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_reserve_token_slot(kc_ngram_token_list_t *tokens) {
    kc_ngram_token_t *next_items;
    size_t next_cap;

    if (tokens == NULL) {
        return -1;
    }

    if (tokens->count < tokens->cap) {
        return 0;
    }

    if (tokens->cap > 0) {
        if (tokens->cap > SIZE_MAX / 2) {
            return -1;
        }
        next_cap = tokens->cap * 2;
    } else {
        next_cap = 16;
    }

    if (next_cap > SIZE_MAX / sizeof(kc_ngram_token_t)) {
        return -1;
    }

    next_items = (kc_ngram_token_t *)realloc(
        tokens->items,
        next_cap * sizeof(kc_ngram_token_t)
    );
    if (next_items == NULL) {
        return -1;
    }

    tokens->items = next_items;
    tokens->cap = next_cap;
    return 0;
}

/**
 * Appends one token span into the token list.
 * @param tokens Destination token list.
 * @param byte_start Inclusive token byte start.
 * @param byte_end Exclusive token byte end.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_push_token(
    kc_ngram_token_list_t *tokens,
    size_t byte_start,
    size_t byte_end
) {
    if (tokens == NULL || byte_end <= byte_start) {
        return -1;
    }

    if (tokens->count == SIZE_MAX) {
        return -1;
    }

    if (kc_ngram_reserve_token_slot(tokens) != 0) {
        return -1;
    }

    tokens->items[tokens->count].byte_start = byte_start;
    tokens->items[tokens->count].byte_end = byte_end;
    tokens->count++;
    return 0;
}

/**
 * Splits input text into tokens using the configured separators.
 * @param input Input text to tokenize.
 * @param separators Separator byte set.
 * @param tokens Destination token list.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_split_tokens(
    const char *input,
    const char *separators,
    kc_ngram_token_list_t *tokens
) {
    const char *cursor;
    const char *base;

    if (input == NULL || tokens == NULL) {
        return -1;
    }

    tokens->items = NULL;
    tokens->count = 0;
    tokens->cap = 0;
    cursor = input;
    base = input;

    while (*cursor != '\0') {
        const char *token_start;

        while (*cursor != '\0' && kc_ngram_is_separator(*cursor, separators)) {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        token_start = cursor;
        while (*cursor != '\0' && !kc_ngram_is_separator(*cursor, separators)) {
            cursor++;
        }

        if (cursor == token_start) {
            continue;
        }

        if (
            kc_ngram_push_token(
                tokens,
                (size_t)(token_start - base),
                (size_t)(cursor - base)
            ) != 0
        ) {
            kc_ngram_free_tokens(tokens);
            return -1;
        }
    }

    return 0;
}

/**
 * Finds where one span should be inserted by start index.
 * @param spans Sorted span array.
 * @param count Number of spans.
 * @param start Inclusive candidate start index.
 * @return Insertion index.
 */
static size_t kc_ngram_find_span_insert_index(
    const kc_ngram_span_t *spans,
    size_t count,
    size_t start
) {
    size_t left;
    size_t right;

    left = 0;
    right = count;

    while (left < right) {
        size_t mid;

        mid = left + (right - left) / 2;
        if (spans[mid].start < start) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return left;
}

/**
 * Returns whether one span is fully contained inside a closed span.
 * @param start Inclusive candidate start index.
 * @param end Inclusive candidate end index.
 * @param closed_spans Closed span array sorted by start index.
 * @param closed_count Number of closed spans.
 * @return 1 when the span is closed, or 0 otherwise.
 */
static int kc_ngram_span_is_closed(
    size_t start,
    size_t end,
    const kc_ngram_span_t *closed_spans,
    size_t closed_count
) {
    size_t left;
    size_t right;
    size_t index;

    if (closed_spans == NULL || closed_count < 1) {
        return 0;
    }

    left = 0;
    right = closed_count;

    while (left < right) {
        size_t mid;

        mid = left + (right - left) / 2;
        if (closed_spans[mid].start <= start) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    if (left == 0) {
        return 0;
    }

    index = left - 1;
    return start >= closed_spans[index].start && end <= closed_spans[index].end;
}

/**
 * Ensures span storage capacity for at least one more element.
 * @param spans Span array pointer.
 * @param count Current number of spans.
 * @param cap Current allocated capacity.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_reserve_span_slot(
    kc_ngram_span_t **spans,
    size_t count,
    size_t *cap
) {
    kc_ngram_span_t *next_spans;
    size_t next_cap;

    if (spans == NULL || cap == NULL) {
        return -1;
    }

    if (count < *cap) {
        return 0;
    }

    if (*cap > 0) {
        if (*cap > SIZE_MAX / 2) {
            return -1;
        }
        next_cap = *cap * 2;
    } else {
        next_cap = 16;
    }

    if (next_cap > SIZE_MAX / sizeof(kc_ngram_span_t)) {
        return -1;
    }

    next_spans = (kc_ngram_span_t *)realloc(
        *spans,
        next_cap * sizeof(kc_ngram_span_t)
    );
    if (next_spans == NULL) {
        return -1;
    }

    *spans = next_spans;
    *cap = next_cap;
    return 0;
}

/**
 * Inserts one closed span while pruning redundant contained spans.
 * @param spans Sorted span array pointer.
 * @param count Current number of spans.
 * @param cap Current allocated capacity.
 * @param start Inclusive token start index.
 * @param end Inclusive token end index.
 * @return 0 on success, or -1 on allocation failure.
 */
static int kc_ngram_add_closed_span(
    kc_ngram_span_t **spans,
    size_t *count,
    size_t *cap,
    size_t start,
    size_t end
) {
    size_t insert_at;
    size_t remove_end;
    size_t tail_count;

    if (spans == NULL || count == NULL || cap == NULL) {
        return -1;
    }

    insert_at = kc_ngram_find_span_insert_index(*spans, *count, start);

    if (
        insert_at > 0 &&
        start >= (*spans)[insert_at - 1].start &&
        end <= (*spans)[insert_at - 1].end
    ) {
        return 0;
    }

    if (
        insert_at < *count &&
        (*spans)[insert_at].start == start &&
        (*spans)[insert_at].end >= end
    ) {
        return 0;
    }

    remove_end = insert_at;
    while (remove_end < *count && (*spans)[remove_end].end <= end) {
        remove_end++;
    }

    if (remove_end > insert_at) {
        memmove(
            *spans + insert_at,
            *spans + remove_end,
            (size_t)(*count - remove_end) * sizeof(kc_ngram_span_t)
        );
        *count -= remove_end - insert_at;
    }

    if (
        insert_at < *count &&
        start >= (*spans)[insert_at].start &&
        end <= (*spans)[insert_at].end
    ) {
        return 0;
    }

    if (kc_ngram_reserve_span_slot(spans, *count, cap) != 0) {
        return -1;
    }

    tail_count = *count - insert_at;
    if (tail_count > 0) {
        memmove(
            *spans + insert_at + 1,
            *spans + insert_at,
            tail_count * sizeof(kc_ngram_span_t)
        );
    }

    (*spans)[insert_at].start = start;
    (*spans)[insert_at].end = end;
    (*count)++;
    return 0;
}

/**
 * Traverses descending sliding-window n-grams for the input text.
 * This function is reentrant and uses only per-call traversal state.
 * Omitted option fields use library defaults.
 * @param input Input text to tokenize and traverse.
 * @param options Optional traversal configuration. NULL fields use defaults.
 * @param visit Synchronous callback invoked for each chunk.
 * @param userdata Caller-provided opaque user data for the callback.
 * @return KC_NGRAM_OK on success, KC_NGRAM_EABORT on visitor abort,
 *         or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_traverse(
    const char *input,
    const kc_ngram_options_t *options,
    kc_ngram_visit_fn visit,
    void *userdata
) {
    kc_ngram_token_list_t tokens;
    kc_ngram_span_t *closed_spans;
    const char *separators;
    size_t max_tokens;
    size_t min_tokens;
    size_t closed_count;
    size_t closed_cap;
    size_t loop_max;
    size_t window_size;
    size_t start;

    if (input == NULL || visit == NULL) {
        return KC_NGRAM_ERROR;
    }

    max_tokens = 10U;
    min_tokens = 1U;
    separators = " \t\r\n";

    if (options != NULL) {
        if (options->max_tokens != NULL) {
            max_tokens = *options->max_tokens;
        }
        if (options->min_tokens != NULL) {
            min_tokens = *options->min_tokens;
        }
        if (options->separators != NULL) {
            separators = options->separators;
        }
    }

    if (
        min_tokens < 1U ||
        (max_tokens != 0U && max_tokens < min_tokens)
    ) {
        return KC_NGRAM_ERROR;
    }

    if (kc_ngram_split_tokens(input, separators, &tokens) != 0) {
        return KC_NGRAM_ERROR;
    }

    if (tokens.count == 0U) {
        kc_ngram_free_tokens(&tokens);
        return KC_NGRAM_OK;
    }

    closed_spans = NULL;
    closed_count = 0U;
    closed_cap = 0U;

    loop_max = max_tokens;
    if (loop_max == 0U || tokens.count < loop_max) {
        loop_max = tokens.count;
    }

    window_size = loop_max;
    while (window_size >= min_tokens) {
        for (start = 0U; start <= tokens.count - window_size; start++) {
            kc_ngram_chunk_t chunk;
            size_t end;
            int decision;

            end = start + window_size - 1U;
            if (kc_ngram_span_is_closed(start, end, closed_spans, closed_count)) {
                continue;
            }

            chunk.data = input + tokens.items[start].byte_start;
            chunk.data_size =
                tokens.items[end].byte_end - tokens.items[start].byte_start;
            chunk.token_start = start;
            chunk.token_count = window_size;

            decision = visit(&chunk, userdata);
            if (decision < 0) {
                free(closed_spans);
                kc_ngram_free_tokens(&tokens);
                return KC_NGRAM_EABORT;
            }

            if (decision == 1) {
                if (
                    kc_ngram_add_closed_span(
                        &closed_spans,
                        &closed_count,
                        &closed_cap,
                        start,
                        end
                    ) != 0
                ) {
                    free(closed_spans);
                    kc_ngram_free_tokens(&tokens);
                    return KC_NGRAM_ERROR;
                }
            }
        }

        if (window_size == min_tokens) {
            break;
        }
        window_size--;
    }

    free(closed_spans);
    kc_ngram_free_tokens(&tokens);
    return KC_NGRAM_OK;
}

/**
 * Retrieves the library build version as a Unix timestamp.
 * @return Build version timestamp.
 */
uint64_t kc_ngram_version(void) {
    return (uint64_t)KC_NGRAM_BUILD_VERSION;
}
