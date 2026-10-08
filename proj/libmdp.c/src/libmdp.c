/**
 * libmdp.c - Markdown Parser
 * Summary: Core implementation for the mdp library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "libmdp.h"

#include <stdlib.h>
#include <string.h>

#define MDP_OK     0
#define MDP_ERROR -1

struct kc_mdp {
    char *body;
    char *meta;
    char *html;
};

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
    int oom;
} mdp_buf_t;

static void mdp_buf_write(mdp_buf_t *buf, const void *data, size_t len) {
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

static void mdp_buf_puts(mdp_buf_t *buf, const char *text) {
    if (text) {
        mdp_buf_write(buf, text, strlen(text));
    }
}

static void mdp_buf_putc(mdp_buf_t *buf, char c) {
    mdp_buf_write(buf, &c, 1);
}

static char *kc_mdp_dup(const char *start, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

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

static void kc_mdp_escape(mdp_buf_t *out, const char *text, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        if (text[i] == '&') mdp_buf_puts(out, "&amp;");
        else if (text[i] == '<') mdp_buf_puts(out, "&lt;");
        else if (text[i] == '>') mdp_buf_puts(out, "&gt;");
        else if (text[i] == '"') mdp_buf_puts(out, "&quot;");
        else mdp_buf_putc(out, text[i]);
    }
}

static size_t kc_mdp_find_close(const char *text, size_t start, char close) {
    size_t i = start;
    while (text[i]) {
        if (text[i] == '\\' && text[i + 1]) {
            i += 2;
            continue;
        }
        if (text[i] == close) return i;
        i++;
    }
    return (size_t)-1;
}

static size_t kc_mdp_find_label_end(const char *text, size_t start) {
    size_t i = start;
    int depth = 1;
    while (text[i]) {
        if (text[i] == '\\' && text[i + 1]) {
            i += 2;
            continue;
        }
        if (text[i] == '[') depth++;
        else if (text[i] == ']') {
            depth--;
            if (depth == 0) return i;
        }
        i++;
    }
    return (size_t)-1;
}

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

static size_t kc_mdp_scan_html_tag(const char *text, size_t i) {
    size_t j = i + 1;
    size_t k;
    char quote = 0;

    if (!text[j]) return (size_t)-1;
    if (text[j] == '!') {
        if (text[j + 1] == '-' && text[j + 2] == '-') {
            k = j + 3;
            while (text[k]) {
                if (text[k] == '-' && text[k + 1] == '-' && text[k + 2] == '>') return k + 3;
                k++;
            }
            return (size_t)-1;
        }
        while (text[j] && text[j] != '>') j++;
        return text[j] ? j + 1 : (size_t)-1;
    }
    if (text[j] == '/') j++;
    if (!((text[j] >= 'a' && text[j] <= 'z') || (text[j] >= 'A' && text[j] <= 'Z'))) return (size_t)-1;
    j++;
    while (text[j] && ((text[j] >= 'a' && text[j] <= 'z') ||
        (text[j] >= 'A' && text[j] <= 'Z') || (text[j] >= '0' && text[j] <= '9') || text[j] == '-')) j++;
    if (!text[j] || (text[j] != '>' && text[j] != '/' && text[j] != ' ' && text[j] != '\t')) return (size_t)-1;
    while (text[j]) {
        if (quote) {
            if (text[j] == quote) quote = 0;
        } else if (text[j] == '"' || text[j] == '\'') quote = text[j];
        else if (text[j] == '>') return j + 1;
        j++;
    }
    return (size_t)-1;
}

static int kc_mdp_is_block_tag(const char *name, size_t len) {
    static const char *const tags[] = {
        "address", "article", "aside", "audio", "base", "basefont", "blockquote", "body", "caption", "center",
        "col", "colgroup", "dd", "details", "dialog", "dir", "div", "dl", "dt", "fieldset", "figcaption",
        "figure", "footer", "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head",
        "header", "hr", "html", "iframe", "legend", "li", "link", "main", "menu", "menuitem", "nav",
        "noframes", "ol", "optgroup", "option", "p", "param", "picture", "pre", "script", "section", "source",
        "style", "summary", "svg", "table", "tbody", "td", "template", "tfoot", "th", "thead", "title", "tr",
        "track", "ul", "video",
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        if (strlen(tags[i]) == len && kc_mdp_casecmp(name, tags[i], len) == 0) return 1;
    }
    return 0;
}

static const char *kc_mdp_html_block_name(const char *line, size_t len, size_t *name_len) {
    size_t i = 0, j, start;
    while (i < len && line[i] == ' ') i++;
    if (i >= len || line[i] != '<' || i + 1 >= len || line[i + 1] == '/') return NULL;
    j = i + 1;
    if (!((line[j] >= 'a' && line[j] <= 'z') || (line[j] >= 'A' && line[j] <= 'Z'))) return NULL;
    start = j++;
    while (j < len && ((line[j] >= 'a' && line[j] <= 'z') || (line[j] >= 'A' && line[j] <= 'Z') ||
        (line[j] >= '0' && line[j] <= '9') || line[j] == '-')) j++;
    if (kc_mdp_scan_html_tag(line, i) == (size_t)-1 || !kc_mdp_is_block_tag(line + start, j - start)) return NULL;
    if (name_len) *name_len = j - start;
    return line + start;
}

static int kc_mdp_has_close(const char *line, size_t len, const char *name, size_t name_len) {
    size_t i;
    for (i = 0; i + name_len + 3 <= len; i++) {
        if (line[i] == '<' && line[i + 1] == '/' &&
            kc_mdp_casecmp(line + i + 2, name, name_len) == 0 && line[i + 2 + name_len] == '>') return 1;
    }
    return 0;
}

static void kc_mdp_inline(mdp_buf_t *out, const char *text);

static void kc_mdp_link_label(mdp_buf_t *out, const char *text, size_t start, size_t len) {
    char *label = kc_mdp_dup(text + start, len);
    if (!label) return;
    kc_mdp_inline(out, label);
    free(label);
}

static void kc_mdp_inline(mdp_buf_t *out, const char *text) {
    size_t i = 0, j, mid, close;
    while (text[i]) {
        if (text[i] == '~' && text[i + 1] == '~' && text[i + 2] && text[i + 2] != ' ') {
            j = i + 2;
            while (text[j] && !(text[j] == '~' && text[j + 1] == '~')) j++;
            if (text[j] && j > i + 2 && text[j - 1] != ' ') {
                mdp_buf_puts(out, "<del>");
                kc_mdp_escape(out, text + i + 2, j - i - 2);
                mdp_buf_puts(out, "</del>");
                i = j + 2;
                continue;
            }
        }
        if (text[i] == '*' && text[i + 1] == '*' && text[i + 2] && text[i + 2] != ' ') {
            j = i + 1;
            while (text[j] && !(text[j] == '*' && text[j + 1] == '*')) j++;
            if (text[j] == '*' && text[j + 1] == '*' && j > i + 2 && text[j - 1] != ' ') {
                mdp_buf_puts(out, "<strong>");
                kc_mdp_escape(out, text + i + 2, j - i - 2);
                mdp_buf_puts(out, "</strong>");
                i = j + 2;
                continue;
            }
        }
        if (text[i] == '*' && text[i + 1] && text[i + 1] != '*' && text[i + 1] != ' ') {
            j = i + 1;
            while (text[j] && text[j] != '*') j++;
            if (text[j] == '*' && j > i + 1 && text[j - 1] != ' ') {
                mdp_buf_puts(out, "<em>");
                kc_mdp_escape(out, text + i + 1, j - i - 1);
                mdp_buf_puts(out, "</em>");
                i = j + 1;
                continue;
            }
        }
        if (text[i] == '!' && text[i + 1] == '[') {
            mid = kc_mdp_find_label_end(text, i + 2);
            close = mid != (size_t)-1 && text[mid + 1] == '(' ? kc_mdp_find_close(text, mid + 2, ')') : (size_t)-1;
            if (mid != (size_t)-1 && close != (size_t)-1) {
                mdp_buf_puts(out, "<img src=\"");
                kc_mdp_escape(out, text + mid + 2, close - mid - 2);
                mdp_buf_puts(out, "\" alt=\"");
                kc_mdp_escape(out, text + i + 2, mid - i - 2);
                mdp_buf_puts(out, "\">");
                i = close + 1;
                continue;
            }
        }
        if (text[i] == '`') {
            j = i + 1;
            while (text[j] && text[j] != '`') j++;
            if (text[j] == '`') {
                mdp_buf_puts(out, "<code>");
                kc_mdp_escape(out, text + i + 1, j - i - 1);
                mdp_buf_puts(out, "</code>");
                i = j + 1;
                continue;
            }
        }
        if (text[i] == '[' && text[i + 1] == '!' && text[i + 2] == '[') {
            size_t img_mid = kc_mdp_find_label_end(text, i + 3);
            size_t img_close = img_mid != (size_t)-1 && text[img_mid + 1] == '(' ? kc_mdp_find_close(text, img_mid + 2, ')') : (size_t)-1;
            size_t lnk_close = img_close != (size_t)-1 && text[img_close + 1] == ']' && text[img_close + 2] == '('
                ? kc_mdp_find_close(text, img_close + 3, ')') : (size_t)-1;
            if (img_mid != (size_t)-1 && img_close != (size_t)-1 && lnk_close != (size_t)-1) {
                mdp_buf_puts(out, "<a href=\"");
                kc_mdp_escape(out, text + img_close + 3, lnk_close - img_close - 3);
                mdp_buf_puts(out, "\"><img src=\"");
                kc_mdp_escape(out, text + img_mid + 2, img_close - img_mid - 2);
                mdp_buf_puts(out, "\" alt=\"");
                kc_mdp_escape(out, text + i + 3, img_mid - i - 3);
                mdp_buf_puts(out, "\"></a>");
                i = lnk_close + 1;
                continue;
            }
        }
        if (text[i] == '[') {
            mid = kc_mdp_find_label_end(text, i + 1);
            close = mid != (size_t)-1 && text[mid + 1] == '(' ? kc_mdp_find_close(text, mid + 2, ')') : (size_t)-1;
            if (mid != (size_t)-1 && close != (size_t)-1) {
                mdp_buf_puts(out, "<a href=\"");
                kc_mdp_escape(out, text + mid + 2, close - mid - 2);
                mdp_buf_puts(out, "\">");
                kc_mdp_link_label(out, text, i + 1, mid - i - 1);
                mdp_buf_puts(out, "</a>");
                i = close + 1;
                continue;
            }
        }
        if (text[i] == '<') {
            size_t tag_end = kc_mdp_scan_html_tag(text, i);
            if (tag_end != (size_t)-1) {
                mdp_buf_write(out, text + i, tag_end - i);
                i = tag_end;
                continue;
            }
        }
        kc_mdp_escape(out, text + i, 1);
        i++;
    }
}

static void kc_mdp_flush(mdp_buf_t *out, char **par) {
    if (!*par || !**par) return;
    mdp_buf_puts(out, "<p>");
    kc_mdp_inline(out, *par);
    mdp_buf_puts(out, "</p>\n");
    (*par)[0] = '\0';
}

static int kc_mdp_join(char **par, const char *line) {
    size_t old = *par ? strlen(*par) : 0;
    size_t add = strlen(line);
    char *grown = (char *)realloc(*par, old + add + 2);
    if (!grown) return MDP_ERROR;
    *par = grown;
    if (old) (*par)[old++] = ' ';
    memcpy(*par + old, line, add + 1);
    return MDP_OK;
}

static void kc_mdp_close_list(mdp_buf_t *out, int *in_list) {
    if (*in_list == 1) mdp_buf_puts(out, "</ul>\n");
    else if (*in_list == 2) mdp_buf_puts(out, "</ol>\n");
    *in_list = 0;
}

static void kc_mdp_close_blocks(mdp_buf_t *out, int *in_list, int *in_quote) {
    kc_mdp_close_list(out, in_list);
    if (*in_quote) {
        mdp_buf_puts(out, "</blockquote>\n");
        *in_quote = 0;
    }
}

static size_t kc_mdp_ordered_item(const char *s) {
    size_t i = 0;
    if (!(s[i] >= '0' && s[i] <= '9')) return 0;
    while (s[i] >= '0' && s[i] <= '9') i++;
    if (s[i] != '.' || s[i + 1] != ' ') return 0;
    return i + 2;
}

static void kc_mdp_list_item(mdp_buf_t *out, const char *text, int tasks) {
    mdp_buf_puts(out, "<li>");
    if (tasks && text[0] == '[' && text[2] == ']' && text[3] == ' ' &&
        (text[1] == ' ' || text[1] == 'x' || text[1] == 'X')) {
        mdp_buf_puts(out, "<input type=\"checkbox\" disabled");
        if (text[1] == 'x' || text[1] == 'X') mdp_buf_puts(out, " checked");
        mdp_buf_puts(out, "> ");
        text += 4;
    }
    kc_mdp_inline(out, text);
    mdp_buf_puts(out, "</li>\n");
}

static int kc_mdp_is_sep(const char *s, size_t len) {
    size_t i, cs, ce, j;
    int cells = 0;
    if (!len || s[0] != '|') return 0;
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

static int kc_mdp_table_row(mdp_buf_t *out, const char *s, size_t len, const char *tag) {
    size_t i, cs, ce;
    char *cell;
    mdp_buf_puts(out, "<tr>");
    i = (len > 0 && s[0] == '|') ? 1 : 0;
    while (i < len) {
        while (i < len && s[i] == ' ') i++;
        cs = i;
        while (i < len && s[i] != '|') i++;
        ce = i;
        while (ce > cs && s[ce - 1] == ' ') ce--;
        if (cs == ce && i >= len) break;
        mdp_buf_puts(out, "<"); mdp_buf_puts(out, tag); mdp_buf_puts(out, ">");
        if (cs < ce) {
            cell = kc_mdp_dup(s + cs, ce - cs);
            if (!cell) {
                mdp_buf_puts(out, "</tr>\n");
                return MDP_ERROR;
            }
            kc_mdp_inline(out, cell);
            free(cell);
        }
        mdp_buf_puts(out, "</"); mdp_buf_puts(out, tag); mdp_buf_puts(out, ">");
        if (i < len) i++;
    }
    mdp_buf_puts(out, "</tr>\n");
    return MDP_OK;
}

static void kc_mdp_open_code(mdp_buf_t *out, const char *line, size_t len) {
    size_t start = 3;
    size_t end;
    while (start < len && (line[start] == ' ' || line[start] == '\t')) start++;
    end = start;
    while (end < len && line[end] != ' ' && line[end] != '\t') end++;
    mdp_buf_puts(out, "<pre><code");
    if (end > start) {
        mdp_buf_puts(out, " class=\"language-");
        kc_mdp_escape(out, line + start, end - start);
        mdp_buf_putc(out, '"');
    }
    mdp_buf_putc(out, '>');
}

static int kc_mdp_render(mdp_buf_t *out, const char *body) {
    const char *line = body;
    char *par = kc_mdp_dup("", 0);
    int in_list = 0;
    int in_code = 0;
    int in_quote = 0;
    int in_table = 0;
    char *in_html = NULL;
    char *table_hdr = NULL;

    if (!par) return MDP_ERROR;

    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char *tmp;
        int reprocess;
        while (len && line[len - 1] == '\r') len--;
        tmp = kc_mdp_dup(line, len);
        if (!tmp) {
            free(table_hdr); free(in_html); free(par);
            return MDP_ERROR;
        }

        do {
            int level = 0;
            size_t ordered = 0;
            reprocess = 0;

            if (in_code) {
                if (len >= 3 && strncmp(tmp, "```", 3) == 0) {
                    mdp_buf_puts(out, "</code></pre>\n");
                    in_code = 0;
                } else {
                    kc_mdp_escape(out, tmp, len);
                    mdp_buf_putc(out, '\n');
                }
            } else if (in_html) {
                mdp_buf_write(out, tmp, len);
                mdp_buf_putc(out, '\n');
                if (kc_mdp_has_close(tmp, len, in_html, strlen(in_html))) {
                    free(in_html);
                    in_html = NULL;
                }
            } else if (!len) {
                if (in_table == 2) {
                    mdp_buf_puts(out, "</tbody>\n</table>\n");
                    in_table = 0;
                } else if (in_table == 1) {
                    if (kc_mdp_join(&par, table_hdr) != MDP_OK) {
                        free(tmp); free(table_hdr); free(par); return MDP_ERROR;
                    }
                    free(table_hdr); table_hdr = NULL; in_table = 0;
                    kc_mdp_flush(out, &par);
                    kc_mdp_close_blocks(out, &in_list, &in_quote);
                } else {
                    kc_mdp_flush(out, &par);
                    kc_mdp_close_blocks(out, &in_list, &in_quote);
                }
            } else if (in_table == 1 && kc_mdp_is_sep(tmp, len)) {
                kc_mdp_flush(out, &par);
                kc_mdp_close_blocks(out, &in_list, &in_quote);
                mdp_buf_puts(out, "<table>\n<thead>\n");
                if (kc_mdp_table_row(out, table_hdr, strlen(table_hdr), "th") != MDP_OK) {
                    free(tmp); free(table_hdr); free(par); return MDP_ERROR;
                }
                mdp_buf_puts(out, "</thead>\n<tbody>\n");
                free(table_hdr); table_hdr = NULL; in_table = 2;
            } else if (in_table == 2 && tmp[0] == '|') {
                if (kc_mdp_table_row(out, tmp, len, "td") != MDP_OK) {
                    free(tmp); free(par); return MDP_ERROR;
                }
            } else if (in_table == 2) {
                mdp_buf_puts(out, "</tbody>\n</table>\n");
                in_table = 0;
                reprocess = 1;
            } else if (in_table == 1) {
                if (kc_mdp_join(&par, table_hdr) != MDP_OK) {
                    free(tmp); free(table_hdr); free(par); return MDP_ERROR;
                }
                free(table_hdr); table_hdr = NULL; in_table = 0;
                kc_mdp_flush(out, &par);
                reprocess = 1;
            } else if (len >= 3 && strncmp(tmp, "```", 3) == 0) {
                kc_mdp_flush(out, &par);
                kc_mdp_close_blocks(out, &in_list, &in_quote);
                kc_mdp_open_code(out, tmp, len);
                in_code = 1;
            } else if (len == 3 && (strncmp(tmp, "---", 3) == 0 || strncmp(tmp, "***", 3) == 0)) {
                kc_mdp_flush(out, &par);
                kc_mdp_close_blocks(out, &in_list, &in_quote);
                mdp_buf_puts(out, "<hr>\n");
            } else {
                const char *html_name;
                size_t html_name_len = 0;
                html_name = kc_mdp_html_block_name(tmp, len, &html_name_len);
                if (html_name) {
                    kc_mdp_flush(out, &par);
                    kc_mdp_close_blocks(out, &in_list, &in_quote);
                    mdp_buf_write(out, tmp, len);
                    mdp_buf_putc(out, '\n');
                    in_html = kc_mdp_dup(html_name, html_name_len);
                    if (!in_html) { free(tmp); free(par); return MDP_ERROR; }
                    if (kc_mdp_has_close(tmp, len, html_name, html_name_len)) {
                        free(in_html); in_html = NULL;
                    }
                } else {
                    while (tmp[level] == '#' && level < 6) level++;
                    ordered = kc_mdp_ordered_item(tmp);
                    if (level && tmp[level] == ' ') {
                        kc_mdp_flush(out, &par);
                        kc_mdp_close_blocks(out, &in_list, &in_quote);
                        mdp_buf_puts(out, "<h"); mdp_buf_putc(out, (char)('0' + level)); mdp_buf_puts(out, ">");
                        kc_mdp_inline(out, tmp + level + 1);
                        mdp_buf_puts(out, "</h"); mdp_buf_putc(out, (char)('0' + level)); mdp_buf_puts(out, ">\n");
                    } else if (tmp[0] == '>' && tmp[1] == ' ') {
                        kc_mdp_flush(out, &par);
                        kc_mdp_close_list(out, &in_list);
                        if (!in_quote) { mdp_buf_puts(out, "<blockquote>\n"); in_quote = 1; }
                        mdp_buf_puts(out, "<p>"); kc_mdp_inline(out, tmp + 2); mdp_buf_puts(out, "</p>\n");
                    } else if ((tmp[0] == '-' || tmp[0] == '*') && tmp[1] == ' ') {
                        kc_mdp_flush(out, &par);
                        if (in_quote) { mdp_buf_puts(out, "</blockquote>\n"); in_quote = 0; }
                        if (in_list != 1) {
                            kc_mdp_close_list(out, &in_list);
                            mdp_buf_puts(out, "<ul>\n");
                            in_list = 1;
                        }
                        kc_mdp_list_item(out, tmp + 2, 1);
                    } else if (ordered) {
                        kc_mdp_flush(out, &par);
                        if (in_quote) { mdp_buf_puts(out, "</blockquote>\n"); in_quote = 0; }
                        if (in_list != 2) {
                            kc_mdp_close_list(out, &in_list);
                            mdp_buf_puts(out, "<ol>\n");
                            in_list = 2;
                        }
                        kc_mdp_list_item(out, tmp + ordered, 0);
                    } else if (tmp[0] == '|') {
                        kc_mdp_flush(out, &par);
                        kc_mdp_close_blocks(out, &in_list, &in_quote);
                        table_hdr = kc_mdp_dup(tmp, len);
                        if (!table_hdr) { free(tmp); free(par); return MDP_ERROR; }
                        in_table = 1;
                    } else {
                        kc_mdp_close_blocks(out, &in_list, &in_quote);
                        if (kc_mdp_join(&par, tmp) != MDP_OK) { free(tmp); free(par); return MDP_ERROR; }
                    }
                }
            }
        } while (reprocess);

        free(tmp);
        line = end ? end + 1 : line + len;
    }

    kc_mdp_flush(out, &par);
    kc_mdp_close_list(out, &in_list);
    if (in_quote) mdp_buf_puts(out, "</blockquote>\n");
    if (in_code) mdp_buf_puts(out, "</code></pre>\n");
    if (in_table == 2) mdp_buf_puts(out, "</tbody>\n</table>\n");
    else if (in_table == 1 && table_hdr && *table_hdr) {
        mdp_buf_puts(out, "<p>"); kc_mdp_inline(out, table_hdr); mdp_buf_puts(out, "</p>\n");
    }
    free(table_hdr);
    free(in_html);
    free(par);
    return MDP_OK;
}

int kc_mdp_open(kc_mdp_t **out, const char *input) {
    kc_mdp_t *mdp;
    char *meta = NULL;
    char *body = NULL;
    if (!out) return KC_MDP_ERROR;
    *out = NULL;
    if (!input) return KC_MDP_ERROR;
    if (kc_mdp_split(input, &meta, &body) != MDP_OK) return KC_MDP_ERROR;
    mdp = (kc_mdp_t *)calloc(1, sizeof(kc_mdp_t));
    if (!mdp) {
        free(meta); free(body); return KC_MDP_ERROR;
    }
    mdp->meta = meta;
    mdp->body = body;
    *out = mdp;
    return KC_MDP_OK;
}

const char *kc_mdp_html(kc_mdp_t *mdp) {
    mdp_buf_t buf;
    if (!mdp) return NULL;
    if (mdp->html) return mdp->html;
    memset(&buf, 0, sizeof(buf));
    if (kc_mdp_render(&buf, mdp->body) != MDP_OK || buf.oom) {
        free(buf.data);
        return NULL;
    }
    if (!buf.data) {
        buf.data = (unsigned char *)malloc(1);
        if (!buf.data) return NULL;
        buf.data[0] = '\0';
    }
    mdp->html = (char *)buf.data;
    return mdp->html;
}

const char *kc_mdp_body(const kc_mdp_t *mdp) {
    return mdp ? mdp->body : NULL;
}

const char *kc_mdp_meta(const kc_mdp_t *mdp) {
    return mdp ? mdp->meta : NULL;
}

void kc_mdp_close(kc_mdp_t *mdp) {
    if (!mdp) return;
    free(mdp->html);
    free(mdp->body);
    free(mdp->meta);
    free(mdp);
}

#ifndef KC_MDP_BUILD_VERSION
#define KC_MDP_BUILD_VERSION 0
#endif

uint64_t kc_mdp_version(void) {
    return (uint64_t)KC_MDP_BUILD_VERSION;
}
