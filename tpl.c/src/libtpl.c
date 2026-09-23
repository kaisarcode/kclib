/**
 * libtpl.c - Template renderer.
 * Summary: Core implementation for scoped template rendering.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <unistd.h>
#endif
#include "libtpl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define KC_TPL_EVAL_CAP 2048
#define KC_TPL_ROOT_CAP 4096
#define KC_TPL_OUTPUT_CAP 262144

typedef struct {
    char key[64];
    char *val;
} kc_tpl_scope_var_t;

typedef struct {
    char name[64];
    char body[8192];
} kc_tpl_block_t;

typedef struct kc_tpl_scope {
    kc_tpl_scope_var_t *vars;
    int var_n;
    int var_cap;
    kc_tpl_block_t blocks[64];
    int block_n;
    struct kc_tpl_scope *parent;
    unsigned char block_scope;
} kc_tpl_scope_t;

struct kc_tpl {
    char *source;
    char root[KC_TPL_ROOT_CAP];
    char out[KC_TPL_OUTPUT_CAP];
    size_t out_n;
    char error[256];
};

static const char *kc_tpl_comment_open = "{{/" "*";
static const char *kc_tpl_comment_close = "*" "/}}";

#ifndef KC_TPL_BUILD_VERSION
#define KC_TPL_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_tpl_version(void) {
    return (uint64_t)KC_TPL_BUILD_VERSION;
}

/**
 * Stores the latest context error.
 * @param ctx Context pointer.
 * @param message Error summary.
 * @param detail Optional error detail.
 * @return KC_TPL_ERROR.
 */
static int kc_tpl_fail(kc_tpl_t *ctx, const char *message, const char *detail) {
    if (ctx != NULL) {
        if (detail != NULL && detail[0] != '\0') {
            snprintf(ctx->error, sizeof(ctx->error), "%s: %s", message, detail);
        } else {
            snprintf(ctx->error, sizeof(ctx->error), "%s", message);
        }
    }

    return KC_TPL_ERROR;
}

/**
 * Trims leading and trailing whitespace in place.
 * @param text Mutable string pointer.
 * @return Pointer to trimmed content.
 */
static char *kc_tpl_trim(char *text) {
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }

    return text;
}

/**
 * Copies a bounded substring into a destination buffer.
 * @param dst Destination buffer.
 * @param cap Destination capacity.
 * @param src Source text.
 * @param len Number of bytes to copy.
 * @return None.
 */
static void kc_tpl_copy(char *dst, size_t cap, const char *src, size_t len) {
    if (cap == 0U) {
        return;
    }

    if (len >= cap) {
        len = cap - 1U;
    }

    memcpy(dst, src, len);
    dst[len] = '\0';
}

/**
 * Appends text to the context output buffer.
 * @param ctx Context pointer.
 * @param text Source text.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_put(kc_tpl_t *ctx, const char *text) {
    size_t len;

    if (ctx == NULL || text == NULL) {
        return kc_tpl_fail(ctx, "invalid argument", NULL);
    }

    len = strlen(text);
    if (ctx->out_n + len >= sizeof(ctx->out)) {
        return kc_tpl_fail(ctx, "output too large", NULL);
    }

    memcpy(ctx->out + ctx->out_n, text, len);
    ctx->out_n += len;
    ctx->out[ctx->out_n] = '\0';
    return KC_TPL_OK;
}

/**
 * Appends HTML-escaped text to the output buffer.
 * @param ctx Context pointer.
 * @param text Source text.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_put_esc(kc_tpl_t *ctx, const char *text) {
    const char *cursor;

    if (text == NULL) {
        return kc_tpl_fail(ctx, "invalid argument", NULL);
    }

    for (cursor = text; *cursor != '\0'; cursor++) {
        char one[2];

        if (*cursor == '&' && kc_tpl_put(ctx, "&amp;") != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }

        if (*cursor == '<' && kc_tpl_put(ctx, "&lt;") != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }

        if (*cursor == '>' && kc_tpl_put(ctx, "&gt;") != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }

        if (*cursor == '"' && kc_tpl_put(ctx, "&quot;") != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }

        if (*cursor != '&' && *cursor != '<' && *cursor != '>' && *cursor != '"') {
            one[0] = *cursor;
            one[1] = '\0';
            if (kc_tpl_put(ctx, one) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }
        }
    }

    return KC_TPL_OK;
}

/**
 * Duplicates a null-terminated string.
 * @param text Source text.
 * @return Owned copy, or NULL on allocation failure.
 */
