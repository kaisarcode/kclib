/**
 * libmdp-html.c - Markdown HTML renderer
 * Summary: HTML projection backend for shared libmdp parsing.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define KC_MDP_PRIVATE
#include "libmdp.h"

/**
 * Writes HTML-escaped text.
 * @param out Target buffer.
 * @param text Source text.
 * @param len Byte count.
 * @return None.
 */
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

/**
 * Renders plain semantic text as escaped HTML.
 * @param out Target buffer.
 * @param text Source text.
 * @param len Byte count.
 * @return None.
 */
static void html_text(mdp_buf_t *out, const char *text, size_t len) {
    html_escape(out, text, len);
}

/**
 * Opens strong HTML markup.
 * @param out Target buffer.
 * @return None.
 */
static void html_strong_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "<strong>");
}

/**
 * Closes strong HTML markup.
 * @param out Target buffer.
 * @return None.
 */
static void html_strong_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "</strong>");
}

/**
 * Opens emphasis HTML markup.
 * @param out Target buffer.
 * @return None.
 */
static void html_em_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "<em>");
}

/**
 * Closes emphasis HTML markup.
 * @param out Target buffer.
 * @return None.
 */
static void html_em_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "</em>");
}

/**
 * Opens strike HTML markup.
 * @param out Target buffer.
 * @return None.
 */
static void html_strike_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "<del>");
}

/**
 * Closes strike HTML markup.
 * @param out Target buffer.
 * @return None.
 */
static void html_strike_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "</del>");
}

/**
 * Renders inline code as HTML.
 * @param out Target buffer.
 * @param text Code text.
 * @param len Byte count.
 * @return None.
 */
static void html_code_inline(mdp_buf_t *out, const char *text, size_t len) {
    kc_mdp_buf_puts(out, "<code>");
    html_escape(out, text, len);
    kc_mdp_buf_puts(out, "</code>");
}

/**
 * Opens an HTML hyperlink.
 * @param out Target buffer.
 * @param url Link destination.
 * @param len URL byte count.
 * @return None.
 */
static void html_link_open(mdp_buf_t *out, const char *url, size_t len) {
    kc_mdp_buf_puts(out, "<a href=\"");
    html_escape(out, url, len);
    kc_mdp_buf_puts(out, "\">");
}

/**
 * Closes an HTML hyperlink.
 * @param out Target buffer.
 * @return None.
 */
static void html_link_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "</a>");
}

/**
 * Renders an HTML image.
 * @param out Target buffer.
 * @param alt Alternative text.
 * @param alt_len Alternative text byte count.
 * @param url Image URL.
 * @param url_len URL byte count.
 * @return None.
 */
static void html_image(mdp_buf_t *out, const char *alt, size_t alt_len,
    const char *url, size_t url_len) {
    kc_mdp_buf_puts(out, "<img src=\"");
    html_escape(out, url, url_len);
    kc_mdp_buf_puts(out, "\" alt=\"");
    html_escape(out, alt, alt_len);
    kc_mdp_buf_puts(out, "\">");
}

/**
 * Passes inline raw HTML through unchanged.
 * @param out Target buffer.
 * @param text Raw HTML token.
 * @param len Byte count.
 * @return None.
 */
static void html_raw_inline(mdp_buf_t *out, const char *text, size_t len) {
    kc_mdp_buf_write(out, text, len);
}

/**
 * Opens an HTML paragraph.
 * @param out Target buffer.
 * @return None.
 */
static void html_paragraph_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "<p>");
}

/**
 * Closes an HTML paragraph.
 * @param out Target buffer.
 * @return None.
 */
static void html_paragraph_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "</p>\n");
}

/**
 * Opens an HTML heading.
 * @param out Target buffer.
 * @param level Heading level.
 * @return None.
 */
static void html_heading_open(mdp_buf_t *out, int level) {
    kc_mdp_buf_puts(out, "<h");
    kc_mdp_buf_putc(out, (char)('0' + level));
    kc_mdp_buf_putc(out, '>');
}

/**
 * Closes an HTML heading.
 * @param out Target buffer.
 * @param level Heading level.
 * @return None.
 */
static void html_heading_close(mdp_buf_t *out, int level) {
    kc_mdp_buf_puts(out, "</h");
    kc_mdp_buf_putc(out, (char)('0' + level));
    kc_mdp_buf_puts(out, ">\n");
}

/**
 * Opens an HTML blockquote.
 * @param out Target buffer.
 * @return None.
 */
static void html_quote_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "<blockquote>\n");
}

