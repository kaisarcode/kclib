/*
 * BERT BasicTokenizer + greedy WordPiece for BGE small (uncased).
 * No system Unicode library is needed. Unicode data is generated from the
 * Unicode database and stored as compact, sorted lookup tables.
 * License: LGPL-3.0-only
 */
#include "tokenizer.h"
#include "tokenizer_unicode.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define KC_WORDPIECE_MAX_CHARS 100
#define KC_WORDPIECE_MAX_BYTES (KC_WORDPIECE_MAX_CHARS * 4)

static int in_ranges(const kc_unicode_range_t *ranges, size_t count, uint32_t cp) {
    size_t lo = 0;
    size_t hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < ranges[mid].first) hi = mid;
        else if (cp > ranges[mid].last) lo = mid + 1;
        else return 1;
    }
    return 0;
}

static int is_space(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
        in_ranges(kc_unicode_spaces, sizeof(kc_unicode_spaces) / sizeof(kc_unicode_spaces[0]), cp);
}

static int is_control(uint32_t cp) {
    if (cp == '\t' || cp == '\n' || cp == '\r') return 0;
    return in_ranges(kc_unicode_controls,
        sizeof(kc_unicode_controls) / sizeof(kc_unicode_controls[0]), cp);
}

static int is_punctuation(uint32_t cp) {
    if ((cp >= 33 && cp <= 47) || (cp >= 58 && cp <= 64) ||
        (cp >= 91 && cp <= 96) || (cp >= 123 && cp <= 126)) return 1;
    return in_ranges(kc_unicode_punctuation,
        sizeof(kc_unicode_punctuation) / sizeof(kc_unicode_punctuation[0]), cp);
}

static int is_chinese(uint32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) ||
        (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x20000 && cp <= 0x2A6DF) ||
        (cp >= 0x2A700 && cp <= 0x2B73F) ||
        (cp >= 0x2B740 && cp <= 0x2B81F) ||
        (cp >= 0x2B820 && cp <= 0x2CEAF) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0x2F800 && cp <= 0x2FA1F);
}