static char *kc_tpl_dup(const char *text) {
    size_t len;
    char *out;

    if (text == NULL) {
        return NULL;
    }

    len = strlen(text);
    out = (char *)malloc(len + 1U);
    if (out == NULL) {
        return NULL;
    }

    memcpy(out, text, len + 1U);
    return out;
}

/**
 * Resolves a variable pointer from the scope chain.
 * @param scope Scope pointer.
 * @param key Variable name.
 * @return Value pointer or NULL when not found.
 */
static const char *kc_tpl_var_find(kc_tpl_scope_t *scope, const char *key) {
    int i;

    for (; scope != NULL; scope = scope->parent) {
        for (i = 0; i < scope->var_n; i++) {
            if (strcmp(scope->vars[i].key, key) == 0) {
                return scope->vars[i].val;
            }
        }
        if (scope->block_scope != 0) {
            break;
        }
    }

    return NULL;
}

/**
 * Resolves a variable from the scope chain.
 * @param scope Scope pointer.
 * @param key Variable name.
 * @return Value or empty string.
 */
static const char *kc_tpl_var_get(kc_tpl_scope_t *scope, const char *key) {
    const char *value;

    value = kc_tpl_var_find(scope, key);
    return value != NULL ? value : "";
}

/**
 * Stores or updates a variable in the current scope.
 * @param scope Scope pointer.
 * @param key Variable name.
 * @param value Variable value.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_var_set(kc_tpl_scope_t *scope, const char *key, const char *value) {
    int i;
    char *copy;

    if (
        scope == NULL ||
        key == NULL ||
        value == NULL ||
        key[0] == '\0' ||
        strlen(key) >= sizeof(scope->vars[0].key)
    ) {
        return KC_TPL_ERROR;
    }

    for (i = 0; i < scope->var_n; i++) {
        if (strcmp(scope->vars[i].key, key) == 0) {
            break;
        }
    }

    if (i == scope->var_n) {
        if (scope->var_n >= scope->var_cap) {
            int new_cap = scope->var_cap == 0 ? 16 : scope->var_cap * 2;
            kc_tpl_scope_var_t *new_vars = realloc(
                scope->vars, (size_t)new_cap * sizeof(kc_tpl_scope_var_t)
            );
            if (new_vars == NULL) {
                return KC_TPL_ERROR;
            }
            memset(
                new_vars + scope->var_n, 0,
                (size_t)(new_cap - scope->var_n) * sizeof(kc_tpl_scope_var_t)
            );
            scope->vars = new_vars;
            scope->var_cap = new_cap;
        }

        scope->var_n++;
    }

    copy = kc_tpl_dup(value);
    if (copy == NULL) {
        return KC_TPL_ERROR;
    }

    snprintf(scope->vars[i].key, sizeof(scope->vars[i].key), "%s", key);
    free(scope->vars[i].val);
    scope->vars[i].val = copy;
    return KC_TPL_OK;
}

/**
 * Releases dynamic variable allocations in one scope level.
 * @param scope Scope pointer.
 * @return None.
 */
static void kc_tpl_scope_clear(kc_tpl_scope_t *scope) {
    int i;

    if (scope == NULL) {
        return;
    }

    for (i = 0; i < scope->var_n; i++) {
        free(scope->vars[i].val);
        scope->vars[i].val = NULL;
    }

    free(scope->vars);
    scope->vars = NULL;
    scope->var_n = 0;
    scope->var_cap = 0;
}

/**
 * Resolves a block body from the scope chain.
 * @param scope Scope pointer.
 * @param name Block name.
 * @return Block body or NULL when not found.
 */
static const char *kc_tpl_block_get(kc_tpl_scope_t *scope, const char *name) {
    int i;

    for (; scope != NULL; scope = scope->parent) {
        for (i = 0; i < scope->block_n; i++) {
            if (strcmp(scope->blocks[i].name, name) == 0) {
                return scope->blocks[i].body;
            }
        }
    }

    return NULL;
}