/**
 * Closes an HTML blockquote.
 * @param out Target buffer.
 * @return None.
 */
static void html_quote_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "</blockquote>\n");
}

/**
 * Opens an HTML list.
 * @param out Target buffer.
 * @param ordered Non-zero for ordered lists.
 * @return None.
 */
static void html_list_open(mdp_buf_t *out, int ordered) {
    kc_mdp_buf_puts(out, ordered ? "<ol>\n" : "<ul>\n");
}

/**
 * Closes an HTML list.
 * @param out Target buffer.
 * @param ordered Non-zero for ordered lists.
 * @return None.
 */
static void html_list_close(mdp_buf_t *out, int ordered) {
    kc_mdp_buf_puts(out, ordered ? "</ol>\n" : "</ul>\n");
}

/**
 * Opens an HTML list item and optional task checkbox.
 * @param out Target buffer.
 * @param ordered Non-zero for ordered lists.
 * @param number Source ordered item number.
 * @param task_state -1 normal, 0 unchecked, 1 checked.
 * @return None.
 */
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

/**
 * Closes an HTML list item.
 * @param out Target buffer.
 * @return None.
 */
static void html_list_item_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "</li>\n");
}

/**
 * Opens an HTML fenced code block.
 * @param out Target buffer.
 * @param language Optional language identifier.
 * @param len Language byte count.
 * @return None.
 */
static void html_code_open(mdp_buf_t *out, const char *language, size_t len) {
    kc_mdp_buf_puts(out, "<pre><code");
    if (len) {
        kc_mdp_buf_puts(out, " class=\"language-");
        html_escape(out, language, len);
        kc_mdp_buf_putc(out, '"');
    }
    kc_mdp_buf_putc(out, '>');
}

/**
 * Renders one escaped HTML code line.
 * @param out Target buffer.
 * @param text Code text.
 * @param len Byte count.
 * @return None.
 */
static void html_code_text(mdp_buf_t *out, const char *text, size_t len) {
    html_escape(out, text, len);
    kc_mdp_buf_putc(out, '\n');
}

/**
 * Closes an HTML fenced code block.
 * @param out Target buffer.
 * @return None.
 */
static void html_code_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "</code></pre>\n");
}

/**
 * Renders an HTML horizontal rule.
 * @param out Target buffer.
 * @return None.
 */
static void html_horizontal_rule(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "<hr>\n");
}

/**
 * Opens an HTML table.
 * @param out Target buffer.
 * @return None.
 */
static void html_table_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "<table>\n");
}

/**
 * Closes an HTML table.
 * @param out Target buffer.
 * @return None.
 */
static void html_table_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "</tbody>\n</table>\n");
}

/**
 * Opens an HTML table row.
 * @param out Target buffer.
 * @param header Non-zero for the header row.
 * @return None.
 */
static void html_table_row_open(mdp_buf_t *out, int header) {
    if (header) {
        kc_mdp_buf_puts(out, "<thead>\n");
    }
    kc_mdp_buf_puts(out, "<tr>");
}

/**
 * Closes an HTML table row.
 * @param out Target buffer.
 * @param header Non-zero for the header row.
 * @return None.
 */
static void html_table_row_close(mdp_buf_t *out, int header) {
    kc_mdp_buf_puts(out, "</tr>\n");
    if (header) {
        kc_mdp_buf_puts(out, "</thead>\n<tbody>\n");
    }
}

/**
 * Opens an HTML table cell.
 * @param out Target buffer.
 * @param header Non-zero for header cells.
 * @param first Non-zero for the first cell.
 * @return None.
 */
static void html_table_cell_open(mdp_buf_t *out, int header, int first) {
    (void)first;
    kc_mdp_buf_puts(out, header ? "<th>" : "<td>");
}

/**
 * Closes an HTML table cell.
 * @param out Target buffer.
 * @param header Non-zero for header cells.
 * @return None.
 */
static void html_table_cell_close(mdp_buf_t *out, int header) {
    kc_mdp_buf_puts(out, header ? "</th>" : "</td>");
}

/**
 * Passes a raw HTML block line through unchanged.
 * @param out Target buffer.
 * @param text Raw HTML line.
 * @param len Byte count.
 * @return None.
 */
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

/**
 * Renders Markdown through the HTML backend.
 * @param body Markdown body text.
 * @param out Target output buffer.
 * @return MDP_OK on success, MDP_ERROR on allocation failure.
 */
int kc_mdp_render_html(const char *body, mdp_buf_t *out) {
    return kc_mdp_parse(body, out, &renderer);
}