/* Invalid UTF-8 is consumed one byte at a time and discarded as U+FFFD. */
static uint32_t next_utf8(const char *text, size_t size, size_t *cursor) {
    const unsigned char *bytes = (const unsigned char *)text;
    size_t i = *cursor;
    uint32_t cp;
    unsigned int extra;
    unsigned int min_cp;
    unsigned char first = bytes[i];
    *cursor = i + 1;
    if (first < 0x80) return first;
    if (first >= 0xC2 && first <= 0xDF) {
        cp = first & 0x1FU;
        extra = 1;
        min_cp = 0x80;
    } else if (first >= 0xE0 && first <= 0xEF) {
        cp = first & 0x0FU;
        extra = 2;
        min_cp = 0x800;
    } else if (first >= 0xF0 && first <= 0xF4) {
        cp = first & 0x07U;
        extra = 3;
        min_cp = 0x10000;
    } else return 0xFFFD;
    if (size - i <= extra) return 0xFFFD;
    for (unsigned int j = 1; j <= extra; j++) {
        unsigned char b = bytes[i + j];
        if ((b & 0xC0U) != 0x80U) return 0xFFFD;
        cp = (cp << 6) | (uint32_t)(b & 0x3FU);
    }
    if (cp < min_cp || cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
    *cursor = i + extra + 1;
    return cp;
}

static size_t encode_utf8(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static void fold_unicode(uint32_t cp, uint32_t temp[3],
    const uint32_t **values, size_t *count) {
    /* Canonical Hangul decomposition (Unicode UAX #15). */
    if (cp >= 0xAC00 && cp <= 0xD7A3) {
        uint32_t index = cp - 0xAC00;
        uint32_t trailing = index % 28;
        temp[0] = 0x1100 + index / 588;
        temp[1] = 0x1161 + (index % 588) / 28;
        temp[2] = 0x11A7 + trailing;
        *values = temp;
        *count = trailing == 0 ? 2 : 3;
        return;
    }
    size_t lo = 0;
    size_t hi = sizeof(kc_unicode_folds) / sizeof(kc_unicode_folds[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < kc_unicode_folds[mid].codepoint) hi = mid;
        else if (cp > kc_unicode_folds[mid].codepoint) lo = mid + 1;
        else {
            *values = &kc_unicode_fold_values[kc_unicode_folds[mid].offset];
            *count = kc_unicode_folds[mid].count;
            return;
        }
    }
    *values = NULL;
    *count = 0;
}

typedef struct {
    char bytes[KC_WORDPIECE_MAX_BYTES + 1];
    size_t offsets[KC_WORDPIECE_MAX_CHARS + 1];
    size_t length;
    size_t chars;
    int too_long;
} word_buffer_t;

static void word_init(word_buffer_t *word) {
    word->length = 0;
    word->chars = 0;
    word->too_long = 0;
    word->offsets[0] = 0;
}

static void word_add(word_buffer_t *word, uint32_t cp) {
    if (word->too_long) return;
    if (word->chars >= KC_WORDPIECE_MAX_CHARS) {
        word->too_long = 1;
        return;
    }
    word->length += encode_utf8(cp, word->bytes + word->length);
    word->chars++;
    word->offsets[word->chars] = word->length;
}

static void word_flush(word_buffer_t *word, int *tokens, int capacity,
    int unk_id, kc_emb_token_lookup_fn lookup, void *context, int *count) {
    int pieces[KC_WORDPIECE_MAX_CHARS];
    size_t start = 0;
    size_t n_pieces = 0;
    if (word->chars == 0 && !word->too_long) return;
    if (*count >= capacity - 1) { word_init(word); return; }
    if (word->too_long) goto unknown;
    while (start < word->chars) {
        int found = -1;
        size_t match_end = start;
        for (size_t end = word->chars; end > start; end--) {
            char candidate[KC_WORDPIECE_MAX_BYTES + 4];
            const char *prefix = start == 0 ? "\xE2\x96\x81" : "##";
            size_t prefix_len = start == 0 ? 3 : 2;
            size_t n = word->offsets[end] - word->offsets[start];
            /* GGUF BERT vocabularies may encode word starts as ▁word;
             * WordPiece vocabularies use word and ##continuation. */
            memcpy(candidate, prefix, prefix_len);
            memcpy(candidate + prefix_len, word->bytes + word->offsets[start], n);
            candidate[prefix_len + n] = '\0';
            found = lookup(context, candidate);
            if (found < 0) {
                memcpy(candidate, word->bytes + word->offsets[start], n);
                candidate[n] = '\0';
                found = lookup(context, candidate);
            }
            if (found >= 0) {
                match_end = end;
                break;
            }
        }
        if (found < 0) goto unknown;
        pieces[n_pieces++] = found;
        start = match_end;
    }
    for (size_t i = 0; i < n_pieces && *count < capacity - 1; i++) {
        tokens[(*count)++] = pieces[i];
    }
    word_init(word);
    return;
unknown:
    tokens[(*count)++] = unk_id;
    word_init(word);
}

static void emit_character(uint32_t cp, word_buffer_t *word,
    int *tokens, int capacity, int unk_id,
    kc_emb_token_lookup_fn lookup, void *context, int *count) {
    if (is_space(cp) || cp == 0 || cp == 0xFFFD || is_control(cp)) {
        if (is_space(cp)) word_flush(word, tokens, capacity, unk_id, lookup, context, count);
        return;
    }
    if (is_punctuation(cp) || is_chinese(cp)) {
        word_flush(word, tokens, capacity, unk_id, lookup, context, count);
        word_add(word, cp);
        word_flush(word, tokens, capacity, unk_id, lookup, context, count);
        return;
    }
    word_add(word, cp);
}

void kc_emb_wordpiece_tokenize(const char *input, int *tokens, int capacity,
    int cls_id, int sep_id, int unk_id,
    kc_emb_token_lookup_fn lookup, void *context, int *count) {
    word_buffer_t word;
    size_t len, cursor = 0;
    if (count != NULL) *count = 0;
    if (input == NULL || tokens == NULL || lookup == NULL || count == NULL ||
        capacity < 2) return;
    tokens[(*count)++] = cls_id;
    len = strlen(input);
    word_init(&word);
    while (cursor < len && *count < capacity - 1) {
        /* BERT never splits its registered special tokens. */
        if (input[cursor] == '[') {
            static const char *const special[] = {
                "[UNK]", "[CLS]", "[SEP]", "[PAD]", "[MASK]"
            };
            int special_id = -1;
            size_t special_len = 0;
            for (size_t i = 0; i < sizeof(special) / sizeof(special[0]); i++) {
                size_t n = strlen(special[i]);
                if (n <= len - cursor &&
                    memcmp(input + cursor, special[i], n) == 0) {
                    special_id = lookup(context, special[i]);
                    if (special_id >= 0) { special_len = n; break; }
                }
            }
            if (special_id >= 0) {
                word_flush(&word, tokens, capacity, unk_id, lookup, context, count);
                if (*count < capacity - 1) tokens[(*count)++] = special_id;
                cursor += special_len;
                continue;
            }
        }
        uint32_t cp = next_utf8(input, len, &cursor);
        if (cp == 0 || cp == 0xFFFD || is_control(cp)) continue;
        if (is_space(cp)) {
            word_flush(&word, tokens, capacity, unk_id, lookup, context, count);
            continue;
        }
        const uint32_t *folded;
        uint32_t temp[3];
        size_t nfolded;
        fold_unicode(cp, temp, &folded, &nfolded);
        if (folded != NULL) {
            for (size_t i = 0; i < nfolded; i++) {
                emit_character(folded[i], &word, tokens, capacity, unk_id, lookup, context, count);
            }
        } else {
            emit_character(cp, &word, tokens, capacity, unk_id, lookup, context, count);
        }
    }
    if (*count < capacity - 1) word_flush(&word, tokens, capacity, unk_id, lookup, context, count);
    tokens[(*count)++] = sep_id;
}