/**
 * Stores or updates a block in the current scope.
 * @param scope Scope pointer.
 * @param name Block name.
 * @param body Block body.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_block_set(kc_tpl_scope_t *scope, const char *name, const char *body) {
    int i;

    if (scope == NULL || name == NULL || body == NULL || name[0] == '\0') {
        return KC_TPL_ERROR;
    }

    for (i = 0; i < scope->block_n; i++) {
        if (strcmp(scope->blocks[i].name, name) == 0) {
            break;
        }
    }

    if (i == scope->block_n) {
        if (scope->block_n >= (int)(sizeof(scope->blocks) / sizeof(scope->blocks[0]))) {
            return KC_TPL_ERROR;
        }

        scope->block_n++;
    }

    snprintf(scope->blocks[i].name, sizeof(scope->blocks[i].name), "%s", name);
    snprintf(scope->blocks[i].body, sizeof(scope->blocks[i].body), "%s", body);
    return KC_TPL_OK;
}

/**
 * Resolves dot-notation expressions into flat underscore-joined keys.
 * @param scope Scope pointer.
 * @param expr Expression text.
 * @return Owned resolved value or NULL when unresolved.
 */
static char *kc_tpl_eval_dot(kc_tpl_scope_t *scope, const char *expr) {
    char path[512];
    char left[128];
    char right[384];
    char tail[384];
    char key[4096];
    char *dot;
    char *tok;
    char *save;
    const char *found;
    const char *alias;

    if (strchr(expr, '.') == NULL) {
        return NULL;
    }

    snprintf(path, sizeof(path), "%s", expr);
    dot = strchr(path, '.');
    if (dot == NULL || dot == path || dot[1] == '\0') {
        return NULL;
    }

    *dot = '\0';
    snprintf(left, sizeof(left), "%s", kc_tpl_trim(path));
    snprintf(right, sizeof(right), "%s", kc_tpl_trim(dot + 1));
    if (left[0] == '\0' || right[0] == '\0') {
        return NULL;
    }

    key[0] = '\0';
    strncat(key, left, sizeof(key) - strlen(key) - 1U);
    snprintf(tail, sizeof(tail), "%s", right);
    save = tail;
    while ((tok = strtok(save, ".")) != NULL) {
        save = NULL;
        strncat(key, "_", sizeof(key) - strlen(key) - 1U);
        strncat(key, kc_tpl_trim(tok), sizeof(key) - strlen(key) - 1U);
    }

    found = kc_tpl_var_find(scope, key);
    if (found != NULL) {
        return kc_tpl_dup(found);
    }

    alias = kc_tpl_var_find(scope, left);
    if (alias == NULL || alias[0] == '\0') {
        return NULL;
    }

    key[0] = '\0';
    strncat(key, alias, sizeof(key) - strlen(key) - 1U);
    snprintf(tail, sizeof(tail), "%s", right);
    save = tail;
    while ((tok = strtok(save, ".")) != NULL) {
        save = NULL;
        strncat(key, "_", sizeof(key) - strlen(key) - 1U);
        strncat(key, kc_tpl_trim(tok), sizeof(key) - strlen(key) - 1U);
    }

    found = kc_tpl_var_find(scope, key);
    return found != NULL ? kc_tpl_dup(found) : NULL;
}

/**
 * Evaluates a template expression to an owned string.
 * @param scope Scope pointer.
 * @param expr Expression text.
 * @return Owned evaluated value, or NULL on allocation failure.
 */
static char *kc_tpl_eval_dup(kc_tpl_scope_t *scope, char *expr) {
    char *text;
    char *out;
    size_t len;

    text = kc_tpl_trim(expr);
    len = strlen(text);

    if (len >= 2U && text[0] == '"' && text[len - 1U] == '"') {
        out = (char *)malloc(len - 1U);
        if (out == NULL) {
            return NULL;
        }

        memcpy(out, text + 1, len - 2U);
        out[len - 2U] = '\0';
        return out;
    }

    if (
        strcmp(text, "true") == 0 ||
        strcmp(text, "false") == 0 ||
        strcmp(text, "null") == 0
    ) {
        return kc_tpl_dup(text);
    }

    out = kc_tpl_eval_dot(scope, text);
    if (out != NULL) {
        return out;
    }

    return kc_tpl_dup(kc_tpl_var_get(scope, text));
}

/**
 * Evaluates an expression into a bounded output buffer.
 * @param scope Scope pointer.
 * @param expr Expression text.
 * @param out Destination buffer.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_eval(kc_tpl_scope_t *scope, char *expr, char *out) {
    char *value;

    value = kc_tpl_eval_dup(scope, expr);
    if (value == NULL) {
        return KC_TPL_ERROR;
    }

    snprintf(out, KC_TPL_EVAL_CAP, "%s", value);
    free(value);
    return KC_TPL_OK;
}

/**
 * Checks the truthiness of an evaluated string value.
 * @param value Evaluated value.
 * @return 1 when truthy, or 0 otherwise.
 */
