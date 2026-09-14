/**
 * libflow.c - Branch-oriented flow runtime implementation.
 * Summary: Parses flow documents and executes independent data branches.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "libflow.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif
#include <stdarg.h>
#include <stddef.h>

#define KC_FLOW_MAX_RECORDS 2048
#define KC_FLOW_MAX_NODES 512
#define KC_FLOW_MAX_BRANCHES 2048
#define KC_FLOW_MAX_KEY 256
#define KC_FLOW_MAX_PATH 4096
#define KC_FLOW_ERROR_SIZE 512

typedef enum kc_flow_overlay_kind {
    KC_FLOW_OVERLAY_SET = 1,
    KC_FLOW_OVERLAY_UNSET = 2
} kc_flow_overlay_kind_t;

typedef enum kc_flow_value_kind {
    KC_FLOW_VALUE_LITERAL = 1,
    KC_FLOW_VALUE_EXEC = 2
} kc_flow_value_kind_t;

typedef enum kc_flow_template_mode {
    KC_FLOW_TEMPLATE_TEXT = 1,
    KC_FLOW_TEMPLATE_SHELL = 2
} kc_flow_template_mode_t;

typedef enum kc_flow_quote_state {
    KC_FLOW_QUOTE_NONE = 0,
    KC_FLOW_QUOTE_SINGLE = 1,
    KC_FLOW_QUOTE_DOUBLE = 2
} kc_flow_quote_state_t;

typedef struct kc_flow_record {
    char *key;
    char *value;
    kc_flow_value_kind_t kind;
} kc_flow_record_t;

typedef struct kc_flow_store {
    kc_flow_record_t items[KC_FLOW_MAX_RECORDS];
    size_t count;
} kc_flow_store_t;

typedef struct kc_flow_links {
    kc_flow_record_t items[KC_FLOW_MAX_RECORDS];
    size_t count;
} kc_flow_links_t;

typedef struct kc_flow_node {
    char *ref;
    char *use;
    char *import;
    kc_flow_value_kind_t import_kind;
    char *exec;
    kc_flow_value_kind_t exec_kind;
    kc_flow_store_t data;
    kc_flow_links_t links;
} kc_flow_node_t;

typedef struct kc_flow_model {
    char *id;
    kc_flow_store_t data;
    kc_flow_links_t entries;
    kc_flow_node_t nodes[KC_FLOW_MAX_NODES];
    size_t node_count;
    kc_flow_node_t funcs[KC_FLOW_MAX_NODES];
    size_t func_count;
} kc_flow_model_t;

typedef struct kc_flow_overlay {
    kc_flow_overlay_kind_t kind;
    char *key;
    char *value;
} kc_flow_overlay_t;

typedef struct kc_flow_branch {
    char *data;
    size_t size;
} kc_flow_branch_t;

typedef struct kc_flow_branches {
    kc_flow_branch_t items[KC_FLOW_MAX_BRANCHES];
    size_t count;
} kc_flow_branches_t;

struct kc_flow {
    kc_flow_overlay_t overlays[KC_FLOW_MAX_RECORDS];
    size_t overlay_count;
    char error[KC_FLOW_ERROR_SIZE];
    volatile sig_atomic_t stop_requested;
};

/**
 * Duplicate one string.
 * @param text Source text.
 * @return Heap copy or NULL.
 */
static char *kc_flow_dup(const char *text) {
    size_t size;
    char *copy;

    if (!text) {
        return NULL;
    }
    size = strlen(text) + 1;
    copy = (char *)malloc(size);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, size);
    return copy;
}

/**
 * Duplicate one byte buffer and add a trailing null byte.
 * @param data Source bytes.
 * @param size Source size.
 * @return Heap copy or NULL.
 */
static char *kc_flow_dup_bytes(const void *data, size_t size) {
    char *copy;

    copy = (char *)malloc(size + 1);
    if (!copy) {
        return NULL;
    }
    if (size > 0 && data) {
        memcpy(copy, data, size);
    }
    copy[size] = '\0';
    return copy;
}

/**
 * Store one formatted context error.
 * @param ctx Context pointer.
 * @param message Error message.
 * @return KC_FLOW_ERROR.
 */
static int kc_flow_fail(kc_flow_t *ctx, const char *message) {
    if (ctx) {
        snprintf(ctx->error, sizeof(ctx->error), "%s", message);
    }
    return KC_FLOW_ERROR;
}

/**
 * Store one stopped context error.
 * @param ctx Context pointer.
 * @return KC_FLOW_ESTOP.
 */
static int kc_flow_fail_stop(kc_flow_t *ctx) {
    if (ctx) {
        snprintf(ctx->error, sizeof(ctx->error), "%s", "stop requested");
    }
    return KC_FLOW_ESTOP;
}

/**
 * Initialize one key-value store.
 * @param store Store pointer.
 * @return None.
 */
static void kc_flow_store_init(kc_flow_store_t *store) {
    memset(store, 0, sizeof(*store));
}

/**
 * Release one key-value store.
 * @param store Store pointer.
 * @return None.
 */
static void kc_flow_store_free(kc_flow_store_t *store) {
    size_t i;

    for (i = 0; i < store->count; ++i) {
        free(store->items[i].key);
        free(store->items[i].value);
    }
    kc_flow_store_init(store);
}

/**
 * Resolve one record from a store.
 * @param store Store pointer.
 * @param key Lookup key.
 * @return Stored record or NULL.
 */
static const kc_flow_record_t *kc_flow_store_get_record(
    const kc_flow_store_t *store,
    const char *key
) {
    size_t i;

    for (i = 0; i < store->count; ++i) {
        if (strcmp(store->items[i].key, key) == 0) {
            return &store->items[i];
        }
    }
    return NULL;
}

/**
 * Resolve one key from a store.
 * @param store Store pointer.
 * @param key Lookup key.
 * @return Stored value or NULL.
 */
static const char *kc_flow_store_get(const kc_flow_store_t *store, const char *key) {
    const kc_flow_record_t *record = kc_flow_store_get_record(store, key);

    return record ? record->value : NULL;
}

/**
 * Add or replace one store value.
 * @param store Store pointer.
 * @param key Record key.
 * @param value Record value.
 * @param kind Value kind.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_store_set_kind(
    kc_flow_store_t *store,
    const char *key,
    const char *value,
    kc_flow_value_kind_t kind
) {
    size_t i;

    for (i = 0; i < store->count; ++i) {
        if (strcmp(store->items[i].key, key) == 0) {
            char *next = kc_flow_dup(value);
            if (!next) {
                return KC_FLOW_ERROR;
            }
            free(store->items[i].value);
            store->items[i].value = next;
            store->items[i].kind = kind;
            return KC_FLOW_OK;
        }
    }
    if (store->count >= KC_FLOW_MAX_RECORDS) {
        return KC_FLOW_ERROR;
    }
    store->items[store->count].key = kc_flow_dup(key);
    store->items[store->count].value = kc_flow_dup(value);
    store->items[store->count].kind = kind;
    if (!store->items[store->count].key || !store->items[store->count].value) {
        free(store->items[store->count].key);
        free(store->items[store->count].value);
        return KC_FLOW_ERROR;
    }
    store->count++;
    return KC_FLOW_OK;
}

/**
 * Add or replace one literal store value.
 * @param store Store pointer.
 * @param key Record key.
 * @param value Record value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_store_set(
    kc_flow_store_t *store,
    const char *key,
    const char *value
) {
    return kc_flow_store_set_kind(store, key, value, KC_FLOW_VALUE_LITERAL);
}

/**
 * Copy one store into another store.
 * @param dst Destination store.
 * @param src Source store.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_store_copy(kc_flow_store_t *dst, const kc_flow_store_t *src) {
    size_t i;

    kc_flow_store_init(dst);
    for (i = 0; i < src->count; ++i) {
        if (kc_flow_store_set_kind(
            dst,
            src->items[i].key,
            src->items[i].value,
            src->items[i].kind
        ) != KC_FLOW_OK) {
            kc_flow_store_free(dst);
            return KC_FLOW_ERROR;
        }
    }
    return KC_FLOW_OK;
}

/**
 * Initialize one link list.
 * @param links Link list pointer.
 * @return None.
 */
static void kc_flow_links_init(kc_flow_links_t *links) {
    memset(links, 0, sizeof(*links));
}

/**
 * Release one link list.
 * @param links Link list pointer.
 * @return None.
 */
static void kc_flow_links_free(kc_flow_links_t *links) {
    size_t i;

    for (i = 0; i < links->count; ++i) {
        free(links->items[i].key);
        free(links->items[i].value);
    }
    kc_flow_links_init(links);
}

/**
 * Append one link value.
 * @param links Link list pointer.
 * @param value Link value.
 * @param kind Value kind.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_links_add(
    kc_flow_links_t *links,
    const char *value,
    kc_flow_value_kind_t kind
) {
    if (links->count >= KC_FLOW_MAX_RECORDS) {
        return KC_FLOW_ERROR;
    }
    links->items[links->count].key = kc_flow_dup("link");
    links->items[links->count].value = kc_flow_dup(value);
    links->items[links->count].kind = kind;
    if (!links->items[links->count].key || !links->items[links->count].value) {
        free(links->items[links->count].key);
        free(links->items[links->count].value);
        return KC_FLOW_ERROR;
    }
    links->count++;
    return KC_FLOW_OK;
}

/**
 * Trim ASCII whitespace in place.
 * @param text Mutable text.
 * @return Trimmed text pointer.
 */
static char *kc_flow_trim(char *text) {
    char *end;

    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    return text;
}

/**
 * Read one line from a stream.
 * @param fp Stream pointer.
 * @param line Line buffer pointer.
 * @param cap Buffer capacity pointer.
 * @return 1 when a line was read, otherwise 0.
 */
