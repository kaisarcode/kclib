/*
 * Internal BERT WordPiece tokenizer for the embedded BGE model.
 * License: LGPL-3.0-only
 */
#ifndef KC_EMB_TOKENIZER_H
#define KC_EMB_TOKENIZER_H

/* Lookup returns a token ID, or -1 if absent. */
typedef int (*kc_emb_token_lookup_fn)(void *context, const char *token);

/* Includes [CLS]/[SEP], leaving at most capacity token IDs. */
void kc_emb_wordpiece_tokenize(const char *input, int *tokens, int capacity,
    int cls_id, int sep_id, int unk_id,
    kc_emb_token_lookup_fn lookup, void *context, int *count);

#endif