static int kc_tpl_value_truthy(const char *value) {
    return (
        value[0] != '\0' &&
        strcmp(value, "0") != 0 &&
        strcmp(value, "false") != 0 &&
        strcmp(value, "null") != 0
    );
}

/**
 * Evaluates one comparison term of a condition.
 * @param scope Scope pointer.
 * @param expr Term text.
 * @return 1 when the term holds, or 0 otherwise.
 */
static int kc_tpl_cmp(kc_tpl_scope_t *scope, char *expr) {
    char *op;
    char *lv;
    char *rv;
    int result;

    expr = kc_tpl_trim(expr);
    if (expr[0] == '!') {
        return kc_tpl_cmp(scope, expr + 1) ? 0 : 1;
    }

    op = strstr(expr, "==");
    if (op == NULL) {
        op = strstr(expr, "!=");
    }
    if (op != NULL) {
        char not_equal;
        char *left;
        char *right;
        int equal;

        not_equal = op[0] == '!';
        *op = '\0';
        left = kc_tpl_trim(expr);
        right = kc_tpl_trim(op + 2);
        lv = kc_tpl_eval_dup(scope, left);
        rv = kc_tpl_eval_dup(scope, right);
        if (lv == NULL || rv == NULL) {
            free(lv);
            free(rv);
            return 0;
        }

        equal = strcmp(lv, rv) == 0;
        free(lv);
        free(rv);
        return not_equal ? (equal ? 0 : 1) : equal;
    }

    lv = kc_tpl_eval_dup(scope, expr);
    if (lv == NULL) {
        return 0;
    }

    result = kc_tpl_value_truthy(lv);
    free(lv);
    return result;
}

/**
 * Evaluates truthiness of a condition expression.
 * Supports negation (!), equality (==), inequality (!=),
 * conjunction (&&), and disjunction (||) terms.
 * @param scope Scope pointer.
 * @param expr Expression text.
 * @return 1 when truthy, or 0 otherwise.
 */
static int kc_tpl_truth(kc_tpl_scope_t *scope, char *expr) {
    char buf[KC_TPL_EVAL_CAP];
    char *or_start;

    snprintf(buf, sizeof(buf), "%s", expr);

    or_start = buf;
    while (or_start != NULL) {
        char *or_end;
        char *cursor;
        int and_result;

        or_end = strstr(or_start, "||");
        if (or_end != NULL) {
            *or_end = '\0';
        }

        and_result = 1;
        cursor = or_start;
        while (cursor != NULL) {
            char *and_end;

            and_end = strstr(cursor, "&&");
            if (and_end != NULL) {
                *and_end = '\0';
            }

            if (!kc_tpl_cmp(scope, cursor)) {
                and_result = 0;
                break;
            }

            cursor = and_end != NULL ? and_end + 2 : NULL;
        }

        if (and_result) {
            return 1;
        }

        or_start = or_end != NULL ? or_end + 2 : NULL;
    }

    return 0;
}

/**
 * Splits a list expression into comma-separated items.
 * @param text List text.
 * @param out Output item matrix.
 * @return Number of items parsed.
 */
static int kc_tpl_list(char *text, char out[64][128]) {
    int count;
    char buf[2048];
    char *item;

    count = 0;
    snprintf(buf, sizeof(buf), "%s", kc_tpl_trim(text));
    if (buf[0] == '[' && buf[strlen(buf) - 1U] == ']') {
        memmove(buf, buf + 1, strlen(buf));
        buf[strlen(buf) - 1U] = '\0';
    }

    item = strtok(buf, ",");
    while (item != NULL && count < 64) {
        char *trimmed;
        size_t len;

        trimmed = kc_tpl_trim(item);
        len = strlen(trimmed);
        if (len >= 2U && trimmed[0] == '"' && trimmed[len - 1U] == '"') {
            trimmed[len - 1U] = '\0';
            trimmed++;
        }

        snprintf(out[count], 128, "%s", trimmed);
        count++;
        item = strtok(NULL, ",");
    }

    return count;
}

/**
 * Loads a file relative to the configured include root.
 * @param ctx Context pointer.
 * @param path Relative include path.
 * @param out Destination buffer.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_load(kc_tpl_t *ctx, const char *path, char *out) {
    char full[8192];
    FILE *fp;
    size_t len;

    if (ctx == NULL || path == NULL || out == NULL) {
        return kc_tpl_fail(ctx, "invalid argument", NULL);
    }

    snprintf(full, sizeof(full), "%s/%s", ctx->root, path);
    fp = fopen(full, "rb");
    if (fp == NULL) {
        return kc_tpl_fail(ctx, "include not found", path);
    }

    len = fread(out, 1, 8191U, fp);
    if (ferror(fp)) {
        fclose(fp);
        return kc_tpl_fail(ctx, "include read failed", path);
    }

    fclose(fp);
    out[len] = '\0';
    return KC_TPL_OK;
}

/**
 * Advances past HTML comments, returning the next template tag.
 * @param tpl Template cursor.
 * @return Pointer to next tag or NULL.
 */