static int kc_flow_read_line(FILE *fp, char **line, size_t *cap) {
    size_t len = 0;
    int ch;

    if (!*line) {
        *cap = 256;
        *line = (char *)malloc(*cap);
        if (!*line) {
            return 0;
        }
    }
    while ((ch = fgetc(fp)) != EOF) {
        if (len + 2 > *cap) {
            char *next;
            *cap *= 2;
            next = (char *)realloc(*line, *cap);
            if (!next) {
                free(*line);
                *line = NULL;
                return 0;
            }
            *line = next;
        }
        (*line)[len++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (len == 0 && ch == EOF) {
        return 0;
    }
    (*line)[len] = '\0';
    return 1;
}

/**
 * Check whether one key segment is structurally valid.
 * @param text Segment pointer.
 * @param len Segment length.
 * @return 1 when valid, otherwise 0.
 */
static int kc_flow_key_part_valid(const char *text, size_t len) {
    size_t i;

    if (len == 0) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        if (isalnum((unsigned char)text[i]) || text[i] == '_' || text[i] == '-') {
            continue;
        }
        return 0;
    }
    return 1;
}

/**
 * Check whether one dotted key suffix is valid.
 * @param text Suffix text.
 * @return 1 when valid, otherwise 0.
 */
static int kc_flow_key_tail_valid(const char *text) {
    const char *part = text;
    const char *cur = text;

    if (!text || !*text) {
        return 0;
    }
    while (*cur) {
        if (*cur == '.') {
            if (!kc_flow_key_part_valid(part, (size_t)(cur - part))) {
                return 0;
            }
            part = cur + 1;
        }
        cur++;
    }
    return kc_flow_key_part_valid(part, (size_t)(cur - part));
}

/**
 * Check whether one full document key is valid.
 * @param key Full key.
 * @return 1 when valid, otherwise 0.
 */
static int kc_flow_key_valid(const char *key) {
    if (strncmp(key, "flow.", 5) == 0) {
        return kc_flow_key_tail_valid(key + 5);
    }
    if (strncmp(key, "node.", 5) == 0) {
        const char *ref = key + 5;
        const char *field = strchr(ref, '.');
        return field && kc_flow_key_part_valid(ref, (size_t)(field - ref)) && kc_flow_key_tail_valid(field + 1);
    }
    if (strncmp(key, "func.", 5) == 0) {
        const char *ref = key + 5;
        const char *field = strchr(ref, '.');
        return field && kc_flow_key_part_valid(ref, (size_t)(field - ref)) && kc_flow_key_tail_valid(field + 1);
    }
    return 0;
}

/**
 * Add one raw document record.
 * @param records Document records.
 * @param key Record key.
 * @param value Record value.
 * @param kind Value kind.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_records_add(
    kc_flow_store_t *records,
    const char *key,
    const char *value,
    kc_flow_value_kind_t kind
) {
    if (records->count >= KC_FLOW_MAX_RECORDS || !kc_flow_key_valid(key)) {
        return KC_FLOW_ERROR;
    }
    records->items[records->count].key = kc_flow_dup(key);
    records->items[records->count].value = kc_flow_dup(value);
    records->items[records->count].kind = kind;
    if (!records->items[records->count].key || !records->items[records->count].value) {
        free(records->items[records->count].key);
        free(records->items[records->count].value);
        return KC_FLOW_ERROR;
    }
    records->count++;
    return KC_FLOW_OK;
}

/**
 * Remove all exact records matching one key.
 * @param records Document records.
 * @param key Record key.
 * @return None.
 */
static void kc_flow_records_remove(kc_flow_store_t *records, const char *key) {
    size_t i = 0;

    while (i < records->count) {
        if (strcmp(records->items[i].key, key) == 0) {
            size_t j;
            free(records->items[i].key);
            free(records->items[i].value);
            for (j = i + 1; j < records->count; ++j) {
                records->items[j - 1] = records->items[j];
            }
            records->count--;
            memset(&records->items[records->count], 0, sizeof(records->items[records->count]));
            continue;
        }
        i++;
    }
}

/**
 * Read one heredoc value from a flow file.
 * @param fp Stream pointer.
 * @param marker End marker.
 * @param out_value Output value pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_read_heredoc(FILE *fp, const char *marker, char **out_value) {
    char *line = NULL;
    char *value = NULL;
    size_t cap = 0;
    size_t size = 0;
    size_t value_cap = 0;

    while (kc_flow_read_line(fp, &line, &cap)) {
        size_t len = strlen(line);
        char *check;
        char *tmp = kc_flow_dup(line);
        if (!tmp) {
            free(line);
            free(value);
            return KC_FLOW_ERROR;
        }
        check = kc_flow_trim(tmp);
        if (strcmp(check, marker) == 0) {
            free(tmp);
            free(line);
            if (!value) {
                value = kc_flow_dup("");
            }
            *out_value = value;
            return value ? KC_FLOW_OK : KC_FLOW_ERROR;
        }
        free(tmp);
        if (size + len + 1 > value_cap) {
            char *next;
            value_cap = value_cap ? value_cap * 2 : 256;
            while (value_cap < size + len + 1) {
                value_cap *= 2;
            }
            next = (char *)realloc(value, value_cap);
            if (!next) {
                free(line);
                free(value);
                return KC_FLOW_ERROR;
            }
            value = next;
        }
        memcpy(value + size, line, len);
        size += len;
        value[size] = '\0';
    }
    free(line);
    free(value);
    return KC_FLOW_ERROR;
}

/**
 * Read flow file records.
 * @param path Flow file path.
 * @param records Output document records.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_read_records(const char *path, kc_flow_store_t *records) {
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;

    kc_flow_store_init(records);
    fp = fopen(path, "r");
    if (!fp) {
        return KC_FLOW_ERROR;
    }
    while (kc_flow_read_line(fp, &line, &cap)) {
        char *key = kc_flow_trim(line);
        char *eq;
        char *value;
        char *owned = NULL;
        kc_flow_value_kind_t kind = KC_FLOW_VALUE_LITERAL;
        if (!*key || *key == '#') {
            continue;
        }
        eq = strchr(key, '=');
        if (!eq || eq == key) {
            free(line);
            fclose(fp);
            kc_flow_store_free(records);
            return KC_FLOW_ERROR;
        }
        *eq = '\0';
        value = kc_flow_trim(eq + 1);
        key = kc_flow_trim(key);
        if (strncmp(value, "<<", 2) == 0) {
            if (kc_flow_read_heredoc(fp, kc_flow_trim(value + 2), &owned) != KC_FLOW_OK) {
                free(line);
                fclose(fp);
                kc_flow_store_free(records);
                return KC_FLOW_ERROR;
            }
            value = owned;
            kind = KC_FLOW_VALUE_EXEC;
        }
        if (kc_flow_records_add(records, key, value, kind) != KC_FLOW_OK) {
            free(owned);
            free(line);
            fclose(fp);
            kc_flow_store_free(records);
            return KC_FLOW_ERROR;
        }
        free(owned);
    }
    free(line);
    fclose(fp);
    return KC_FLOW_OK;
}

/**
 * Apply context overlays to document records.
 * @param ctx Context pointer.
 * @param records Mutable records.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_apply_overlays(kc_flow_t *ctx, kc_flow_store_t *records) {
    size_t i;

    for (i = 0; i < ctx->overlay_count; ++i) {
        if (!kc_flow_key_valid(ctx->overlays[i].key)) {
            return kc_flow_fail(ctx, "invalid structural key");
        }
        if (ctx->overlays[i].kind == KC_FLOW_OVERLAY_UNSET) {
            kc_flow_records_remove(records, ctx->overlays[i].key);
        } else if (kc_flow_store_set_kind(
            records,
            ctx->overlays[i].key,
            ctx->overlays[i].value,
            KC_FLOW_VALUE_LITERAL
        ) != KC_FLOW_OK) {
            return kc_flow_fail(ctx, "unable to apply overlay");
        }
    }
    return KC_FLOW_OK;
}

/**
 * Initialize one parsed model.
 * @param model Model pointer.
 * @return None.
 */
static void kc_flow_model_init(kc_flow_model_t *model) {
    memset(model, 0, sizeof(*model));
}

/**
 * Release one parsed model.
 * @param model Model pointer.
 * @return None.
 */
static void kc_flow_model_free(kc_flow_model_t *model) {
    size_t i;

    free(model->id);
    kc_flow_store_free(&model->data);
    kc_flow_links_free(&model->entries);
    for (i = 0; i < model->node_count; ++i) {
        free(model->nodes[i].ref);
        free(model->nodes[i].use);
        free(model->nodes[i].import);
        free(model->nodes[i].exec);
        kc_flow_store_free(&model->nodes[i].data);
        kc_flow_links_free(&model->nodes[i].links);
    }
    for (i = 0; i < model->func_count; ++i) {
        free(model->funcs[i].ref);
        free(model->funcs[i].use);
        free(model->funcs[i].import);
        free(model->funcs[i].exec);
        kc_flow_store_free(&model->funcs[i].data);
        kc_flow_links_free(&model->funcs[i].links);
    }
    kc_flow_model_init(model);
}

/**
 * Find one parsed node.
 * @param model Model pointer.
 * @param ref Node reference.
 * @return Node pointer or NULL.
 */
static kc_flow_node_t *kc_flow_model_find(kc_flow_model_t *model, const char *ref) {
    size_t i;

    for (i = 0; i < model->node_count; ++i) {
        if (strcmp(model->nodes[i].ref, ref) == 0) {
            return &model->nodes[i];
        }
    }
    return NULL;
}

/**
 * Find one parsed function.
 * @param model Model pointer.
 * @param ref Function reference.
 * @return Function pointer or NULL.
 */
static kc_flow_node_t *kc_flow_model_find_func(kc_flow_model_t *model, const char *ref) {
    size_t i;

    for (i = 0; i < model->func_count; ++i) {
        if (strcmp(model->funcs[i].ref, ref) == 0) {
            return &model->funcs[i];
        }
    }
    return NULL;
}

/**
 * Resolve one node behavior source through node.<ref>.use.
 * The caller node keeps its own data and links; the behavior node provides
 * file/exec.
 * @param model Model pointer.
 * @param node Caller node pointer.
 * @param depth Recursion guard.
 * @return Behavior node pointer or NULL.
 */
static kc_flow_node_t *kc_flow_node_behavior(kc_flow_model_t *model, const kc_flow_node_t *node, int depth) {
    kc_flow_node_t *target;

    if (!node || depth > 64) {
        return NULL;
    }
    if (!node->use || !*node->use) {
        return (kc_flow_node_t *)node;
    }
    target = kc_flow_model_find(model, node->use);
    if (!target) {
        return NULL;
    }
    return kc_flow_node_behavior(model, target, depth + 1);
}

/**
 * Resolve or create one parsed node.
 * @param model Model pointer.
 * @param ref Node reference.
 * @return Node pointer or NULL.
 */
static kc_flow_node_t *kc_flow_model_node(kc_flow_model_t *model, const char *ref) {
    kc_flow_node_t *node;

    node = kc_flow_model_find(model, ref);
    if (node) {
        return node;
    }
    if (model->node_count >= KC_FLOW_MAX_NODES) {
        return NULL;
    }
    node = &model->nodes[model->node_count++];
    memset(node, 0, sizeof(*node));
    node->ref = kc_flow_dup(ref);
    if (!node->ref) {
        return NULL;
    }
    return node;
}

/**
 * Resolve or create one parsed function.
 * @param model Model pointer.
 * @param ref Function reference.
 * @return Function pointer or NULL.
 */
static kc_flow_node_t *kc_flow_model_func(kc_flow_model_t *model, const char *ref) {
    kc_flow_node_t *func;

    func = kc_flow_model_find_func(model, ref);
    if (func) {
        return func;
    }
    if (model->func_count >= KC_FLOW_MAX_NODES) {
        return NULL;
    }
    func = &model->funcs[model->func_count++];
    memset(func, 0, sizeof(*func));
    func->ref = kc_flow_dup(ref);
    if (!func->ref) {
        return NULL;
    }
    return func;
}

/**
 * Parse one node-scoped record.
 * @param model Model pointer.
 * @param key Record key.
 * @param value Record value.
 * @param kind Value kind.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_parse_node(
    kc_flow_model_t *model,
    const char *key,
    const char *value,
    kc_flow_value_kind_t kind
) {
    const char *ref_start = key + 5;
    const char *field = strchr(ref_start, '.');
    char ref[KC_FLOW_MAX_KEY];
    kc_flow_node_t *node;
    size_t len;

    if (!field || field == ref_start) {
        return KC_FLOW_ERROR;
    }
    len = (size_t)(field - ref_start);
    if (len >= sizeof(ref)) {
        return KC_FLOW_ERROR;
    }
    memcpy(ref, ref_start, len);
    ref[len] = '\0';
    node = kc_flow_model_node(model, ref);
    if (!node) {
        return KC_FLOW_ERROR;
    }
    field++;
    if (strcmp(field, "use") == 0) {
        char *next = kc_flow_dup(value);
        if (!next) {
            return KC_FLOW_ERROR;
        }
        free(node->use);
        node->use = next;
        return KC_FLOW_OK;
    }
    if (strcmp(field, "import") == 0) {
        char *next = kc_flow_dup(value);
        if (!next) {
            return KC_FLOW_ERROR;
        }
        free(node->import);
        node->import = next;
        node->import_kind = kind;
        return KC_FLOW_OK;
    }
    if (strcmp(field, "exec") == 0) {
        char *next = kc_flow_dup(value);
        if (!next) {
            return KC_FLOW_ERROR;
        }
        free(node->exec);
        node->exec = next;
        node->exec_kind = kind;
        return KC_FLOW_OK;
    }
    if (strcmp(field, "link") == 0) {
        return kc_flow_links_add(&node->links, value, kind);
    }
    return kc_flow_store_set_kind(&node->data, field, value, kind);
}

/**
 * Parse one function-scoped record.
 * @param model Model pointer.
 * @param key Record key.
 * @param value Record value.
 * @param kind Value kind.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_parse_func(
    kc_flow_model_t *model,
    const char *key,
    const char *value,
    kc_flow_value_kind_t kind
) {
    const char *ref_start = key + 5;
    const char *field = strchr(ref_start, '.');
    char ref[KC_FLOW_MAX_KEY];
    kc_flow_node_t *func;
    size_t len;

    if (!field || field == ref_start) {
        return KC_FLOW_ERROR;
    }
    len = (size_t)(field - ref_start);
    if (len >= sizeof(ref)) {
        return KC_FLOW_ERROR;
    }
    memcpy(ref, ref_start, len);
    ref[len] = '\0';
    func = kc_flow_model_func(model, ref);
    if (!func) {
        return KC_FLOW_ERROR;
    }
    field++;
    if (strcmp(field, "use") == 0) {
        char *next = kc_flow_dup(value);
        if (!next) {
            return KC_FLOW_ERROR;
        }
        free(func->use);
        func->use = next;
        return KC_FLOW_OK;
    }
    if (strcmp(field, "import") == 0) {
        char *next = kc_flow_dup(value);
        if (!next) {
            return KC_FLOW_ERROR;
        }
        free(func->import);
        func->import = next;
        func->import_kind = kind;
        return KC_FLOW_OK;
    }
    if (strcmp(field, "exec") == 0) {
        char *next = kc_flow_dup(value);
        if (!next) {
            return KC_FLOW_ERROR;
        }
        free(func->exec);
        func->exec = next;
        func->exec_kind = kind;
        return KC_FLOW_OK;
    }
    return kc_flow_store_set_kind(&func->data, field, value, kind);
}

/**
 * Parse effective records into one model.
 * @param records Document records.
 * @param model Output model.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_parse_model(const kc_flow_store_t *records, kc_flow_model_t *model) {
    size_t i;

    kc_flow_model_init(model);
    for (i = 0; i < records->count; ++i) {
        const char *key = records->items[i].key;
        const char *value = records->items[i].value;
        kc_flow_value_kind_t kind = records->items[i].kind;
        if (strcmp(key, "flow.id") == 0) {
            char *next = kc_flow_dup(value);
            if (!next) {
                return KC_FLOW_ERROR;
            }
            free(model->id);
            model->id = next;
        } else if (strcmp(key, "flow.link") == 0) {
            if (kc_flow_links_add(&model->entries, value, kind) != KC_FLOW_OK) {
                return KC_FLOW_ERROR;
            }
        } else if (strncmp(key, "node.", 5) == 0) {
            if (kc_flow_parse_node(model, key, value, kind) != KC_FLOW_OK) {
                return KC_FLOW_ERROR;
            }
        } else if (strncmp(key, "func.", 5) == 0) {
            if (kc_flow_parse_func(model, key, value, kind) != KC_FLOW_OK) {
                return KC_FLOW_ERROR;
            }
        } else if (strncmp(key, "flow.", 5) == 0) {
            if (kc_flow_store_set_kind(&model->data, key + 5, value, kind) != KC_FLOW_OK) {
                return KC_FLOW_ERROR;
            }
        } else {
            return KC_FLOW_ERROR;
        }
    }
    return KC_FLOW_OK;
}

/**
 * Walk one node for validation.
 * @param model Model pointer.
 * @param node Node pointer.
 * @param seen Seen markers.
 * @param stack Stack markers.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_validate_walk(kc_flow_model_t *model, kc_flow_node_t *node, unsigned char *seen, unsigned char *stack) {
    size_t i;
    size_t index = (size_t)(node - model->nodes);

    if (stack[index]) {
        return KC_FLOW_ERROR;
    }
    if (seen[index]) {
        return KC_FLOW_OK;
    }
    seen[index] = 1;
    stack[index] = 1;
    if (node->use && *node->use && !kc_flow_model_find(model, node->use)) {
        return KC_FLOW_ERROR;
    }
    for (i = 0; i < node->links.count; ++i) {
        kc_flow_node_t *child;
        if (node->links.items[i].kind == KC_FLOW_VALUE_EXEC) {
            continue;
        }
        child = kc_flow_model_find(model, node->links.items[i].value);
        if (!child || kc_flow_validate_walk(model, child, seen, stack) != KC_FLOW_OK) {
            return KC_FLOW_ERROR;
        }
    }
    stack[index] = 0;
    return KC_FLOW_OK;
}

/**
 * Validate targets and cycles in one model.
 * @param model Model pointer.
 * @param entry Optional explicit entry.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_validate_model(kc_flow_model_t *model, const char *entry) {
    unsigned char seen[KC_FLOW_MAX_NODES];
    unsigned char stack[KC_FLOW_MAX_NODES];
    size_t i;

    memset(seen, 0, sizeof(seen));
    memset(stack, 0, sizeof(stack));
    if (entry) {
        kc_flow_node_t *node = kc_flow_model_find(model, entry);
        return node ? kc_flow_validate_walk(model, node, seen, stack) : KC_FLOW_ERROR;
    }
    for (i = 0; i < model->entries.count; ++i) {
        kc_flow_node_t *node;
        if (model->entries.items[i].kind == KC_FLOW_VALUE_EXEC) {
            continue;
        }
        node = kc_flow_model_find(model, model->entries.items[i].value);
        if (!node || kc_flow_validate_walk(model, node, seen, stack) != KC_FLOW_OK) {
            return KC_FLOW_ERROR;
        }
    }
    return KC_FLOW_OK;
}

/**
 * Build one path relative to another file.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @param base_file Base file path.
 * @param value Target value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_relative_path(char *out, size_t out_size, const char *base_file, const char *value) {
    const char *slash;
    size_t dir_len;

    if (!value || !*value) {
        return KC_FLOW_ERROR;
    }
    if (value[0] == '/'
#ifdef _WIN32
        || (isalpha((unsigned char)value[0]) && value[1] == ':')
#endif
    ) {
        return snprintf(out, out_size, "%s", value) >= (int)out_size ? KC_FLOW_ERROR : KC_FLOW_OK;
    }
    slash = strrchr(base_file, '/');
#ifdef _WIN32
    const char *backslash = strrchr(base_file, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
#endif
    if (!slash) {
        return snprintf(out, out_size, "%s", value) >= (int)out_size ? KC_FLOW_ERROR : KC_FLOW_OK;
    }
    dir_len = (size_t)(slash - base_file);
    if (dir_len + 1 + strlen(value) + 1 > out_size) {
        return KC_FLOW_ERROR;
    }
    memcpy(out, base_file, dir_len);
    out[dir_len] = '/';
    strcpy(out + dir_len + 1, value);
    return KC_FLOW_OK;
}

/**
 * Build one directory string from a file path.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @param file File path.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_dir(char *out, size_t out_size, const char *file) {
    const char *slash = strrchr(file, '/');
#ifdef _WIN32
    const char *backslash = strrchr(file, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
#endif
    size_t len;

    if (!slash) {
        return snprintf(out, out_size, ".") >= (int)out_size ? KC_FLOW_ERROR : KC_FLOW_OK;
    }
    len = (size_t)(slash - file);
    if (len == 0) {
        len = 1;
    }
    if (len >= out_size) {
        return KC_FLOW_ERROR;
    }
    memcpy(out, file, len);
    out[len] = '\0';
    return KC_FLOW_OK;
}

/**
 * Append text to a heap buffer.
 * @param out Output buffer pointer.
 * @param cap Output capacity pointer.
 * @param len Output length pointer.
 * @param text Text to append.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_append_text(char **out, size_t *cap, size_t *len, const char *text) {
    size_t add = strlen(text);

    while (*len + add + 1 > *cap) {
        char *next;
        *cap = *cap ? *cap * 2 : 256;
        next = (char *)realloc(*out, *cap);
        if (!next) {
            free(*out);
            *out = NULL;
            return KC_FLOW_ERROR;
        }
        *out = next;
    }
    memcpy(*out + *len, text, add);
    *len += add;
    (*out)[*len] = '\0';
    return KC_FLOW_OK;
}

/**
 * Append one resolved shell value in the current quote state.
 * @param out Output buffer pointer.
 * @param cap Output capacity pointer.
 * @param len Output length pointer.
 * @param text Text to append.
 * @param quote Current shell quote state.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_append_shell_value(
    char **out,
    size_t *cap,
    size_t *len,
    const char *text,
    kc_flow_quote_state_t quote
) {
    size_t i;

    if (quote == KC_FLOW_QUOTE_NONE) {
        return kc_flow_append_text(out, cap, len, text);
    }
    for (i = 0; text[i]; i++) {
        char ch[2];
        ch[0] = text[i];
        ch[1] = '\0';
        if (quote == KC_FLOW_QUOTE_SINGLE && text[i] == '\'') {
            if (kc_flow_append_text(out, cap, len, "'\\''") != KC_FLOW_OK) return KC_FLOW_ERROR;
        } else {
            if (quote == KC_FLOW_QUOTE_DOUBLE && strchr("\"\\$`", text[i])) {
                if (kc_flow_append_text(out, cap, len, "\\") != KC_FLOW_OK) return KC_FLOW_ERROR;
            }
            if (kc_flow_append_text(out, cap, len, ch) != KC_FLOW_OK) return KC_FLOW_ERROR;
        }
    }
    return KC_FLOW_OK;
}

/**
 * Updates shell quote state from one literal template character.
 * @param quote Current shell quote state pointer.
 * @param escaped Current escape state pointer.
 * @param ch Literal character.
 * @return None.
 */
static void kc_flow_shell_quote_step(kc_flow_quote_state_t *quote, int *escaped, char ch) {
    if (*quote != KC_FLOW_QUOTE_SINGLE && ch == '\\' && !*escaped) {
        *escaped = 1;
        return;
    }
    if (ch == '\'' && *quote == KC_FLOW_QUOTE_NONE && !*escaped) {
        *quote = KC_FLOW_QUOTE_SINGLE;
    } else if (ch == '\'' && *quote == KC_FLOW_QUOTE_SINGLE) {
        *quote = KC_FLOW_QUOTE_NONE;
    } else if (ch == '"' && *quote != KC_FLOW_QUOTE_SINGLE && !*escaped) {
        *quote = (*quote == KC_FLOW_QUOTE_DOUBLE) ? KC_FLOW_QUOTE_NONE : KC_FLOW_QUOTE_DOUBLE;
    }
    *escaped = 0;
}

static int kc_flow_run_command(
    kc_flow_t *ctx,
    const char *command,
    const char *flow_path,
    const void *input,
    size_t input_size,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *current_node,
    const kc_flow_store_t *node,
    char **out_data,
    size_t *out_size,
    int ignore_error
);

static char *kc_flow_template(
    kc_flow_t *ctx,
    const char *flow_path,
    const char *text,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *node,
    const kc_flow_store_t *node_data,
    kc_flow_store_t *cache,
    int depth
);

static char *kc_flow_template_mode(
    kc_flow_t *ctx,
    const char *flow_path,
    const char *text,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *node,
    const kc_flow_store_t *node_data,
    kc_flow_store_t *cache,
    int depth,
    kc_flow_template_mode_t mode
);

static int kc_flow_collect_node_from(
    kc_flow_t *ctx,
    const char *flow_path,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *current,
    const kc_flow_node_t *source,
    kc_flow_store_t *out,
    kc_flow_store_t *cache,
    int depth
);

/**
 * Remove exactly one trailing newline from one owned buffer.
 * @param value Mutable value.
 * @param size Value size pointer.
 * @return None.
 */
static void kc_flow_chomp_one(char *value, size_t *size) {
    if (*size > 0 && value[*size - 1] == '\n') {
        (*size)--;
        value[*size] = '\0';
    }
}

/**
 * Resolve one value record in the current execution scope.
 * @param ctx Context pointer.
 * @param flow_path Current flow path.
 * @param model Parsed model.
 * @param flow Effective flow data.
 * @param current Current node.
 * @param node_data Current node data.
 * @param cache Per-node value cache.
 * @param cache_key Cache key.
 * @param record Source value record.
 * @param input Input data for executable values.
 * @param input_size Input data size.
 * @param depth Recursion guard.
 * @return Owned resolved value or NULL.
 */
static char *kc_flow_resolve_record(
    kc_flow_t *ctx,
    const char *flow_path,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *current,
    const kc_flow_store_t *node_data,
    kc_flow_store_t *cache,
    const char *cache_key,
    const kc_flow_record_t *record,
    const void *input,
    size_t input_size,
    int depth
) {
    const char *cached;
    char *command;
    char *output = NULL;
    size_t output_size = 0;

    if (!record || depth > 64) {
        return NULL;
    }
    if (record->kind == KC_FLOW_VALUE_LITERAL) {
        return kc_flow_template(
            ctx,
            flow_path,
            record->value,
            model,
            flow,
            current,
            node_data,
            cache,
            depth + 1
        );
    }
    cached = cache ? kc_flow_store_get(cache, cache_key) : NULL;
    if (cached) {
        return kc_flow_dup(cached);
    }
    command = kc_flow_template_mode(
        ctx,
        flow_path,
        record->value,
        model,
        flow,
        current,
        node_data,
        cache,
        depth + 1,
        KC_FLOW_TEMPLATE_SHELL
    );
    if (!command) {
        kc_flow_fail(ctx, "template expansion failed inside computed value");
        return NULL;
    }
    if (kc_flow_run_command(
        ctx,
        command,
        flow_path,
        input,
        input_size,
        model,
        flow,
        current,
        node_data,
        &output,
        &output_size,
        0
    ) != KC_FLOW_OK) {
        free(command);
        kc_flow_fail(ctx, "computed value exited with non-zero status");
        return NULL;
    }
    free(command);
    kc_flow_chomp_one(output, &output_size);
    if (cache && kc_flow_store_set(cache, cache_key, output) != KC_FLOW_OK) {
        free(output);
        kc_flow_fail(ctx, "unable to cache computed value");
        return NULL;
    }
    return output;
}

/**
 * Resolve one specific key from a target node, possibly template-expanded.
 * @param model Parsed model.
 * @param flow Flow data.
 * @param current Current node (for template context).
 * @param current_data Resolved current data.
 * @param target Target node to read from.
 * @param key Key to resolve.
 * @param depth Recursion guard.
 * @return Owned resolved value or NULL.
 */
static char *kc_flow_resolve_node_value(
    kc_flow_t *ctx,
    const char *flow_path,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *current,
    const kc_flow_store_t *current_data,
    const kc_flow_node_t *target,
    const char *key,
    kc_flow_store_t *cache,
    int depth
) {
    const kc_flow_record_t *record;
    kc_flow_record_t special;
    char cache_key[KC_FLOW_MAX_KEY * 2];

    if (depth > 64 || !target || !key || !*key) {
        return NULL;
    }

    if (target == current && current_data) {
        const char *value = kc_flow_store_get(current_data, key);
        if (value) {
            return kc_flow_dup(value);
        }
    }

    if (snprintf(
        cache_key,
        sizeof(cache_key),
        "node.%s.%s",
        target->ref,
        key
    ) >= (int)sizeof(cache_key)) {
        return NULL;
    }

    if (strcmp(key, "import") == 0 && target->import) {
        special.key = (char *)key;
        special.value = target->import;
        special.kind = target->import_kind ? target->import_kind : KC_FLOW_VALUE_LITERAL;
        return kc_flow_resolve_record(
            ctx,
            flow_path,
            model,
            flow,
            target,
            NULL,
            cache,
            cache_key,
            &special,
            NULL,
            0,
            depth + 1
        );
    }
    if (strcmp(key, "exec") == 0 && target->exec) {
        special.key = (char *)key;
        special.value = target->exec;
        special.kind = target->exec_kind ? target->exec_kind : KC_FLOW_VALUE_LITERAL;
        return kc_flow_resolve_record(
            ctx,
            flow_path,
            model,
            flow,
            target,
            NULL,
            cache,
            cache_key,
            &special,
            NULL,
            0,
            depth + 1
        );
    }

    record = kc_flow_store_get_record(&target->data, key);
    if (!record) {
        return NULL;
    }

    return kc_flow_resolve_record(
        ctx,
        flow_path,
        model,
        flow,
        target,
        NULL,
        cache,
        cache_key,
        record,
        NULL,
        0,
        depth + 1
    );
}

/**
 * Parse one func placeholder body.
 * Syntax: func.<function-name> <argument-node-or-prefix>
 * Examples:
 *   <func.static robots>
 *   <func.static route.robots>
 * @param text Placeholder text beginning after "call.".
 * @param fn Function node reference output.
 * @param fn_size Function node reference output size.
 * @param arg Argument node or node-prefix output.
 * @param arg_size Argument output size.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_parse_call_ref(const char *text, char *fn, size_t fn_size, char *arg, size_t arg_size) {
    const char *p = text;
    const char *start;
    size_t len;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    start = p;
    while (*p && *p != ' ' && *p != '\t') {
        p++;
    }
    len = (size_t)(p - start);
    if (len == 0 || len >= fn_size) {
        return KC_FLOW_ERROR;
    }
    memcpy(fn, start, len);
    fn[len] = '\0';

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    start = p;
    while (*p) {
        p++;
    }
    while (p > start && (p[-1] == ' ' || p[-1] == '\t')) {
        p--;
    }
    len = (size_t)(p - start);
    if (len == 0 || len >= arg_size) {
        return KC_FLOW_ERROR;
    }
    memcpy(arg, start, len);
    arg[len] = '\0';
    return KC_FLOW_OK;
}

/**
 * Resolve a call argument target. The argument may name one node directly
 * ("robots") or one data prefix in a node ("route.robots").
 * @param model Parsed model.
 * @param arg Argument text.
 * @param out_prefix Prefix inside the argument node data.
 * @param out_prefix_size Prefix output size.
 * @return Argument node pointer or NULL.
 */
static kc_flow_node_t *kc_flow_call_arg_node(
    kc_flow_model_t *model,
    const char *arg,
    char *out_prefix,
    size_t out_prefix_size
) {
    kc_flow_node_t *node;
    const char *dot;
    size_t len;

    node = kc_flow_model_find(model, arg);
    if (node) {
        if (out_prefix_size > 0) {
            out_prefix[0] = '\0';
        }
        return node;
    }

    dot = strchr(arg, '.');
    if (!dot || dot == arg) {
        return NULL;
    }

    len = (size_t)(dot - arg);
    if (len == 0 || len >= KC_FLOW_MAX_KEY) {
        return NULL;
    }

    {
        char ref[KC_FLOW_MAX_KEY];
        memcpy(ref, arg, len);
        ref[len] = '\0';
        node = kc_flow_model_find(model, ref);
    }

    if (!node) {
        return NULL;
    }

    if (snprintf(out_prefix, out_prefix_size, "%s", dot + 1) >= (int)out_prefix_size) {
        return NULL;
    }
    return node;
}

/**
 * Resolve one <arg.*> value for a func expansion.
 * @param model Parsed model.
 * @param flow Flow data.
 * @param caller Current caller node.
 * @param caller_data Current caller data.
 * @param arg_node Argument node.
 * @param prefix Optional argument key prefix.
 * @param key Argument key.
 * @param depth Recursion guard.
 * @return Owned resolved value or NULL.
 */
static char *kc_flow_resolve_arg_value(
    kc_flow_t *ctx,
    const char *flow_path,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *caller,
    const kc_flow_store_t *caller_data,
    const kc_flow_node_t *arg_node,
    const char *prefix,
    const char *key,
    kc_flow_store_t *cache,
    int depth
) {
    char full[KC_FLOW_MAX_KEY];

    (void)caller;
    (void)caller_data;

    if (!arg_node || !key || !*key || depth > 64) {
        return NULL;
    }

    if (prefix && *prefix) {
        if (snprintf(full, sizeof(full), "%s.%s", prefix, key) >= (int)sizeof(full)) {
            return NULL;
        }
        key = full;
    }

    return kc_flow_resolve_node_value(
        ctx,
        flow_path,
        model,
        flow,
        arg_node,
        NULL,
        arg_node,
        key,
        cache,
        depth + 1
    );
}

/**
 * Expand only <arg.*> tags in one function body. Other tags are preserved
 * and resolved by the normal template engine afterwards.
 * @param text Function body.
 * @param model Parsed model.
 * @param flow Flow data.
 * @param caller Caller node.
 * @param caller_data Caller node data.
 * @param arg_node Argument node.
 * @param prefix Optional argument key prefix.
 * @param depth Recursion guard.
 * @return Owned expanded body or NULL.
 */
static char *kc_flow_expand_arg_tags(
    kc_flow_t *ctx,
    const char *flow_path,
    const char *text,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *caller,
    const kc_flow_store_t *caller_data,
    const kc_flow_node_t *arg_node,
    const char *prefix,
    kc_flow_store_t *cache,
    int depth,
    kc_flow_template_mode_t mode
) {
    char *out;
    size_t cap;
    size_t len = 0;
    size_t i;
    kc_flow_quote_state_t quote = KC_FLOW_QUOTE_NONE;
    int escaped = 0;

    if (!text || depth > 64) {
        return NULL;
    }

    cap = strlen(text) + 1;
    out = (char *)malloc(cap);
    if (!out) {
        return NULL;
    }
    out[0] = '\0';

    for (i = 0; text[i]; ++i) {
        if (text[i] == '<' && strncmp(text + i + 1, "arg.", 4) == 0) {
            size_t end = i + 1;
            size_t name_len;
            char key[KC_FLOW_MAX_KEY];
            char *value;

            while (text[end] && text[end] != '>') {
                end++;
            }
            if (text[end] != '>') {
                free(out);
                return NULL;
            }

            name_len = end - i - 1;
            if (name_len <= 4 || name_len >= sizeof(key) + 4) {
                free(out);
                return NULL;
            }
            if (name_len - 4 >= sizeof(key)) {
                free(out);
                return NULL;
            }
            memcpy(key, text + i + 1 + 4, name_len - 4);
            key[name_len - 4] = '\0';

            value = kc_flow_resolve_arg_value(
                ctx,
                flow_path,
                model,
                flow,
                caller,
                caller_data,
                arg_node,
                prefix,
                key,
                cache,
                depth + 1
            );
            if (!value) {
                free(out);
                return NULL;
            }
            if (mode == KC_FLOW_TEMPLATE_SHELL) {
                if (kc_flow_append_shell_value(&out, &cap, &len, value, quote) != KC_FLOW_OK) {
                    free(value);
                    return NULL;
                }
            } else if (kc_flow_append_text(&out, &cap, &len, value) != KC_FLOW_OK) {
                free(value);
                return NULL;
            }
            free(value);
            i = end;
        } else {
            char ch[2];
            ch[0] = text[i];
            ch[1] = '\0';
            if (mode == KC_FLOW_TEMPLATE_SHELL) {
                kc_flow_shell_quote_step(&quote, &escaped, text[i]);
            }
            if (kc_flow_append_text(&out, &cap, &len, ch) != KC_FLOW_OK) {
                return NULL;
            }
        }
    }
    return out;
}

/**
 * Expand one func placeholder into inline shell/text.
 * Syntax:
 *   <func.static robots>
 *   <func.static route.robots>
 * The function node provides exec text; the argument node/prefix provides
 * <arg.*> values. The expanded body is then resolved as a normal template.
 * @param name Placeholder text without angle brackets.
 * @param model Parsed model.
 * @param flow Flow data.
 * @param caller Current caller node.
 * @param caller_data Caller data.
 * @param depth Recursion guard.
 * @return Owned expanded call output or NULL.
 */
static char *kc_flow_expand_call(
    kc_flow_t *ctx,
    const char *flow_path,
    const char *name,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *caller,
    const kc_flow_store_t *caller_data,
    kc_flow_store_t *cache,
    int depth
) {
    char fn_ref[KC_FLOW_MAX_KEY];
    char arg_ref[KC_FLOW_MAX_KEY];
    char prefix[KC_FLOW_MAX_KEY];
    kc_flow_node_t *fn_node;
    kc_flow_node_t *behavior;
    kc_flow_node_t *arg_node;
    kc_flow_store_t fn_data;
    char *with_args;
    char *expanded;
    kc_flow_template_mode_t mode;

    if (depth > 64 || strncmp(name, "func.", 5) != 0) {
        return NULL;
    }

    if (kc_flow_parse_call_ref(name + 5, fn_ref, sizeof(fn_ref), arg_ref, sizeof(arg_ref)) != KC_FLOW_OK) {
        return NULL;
    }

    fn_node = kc_flow_model_find_func(model, fn_ref);
    if (!fn_node) {
        fn_node = kc_flow_model_find(model, fn_ref);
    }
    if (!fn_node) {
        return NULL;
    }
    behavior = kc_flow_model_find_func(model, fn_ref) ? fn_node : kc_flow_node_behavior(model, fn_node, 0);
    if (!behavior || !behavior->exec) {
        return NULL;
    }
    mode = KC_FLOW_TEMPLATE_SHELL;

    arg_node = kc_flow_call_arg_node(model, arg_ref, prefix, sizeof(prefix));
    if (!arg_node) {
        return NULL;
    }

    with_args = kc_flow_expand_arg_tags(
        ctx,
        flow_path,
        behavior->exec,
        model,
        flow,
        caller,
        caller_data,
        arg_node,
        prefix,
        cache,
        depth + 1,
        mode
    );
    if (!with_args) {
        return NULL;
    }

    kc_flow_store_init(&fn_data);
    if (kc_flow_collect_node_from(
        ctx,
        flow_path,
        model,
        flow,
        fn_node,
        fn_node,
        &fn_data,
        cache,
        0
    ) != KC_FLOW_OK) {
        free(with_args);
        kc_flow_store_free(&fn_data);
        return NULL;
    }

    expanded = kc_flow_template_mode(
        ctx,
        flow_path,
        with_args,
        model,
        flow,
        fn_node,
        &fn_data,
        cache,
        depth + 1,
        mode
    );
    free(with_args);
    if (expanded && behavior->exec_kind == KC_FLOW_VALUE_EXEC) {
        char *output = NULL;
        size_t output_size = 0;

        if (kc_flow_run_command(
            ctx,
            expanded,
            flow_path,
            NULL,
            0,
            model,
            flow,
            fn_node,
            &fn_data,
            &output,
            &output_size,
            0
        ) != KC_FLOW_OK) {
            free(expanded);
            kc_flow_store_free(&fn_data);
            kc_flow_fail(ctx, "computed value exited with non-zero status");
            return NULL;
        }
        free(expanded);
        kc_flow_chomp_one(output, &output_size);
        expanded = output;
    }
    kc_flow_store_free(&fn_data);
    return expanded;
}

/**
 * Resolve one placeholder reference.
 * @param name Placeholder name without angle brackets.
 * @param model Parsed model.
 * @param flow Effective flow data.
 * @param node Current node.
 * @param node_data Resolved current node data.
 * @param depth Recursion guard.
 * @return Owned resolved value or NULL.
 */
static char *kc_flow_resolve_ref(
    kc_flow_t *ctx,
    const char *flow_path,
    const char *name,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *node,
    const kc_flow_store_t *node_data,
    kc_flow_store_t *cache,
    int depth
) {
    if (depth > 64 || !name || !*name) {
        return NULL;
    }

    if (strncmp(name, "func.", 5) == 0 && strchr(name, ' ') != NULL) {
        return kc_flow_expand_call(
            ctx,
            flow_path,
            name,
            model,
            flow,
            node,
            node_data,
            cache,
            depth + 1
        );
    }

    if (strncmp(name, "flow.", 5) == 0) {
        const char *key = name + 5;
        const kc_flow_record_t *record;
        char cache_key[KC_FLOW_MAX_KEY * 2];

        if (strcmp(key, "id") == 0 && model->id) {
            return kc_flow_dup(model->id);
        }
        record = kc_flow_store_get_record(flow, key);
        if (!record) {
            return NULL;
        }
        if (snprintf(cache_key, sizeof(cache_key), "flow.%s", key) >= (int)sizeof(cache_key)) {
            return NULL;
        }
        return kc_flow_resolve_record(
            ctx,
            flow_path,
            model,
            flow,
            node,
            node_data,
            cache,
            cache_key,
            record,
            NULL,
            0,
            depth + 1
        );
    }

    if (strncmp(name, "func.", 5) == 0) {
        const char *rest = name + 5;
        const char *dot = strchr(rest, '.');

        if (dot && dot != rest) {
            char ref[KC_FLOW_MAX_KEY];
            size_t ref_len = (size_t)(dot - rest);
            kc_flow_node_t *target;

            if (ref_len < sizeof(ref)) {
                memcpy(ref, rest, ref_len);
                ref[ref_len] = '\0';
                target = kc_flow_model_find_func(model, ref);
                if (target) {
                    return kc_flow_resolve_node_value(
                        ctx,
                        flow_path,
                        model,
                        flow,
                        target,
                        NULL,
                        target,
                        dot + 1,
                        cache,
                        depth + 1
                    );
                }
            }
        }

        return kc_flow_resolve_node_value(
            ctx,
            flow_path,
            model,
            flow,
            node,
            node_data,
            node,
            rest,
            cache,
            depth + 1
        );
    }

    if (strncmp(name, "node.", 5) == 0) {
        const char *rest = name + 5;
        const char *dot = strchr(rest, '.');

        if (dot && dot != rest) {
            char ref[KC_FLOW_MAX_KEY];
            size_t ref_len = (size_t)(dot - rest);
            kc_flow_node_t *target;

            if (ref_len < sizeof(ref)) {
                memcpy(ref, rest, ref_len);
                ref[ref_len] = '\0';
                target = kc_flow_model_find(model, ref);
                if (target) {
                    return kc_flow_resolve_node_value(
                        ctx,
                        flow_path,
                        model,
                        flow,
                        node,
                        node_data,
                        target,
                        dot + 1,
                        cache,
                        depth + 1
                    );
                }
            }
        }

        return kc_flow_resolve_node_value(
            ctx,
            flow_path,
            model,
            flow,
            node,
            node_data,
            node,
            rest,
            cache,
            depth + 1
        );
    }

    return NULL;
}

/**
 * Resolve placeholders in one template.
 * @param text Template text.
 * @param model Parsed model.
 * @param flow Effective flow data.
 * @param node Current node.
 * @param node_data Resolved current node data.
 * @param depth Recursion guard.
 * @return Resolved heap string or NULL.
 */
static char *kc_flow_template(
    kc_flow_t *ctx,
    const char *flow_path,
    const char *text,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *node,
    const kc_flow_store_t *node_data,
    kc_flow_store_t *cache,
    int depth
) {
    return kc_flow_template_mode(
        ctx,
        flow_path,
        text,
        model,
        flow,
        node,
        node_data,
        cache,
        depth,
        KC_FLOW_TEMPLATE_TEXT
    );
}

/**
 * Resolve placeholders in one template.
 * @param text Template text.
 * @param model Parsed model.
 * @param flow Effective flow data.
 * @param node Current node.
 * @param node_data Resolved current node data.
 * @param depth Recursion guard.
 * @param mode Template expansion mode.
 * @return Resolved heap string or NULL.
 */
static char *kc_flow_template_mode(
    kc_flow_t *ctx,
    const char *flow_path,
    const char *text,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *node,
    const kc_flow_store_t *node_data,
    kc_flow_store_t *cache,
    int depth,
    kc_flow_template_mode_t mode
) {
    char *out;
    size_t cap;
    size_t len = 0;
    size_t i;
    kc_flow_quote_state_t quote = KC_FLOW_QUOTE_NONE;
    int escaped = 0;

    if (!text || depth > 64) {
        return NULL;
    }
    cap = strlen(text) + 1;
    out = (char *)malloc(cap);
    if (!out) {
        return NULL;
    }
    out[0] = '\0';

    for (i = 0; text[i]; ++i) {
        if (text[i] == '<') {
            char name[KC_FLOW_MAX_KEY];
            char *resolved_name;
            char *value;
            size_t end = i + 1;
            size_t name_len;
            int balance = 1;

            while (text[end] && balance > 0) {
                if (text[end] == '<') {
                    balance++;
                } else if (text[end] == '>') {
                    balance--;
                }
                if (balance > 0) {
                    end++;
                }
            }
            if (text[end] != '>') {
                free(out);
                return NULL;
            }
            name_len = end - i - 1;
            if (name_len == 0 || name_len >= sizeof(name)) {
                free(out);
                return NULL;
            }
            memcpy(name, text + i + 1, name_len);
            name[name_len] = '\0';

            resolved_name = kc_flow_template(
                ctx,
                flow_path,
                name,
                model,
                flow,
                node,
                node_data,
                cache,
                depth + 1
            );
            value = kc_flow_resolve_ref(
                ctx,
                flow_path,
                resolved_name ? resolved_name : name,
                model,
                flow,
                node,
                node_data,
                cache,
                depth + 1
            );
            free(resolved_name);
            if (!value) {
                free(out);
                return NULL;
            }
            if (mode == KC_FLOW_TEMPLATE_SHELL) {
                if (kc_flow_append_shell_value(&out, &cap, &len, value, quote) != KC_FLOW_OK) {
                    free(value);
                    return NULL;
                }
            } else if (kc_flow_append_text(&out, &cap, &len, value) != KC_FLOW_OK) {
                free(value);
                return NULL;
            }
            free(value);
            i = end;
        } else {
            char ch[2];
            ch[0] = text[i];
            ch[1] = '\0';
            if (mode == KC_FLOW_TEMPLATE_SHELL) {
                kc_flow_shell_quote_step(&quote, &escaped, text[i]);
            }
            if (kc_flow_append_text(&out, &cap, &len, ch) != KC_FLOW_OK) {
                return NULL;
            }
        }
    }
    return out;
}

/**
 * Collect one node data scope.
 * @param model Parsed model.
 * @param flow Effective flow data.
 * @param node Node pointer.
 * @param out Output data store.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_collect_node_from(
    kc_flow_t *ctx,
    const char *flow_path,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *current,
    const kc_flow_node_t *source,
    kc_flow_store_t *out,
    kc_flow_store_t *cache,
    int depth
) {
    size_t i;

    if (!source || depth > 64) {
        return KC_FLOW_ERROR;
    }

    if (source->use && *source->use) {
        kc_flow_node_t *base = kc_flow_model_find_func(model, source->use);
        if (!base) {
            base = kc_flow_model_find(model, source->use);
        }
        if (!base || kc_flow_collect_node_from(
            ctx,
            flow_path,
            model,
            flow,
            current,
            base,
            out,
            cache,
            depth + 1
        ) != KC_FLOW_OK) {
            return KC_FLOW_ERROR;
        }
    }

    for (i = 0; i < source->data.count; ++i) {
        char cache_key[KC_FLOW_MAX_KEY * 2];
        char *value;

        if (snprintf(
            cache_key,
            sizeof(cache_key),
            "node.%s.%s",
            source->ref,
            source->data.items[i].key
        ) >= (int)sizeof(cache_key)) {
            return KC_FLOW_ERROR;
        }
        value = kc_flow_resolve_record(
            ctx,
            flow_path,
            model,
            flow,
            current,
            out,
            cache,
            cache_key,
            &source->data.items[i],
            NULL,
            0,
            0
        );
        if (!value) {
            return KC_FLOW_ERROR;
        }
        if (kc_flow_store_set(out, source->data.items[i].key, value) != KC_FLOW_OK) {
            free(value);
            return KC_FLOW_ERROR;
        }
        free(value);
    }
    return KC_FLOW_OK;
}

/**
 * Collect all data for one node into a store.
 * @param model Parsed model.
 * @param flow Flow data.
 * @param node Node pointer.
 * @param out Output store.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_collect_node(
    kc_flow_t *ctx,
    const char *flow_path,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *node,
    kc_flow_store_t *out,
    kc_flow_store_t *cache
) {
    kc_flow_store_init(out);
    if (kc_flow_collect_node_from(
        ctx,
        flow_path,
        model,
        flow,
        node,
        node,
        out,
        cache,
        0
    ) != KC_FLOW_OK) {
        kc_flow_store_free(out);
        return KC_FLOW_ERROR;
    }
    return KC_FLOW_OK;
}

/**
 * Initialize one branch list.
 * @param branches Branch list pointer.
 * @return None.
 */
static void kc_flow_branches_init(kc_flow_branches_t *branches) {
    memset(branches, 0, sizeof(*branches));
}

/**
 * Release one branch list.
 * @param branches Branch list pointer.
 * @return None.
 */
static void kc_flow_branches_free(kc_flow_branches_t *branches) {
    size_t i;

    for (i = 0; i < branches->count; ++i) {
        free(branches->items[i].data);
    }
    kc_flow_branches_init(branches);
}

/**
 * Append one owned branch.
 * @param branches Branch list pointer.
 * @param data Owned data pointer.
 * @param size Data size.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_branches_take(kc_flow_branches_t *branches, char *data, size_t size) {
    if (branches->count >= KC_FLOW_MAX_BRANCHES) {
        free(data);
        return KC_FLOW_ERROR;
    }
    branches->items[branches->count].data = data;
    branches->items[branches->count].size = size;
    branches->count++;
    return KC_FLOW_OK;
}

/**
 * Append one copied branch.
 * @param branches Branch list pointer.
 * @param data Source data.
 * @param size Source size.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_branches_copy(kc_flow_branches_t *branches, const void *data, size_t size) {
    char *copy = kc_flow_dup_bytes(data, size);

    if (!copy) {
        return KC_FLOW_ERROR;
    }
    return kc_flow_branches_take(branches, copy, size);
}

#ifndef _WIN32
/**
 * Convert one dotted key into an environment variable name.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @param prefix Environment prefix.
 * @param key Dotted data key.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_env_name(char *out, size_t out_size, const char *prefix, const char *key) {
    size_t i;
    int n = snprintf(out, out_size, "%s", prefix);

    if (n < 0 || (size_t)n >= out_size) {
        return KC_FLOW_ERROR;
    }
    for (i = 0; key[i]; ++i) {
        unsigned char ch = (unsigned char)key[i];
        if ((size_t)n + 1 >= out_size) {
            return KC_FLOW_ERROR;
        }
        out[n++] = (char)(isalnum(ch) ? toupper(ch) : '_');
    }
    out[n] = '\0';
    return KC_FLOW_OK;
}

/**
 * Add one data store to the process environment.
 * @param prefix Environment prefix.
 * @param store Data store.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_set_env_store(const char *prefix, const kc_flow_store_t *store) {
    size_t i;

    for (i = 0; i < store->count; ++i) {
        char name[KC_FLOW_MAX_KEY];

        if (kc_flow_env_name(name, sizeof(name), prefix, store->items[i].key) != KC_FLOW_OK) {
            return KC_FLOW_ERROR;
        }
        if (setenv(name, store->items[i].value, 1) != 0) {
            return KC_FLOW_ERROR;
        }
    }
    return KC_FLOW_OK;
}

/**
 * Build one node-specific environment prefix.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @param ref Node reference.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_node_env_prefix(char *out, size_t out_size, const char *ref) {
    size_t i;
    size_t n = strlen("NODE_");

    if (out_size <= n + 1) {
        return KC_FLOW_ERROR;
    }
    memcpy(out, "NODE_", n);
    for (i = 0; ref[i]; ++i) {
        unsigned char ch = (unsigned char)ref[i];
        if (n + 2 >= out_size) {
            return KC_FLOW_ERROR;
        }
        out[n++] = (char)(isalnum(ch) ? toupper(ch) : '_');
    }
    out[n++] = '_';
    out[n] = '\0';
    return KC_FLOW_OK;
}

/**
 * Export every node data store as NODE_<REF>_<KEY>.
 * @param model Parsed model.
 * @param flow Effective flow data.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_set_env_nodes(kc_flow_model_t *model, const kc_flow_store_t *flow) {
    size_t i;

    (void)flow;
    for (i = 0; i < model->node_count; ++i) {
        char prefix[KC_FLOW_MAX_KEY];
        kc_flow_store_t data;
        size_t j;

        if (kc_flow_node_env_prefix(prefix, sizeof(prefix), model->nodes[i].ref) != KC_FLOW_OK) {
            return KC_FLOW_ERROR;
        }
        kc_flow_store_init(&data);
        for (j = 0; j < model->nodes[i].data.count; ++j) {
            if (model->nodes[i].data.items[j].kind != KC_FLOW_VALUE_LITERAL) {
                continue;
            }
            if (kc_flow_store_set(
                &data,
                model->nodes[i].data.items[j].key,
                model->nodes[i].data.items[j].value
            ) != KC_FLOW_OK) {
                kc_flow_store_free(&data);
                return KC_FLOW_ERROR;
            }
        }
        if (kc_flow_set_env_store(prefix, &data) != KC_FLOW_OK) {
            kc_flow_store_free(&data);
            return KC_FLOW_ERROR;
        }
        kc_flow_store_free(&data);
    }
    return KC_FLOW_OK;
}
#endif
/**
 * Parse a command string into an argv array.
 * @param command Command string.
 * @param out_argc Output argument count.
 * @return Null-terminated array of strings, or NULL.
 */
static char **kc_flow_parse_argv(const char *command, int *out_argc) {
    int argc = 0;
    int cap = 8;
    char **argv = (char **)malloc(cap * sizeof(char *));
    const char *p = command;
    if (!argv) return NULL;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (argc + 1 >= cap) {
            char **next;
            cap *= 2;
            next = (char **)realloc(argv, cap * sizeof(char *));
            if (!next) {
                int i;
                for (i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return NULL;
            }
            argv = next;
        }
        if (*p == '"') {
            const char *start = ++p;
            while (*p && *p != '"') p++;
            argv[argc++] = kc_flow_dup_bytes(start, (size_t)(p - start));
            if (*p == '"') p++;
        } else {
            const char *start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            argv[argc++] = kc_flow_dup_bytes(start, (size_t)(p - start));
        }
    }
    argv[argc] = NULL;
    *out_argc = argc;
    return argv;
}

/**
 * Release an argv array.
 * @param argv Null-terminated array pointer.
 * @return None.
 */
static void kc_flow_free_argv(char **argv) {
    int i;
    if (!argv) return;
    for (i = 0; argv[i]; i++) free(argv[i]);
    free(argv);
}

typedef struct kc_flow_buf {
    unsigned char *data;
    size_t len;
    size_t cap;
} kc_flow_buf_t;

/**
 * Initialize one growable byte buffer.
 * @param buf Buffer pointer.
 * @return None.
 */
static void kc_flow_buf_init(kc_flow_buf_t *buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

/**
 * Append bytes to one growable byte buffer.
 * @param buf Buffer pointer.
 * @param data Byte source.
 * @param size Byte count.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_buf_write(kc_flow_buf_t *buf, const void *data, size_t size) {
    size_t nc;
    unsigned char *grown;
    if (size == 0) return KC_FLOW_OK;
    if (buf->len + size + 1 > buf->cap) {
        nc = buf->cap ? buf->cap * 2 : 4096;
        while (nc < buf->len + size + 1) nc *= 2;
        grown = (unsigned char *)realloc(buf->data, nc);
        if (!grown) return KC_FLOW_ERROR;
        buf->data = grown;
        buf->cap = nc;
    }
    memcpy(buf->data + buf->len, data, size);
    buf->len += size;
    return KC_FLOW_OK;
}

/**
 * Append formatted text to one growable byte buffer.
 * @param buf Buffer pointer.
 * @param format Printf-style format string.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_buf_printf(kc_flow_buf_t *buf, const char *format, ...) {
    va_list args;
    char stack_buf[1024];
    char *heap_buf;
    int n;
    int rc;
    va_start(args, format);
    n = vsnprintf(stack_buf, sizeof(stack_buf), format, args);
    va_end(args);
    if (n < 0) return KC_FLOW_ERROR;
    if ((size_t)n < sizeof(stack_buf)) return kc_flow_buf_write(buf, stack_buf, (size_t)n);
    heap_buf = (char *)malloc((size_t)n + 1);
    if (!heap_buf) return KC_FLOW_ERROR;
    va_start(args, format);
    vsnprintf(heap_buf, (size_t)n + 1, format, args);
    va_end(args);
    rc = kc_flow_buf_write(buf, heap_buf, (size_t)n);
    free(heap_buf);
    return rc;
}

/**
 * Finalize one built-in output buffer and transfer ownership to the caller.
 * @param ctx Context pointer.
 * @param buf Built-in output buffer.
 * @param out_data Output data pointer.
 * @param out_size Output size pointer.
 * @return KC_FLOW_OK, KC_FLOW_ERROR, or the stop result.
 */
static int kc_flow_builtin_finish(
    kc_flow_t *ctx,
    kc_flow_buf_t *buf,
    char **out_data,
    size_t *out_size
) {
    if (!buf->data) {
        buf->data = (unsigned char *)kc_flow_dup("");
        if (!buf->data) return KC_FLOW_ERROR;
    }
    buf->data[buf->len] = '\0';
    if (ctx && ctx->stop_requested) {
        free(buf->data);
        buf->data = NULL;
        return kc_flow_fail_stop(ctx);
    }
    *out_data = (char *)buf->data;
    *out_size = buf->len;
    buf->data = NULL;
    return KC_FLOW_OK;
}

/**
 * Execute a built-in command internally.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param in Input data.
 * @param in_size Input size.
 * @param out Output buffer.
 * @param dir Working directory.
 * @return KC_FLOW_OK on success, KC_FLOW_ERROR, or 1 if not a builtin.
 */
static int kc_flow_run_builtin(
    int argc,
    char **argv,
    const void *in,
    size_t in_size,
    kc_flow_buf_t *out,
    const char *dir
) {
    int i;
    if (argc == 0) return 1;
    if (strcmp(argv[0], "echo") == 0) {
        for (i = 1; i < argc; i++) {
            if (kc_flow_buf_printf(out, "%s%s", argv[i], (i + 1 < argc) ? " " : "") != KC_FLOW_OK) {
                return KC_FLOW_ERROR;
            }
        }
        if (kc_flow_buf_printf(out, "\n") != KC_FLOW_OK) {
            return KC_FLOW_ERROR;
        }
        return KC_FLOW_OK;
    }
    if (strcmp(argv[0], "cat") == 0) {
        if (argc == 1) {
            return kc_flow_buf_write(out, in, in_size);
        }
        for (i = 1; i < argc; i++) {
            char path[KC_FLOW_MAX_PATH * 2];
            FILE *f;
            if (argv[i][0] == '-') return 1;
            if (argv[i][0] == '/' || (argv[i][0] && argv[i][1] == ':')) {
                snprintf(path, sizeof(path), "%s", argv[i]);
            } else {
                snprintf(path, sizeof(path), "%s/%s", dir, argv[i]);
            }
            f = fopen(path, "rb");
            if (!f) return KC_FLOW_ERROR;
            {
                char chunk[4096];
                size_t n;
                while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
                    if (kc_flow_buf_write(out, chunk, n) != KC_FLOW_OK) {
                        fclose(f);
                        return KC_FLOW_ERROR;
                    }
                }
            }
            fclose(f);
        }
        return KC_FLOW_OK;
    }
    if (strcmp(argv[0], "mkdir") == 0) {
        int i;
        for (i = 1; i < argc; i++) {
            char path[KC_FLOW_MAX_PATH * 2];
            if (argv[i][0] == '-') continue;
            if (argv[i][0] == '/' || (argv[i][0] && argv[i][1] == ':')) {
                snprintf(path, sizeof(path), "%s", argv[i]);
            } else {
                snprintf(path, sizeof(path), "%s/%s", dir, argv[i]);
            }
#ifndef _WIN32
            if (mkdir(path, 0777) != 0 && errno != EEXIST) return KC_FLOW_ERROR;
#else
            if (!CreateDirectoryA(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return KC_FLOW_ERROR;
#endif
        }
        return KC_FLOW_OK;
    }
    return 1;
}

/**
 * Execute one resolved shell command.
 * @param command Shell command.
 * @param flow_path Current flow path.
 * @param input Input data.
 * @param input_size Input size.
 * @param flow Flow data.
 * @param node Node data.
 * @param out_data Output data pointer.
 * @param out_size Output size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_run_command(
    kc_flow_t *ctx,
    const char *command,
    const char *flow_path,
    const void *input,
    size_t input_size,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow,
    const kc_flow_node_t *current_node,
    const kc_flow_store_t *node,
    char **out_data,
    size_t *out_size,
    int ignore_error
) {
#ifndef _WIN32
    FILE *in = NULL;
    FILE *out = NULL;
    char dir[KC_FLOW_MAX_PATH];
    pid_t pid;
    int status;
    char chunk[4096];
    size_t total = 0;
    size_t cap = 0;
    char *data = NULL;
    char **argv = NULL;
    int argc = 0;
    int builtin_rc;
    kc_flow_buf_t builtin_out;

    kc_flow_buf_init(&builtin_out);

    if (ctx && ctx->stop_requested) return kc_flow_fail_stop(ctx);

    if (kc_flow_dir(dir, sizeof(dir), flow_path) != KC_FLOW_OK) {
        return KC_FLOW_ERROR;
    }

    if (strpbrk(command, "|&;<>()$`\\\"'*?#~[]") == NULL) {
        argv = kc_flow_parse_argv(command, &argc);
        builtin_rc = kc_flow_run_builtin(argc, argv, input, input_size, &builtin_out, dir);
        if (builtin_rc != 1) {
            kc_flow_free_argv(argv);
            if (builtin_rc != KC_FLOW_OK) {
                free(builtin_out.data);
                return KC_FLOW_ERROR;
            }
            return kc_flow_builtin_finish(ctx, &builtin_out, out_data, out_size);
        }
    }

    if (ctx && ctx->stop_requested) {
        kc_flow_free_argv(argv);
        return kc_flow_fail_stop(ctx);
    }

    in = tmpfile();
    out = tmpfile();
    if (!in || !out) {
        if (in) {
            fclose(in);
        }
        if (out) {
            fclose(out);
        }
        kc_flow_free_argv(argv);
        return KC_FLOW_ERROR;
    }
    if (input_size > 0 && fwrite(input, 1, input_size, in) != input_size) {
        fclose(in);
        fclose(out);
        kc_flow_free_argv(argv);
        return KC_FLOW_ERROR;
    }
    fflush(in);
    rewind(in);

    pid = fork();
    if (pid < 0) {
        kc_flow_free_argv(argv);
        fclose(in);
        fclose(out);
        return KC_FLOW_ERROR;
    }
    if (pid == 0) {
        setenv("KC_FLOW_FILE", flow_path, 1);
        setenv("KC_FLOW_DIR", dir, 1);
        setenv("FLOW_FILE", flow_path, 1);
        setenv("FLOW_DIR", dir, 1);
        kc_flow_set_env_store("FLOW_", flow);
        kc_flow_set_env_nodes(model, flow);
        kc_flow_set_env_store("NODE_", node);
        kc_flow_set_env_store("KC_FLOW_FLOW_", flow);
        kc_flow_set_env_store("KC_FLOW_NODE_", node);
        kc_flow_set_env_store("KC_FLOW_", node);
        (void)current_node;
        dup2(fileno(in), STDIN_FILENO);
        dup2(fileno(out), STDOUT_FILENO);
        if (chdir(dir) != 0) {
            _exit(1);
        }
        if (argv) {
            execvp(argv[0], argv);
        } else {
            execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        }
        _exit(127);
    }
    kc_flow_free_argv(argv);
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || (WEXITSTATUS(status) != 0 && !ignore_error)) {
        fclose(in);
        fclose(out);
        return ctx && ctx->stop_requested ? kc_flow_fail_stop(ctx) : KC_FLOW_ERROR;
    }
    fflush(out);
    rewind(out);
    while (!feof(out)) {
        size_t n = fread(chunk, 1, sizeof(chunk), out);
        if (ctx && ctx->stop_requested) {
            free(data);
            fclose(in);
            fclose(out);
            return kc_flow_fail_stop(ctx);
        }
        if (n > 0) {
            if (total + n + 1 > cap) {
                char *next;
                cap = cap ? cap * 2 : 4096;
                while (cap < total + n + 1) {
                    cap *= 2;
                }
                next = (char *)realloc(data, cap);
                if (!next) {
                    free(data);
                    fclose(in);
                    fclose(out);
                    return KC_FLOW_ERROR;
                }
                data = next;
            }
            memcpy(data + total, chunk, n);
            total += n;
        }
        if (ferror(out)) {
            free(data);
            fclose(in);
            fclose(out);
            return KC_FLOW_ERROR;
        }
    }
    fclose(in);
    fclose(out);
    if (!data) {
        data = kc_flow_dup("");
        if (!data) {
            return KC_FLOW_ERROR;
        }
    }
    if (ctx && ctx->stop_requested) {
        free(data);
        return kc_flow_fail_stop(ctx);
    }
    data[total] = '\0';
    *out_data = data;
    *out_size = total;
    return KC_FLOW_OK;
#else
    FILE *in = NULL;
    FILE *out = NULL;
    char chunk[4096];
    size_t total = 0;
    size_t cap = 0;
    char *data = NULL;
    char **argv = NULL;
    int argc = 0;
    int builtin_rc;
    char dir[KC_FLOW_MAX_PATH];
    kc_flow_buf_t builtin_out;
    (void)model;
    (void)flow;
    (void)current_node;
    (void)node;

    kc_flow_buf_init(&builtin_out);

    if (ctx && ctx->stop_requested) return kc_flow_fail_stop(ctx);

    if (kc_flow_dir(dir, sizeof(dir), flow_path) != KC_FLOW_OK) {
        return KC_FLOW_ERROR;
    }

    if (strpbrk(command, "|&;<>()$`\\\"'*?#~[]") == NULL) {
        argv = kc_flow_parse_argv(command, &argc);
        builtin_rc = kc_flow_run_builtin(argc, argv, input, input_size, &builtin_out, dir);
        if (builtin_rc != 1) {
            kc_flow_free_argv(argv);
            if (builtin_rc != KC_FLOW_OK) {
                free(builtin_out.data);
                return KC_FLOW_ERROR;
            }
            return kc_flow_builtin_finish(ctx, &builtin_out, out_data, out_size);
        }
        kc_flow_free_argv(argv);
        argv = NULL;
    }

    if (ctx && ctx->stop_requested) {
        return kc_flow_fail_stop(ctx);
    }

    in = tmpfile();
    out = tmpfile();
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return KC_FLOW_ERROR;
    }
    if (input_size > 0 && fwrite(input, 1, input_size, in) != input_size) {
        fclose(in);
        fclose(out);
        return KC_FLOW_ERROR;
    }
    fflush(in);
    rewind(in);

    {
        HANDLE hIn = (HANDLE)_get_osfhandle(_fileno(in));
        HANDLE hOut = (HANDLE)_get_osfhandle(_fileno(out));
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        char *cmdline;
        size_t cmd_len;
        DWORD exit_code;

        SetHandleInformation(hIn, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(hOut, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = hIn;
        si.hStdOutput = hOut;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        ZeroMemory(&pi, sizeof(pi));

        cmd_len = strlen(command) + 16;
        cmdline = (char *)malloc(cmd_len);
        if (!cmdline) {
            fclose(in);
            fclose(out);
            return KC_FLOW_ERROR;
        }
        snprintf(cmdline, cmd_len, "cmd.exe /c \"%s\"", command);

        if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, dir, &si, &pi)) {
            free(cmdline);
            fclose(in);
            fclose(out);
            return KC_FLOW_ERROR;
        }
        free(cmdline);

        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exit_code != 0 && !ignore_error) {
            fclose(in);
            fclose(out);
            return ctx && ctx->stop_requested ? kc_flow_fail_stop(ctx) : KC_FLOW_ERROR;
        }
    }
    fflush(out);
    rewind(out);
    while (1) {
        size_t n = fread(chunk, 1, sizeof(chunk), out);
        if (ctx && ctx->stop_requested) {
            free(data);
            fclose(out);
            return kc_flow_fail_stop(ctx);
        }
        if (n > 0) {
            if (total + n + 1 > cap) {
                char *next;
                cap = cap ? cap * 2 : 4096;
                while (cap < total + n + 1) cap *= 2;
                next = (char *)realloc(data, cap);
                if (!next) {
                    free(data);
                    fclose(out);
                    return KC_FLOW_ERROR;
                }
                data = next;
            }
            memcpy(data + total, chunk, n);
            total += n;
        }
        if (n < sizeof(chunk)) {
            if (ferror(out)) {
                free(data);
                fclose(out);
                return KC_FLOW_ERROR;
            }
            break;
        }
    }
    fclose(out);
    if (!data) {
        data = kc_flow_dup("");
        if (!data) return KC_FLOW_ERROR;
    }
    if (ctx && ctx->stop_requested) {
        free(data);
        return kc_flow_fail_stop(ctx);
    }
    data[total] = '\0';
    *out_data = data;
    *out_size = total;
    return KC_FLOW_OK;
#endif
}

static int kc_flow_run_node(
    kc_flow_t *ctx,
    const char *flow_path,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow_data,
    const kc_flow_node_t *node,
    const kc_flow_branch_t *input,
    kc_flow_branches_t *outputs,
    int depth
);

/**
 * Execute one child flow expansion.
 * @param ctx Context pointer.
 * @param path Child path.
 * @param overrides Parent node data.
 * @param input Input branch.
 * @param outputs Output branches.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_run_child(
    kc_flow_t *ctx,
    const char *path,
    const kc_flow_store_t *overrides,
    const kc_flow_branch_t *input,
    kc_flow_branches_t *outputs,
    int depth
) {
    kc_flow_store_t records;
    kc_flow_model_t *model;
    kc_flow_store_t params;
    size_t i;
    int rc = KC_FLOW_OK;

    if (kc_flow_read_records(path, &records) != KC_FLOW_OK) {
        return kc_flow_fail(ctx, "unable to read child flow");
    }
    model = (kc_flow_model_t *)malloc(sizeof(*model));
    if (!model) {
        kc_flow_store_free(&records);
        return kc_flow_fail(ctx, "out of memory");
    }
    if (kc_flow_parse_model(&records, model) != KC_FLOW_OK) {
        kc_flow_store_free(&records);
        free(model);
        return kc_flow_fail(ctx, "unable to parse child flow");
    }
    kc_flow_store_free(&records);
    if (kc_flow_validate_model(model, NULL) != KC_FLOW_OK) {
        kc_flow_model_free(model);
        free(model);
        return kc_flow_fail(ctx, "invalid child flow");
    }
    if (kc_flow_store_copy(&params, &model->data) != KC_FLOW_OK) {
        kc_flow_model_free(model);
        free(model);
        return kc_flow_fail(ctx, "unable to copy child data");
    }
    for (i = 0; i < overrides->count; ++i) {
        if (kc_flow_store_set(&params, overrides->items[i].key, overrides->items[i].value) != KC_FLOW_OK) {
            rc = kc_flow_fail(ctx, "unable to merge child data");
            break;
        }
    }
    for (i = 0; rc == KC_FLOW_OK && i < model->entries.count; ++i) {
        kc_flow_store_t cache;
        char cache_key[KC_FLOW_MAX_KEY];
        char *target;
        kc_flow_node_t *entry;

        kc_flow_store_init(&cache);
        if (snprintf(
            cache_key,
            sizeof(cache_key),
            "flow.link.%lu",
            (unsigned long)i
        ) >= (int)sizeof(cache_key)) {
            kc_flow_store_free(&cache);
            rc = kc_flow_fail(ctx, "unable to resolve child entry");
            break;
        }
        target = kc_flow_resolve_record(
            ctx,
            path,
            model,
            &params,
            NULL,
            NULL,
            &cache,
            cache_key,
            &model->entries.items[i],
            NULL,
            0,
            0
        );
        kc_flow_store_free(&cache);
        if (!target || !*target) {
            free(target);
            rc = kc_flow_fail(ctx, "computed link resolved to empty target");
            break;
        }
        entry = kc_flow_model_find(model, target);
        free(target);
        if (!entry || kc_flow_run_node(ctx, path, model, &params, entry, input, outputs, depth + 1) != KC_FLOW_OK) {
            rc = kc_flow_fail(ctx, "child flow execution failed");
        }
    }
    kc_flow_store_free(&params);
    kc_flow_model_free(model);
    free(model);
    return rc;
}

/**
 * Execute one node.
 * @param ctx Context pointer.
 * @param flow_path Current flow path.
 * @param model Model pointer.
 * @param flow_data Effective flow data.
 * @param node Node pointer.
 * @param input Input branch.
 * @param outputs Output branches.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_run_node(
    kc_flow_t *ctx,
    const char *flow_path,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow_data,
    const kc_flow_node_t *node,
    const kc_flow_branch_t *input,
    kc_flow_branches_t *outputs,
    int depth
) {
    kc_flow_store_t node_data;
    kc_flow_store_t cache;
    kc_flow_branches_t active;
    const kc_flow_node_t *behavior;
    size_t i;
    int rc = KC_FLOW_OK;

    if (depth > 64) {
        return kc_flow_fail(ctx, "maximum flow depth exceeded");
    }

    behavior = kc_flow_node_behavior(model, node, 0);
    if (!behavior) {
        return kc_flow_fail(ctx, "invalid node use");
    }

    kc_flow_store_init(&cache);
    if (kc_flow_collect_node(
        ctx,
        flow_path,
        model,
        flow_data,
        node,
        &node_data,
        &cache
    ) != KC_FLOW_OK) {
        kc_flow_store_free(&cache);
        return kc_flow_fail(ctx, "unable to resolve node data");
    }
    kc_flow_branches_init(&active);
    if (kc_flow_branches_copy(&active, input->data, input->size) != KC_FLOW_OK) {
        kc_flow_store_free(&node_data);
        kc_flow_store_free(&cache);
        return kc_flow_fail(ctx, "unable to create branch");
    }
    if (behavior->import && *behavior->import) {
        kc_flow_branches_t next;
        kc_flow_record_t record;
        char *import;
        char child_path[KC_FLOW_MAX_PATH];

        record.key = (char *)"import";
        record.value = behavior->import;
        record.kind = behavior->import_kind ? behavior->import_kind : KC_FLOW_VALUE_LITERAL;
        import = kc_flow_resolve_record(
            ctx,
            flow_path,
            model,
            flow_data,
            node,
            &node_data,
            &cache,
            "node.import",
            &record,
            NULL,
            0,
            0
        );
        kc_flow_branches_init(&next);
        if (!import || kc_flow_relative_path(child_path, sizeof(child_path), flow_path, import) != KC_FLOW_OK) {
            free(import);
            kc_flow_branches_free(&active);
            kc_flow_store_free(&node_data);
            kc_flow_store_free(&cache);
            return kc_flow_fail(ctx, "invalid child flow path");
        }
        free(import);
        for (i = 0; rc == KC_FLOW_OK && i < active.count; ++i) {
            rc = kc_flow_run_child(ctx, child_path, &node_data, &active.items[i], &next, depth + 1);
        }
        kc_flow_branches_free(&active);
        active = next;
    }
    if (rc == KC_FLOW_OK && behavior->exec && *behavior->exec) {
        kc_flow_branches_t next;
        kc_flow_branches_init(&next);
        for (i = 0; rc == KC_FLOW_OK && i < active.count; ++i) {
            char *command = kc_flow_template_mode(
                ctx,
                flow_path,
                behavior->exec,
                model,
                flow_data,
                node,
                &node_data,
                &cache,
                0,
                KC_FLOW_TEMPLATE_SHELL
            );
            char *out = NULL;
            size_t out_size = 0;
            const char *ignore = kc_flow_store_get(&node_data, "ignore_error");
            int ignore_val = (ignore && strcmp(ignore, "1") == 0);
            if (!command || kc_flow_run_command(ctx, command, flow_path, active.items[i].data, active.items[i].size, model, flow_data, node, &node_data, &out, &out_size, ignore_val) != KC_FLOW_OK) {
                free(command);
                rc = kc_flow_fail(ctx, "node command failed");
                break;
            }
            free(command);
            if (kc_flow_branches_take(&next, out, out_size) != KC_FLOW_OK) {
                rc = kc_flow_fail(ctx, "too many branches");
            }
        }
        kc_flow_branches_free(&active);
        active = next;
    }
    if (rc == KC_FLOW_OK && node->links.count > 0) {
        for (i = 0; rc == KC_FLOW_OK && i < active.count; ++i) {
            size_t j;
            for (j = 0; rc == KC_FLOW_OK && j < node->links.count; ++j) {
                char cache_key[KC_FLOW_MAX_KEY * 2];
                char *target;
                kc_flow_node_t *next;

                if (snprintf(
                    cache_key,
                    sizeof(cache_key),
                    "node.%s.link.%lu",
                    node->ref,
                    (unsigned long)j
                ) >= (int)sizeof(cache_key)) {
                    rc = kc_flow_fail(ctx, "unable to resolve computed link");
                    break;
                }
                target = kc_flow_resolve_record(
                    ctx,
                    flow_path,
                    model,
                    flow_data,
                    node,
                    &node_data,
                    &cache,
                    cache_key,
                    &node->links.items[j],
                    active.items[i].data,
                    active.items[i].size,
                    0
                );
                if (!target) {
                    rc = kc_flow_fail(ctx, "unable to execute computed link");
                    break;
                }
                if (!*target) {
                    free(target);
                    rc = kc_flow_fail(ctx, "computed link resolved to empty target");
                    break;
                }
                next = kc_flow_model_find(model, target);
                free(target);
                if (!next) {
                    rc = kc_flow_fail(ctx, "computed link target not found");
                } else {
                    rc = kc_flow_run_node(ctx, flow_path, model, flow_data, next, &active.items[i], outputs, depth + 1);
                }
            }
        }
    } else if (rc == KC_FLOW_OK) {
        for (i = 0; rc == KC_FLOW_OK && i < active.count; ++i) {
            rc = kc_flow_branches_copy(outputs, active.items[i].data, active.items[i].size);
        }
    }
    kc_flow_branches_free(&active);
    kc_flow_store_free(&node_data);
    kc_flow_store_free(&cache);
    return rc;
}

/**
 * Load one model from a file and context overlays.
 * @param ctx Context pointer.
 * @param path Flow file path.
 * @param entry Optional explicit entry.
 * @param model Output model.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_load(kc_flow_t *ctx, const char *path, const char *entry, kc_flow_model_t *model) {
    kc_flow_store_t records;
    struct stat st;

    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return kc_flow_fail(ctx, "flow file not found");
    }
    if (kc_flow_read_records(path, &records) != KC_FLOW_OK) {
        return kc_flow_fail(ctx, "unable to read flow file");
    }
    if (kc_flow_apply_overlays(ctx, &records) != KC_FLOW_OK) {
        kc_flow_store_free(&records);
        return KC_FLOW_ERROR;
    }
    if (kc_flow_parse_model(&records, model) != KC_FLOW_OK) {
        kc_flow_store_free(&records);
        return kc_flow_fail(ctx, "unable to parse flow file");
    }
    kc_flow_store_free(&records);
    if (kc_flow_validate_model(model, entry) != KC_FLOW_OK) {
        kc_flow_model_free(model);
        return kc_flow_fail(ctx, "invalid flow structure");
    }
    return KC_FLOW_OK;
}

/**
 * Execute one loaded model.
 * @param ctx Context pointer.
 * @param path Flow file path.
 * @param entry Optional explicit entry.
 * @param input Input buffer.
 * @param input_size Input size.
 * @param output Output pointer.
 * @param output_size Output size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_exec_loaded(
    kc_flow_t *ctx,
    const char *path,
    const char *entry,
    const void *input,
    size_t input_size,
    char **output,
    size_t *output_size
) {
    kc_flow_model_t *model;
    kc_flow_store_t params;
    kc_flow_branches_t outputs;
    kc_flow_branch_t start;
    size_t i;
    size_t total = 0;
    char *data;
    char *cursor;
    int rc = KC_FLOW_OK;

    if (!ctx || !path || !output || !output_size || (input_size > 0 && !input)) {
        return kc_flow_fail(ctx, "invalid argument");
    }
    if (ctx->stop_requested) {
        return kc_flow_fail_stop(ctx);
    }
    *output = NULL;
    *output_size = 0;
    model = (kc_flow_model_t *)malloc(sizeof(*model));
    if (!model) {
        return kc_flow_fail(ctx, "out of memory");
    }
    if (kc_flow_load(ctx, path, entry, model) != KC_FLOW_OK) {
        free(model);
        return KC_FLOW_ERROR;
    }
    if (kc_flow_store_copy(&params, &model->data) != KC_FLOW_OK) {
        kc_flow_model_free(model);
        free(model);
        return kc_flow_fail(ctx, "unable to copy flow data");
    }
    kc_flow_branches_init(&outputs);
    start.data = (char *)(input ? input : "");
    start.size = input_size;
    if (entry) {
        kc_flow_node_t *node = kc_flow_model_find(model, entry);
        rc = node ? kc_flow_run_node(ctx, path, model, &params, node, &start, &outputs, 0) : kc_flow_fail(ctx, "unknown entry");
    } else {
        for (i = 0; rc == KC_FLOW_OK && i < model->entries.count; ++i) {
            kc_flow_store_t cache;
            char cache_key[KC_FLOW_MAX_KEY];
            char *target;
            kc_flow_node_t *node;

            kc_flow_store_init(&cache);
            if (snprintf(
                cache_key,
                sizeof(cache_key),
                "flow.link.%lu",
                (unsigned long)i
            ) >= (int)sizeof(cache_key)) {
                kc_flow_store_free(&cache);
                rc = kc_flow_fail(ctx, "unable to resolve flow entry");
                break;
            }
            target = kc_flow_resolve_record(
                ctx,
                path,
                model,
                &params,
                NULL,
                NULL,
                &cache,
                cache_key,
                &model->entries.items[i],
                NULL,
                0,
                0
            );
            kc_flow_store_free(&cache);
            if (!target) {
                rc = kc_flow_fail(ctx, "unable to resolve flow entry");
                break;
            }
            if (!*target) {
                free(target);
                rc = kc_flow_fail(ctx, "computed link resolved to empty target");
                break;
            }
            node = kc_flow_model_find(model, target);
            free(target);
            rc = node ? kc_flow_run_node(ctx, path, model, &params, node, &start, &outputs, 0) : kc_flow_fail(ctx, "computed link target not found");
        }
    }
    for (i = 0; rc == KC_FLOW_OK && i < outputs.count; ++i) {
        total += outputs.items[i].size;
    }
    data = rc == KC_FLOW_OK ? (char *)malloc(total + 1) : NULL;
    if (rc == KC_FLOW_OK && !data) {
        rc = kc_flow_fail(ctx, "out of memory");
    }
    if (rc == KC_FLOW_OK) {
        cursor = data;
        for (i = 0; i < outputs.count; ++i) {
            memcpy(cursor, outputs.items[i].data, outputs.items[i].size);
            cursor += outputs.items[i].size;
        }
        data[total] = '\0';
        *output = data;
        *output_size = total;
    }
    kc_flow_branches_free(&outputs);
    kc_flow_store_free(&params);
    kc_flow_model_free(model);
    free(model);
    return rc;
}

/**
 * Allocate one flow runtime context.
 * @param out Pointer to receive context pointer.
 * @param opts Configuration options.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
int kc_flow_open(kc_flow_t **out, const kc_flow_options_t *opts) {
    kc_flow_t *ctx;

    if (!out || !opts) {
        return KC_FLOW_ERROR;
    }

    ctx = (kc_flow_t *)calloc(1, sizeof(kc_flow_t));
    if (!ctx) {
        return KC_FLOW_ERROR;
    }

    *out = ctx;

    return KC_FLOW_OK;
}

/**
 * Release one flow runtime context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_flow_close(kc_flow_t *ctx) {
    size_t i;

    if (!ctx) {
        return;
    }

    for (i = 0; i < ctx->overlay_count; ++i) {
        free(ctx->overlays[i].key);
        free(ctx->overlays[i].value);
    }
    free(ctx);
}

/**
 * Request stop for a specific flow context.
 * @param ctx Context pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_stop(kc_flow_t *ctx) {
    if (!ctx) return KC_FLOW_ERROR;
    ctx->stop_requested = 1;
    snprintf(ctx->error, sizeof(ctx->error), "%s", "stop requested");
    return KC_FLOW_OK;
}

/**
 * Report whether a stop request is pending on one context.
 * @param ctx Context pointer.
 * @return 1 when stop was requested, otherwise 0.
 */
int kc_flow_stop_requested(const kc_flow_t *ctx) {
    return ctx && ctx->stop_requested ? 1 : 0;
}

/**
 * Append one ordered key-value overlay operation.
 * @param ctx Context pointer.
 * @param key Flow document key.
 * @param value Overlay value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_set(kc_flow_t *ctx, const char *key, const char *value) {
    kc_flow_overlay_t *overlay;

    if (!ctx || !key || !value || ctx->overlay_count >= KC_FLOW_MAX_RECORDS || !kc_flow_key_valid(key)) {
        return kc_flow_fail(ctx, "invalid set overlay");
    }
    overlay = &ctx->overlays[ctx->overlay_count];
    overlay->kind = KC_FLOW_OVERLAY_SET;
    overlay->key = kc_flow_dup(key);
    overlay->value = kc_flow_dup(value);
    if (!overlay->key || !overlay->value) {
        free(overlay->key);
        free(overlay->value);
        return kc_flow_fail(ctx, "out of memory");
    }
    ctx->overlay_count++;
    return KC_FLOW_OK;
}

/**
 * Append one ordered key removal overlay operation.
 * @param ctx Context pointer.
 * @param key Flow document key.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_unset(kc_flow_t *ctx, const char *key) {
    kc_flow_overlay_t *overlay;

    if (!ctx || !key || ctx->overlay_count >= KC_FLOW_MAX_RECORDS || !kc_flow_key_valid(key)) {
        return kc_flow_fail(ctx, "invalid unset overlay");
    }
    overlay = &ctx->overlays[ctx->overlay_count];
    overlay->kind = KC_FLOW_OVERLAY_UNSET;
    overlay->key = kc_flow_dup(key);
    if (!overlay->key) {
        return kc_flow_fail(ctx, "out of memory");
    }
    ctx->overlay_count++;
    return KC_FLOW_OK;
}

/**
 * Execute one flow file from its declared entries.
 * @param ctx Context pointer.
 * @param path Flow file path.
 * @param input Optional input buffer.
 * @param input_size Input buffer size.
 * @param output Owned output buffer pointer.
 * @param output_size Output buffer size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_exec(
    kc_flow_t *ctx,
    const char *path,
    const void *input,
    size_t input_size,
    char **output,
    size_t *output_size
) {
    return kc_flow_exec_loaded(ctx, path, NULL, input, input_size, output, output_size);
}

/**
 * Execute one flow file from one explicit entry node.
 * @param ctx Context pointer.
 * @param path Flow file path.
 * @param entry Entry node reference.
 * @param input Optional input buffer.
 * @param input_size Input buffer size.
 * @param output Owned output buffer pointer.
 * @param output_size Output buffer size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_exec_entry(
    kc_flow_t *ctx,
    const char *path,
    const char *entry,
    const void *input,
    size_t input_size,
    char **output,
    size_t *output_size
) {
    if (!entry || !*entry) {
        return kc_flow_fail(ctx, "invalid entry");
    }
    return kc_flow_exec_loaded(ctx, path, entry, input, input_size, output, output_size);
}

/**
 * Release one output buffer produced by the runtime.
 * @param output Owned output buffer.
 * @return None.
 */
void kc_flow_free(void *output) {
    free(output);
}

/**
 * Returns the last error message from a flow context.
 * @param ctx Context pointer.
 * @return Static error string.
 */
const char *kc_flow_strerror(kc_flow_t *ctx) {
    if (!ctx || !ctx->error[0]) {
        return "unknown error";
    }
    return ctx->error;
}

/**
 * Create an options struct initialized with default values.
 * @param none Unused.
 * @return Default-initialized options.
 */
kc_flow_options_t kc_flow_options_default(void) {
    kc_flow_options_t opts;
    memset(&opts, 0, sizeof(opts));
    return opts;
}

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_flow_options_load_env(kc_flow_options_t *opts) {
    (void)opts;
}

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_flow_options_free(kc_flow_options_t *opts) {
    (void)opts;
}

#ifndef KC_FLOW_BUILD_VERSION
#define KC_FLOW_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_flow_version(void) {
    return (uint64_t)KC_FLOW_BUILD_VERSION;
}
