/**
 * libmdp.c - Markdown Parser
 * Summary: Shared Markdown parser and document lifecycle for mdp.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#define KC_MDP_PRIVATE
#include "libmdp.h"

#include <stdlib.h>
#include <string.h>

struct kc_mdp {
    char *body;
    char *meta;
    char *html;
    char *ansi;
};

/**
 * Appends bytes to a growable output buffer.
 * The buffer stays NUL-terminated after every append.
 * @param buf Target buffer.
 * @param data Bytes to append.
 * @param len Byte count.
 * @return None.
 */
void kc_mdp_buf_write(mdp_buf_t *buf, const void *data, size_t len) {
    if (buf->oom || len == 0) {
        return;
    }
    if (buf->len + len + 1 > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 256;
        unsigned char *grown;

        while (cap < buf->len + len + 1) {
            cap *= 2;
        }
        grown = (unsigned char *)realloc(buf->data, cap);
        if (!grown) {
            buf->oom = 1;
            return;
        }
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

/**
 * Appends a NUL-terminated string to a growable output buffer.
 * @param buf Target buffer.
 * @param text String to append.
 * @return None.
 */
void kc_mdp_buf_puts(mdp_buf_t *buf, const char *text) {
    if (text) {
        kc_mdp_buf_write(buf, text, strlen(text));
    }
}

/**
 * Appends one byte to a growable output buffer.
 * @param buf Target buffer.
 * @param c Byte to append.
 * @return None.
 */
void kc_mdp_buf_putc(mdp_buf_t *buf, char c) {
    kc_mdp_buf_write(buf, &c, 1);
}

/**
 * Duplicates a byte range from a source pointer.
 * @param start Source pointer.
 * @param len Byte count.
 * @return Owned buffer or NULL on failure.
 */
static char *kc_mdp_dup(const char *start, size_t len) {
    char *out;

    out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

/**
 * Splits a document into frontmatter metadata and body text.
 * @param src Document source string.
 * @param meta Receives allocated metadata buffer.
 * @param body Receives allocated body buffer.
 * @return MDP_OK on success, MDP_ERROR on allocation failure.
 */
static int kc_mdp_split(const char *src, char **meta, char **body) {
    const char *head_end;
    const char *tail;
    const char *body_start;
    size_t meta_len;

    *meta = kc_mdp_dup("", 0);
    *body = kc_mdp_dup(src, strlen(src));
    if (!*meta || !*body) {
        free(*meta);
        free(*body);
        return MDP_ERROR;
    }

    if (strncmp(src, "---\n", 4) == 0) {
        head_end = src + 4;
        tail = strstr(head_end, "\n---\n");
        if (!tail) {
            return MDP_OK;
        }
        meta_len = (size_t)(tail - head_end);
        body_start = tail + 5;
    } else if (strncmp(src, "---\r\n", 5) == 0) {
        head_end = src + 5;
        tail = strstr(head_end, "\r\n---\r\n");
        if (!tail) {
            return MDP_OK;
        }
        meta_len = (size_t)(tail - head_end);
        body_start = tail + 7;
    } else {
        return MDP_OK;
    }

    free(*meta);
    free(*body);
    *meta = kc_mdp_dup(head_end, meta_len);
    *body = kc_mdp_dup(body_start, strlen(body_start));
    if (!*meta || !*body) {
        free(*meta);
        free(*body);
        return MDP_ERROR;
    }
    return MDP_OK;
}

/**
 * Finds a closing delimiter while respecting backslash escapes.
 * @param text Source buffer.
 * @param start Search start offset.
 * @param close Closing character.
 * @return Closing offset or (size_t)-1 when absent.
 */
static size_t kc_mdp_find_close(const char *text, size_t start, char close) {
    size_t i = start;

    while (text[i]) {
        if (text[i] == '\\' && text[i + 1]) {
            i += 2;
            continue;
        }
        if (text[i] == close) {
            return i;
        }
        i++;
    }
    return (size_t)-1;
}

/**
 * Finds a closing label bracket with nested bracket awareness.
 * @param text Source buffer.
 * @param start Offset after the opening bracket.
 * @return Closing offset or (size_t)-1 when absent.
 */
static size_t kc_mdp_find_label_end(const char *text, size_t start) {
    size_t i = start;
    int depth = 1;

    while (text[i]) {
        if (text[i] == '\\' && text[i + 1]) {
            i += 2;
            continue;
        }
        if (text[i] == '[') {
            depth++;
        } else if (text[i] == ']') {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
        i++;
    }
    return (size_t)-1;
}

/**
 * Compares two byte sequences case-insensitively.
 * @param a First sequence.
 * @param b Second sequence.
 * @param n Byte count.
 * @return Zero when equal, non-zero otherwise.
 */
static int kc_mdp_casecmp(const char *a, const char *b, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 1;
    }
    return 0;
}

/**
 * Scans a raw HTML token starting at an opening angle bracket.
 * @param text Source buffer.
 * @param i Opening bracket offset.
 * @return Offset after the tag or (size_t)-1 when invalid.
 */
static size_t kc_mdp_scan_html_tag(const char *text, size_t i) {
    size_t j = i + 1;
    size_t k;
    char quote = 0;

    if (!text[j]) {
        return (size_t)-1;
    }
    if (text[j] == '!') {
        if (text[j + 1] == '-' && text[j + 2] == '-') {
            k = j + 3;
            while (text[k]) {
                if (text[k] == '-' && text[k + 1] == '-' && text[k + 2] == '>') {
                    return k + 3;
                }
                k++;
            }
            return (size_t)-1;
        }
        while (text[j] && text[j] != '>') {
            j++;
        }
        return text[j] ? j + 1 : (size_t)-1;
    }
    if (text[j] == '/') {
        j++;
    }
    if (!((text[j] >= 'a' && text[j] <= 'z') ||
        (text[j] >= 'A' && text[j] <= 'Z'))) {
        return (size_t)-1;
    }
    j++;
    while (text[j] && ((text[j] >= 'a' && text[j] <= 'z') ||
        (text[j] >= 'A' && text[j] <= 'Z') ||
        (text[j] >= '0' && text[j] <= '9') || text[j] == '-')) {
        j++;
    }
    if (!text[j] || (text[j] != '>' && text[j] != '/' &&
        text[j] != ' ' && text[j] != '\t')) {
        return (size_t)-1;
    }
    while (text[j]) {
        if (quote) {
            if (text[j] == quote) {
                quote = 0;
            }
        } else if (text[j] == '"' || text[j] == '\'') {
            quote = text[j];
        } else if (text[j] == '>') {
            return j + 1;
        }
        j++;
    }
    return (size_t)-1;
}

/**
 * Reports whether an HTML tag name is block-level for mdp purposes.
 * @param name Tag name.
 * @param len Tag name byte count.
 * @return Non-zero for recognized block tags.
 */
static int kc_mdp_is_block_tag(const char *name, size_t len) {
    static const char *const tags[] = {
        "address", "article", "aside", "audio", "base", "basefont",
        "blockquote", "body", "caption", "center", "col", "colgroup",
        "dd", "details", "dialog", "dir", "div", "dl", "dt", "fieldset",
        "figcaption", "figure", "footer", "form", "frame", "frameset",
        "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr",
        "html", "iframe", "legend", "li", "link", "main", "menu",
        "menuitem", "nav", "noframes", "ol", "optgroup", "option", "p",
        "param", "picture", "pre", "script", "section", "source", "style",
        "summary", "svg", "table", "tbody", "td", "template", "tfoot",
        "th", "thead", "title", "tr", "track", "ul", "video",
    };
    size_t i;

    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        if (strlen(tags[i]) == len && kc_mdp_casecmp(name, tags[i], len) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * Returns the block-level tag name opened by a line.
 * @param line Line text.
 * @param len Line byte count.
 * @param name_len Receives tag name byte count.
 * @return Pointer to tag name or NULL.
 */
static const char *kc_mdp_html_block_name(const char *line, size_t len,
    size_t *name_len) {
    size_t i = 0;
    size_t j;
    size_t start;

    while (i < len && line[i] == ' ') {
        i++;
    }
    if (i >= len || line[i] != '<' || i + 1 >= len || line[i + 1] == '/') {
        return NULL;
    }
    j = i + 1;
    if (!((line[j] >= 'a' && line[j] <= 'z') ||
        (line[j] >= 'A' && line[j] <= 'Z'))) {
        return NULL;
    }
    start = j;
    j++;
    while (j < len && ((line[j] >= 'a' && line[j] <= 'z') ||
        (line[j] >= 'A' && line[j] <= 'Z') ||
        (line[j] >= '0' && line[j] <= '9') || line[j] == '-')) {
        j++;
    }
    if (kc_mdp_scan_html_tag(line, i) == (size_t)-1 ||
        !kc_mdp_is_block_tag(line + start, j - start)) {
        return NULL;
    }
    if (name_len) {
        *name_len = j - start;
    }
    return line + start;
}

/**
 * Reports whether a line contains a matching HTML closing tag.
 * @param line Line text.
 * @param len Line byte count.
 * @param name Tag name.
 * @param name_len Tag name byte count.
 * @return Non-zero when a closing tag is present.
 */
static int kc_mdp_has_close(const char *line, size_t len, const char *name,
    size_t name_len) {
    size_t i;

    for (i = 0; i + name_len + 3 <= len; i++) {
        if (line[i] == '<' && line[i + 1] == '/' &&
            kc_mdp_casecmp(line + i + 2, name, name_len) == 0 &&
            line[i + 2 + name_len] == '>') {
            return 1;
        }
    }
    return 0;
}

static void kc_mdp_inline(mdp_buf_t *out, const kc_mdp_renderer_t *renderer,
    const char *text);

/**
 * Renders a link label through the shared inline parser.
 * @param out Target buffer.
 * @param renderer Active renderer.
 * @param text Source text.
 * @param start Label start offset.
 * @param len Label byte count.
 * @return None.
 */
static void kc_mdp_link_label(mdp_buf_t *out,
    const kc_mdp_renderer_t *renderer, const char *text, size_t start,
    size_t len) {
    char *label = kc_mdp_dup(text + start, len);

    if (!label) {
        out->oom = 1;
        return;
    }
    kc_mdp_inline(out, renderer, label);
    free(label);
}

/**
 * Parses and renders inline Markdown through the selected renderer.
 * @param out Target buffer.
 * @param renderer Active renderer.
 * @param text Inline Markdown source.
 * @return None.
 */
static void kc_mdp_inline(mdp_buf_t *out, const kc_mdp_renderer_t *renderer,
    const char *text) {
    size_t i = 0;
    size_t j;
    size_t mid;
    size_t close;

    while (text[i]) {
        if (text[i] == '~' && text[i + 1] == '~' && text[i + 2] &&
            text[i + 2] != ' ') {
            j = i + 2;
            while (text[j] && !(text[j] == '~' && text[j + 1] == '~')) {
                j++;
            }
            if (text[j] && j > i + 2 && text[j - 1] != ' ') {
                renderer->strike_open(out);
                renderer->text(out, text + i + 2, j - i - 2);
                renderer->strike_close(out);
                i = j + 2;
                continue;
            }
        }
        if (text[i] == '*' && text[i + 1] == '*' && text[i + 2] &&
            text[i + 2] != ' ') {
            j = i + 2;
            while (text[j] && !(text[j] == '*' && text[j + 1] == '*')) {
                j++;
            }
            if (text[j] && j > i + 2 && text[j - 1] != ' ') {
                renderer->strong_open(out);
                renderer->text(out, text + i + 2, j - i - 2);
                renderer->strong_close(out);
                i = j + 2;
                continue;
            }
        }
        if (text[i] == '*' && text[i + 1] && text[i + 1] != '*' &&
            text[i + 1] != ' ') {
            j = i + 1;
            while (text[j] && text[j] != '*') {
                j++;
            }
            if (text[j] == '*' && j > i + 1 && text[j - 1] != ' ') {
                renderer->em_open(out);
                renderer->text(out, text + i + 1, j - i - 1);
                renderer->em_close(out);
                i = j + 1;
                continue;
            }
        }
        if (text[i] == '[' && text[i + 1] == '!' && text[i + 2] == '[') {
            size_t img_mid = kc_mdp_find_label_end(text, i + 3);
            size_t img_close = img_mid != (size_t)-1 && text[img_mid + 1] == '('
                ? kc_mdp_find_close(text, img_mid + 2, ')') : (size_t)-1;
            size_t lnk_close = img_close != (size_t)-1 &&
                text[img_close + 1] == ']' && text[img_close + 2] == '('
                ? kc_mdp_find_close(text, img_close + 3, ')') : (size_t)-1;

            if (img_mid != (size_t)-1 && img_close != (size_t)-1 &&
                lnk_close != (size_t)-1) {
                renderer->link_open(out, text + img_close + 3,
                    lnk_close - img_close - 3);
                renderer->image(out, text + i + 3, img_mid - i - 3,
                    text + img_mid + 2, img_close - img_mid - 2);
                renderer->link_close(out);
                i = lnk_close + 1;
                continue;
            }
        }
        if (text[i] == '!' && text[i + 1] == '[') {
            mid = kc_mdp_find_label_end(text, i + 2);
            close = mid != (size_t)-1 && text[mid + 1] == '('
                ? kc_mdp_find_close(text, mid + 2, ')') : (size_t)-1;
            if (mid != (size_t)-1 && close != (size_t)-1) {
                renderer->image(out, text + i + 2, mid - i - 2,
                    text + mid + 2, close - mid - 2);
                i = close + 1;
                continue;
            }
        }
        if (text[i] == '`') {
            j = i + 1;
            while (text[j] && text[j] != '`') {
                j++;
            }
            if (text[j] == '`') {
                renderer->code_inline(out, text + i + 1, j - i - 1);
                i = j + 1;
                continue;
            }
        }
        if (text[i] == '[') {
            mid = kc_mdp_find_label_end(text, i + 1);
            close = mid != (size_t)-1 && text[mid + 1] == '('
                ? kc_mdp_find_close(text, mid + 2, ')') : (size_t)-1;
            if (mid != (size_t)-1 && close != (size_t)-1) {
                renderer->link_open(out, text + mid + 2, close - mid - 2);
                kc_mdp_link_label(out, renderer, text, i + 1, mid - i - 1);
                renderer->link_close(out);
                i = close + 1;
                continue;
            }
        }
        if (text[i] == '<') {
            size_t tag_end = kc_mdp_scan_html_tag(text, i);

            if (tag_end != (size_t)-1) {
                renderer->raw_inline(out, text + i, tag_end - i);
                i = tag_end;
                continue;
            }
        }
        renderer->text(out, text + i, 1);
        i++;
    }
}

/**
 * Flushes a pending paragraph through the selected renderer.
 * @param out Target buffer.
 * @param renderer Active renderer.
 * @param par Pointer to paragraph buffer.
 * @return None.
 */
static void kc_mdp_flush(mdp_buf_t *out, const kc_mdp_renderer_t *renderer,
    char **par) {
    if (!*par || !**par) {
        return;
    }
    renderer->paragraph_open(out);
    kc_mdp_inline(out, renderer, *par);
    renderer->paragraph_close(out);
    (*par)[0] = '\0';
}

/**
 * Appends a source line to a paragraph buffer.
 * @param par Pointer to allocated paragraph buffer.
 * @param line Line text.
 * @return MDP_OK on success, MDP_ERROR on allocation failure.
 */
static int kc_mdp_join(char **par, const char *line) {
    size_t old = *par ? strlen(*par) : 0;
    size_t add = strlen(line);
    char *grown = (char *)realloc(*par, old + add + 2);

    if (!grown) {
        return MDP_ERROR;
    }
    *par = grown;
    if (old) {
        (*par)[old++] = ' ';
    }
    memcpy(*par + old, line, add + 1);
    return MDP_OK;
}

/**
 * Closes an open list context.
 * @param out Target buffer.
 * @param renderer Active renderer.
 * @param in_list Pointer to list state: 1 unordered, 2 ordered.
 * @return None.
 */
static void kc_mdp_close_list(mdp_buf_t *out,
    const kc_mdp_renderer_t *renderer, int *in_list) {
    if (*in_list) {
        renderer->list_close(out, *in_list == 2);
        *in_list = 0;
    }
}

/**
 * Closes open list and blockquote contexts.
 * @param out Target buffer.
 * @param renderer Active renderer.
 * @param in_list Pointer to list state.
 * @param in_quote Pointer to blockquote state.
 * @return None.
 */
static void kc_mdp_close_blocks(mdp_buf_t *out,
    const kc_mdp_renderer_t *renderer, int *in_list, int *in_quote) {
    kc_mdp_close_list(out, renderer, in_list);
    if (*in_quote) {
        renderer->quote_close(out);
        *in_quote = 0;
    }
}

/**
 * Parses an ordered list marker.
 * @param s Line text.
 * @param number Receives source item number.
 * @return Content offset or zero when not ordered-list syntax.
 */
static size_t kc_mdp_ordered_item(const char *s, unsigned int *number) {
    size_t i = 0;
    unsigned int value = 0;

    if (!(s[i] >= '0' && s[i] <= '9')) {
        return 0;
    }
    while (s[i] >= '0' && s[i] <= '9') {
        value = value * 10U + (unsigned int)(s[i] - '0');
        i++;
    }
    if (s[i] != '.' || s[i + 1] != ' ') {
        return 0;
    }
    if (number) {
        *number = value;
    }
    return i + 2;
}

/**
 * Renders one parsed list item.
 * @param out Target buffer.
 * @param renderer Active renderer.
 * @param text Item source text.
 * @param ordered Non-zero for ordered lists.
 * @param number Source ordered item number.
 * @param tasks Non-zero to recognize task markers.
 * @return None.
 */
static void kc_mdp_list_item(mdp_buf_t *out,
    const kc_mdp_renderer_t *renderer, const char *text, int ordered,
    unsigned int number, int tasks) {
    int task_state = -1;
    size_t len = strlen(text);

    if (tasks && len >= 4 && text[0] == '[' && text[2] == ']' &&
        text[3] == ' ' &&
        (text[1] == ' ' || text[1] == 'x' || text[1] == 'X')) {
        task_state = text[1] == ' ' ? 0 : 1;
        text += 4;
    }
    renderer->list_item_open(out, ordered, number, task_state);
    kc_mdp_inline(out, renderer, text);
    renderer->list_item_close(out);
}

/**
 * Reports whether a line is a Markdown table separator row.
 * @param s Line text.
 * @param len Byte count.
 * @return Non-zero for valid separator syntax.
 */
static int kc_mdp_is_sep(const char *s, size_t len) {
    size_t i, cs, ce, j;
    int cells = 0;

    if (!len || s[0] != '|') {
        return 0;
    }
    i = 1;
    while (i <= len) {
        while (i < len && s[i] == ' ') i++;
        cs = i;
        while (i < len && s[i] != '|') i++;
        ce = i;
        while (ce > cs && s[ce - 1] == ' ') ce--;
        if (cs == ce) {
            if (i >= len) break;
            return 0;
        }
        j = cs;
        if (j < ce && s[j] == ':') j++;
        if (j >= ce || s[j] != '-') return 0;
        while (j < ce && s[j] == '-') j++;
        if (j < ce && s[j] == ':') j++;
        if (j != ce) return 0;
        cells++;
        if (i < len) i++;
    }
    return cells > 0;
}

/**
 * Parses and renders one Markdown table row.
 * @param out Target buffer.
 * @param renderer Active renderer.
 * @param s Row text.
 * @param len Byte count.
 * @param header Non-zero for a header row.
 * @return MDP_OK on success, MDP_ERROR on allocation failure.
 */
static int kc_mdp_table_row(mdp_buf_t *out,
    const kc_mdp_renderer_t *renderer, const char *s, size_t len, int header) {
    size_t i, cs, ce;
    int first = 1;
    char *cell;

    renderer->table_row_open(out, header);
    i = (len > 0 && s[0] == '|') ? 1 : 0;
    while (i < len) {
        while (i < len && s[i] == ' ') i++;
        cs = i;
        while (i < len && s[i] != '|') i++;
        ce = i;
        while (ce > cs && s[ce - 1] == ' ') ce--;
        if (cs == ce && i >= len) break;
        renderer->table_cell_open(out, header, first);
        if (cs < ce) {
            cell = kc_mdp_dup(s + cs, ce - cs);
            if (!cell) {
                out->oom = 1;
                return MDP_ERROR;
            }
            kc_mdp_inline(out, renderer, cell);
            free(cell);
        }
        renderer->table_cell_close(out, header);
        first = 0;
        if (i < len) i++;
    }
    renderer->table_row_close(out, header);
    return MDP_OK;
}

/**
 * Extracts an optional fenced-code language identifier.
 * @param line Fence line.
 * @param len Fence line byte count.
 * @param language Receives language start pointer.
 * @param language_len Receives language byte count.
 * @return None.
 */
static void kc_mdp_code_language(const char *line, size_t len,
    const char **language, size_t *language_len) {
    size_t start = 3;
    size_t end;

    while (start < len && (line[start] == ' ' || line[start] == '\t')) start++;
    end = start;
    while (end < len && line[end] != ' ' && line[end] != '\t') end++;
    *language = line + start;
    *language_len = end - start;
}

/**
 * Parses Markdown once and emits semantic events to a renderer backend.
 * @param body Markdown body text.
 * @param out Target output buffer.
 * @param renderer Rendering backend callbacks.
 * @return MDP_OK on success, MDP_ERROR on allocation failure.
 */
int kc_mdp_parse(const char *body, mdp_buf_t *out,
    const kc_mdp_renderer_t *renderer) {
    const char *line = body;
    char *par = kc_mdp_dup("", 0);
    int in_list = 0;
    int in_code = 0;
    int in_quote = 0;
    int in_table = 0;
    char *in_html = NULL;
    char *table_hdr = NULL;

    if (!par) {
        return MDP_ERROR;
    }

    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char *tmp;
        int reprocess;

        while (len && line[len - 1] == '\r') len--;
        tmp = kc_mdp_dup(line, len);
        if (!tmp) {
            free(table_hdr);
            free(in_html);
            free(par);
            return MDP_ERROR;
        }

        do {
            int level = 0;
            size_t ordered = 0;
            unsigned int ordered_number = 0;
            reprocess = 0;

            if (in_code) {
                if (len >= 3 && strncmp(tmp, "```", 3) == 0) {
                    renderer->code_close(out);
                    in_code = 0;
                } else {
                    renderer->code_text(out, tmp, len);
                }
            } else if (in_html) {
                renderer->raw_block(out, tmp, len);
                if (kc_mdp_has_close(tmp, len, in_html, strlen(in_html))) {
                    free(in_html);
                    in_html = NULL;
                }
            } else if (!len) {
                if (in_table == 2) {
                    renderer->table_close(out);
                    in_table = 0;
                } else if (in_table == 1) {
                    if (kc_mdp_join(&par, table_hdr) != MDP_OK) {
                        free(tmp);
                        free(table_hdr);
                        free(par);
                        return MDP_ERROR;
                    }
                    free(table_hdr);
                    table_hdr = NULL;
                    in_table = 0;
                    kc_mdp_flush(out, renderer, &par);
                    kc_mdp_close_blocks(out, renderer, &in_list, &in_quote);
                } else {
                    kc_mdp_flush(out, renderer, &par);
                    kc_mdp_close_blocks(out, renderer, &in_list, &in_quote);
                }
            } else if (in_table == 1 && kc_mdp_is_sep(tmp, len)) {
                kc_mdp_flush(out, renderer, &par);
                kc_mdp_close_blocks(out, renderer, &in_list, &in_quote);
                renderer->table_open(out);
                if (kc_mdp_table_row(out, renderer, table_hdr,
                    strlen(table_hdr), 1) != MDP_OK) {
                    free(tmp);
                    free(table_hdr);
                    free(par);
                    return MDP_ERROR;
                }
                free(table_hdr);
                table_hdr = NULL;
                in_table = 2;
            } else if (in_table == 2 && tmp[0] == '|') {
                if (kc_mdp_table_row(out, renderer, tmp, len, 0) != MDP_OK) {
                    free(tmp);
                    free(par);
                    return MDP_ERROR;
                }
            } else if (in_table == 2) {
                renderer->table_close(out);
                in_table = 0;
                reprocess = 1;
            } else if (in_table == 1) {
                if (kc_mdp_join(&par, table_hdr) != MDP_OK) {
                    free(tmp);
                    free(table_hdr);
                    free(par);
                    return MDP_ERROR;
                }
                free(table_hdr);
                table_hdr = NULL;
                in_table = 0;
                kc_mdp_flush(out, renderer, &par);
                reprocess = 1;
            } else if (len >= 3 && strncmp(tmp, "```", 3) == 0) {
                const char *language;
                size_t language_len;

                kc_mdp_flush(out, renderer, &par);
                kc_mdp_close_blocks(out, renderer, &in_list, &in_quote);
                kc_mdp_code_language(tmp, len, &language, &language_len);
                renderer->code_open(out, language, language_len);
                in_code = 1;
            } else if (len == 3 &&
                (strncmp(tmp, "---", 3) == 0 || strncmp(tmp, "***", 3) == 0)) {
                kc_mdp_flush(out, renderer, &par);
                kc_mdp_close_blocks(out, renderer, &in_list, &in_quote);
                renderer->horizontal_rule(out);
            } else {
                const char *html_name;
                size_t html_name_len = 0;

                html_name = kc_mdp_html_block_name(tmp, len, &html_name_len);
                if (html_name) {
                    kc_mdp_flush(out, renderer, &par);
                    kc_mdp_close_blocks(out, renderer, &in_list, &in_quote);
                    renderer->raw_block(out, tmp, len);
                    in_html = kc_mdp_dup(html_name, html_name_len);
                    if (!in_html) {
                        free(tmp);
                        free(par);
                        return MDP_ERROR;
                    }
                    if (kc_mdp_has_close(tmp, len, html_name, html_name_len)) {
                        free(in_html);
                        in_html = NULL;
                    }
                } else {
                    while (tmp[level] == '#' && level < 6) level++;
                    ordered = kc_mdp_ordered_item(tmp, &ordered_number);

                    if (level && tmp[level] == ' ') {
                        kc_mdp_flush(out, renderer, &par);
                        kc_mdp_close_blocks(out, renderer, &in_list, &in_quote);
                        renderer->heading_open(out, level);
                        kc_mdp_inline(out, renderer, tmp + level + 1);
                        renderer->heading_close(out, level);
                    } else if (tmp[0] == '>' && tmp[1] == ' ') {
                        kc_mdp_flush(out, renderer, &par);
                        kc_mdp_close_list(out, renderer, &in_list);
                        if (!in_quote) {
                            renderer->quote_open(out);
                            in_quote = 1;
                        }
                        renderer->paragraph_open(out);
                        kc_mdp_inline(out, renderer, tmp + 2);
                        renderer->paragraph_close(out);
                    } else if ((tmp[0] == '-' || tmp[0] == '*') &&
                        tmp[1] == ' ') {
                        kc_mdp_flush(out, renderer, &par);
                        if (in_quote) {
                            renderer->quote_close(out);
                            in_quote = 0;
                        }
                        if (in_list != 1) {
                            kc_mdp_close_list(out, renderer, &in_list);
                            renderer->list_open(out, 0);
                            in_list = 1;
                        }
                        kc_mdp_list_item(out, renderer, tmp + 2, 0, 0, 1);
                    } else if (ordered) {
                        kc_mdp_flush(out, renderer, &par);
                        if (in_quote) {
                            renderer->quote_close(out);
                            in_quote = 0;
                        }
                        if (in_list != 2) {
                            kc_mdp_close_list(out, renderer, &in_list);
                            renderer->list_open(out, 1);
                            in_list = 2;
                        }
                        kc_mdp_list_item(out, renderer, tmp + ordered, 1,
                            ordered_number, 0);
                    } else if (tmp[0] == '|') {
                        kc_mdp_flush(out, renderer, &par);
                        kc_mdp_close_blocks(out, renderer, &in_list, &in_quote);
                        table_hdr = kc_mdp_dup(tmp, len);
                        if (!table_hdr) {
                            free(tmp);
                            free(par);
                            return MDP_ERROR;
                        }
                        in_table = 1;
                    } else {
                        kc_mdp_close_blocks(out, renderer, &in_list, &in_quote);
                        if (kc_mdp_join(&par, tmp) != MDP_OK) {
                            free(tmp);
                            free(par);
                            return MDP_ERROR;
                        }
                    }
                }
            }
        } while (reprocess);

        free(tmp);
        line = end ? end + 1 : line + len;
    }

    kc_mdp_flush(out, renderer, &par);
    kc_mdp_close_list(out, renderer, &in_list);
    if (in_quote) renderer->quote_close(out);
    if (in_code) renderer->code_close(out);
    if (in_table == 2) {
        renderer->table_close(out);
    } else if (in_table == 1 && table_hdr && *table_hdr) {
        renderer->paragraph_open(out);
        kc_mdp_inline(out, renderer, table_hdr);
        renderer->paragraph_close(out);
    }
    free(table_hdr);
    free(in_html);
    free(par);
    return out->oom ? MDP_ERROR : MDP_OK;
}

/**
 * Finalizes one renderer output buffer as an owned string.
 * @param buf Render buffer.
 * @return Owned NUL-terminated result or NULL on failure.
 */
static char *kc_mdp_finish(mdp_buf_t *buf) {
    if (buf->oom) {
        free(buf->data);
        return NULL;
    }
    if (!buf->data) {
        buf->data = (unsigned char *)malloc(1);
        if (!buf->data) {
            return NULL;
        }
        buf->data[0] = '\0';
    }
    return (char *)buf->data;
}

/**
 * Create a reusable Markdown document.
 * Frontmatter is split from the body once during this call.
 * @param out Pointer to receive the document.
 * @param input Null-terminated Markdown input.
 * @return KC_MDP_OK on success, KC_MDP_ERROR on failure.
 */
int kc_mdp_open(kc_mdp_t **out, const char *input) {
    kc_mdp_t *mdp;
    char *meta = NULL;
    char *body = NULL;

    if (!out) {
        return KC_MDP_ERROR;
    }
    *out = NULL;
    if (!input) {
        return KC_MDP_ERROR;
    }
    if (kc_mdp_split(input, &meta, &body) != MDP_OK) {
        return KC_MDP_ERROR;
    }
    mdp = (kc_mdp_t *)calloc(1, sizeof(kc_mdp_t));
    if (!mdp) {
        free(meta);
        free(body);
        return KC_MDP_ERROR;
    }
    mdp->meta = meta;
    mdp->body = body;
    *out = mdp;
    return KC_MDP_OK;
}

/**
 * Render and cache the document body as an HTML fragment.
 * @param mdp Document returned by kc_mdp_open().
 * @return Cached NUL-terminated HTML fragment, or NULL on failure.
 */
const char *kc_mdp_html(kc_mdp_t *mdp) {
    mdp_buf_t buf;

    if (!mdp) return NULL;
    if (mdp->html) return mdp->html;
    memset(&buf, 0, sizeof(buf));
    if (kc_mdp_render_html(mdp->body, &buf) != MDP_OK) {
        free(buf.data);
        return NULL;
    }
    mdp->html = kc_mdp_finish(&buf);
    return mdp->html;
}

/**
 * Render and cache the document body for an ANSI terminal.
 * @param mdp Document returned by kc_mdp_open().
 * @return Cached NUL-terminated ANSI text, or NULL on failure.
 */
const char *kc_mdp_ansi(kc_mdp_t *mdp) {
    mdp_buf_t buf;

    if (!mdp) return NULL;
    if (mdp->ansi) return mdp->ansi;
    memset(&buf, 0, sizeof(buf));
    if (kc_mdp_render_ansi(mdp->body, &buf) != MDP_OK) {
        free(buf.data);
        return NULL;
    }
    mdp->ansi = kc_mdp_finish(&buf);
    return mdp->ansi;
}

/**
 * Return the document body after recognized frontmatter.
 * @param mdp Document returned by kc_mdp_open().
 * @return NUL-terminated body text, or NULL for an invalid document.
 */
const char *kc_mdp_body(const kc_mdp_t *mdp) {
    return mdp ? mdp->body : NULL;
}

/**
 * Return recognized raw frontmatter content.
 * @param mdp Document returned by kc_mdp_open().
 * @return NUL-terminated metadata text, or NULL for an invalid document.
 */
const char *kc_mdp_meta(const kc_mdp_t *mdp) {
    return mdp ? mdp->meta : NULL;
}

/**
 * Release a Markdown document. The pointer may be NULL.
 * @param mdp Document returned by kc_mdp_open(), or NULL.
 * @return None.
 */
void kc_mdp_close(kc_mdp_t *mdp) {
    if (!mdp) return;
    free(mdp->ansi);
    free(mdp->html);
    free(mdp->body);
    free(mdp->meta);
    free(mdp);
}

#ifndef KC_MDP_BUILD_VERSION
#define KC_MDP_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_mdp_version(void) {
    return (uint64_t)KC_MDP_BUILD_VERSION;
}