static char *kc_tpl_next_tag(char *tpl) {
    char *cursor;

    cursor = tpl;
    while (*cursor != '\0') {
        char *tag;
        char *comment;

        tag = strstr(cursor, "{{");
        comment = strstr(cursor, "<!--");
        if (comment == NULL) {
            return tag;
        }

        if (tag == NULL || comment < tag) {
            char *end;

            end = strstr(comment + 4, "-->");
            if (end == NULL) {
                return tag;
            }

            cursor = end + 3;
            continue;
        }

        return tag;
    }

    return NULL;
}

/**
 * Finds the matching close directive.
 * @param tpl Template text after the opening tag.
 * @param open Opening directive keyword.
 * @param close Closing directive keyword.
 * @param alt Optional alternate keyword.
 * @param mid Output pointer set to the alternate position.
 * @return Pointer to the closing tag or NULL.
 */
static char *kc_tpl_find(
    char *tpl,
    const char *open,
    const char *close,
    const char *alt,
    char **mid
) {
    int depth;
    char *cursor;

    depth = 1;
    cursor = tpl;
    *mid = NULL;

    while ((cursor = kc_tpl_next_tag(cursor)) != NULL) {
        if (strncmp(cursor, kc_tpl_comment_open, 4) == 0) {
            char *end;

            end = strstr(cursor + 4, kc_tpl_comment_close);
            if (end == NULL) {
                return NULL;
            }

            cursor = end + 4;
            continue;
        }

        if (strncmp(cursor, "{{@", 3) != 0) {
            cursor += 2;
            continue;
        }

        if (strncmp(cursor + 3, open, strlen(open)) == 0) {
            depth++;
        } else if (
            alt != NULL &&
            depth == 1 &&
            strncmp(cursor + 3, alt, strlen(alt)) == 0 &&
            *mid == NULL
        ) {
            *mid = cursor;
        } else if (strncmp(cursor + 3, close, strlen(close)) == 0) {
            depth--;
            if (depth == 0) {
                return cursor;
            }
        }

        cursor += 3;
    }

    return NULL;
}

static int kc_tpl_render_internal(kc_tpl_t *ctx, kc_tpl_scope_t *scope, char *tpl);

