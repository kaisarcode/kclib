#include "libmdp_internal.h"

#include <stdlib.h>
#include <string.h>

static void html_escape(mdp_buf_t *out, const char *text, size_t len) {
    size_t i;

    for (i = 0; i < len; i++) {
        if (text[i] == '&') {
            kc_mdp_buf_puts(out, "&amp;");
        } else if (text[i] == '<') {
            kc_mdp_buf_puts(out, "&lt;");
        } else if (text[i] == '>') {
            kc_mdp_buf_puts(out, "&gt;");
        } else if (text[i] == '"') {
            kc_mdp_buf_puts(out, "&quot;");
        } else {
            kc_mdp_buf_putc(out, text[i]);
        }
    }
}

static void html_text(mdp_buf_t *out, const char *text, size_t len) {
    html_escape(out, text, len);
}

static void html_strong_open(mdp_buf_t *out) { kc_mdp_buf_puts(out, "<strong>"); }
static void html_strong_close(mdp_buf_t *out) { kc_mdp_buf_puts(out, "</strong>"); }
static void html_em_open(mdp_buf_t *out) { kc_mdp_buf_puts(out, "<em>"); }
static void html_em_close(mdp_buf_t *out) { kc_mdp_buf_puts(out, "</em>"); }
static void html_strike_open(mdp_buf_t *out) { kc_mdp_buf_puts(out, "<del>"); }
static void html_strike_close(mdp_buf_t *out) { kc_mdp_buf_puts(out, "</del>"); }

static void html_code_inline(mdp_buf_t *out, const char *text, size_t len) {
    kc_mdp_buf_puts(out, "<code>");
    html_escape(out, text, len);
    kc_mdp_buf_puts(out, "</code>");
}

static void html_link_open(mdp_buf_t *out, const char *url, size_t len) {
    kc_mdp_buf_puts(out, "<a href=\"");
    html_escape(out, url, len);
    kc_mdp_buf_puts(out, "\">");
}

static void html_link_close(mdp_buf_t *out) { kc_mdp_buf_puts(out, "</a>"); }

static void html_image(mdp_buf_t *out, const char *alt, size_t alt_len,
    const char *url, size_t url_len) {
    kc_mdp_buf_puts(out, "<img src=\"");
    html_escape(out, url, url_len);
    kc_mdp_buf_puts(out, "\" alt=\"");
    html_escape(out, alt, alt_len);
    kc_mdp_buf_puts(out, "\">");
}

static void html_raw_inline(mdp_buf_t *out, const char *text, size_t len) {
    kc_mdp_buf_write(out, text, len);
}

static void html_paragraph_open(mdp_buf_t *out) { kc_mdp_buf_puts(out, "<p>"); }
static void html_paragraph_close(mdp_buf_t *out) { kc_mdp_buf_puts(out, "</p>\n"); }

static void html_heading_open(mdp_buf_t *out, int level) {
    kc_mdp_buf_puts(out, "<h");
    kc_mdp_buf_putc(out, (char)('0' + level));
    kc_mdp_buf_putc(out, '>');
}

static void html_heading_close(mdp_buf_t *out, int level) {
    kc_mdp_buf_puts(out, "</h");
    kc_mdp_buf_putc(out, (char)('0' + level));
    kc_mdp_buf_puts(out, ">\n");
}

static void html_quote_open(mdp_buf_t *out) { kc_mdp_buf_puts(out, "<blockquote>\n"); }
static void html_quote_close(mdp_buf_t *out) { kc_mdp_buf_puts(out, "</blockquote>\n"); }

static void html_list_open(mdp_buf_t *out, int ordered) {
    kc_mdp_buf_puts(out, ordered ? "<ol>\n" : "<ul>\n");
}

static void html_list_close(mdp_buf_t *out, int ordered) {
    kc_mdp_buf_puts(out, ordered ? "</ol>\n" : "</ul>\n");
}

static void html_list_item_open(mdp_buf_t *out, int ordered,
    unsigned int number, int task_state) {
    (void)ordered;
    (void)number;
    kc_mdp_buf_puts(out, "<li>");
    if (task_state >= 0) {
        kc_mdp_buf_puts(out, "<input type=\"checkbox\" disabled");
        if (task_state) {
            kc_mdp_buf_puts(out, " checked");
        }
        kc_mdp_buf_puts(out, "> ");
    }
}

static void html_list_item_close(mdp_buf_t *out) { kc_mdp_buf_puts(out, "</li>\n"); }

static void html_code_open(mdp_buf_t *out, const char *language, size_t len) {
    kc_mdp_buf_puts(out, "<pre><code");
    if (len) {
        kc_mdp_buf_puts(out, " class=\"language-");
        html_escape(out, language, len);
        kc_mdp_buf_putc(out, '"');
    }
    kc_mdp_buf_putc(out, '>');
}

static void html_code_text(mdp_buf_t *out, const char *text, size_t len) {
    html_escape(out, text, len);
    kc_mdp_buf_putc(out, '\n');
}

static void html_code_close(mdp_buf_t *out) { kc_mdp_buf_puts(out, "</code></pre>\n"); }
static void html_horizontal_rule(mdp_buf_t *out) { kc_mdp_buf_puts(out, "<hr>\n"); }
static void html_table_open(mdp_buf_t *out) { kc_mdp_buf_puts(out, "<table>\n"); }
static void html_table_close(mdp_buf_t *out) { kc_mdp_buf_puts(out, "</tbody>\n</table>\n"); }

static void html_table_row_open(mdp_buf_t *out, int header) {
    if (header) {
        kc_mdp_buf_puts(out, "<thead>\n");
    }
    kc_mdp_buf_puts(out, "<tr>");
}

static void html_table_row_close(mdp_buf_t *out, int header) {
    kc_mdp_buf_puts(out, "</tr>\n");
    if (header) {
        kc_mdp_buf_puts(out, "</thead>\n<tbody>\n");
    }
}

static void html_table_cell_open(mdp_buf_t *out, int header, int first) {
    (void)first;
    kc_mdp_buf_puts(out, header ? "<th>" : "<td>");
}

static void html_table_cell_close(mdp_buf_t *out, int header) {
    kc_mdp_buf_puts(out, header ? "</th>" : "</td>");
}

static void html_raw_block(mdp_buf_t *out, const char *text, size_t len) {
    kc_mdp_buf_write(out, text, len);
    kc_mdp_buf_putc(out, '\n');
}

static const kc_mdp_renderer_t renderer = {
    html_text,
    html_strong_open,
    html_strong_close,
    html_em_open,
    html_em_close,
    html_strike_open,
    html_strike_close,
    html_code_inline,
    html_link_open,
    html_link_close,
    html_image,
    html_raw_inline,
    html_paragraph_open,
    html_paragraph_close,
    html_heading_open,
    html_heading_close,
    html_quote_open,
    html_quote_close,
    html_list_open,
    html_list_close,
    html_list_item_open,
    html_list_item_close,
    html_code_open,
    html_code_text,
    html_code_close,
    html_horizontal_rule,
    html_table_open,
    html_table_close,
    html_table_row_open,
    html_table_row_close,
    html_table_cell_open,
    html_table_cell_close,
    html_raw_block
};

int kc_mdp_render_html(const char *body, mdp_buf_t *out) {
    return kc_mdp_parse(body, out, &renderer);
}