/**
 * Parses inline block properties into a child scope.
 * @param scope Parent scope.
 * @param child Child scope.
 * @param tag Directive tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_block_props(kc_tpl_scope_t *scope, kc_tpl_scope_t *child, char *tag) {
    char props[512];
    char *left;
    char *right;

    left = strchr(tag, '[');
    right = strrchr(tag, ']');
    if (left == NULL || right == NULL || right <= left) {
        return KC_TPL_OK;
    }

    snprintf(props, (size_t)(right - left), "%s", left + 1);
    for (char *item = strtok(props, ","); item != NULL; item = strtok(NULL, ",")) {
        char key[64];
        char expr[512];
        char val[KC_TPL_EVAL_CAP];
        char *name;
        char *sep;

        sep = strchr(item, ':');
        if (sep == NULL) {
            continue;
        }

        *sep = '\0';
        name = kc_tpl_trim(item);
        if (name[0] == '"') {
            size_t len;

            len = strlen(name);
            if (len >= 2U && name[len - 1U] == '"') {
                name[len - 1U] = '\0';
                name++;
            }
        }

        snprintf(key, sizeof(key), "%s", name);
        snprintf(expr, sizeof(expr), "%s", sep + 1);
        if (kc_tpl_eval(scope, expr, val) != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }

        if (kc_tpl_var_set(child, key, val) != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }
    }

    return KC_TPL_OK;
}

/**
 * Renders one include directive.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tag Directive tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_include(kc_tpl_t *ctx, kc_tpl_scope_t *scope, char *tag) {
    char file[KC_TPL_ROOT_CAP];
    char data[8192];
    char expr[512];

    snprintf(expr, sizeof(expr), "%s", kc_tpl_trim(tag + 9));
    if (kc_tpl_eval(scope, expr, file) != KC_TPL_OK) {
        return kc_tpl_fail(ctx, "expression evaluation failed", NULL);
    }

    if (kc_tpl_load(ctx, file, data) != KC_TPL_OK) {
        return KC_TPL_ERROR;
    }

    return kc_tpl_render_internal(ctx, scope, data);
}

/**
 * Renders one variable assignment directive.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tag Directive tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_var(kc_tpl_t *ctx, kc_tpl_scope_t *scope, char *tag) {
    char name[64];
    char expr[512];
    char val[KC_TPL_EVAL_CAP];
    char *start;

    if (sscanf(tag + 5, "%63s", name) != 1) {
        return kc_tpl_fail(ctx, "invalid var directive", NULL);
    }

    start = strstr(tag + 5, name);
    if (start == NULL) {
        return kc_tpl_fail(ctx, "invalid var directive", NULL);
    }

    snprintf(expr, sizeof(expr), "%s", start + strlen(name));
    if (kc_tpl_eval(scope, expr, val) != KC_TPL_OK) {
        return kc_tpl_fail(ctx, "expression evaluation failed", NULL);
    }

    if (kc_tpl_var_set(scope, name, val) != KC_TPL_OK) {
        return kc_tpl_fail(ctx, "variable assignment failed", name);
    }

    return KC_TPL_OK;
}

/**
 * Renders one block directive.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tag Directive tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_block(kc_tpl_t *ctx, kc_tpl_scope_t *scope, char *tag) {
    char name[64];
    kc_tpl_scope_t child;
    const char *body;
    int rc;

    memset(&child, 0, sizeof(child));
    child.parent = scope;
    child.block_scope = 1;

    if (sscanf(tag + 7, "%63s", name) != 1) {
        return kc_tpl_fail(ctx, "invalid block directive", NULL);
    }

    body = kc_tpl_block_get(scope, name);
    if (body == NULL) {
        return KC_TPL_OK;
    }

    if (kc_tpl_block_props(scope, &child, tag) != KC_TPL_OK) {
        kc_tpl_scope_clear(&child);
        return kc_tpl_fail(ctx, "invalid block properties", name);
    }

    rc = kc_tpl_render_internal(ctx, &child, (char *)body);
    kc_tpl_scope_clear(&child);
    return rc;
}

/**
 * Renders one conditional directive.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tag Directive tag.
 * @param body_start Pointer after opening tag.
 * @param out_end Output pointer set after closing tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_if(
    kc_tpl_t *ctx,
    kc_tpl_scope_t *scope,
    char *tag,
    char *body_start,
    char **out_end
) {
    char body[8192];
    char alt_body[8192];
    char *mid;
    char *end;

    end = kc_tpl_find(body_start, "if ", "endif", "else", &mid);
    if (end == NULL) {
        return kc_tpl_fail(ctx, "missing endif", NULL);
    }

    if (mid != NULL) {
        kc_tpl_copy(body, sizeof(body), body_start, (size_t)(mid - body_start));
        kc_tpl_copy(alt_body, sizeof(alt_body), mid + 9, (size_t)(end - (mid + 9)));
    } else {
        kc_tpl_copy(body, sizeof(body), body_start, (size_t)(end - body_start));
        alt_body[0] = '\0';
    }

    *out_end = end + 10;
    return kc_tpl_render_internal(ctx, scope, kc_tpl_truth(scope, tag + 4) ? body : alt_body);
}

/**
 * Renders one foreach directive.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tag Directive tag.
 * @param body_start Pointer after opening tag.
 * @param out_end Output pointer set after closing tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_foreach(
    kc_tpl_t *ctx,
    kc_tpl_scope_t *scope,
    char *tag,
    char *body_start,
    char **out_end
) {
    char item[64];
    char list_expr[256];
    char body[8192];
    char expr[512];
    char val[KC_TPL_EVAL_CAP];
    char items[64][128];
    char *in;
    char *mid;
    char *end;
    int count;
    int i;

    snprintf(expr, sizeof(expr), "%s", tag + 9);
    in = strstr(expr, " in ");
    if (in == NULL) {
        return kc_tpl_fail(ctx, "invalid foreach", NULL);
    }

    *in = '\0';
    snprintf(item, sizeof(item), "%s", kc_tpl_trim(expr));
    snprintf(list_expr, sizeof(list_expr), "%s", kc_tpl_trim(in + 4));
    if (kc_tpl_eval(scope, list_expr, val) != KC_TPL_OK) {
        return kc_tpl_fail(ctx, "expression evaluation failed", NULL);
    }

    count = kc_tpl_list(val, items);
    end = kc_tpl_find(body_start, "foreach ", "endforeach", NULL, &mid);
    if (end == NULL) {
        return kc_tpl_fail(ctx, "missing endforeach", NULL);
    }

    kc_tpl_copy(body, sizeof(body), body_start, (size_t)(end - body_start));
    for (i = 0; i < count; i++) {
        kc_tpl_scope_t child;
        int rc;

        memset(&child, 0, sizeof(child));
        child.parent = scope;
        if (kc_tpl_var_set(&child, item, items[i]) != KC_TPL_OK) {
            kc_tpl_scope_clear(&child);
            return kc_tpl_fail(ctx, "foreach assignment failed", item);
        }

        rc = kc_tpl_render_internal(ctx, &child, body);
        kc_tpl_scope_clear(&child);
        if (rc != KC_TPL_OK) {
            return rc;
        }
    }

    *out_end = end + 15;
    return KC_TPL_OK;
}

/**
 * Renders a template string with scope-aware directive evaluation.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tpl Template text.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_internal(kc_tpl_t *ctx, kc_tpl_scope_t *scope, char *tpl) {
    char *cursor;

    cursor = tpl;
    while (*cursor != '\0') {
        char *tag_start;
        char *tag_end;
        char tag[512];

        tag_start = kc_tpl_next_tag(cursor);
        if (tag_start == NULL) {
            return kc_tpl_put(ctx, cursor);
        }

        if (tag_start > cursor) {
            char keep;

            keep = *tag_start;
            *tag_start = '\0';
            if (kc_tpl_put(ctx, cursor) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }

            *tag_start = keep;
        }

        if (strncmp(tag_start, kc_tpl_comment_open, 4) == 0) {
            tag_end = strstr(tag_start + 4, kc_tpl_comment_close);
            if (tag_end == NULL) {
                return kc_tpl_fail(ctx, "unterminated comment", NULL);
            }

            cursor = tag_end + 4;
            continue;
        }

        if (strncmp(tag_start, "{{{", 3) == 0) {
            char expr[512];
            char *value;

            tag_end = strstr(tag_start + 3, "}}}");
            if (tag_end == NULL) {
                return kc_tpl_fail(ctx, "unterminated raw tag", NULL);
            }

            kc_tpl_copy(expr, sizeof(expr), tag_start + 3, (size_t)(tag_end - (tag_start + 3)));
            value = kc_tpl_eval_dup(scope, expr);
            if (value == NULL) {
                return kc_tpl_fail(ctx, "expression evaluation failed", NULL);
            }

            if (kc_tpl_put(ctx, value) != KC_TPL_OK) {
                free(value);
                return KC_TPL_ERROR;
            }

            free(value);
            cursor = tag_end + 3;
            continue;
        }

        tag_end = strstr(tag_start + 2, "}}");
        if (tag_end == NULL) {
            return kc_tpl_fail(ctx, "unterminated tag", NULL);
        }

        kc_tpl_copy(tag, sizeof(tag), tag_start + 2, (size_t)(tag_end - (tag_start + 2)));
        if (tag[0] != '@') {
            char *text;

            text = kc_tpl_eval_dup(scope, tag);
            if (text == NULL) {
                return kc_tpl_fail(ctx, "expression evaluation failed", NULL);
            }

            if (kc_tpl_put_esc(ctx, text) != KC_TPL_OK) {
                free(text);
                return KC_TPL_ERROR;
            }

            free(text);
            cursor = tag_end + 2;
            continue;
        }

        if (strncmp(tag + 1, "include ", 8) == 0) {
            if (kc_tpl_render_include(ctx, scope, tag) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }
        } else if (strncmp(tag + 1, "var ", 4) == 0) {
            if (kc_tpl_render_var(ctx, scope, tag) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }
        } else if (strncmp(tag + 1, "setblock ", 9) == 0) {
            char name[64];
            char body[8192];
            char *mid;
            char *end;

            if (sscanf(tag + 10, "%63s", name) != 1) {
                return kc_tpl_fail(ctx, "invalid setblock directive", NULL);
            }

            end = kc_tpl_find(tag_end + 2, "setblock ", "endsetblock", NULL, &mid);
            if (end == NULL) {
                return kc_tpl_fail(ctx, "missing endsetblock", NULL);
            }

            kc_tpl_copy(body, sizeof(body), tag_end + 2, (size_t)(end - (tag_end + 2)));
            if (kc_tpl_block_set(scope, name, body) != KC_TPL_OK) {
                return kc_tpl_fail(ctx, "block assignment failed", name);
            }

            cursor = end + 16;
            continue;
        } else if (strncmp(tag + 1, "block ", 6) == 0) {
            if (kc_tpl_render_block(ctx, scope, tag) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }
        } else if (strncmp(tag + 1, "if ", 3) == 0) {
            if (kc_tpl_render_if(ctx, scope, tag, tag_end + 2, &cursor) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }

            continue;
        } else if (strncmp(tag + 1, "foreach ", 8) == 0) {
            if (kc_tpl_render_foreach(ctx, scope, tag, tag_end + 2, &cursor) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }

            continue;
        }

        cursor = tag_end + 2;
    }

    return KC_TPL_OK;
}

/**
 * Creates a reusable template instance and copies the template source.
 * @param out Pointer to receive the template instance.
 * @param source Borrowed null-terminated template source.
 * @param options Optional template options. NULL uses defaults.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_open(
    kc_tpl_t **out,
    const char *source,
    const kc_tpl_options_t *options
) {
    kc_tpl_t *tpl;
    const char *root = ".";

    if (out == NULL) {
        return KC_TPL_ERROR;
    }
    *out = NULL;

    if (source == NULL) {
        return KC_TPL_ERROR;
    }

    if (options != NULL) {
        if (options->root == NULL || options->root[0] == '\0') {
            return KC_TPL_ERROR;
        }
        root = options->root;
    }

    tpl = (kc_tpl_t *)calloc(1U, sizeof(kc_tpl_t));
    if (tpl == NULL) {
        return KC_TPL_ERROR;
    }

    tpl->source = kc_tpl_dup(source);
    if (tpl->source == NULL) {
        free(tpl);
        return KC_TPL_ERROR;
    }

    snprintf(tpl->root, sizeof(tpl->root), "%s", root);
    snprintf(tpl->error, sizeof(tpl->error), "ok");
    *out = tpl;
    return KC_TPL_OK;
}

/**
 * Renders the stored template with one isolated set of variables.
 * @param tpl Template instance.
 * @param vars Optional variable array.
 * @param var_count Number of entries in vars.
 * @return Owned null-terminated output, or NULL on failure.
 */
char *kc_tpl_render(
    kc_tpl_t *tpl,
    const kc_tpl_var_t *vars,
    size_t var_count
) {
    kc_tpl_scope_t scope;
    char *work;
    char *output;
    size_t i;
    int rc;

    if (tpl == NULL) {
        return NULL;
    }

    if (var_count > 0U && vars == NULL) {
        kc_tpl_fail(tpl, "invalid variables", NULL);
        return NULL;
    }

    memset(&scope, 0, sizeof(scope));

    for (i = 0U; i < var_count; i++) {
        if (
            vars[i].key == NULL ||
            vars[i].value == NULL ||
            kc_tpl_var_set(&scope, vars[i].key, vars[i].value) != KC_TPL_OK
        ) {
            kc_tpl_scope_clear(&scope);
            kc_tpl_fail(tpl, "variable assignment failed",
                vars[i].key != NULL ? vars[i].key : NULL);
            return NULL;
        }
    }

    work = kc_tpl_dup(tpl->source);
    if (work == NULL) {
        kc_tpl_scope_clear(&scope);
        kc_tpl_fail(tpl, "out of memory", NULL);
        return NULL;
    }

    tpl->out_n = 0U;
    tpl->out[0] = '\0';
    snprintf(tpl->error, sizeof(tpl->error), "ok");

    rc = kc_tpl_render_internal(tpl, &scope, work);
    free(work);
    kc_tpl_scope_clear(&scope);

    if (rc != KC_TPL_OK) {
        return NULL;
    }

    output = kc_tpl_dup(tpl->out);
    if (output == NULL) {
        kc_tpl_fail(tpl, "out of memory", NULL);
        return NULL;
    }

    return output;
}

/**
 * Returns the latest template error.
 * @param tpl Template instance.
 * @return Static or template-owned error text.
 */
const char *kc_tpl_error(const kc_tpl_t *tpl) {
    if (tpl == NULL) {
        return "invalid template";
    }

    return tpl->error;
}

/**
 * Releases output returned by kc_tpl_render.
 * @param ptr Output allocation to release.
 * @return None.
 */
void kc_tpl_free(void *ptr) {
    free(ptr);
}

/**
 * Releases a template instance and its owned source.
 * @param tpl Template instance, or NULL.
 * @return None.
 */
void kc_tpl_close(kc_tpl_t *tpl) {
    if (tpl == NULL) {
        return;
    }

    free(tpl->source);
    free(tpl);
}

