/**
 * libmdp-ansi.c - Markdown ANSI renderer
 * Summary: ANSI terminal projection backend for shared libmdp parsing.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define KC_MDP_PRIVATE
#include "libmdp.h"
#include "style.h"

#include <stdio.h>

/**
 * Writes terminal text without HTML escaping.
 * @param out Target buffer.
 * @param text Source text.
 * @param len Byte count.
 * @return None.
 */
static void ansi_text(mdp_buf_t *out, const char *text, size_t len) {
    kc_mdp_buf_write(out, text, len);
}

/**
 * Opens bold terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_strong_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_STRONG_OPEN);
}

/**
 * Closes bold terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_strong_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_STRONG_CLOSE);
}

/**
 * Opens italic terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_em_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_EM_OPEN);
}

/**
 * Closes italic terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_em_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_EM_CLOSE);
}

/**
 * Opens strike terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_strike_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_STRIKE_OPEN);
}

/**
 * Closes strike terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_strike_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_STRIKE_CLOSE);
}

/**
 * Renders inline code using the configured terminal style.
 * @param out Target buffer.
 * @param text Code text.
 * @param len Byte count.
 * @return None.
 */
static void ansi_code_inline(mdp_buf_t *out, const char *text, size_t len) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_CODE_INLINE_OPEN);
    kc_mdp_buf_write(out, text, len);
    kc_mdp_buf_puts(out, KC_MDP_STYLE_CODE_INLINE_CLOSE);
}

/**
 * Opens an OSC 8 terminal hyperlink.
 * @param out Target buffer.
 * @param url Link destination.
 * @param len URL byte count.
 * @return None.
 */
static void ansi_link_open(mdp_buf_t *out, const char *url, size_t len) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_LINK_OPEN);
    kc_mdp_buf_puts(out, "\x1b]8;;");
    kc_mdp_buf_write(out, url, len);
    kc_mdp_buf_puts(out, "\x1b\\");
}

/**
 * Closes an OSC 8 terminal hyperlink.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_link_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, "\x1b]8;;\x1b\\");
    kc_mdp_buf_puts(out, KC_MDP_STYLE_LINK_CLOSE);
}

/**
 * Renders an image as accessible terminal alt text.
 * @param out Target buffer.
 * @param alt Image alternative text.
 * @param alt_len Alternative text byte count.
 * @param url Image source URL.
 * @param url_len URL byte count.
 * @return None.
 */
static void ansi_image(mdp_buf_t *out, const char *alt, size_t alt_len,
    const char *url, size_t url_len) {
    (void)url;
    (void)url_len;
    kc_mdp_buf_puts(out, "[image: ");
    kc_mdp_buf_write(out, alt, alt_len);
    kc_mdp_buf_putc(out, ']');
}

/**
 * Emits visible text from inline raw HTML while omitting tags.
 * @param out Target buffer.
 * @param text Raw HTML token.
 * @param len Byte count.
 * @return None.
 */
static void ansi_raw_inline(mdp_buf_t *out, const char *text, size_t len) {
    size_t i;
    int tag = 0;

    for (i = 0; i < len; i++) {
        if (text[i] == '<') {
            tag = 1;
        } else if (text[i] == '>') {
            tag = 0;
        } else if (!tag) {
            kc_mdp_buf_putc(out, text[i]);
        }
    }
}

/**
 * Opens a terminal paragraph.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_paragraph_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_COLOR_TEXT);
}

/**
 * Closes a terminal paragraph.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_paragraph_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_COLOR_RESET);
    kc_mdp_buf_puts(out, KC_MDP_STYLE_PARAGRAPH_GAP);
}

/**
 * Opens a terminal heading style.
 * @param out Target buffer.
 * @param level Heading level.
 * @return None.
 */
static void ansi_heading_open(mdp_buf_t *out, int level) {
    (void)level;
    kc_mdp_buf_puts(out, KC_MDP_STYLE_HEADING_OPEN);
}

/**
 * Closes a terminal heading style.
 * @param out Target buffer.
 * @param level Heading level.
 * @return None.
 */
static void ansi_heading_close(mdp_buf_t *out, int level) {
    (void)level;
    kc_mdp_buf_puts(out, KC_MDP_STYLE_HEADING_CLOSE);
    kc_mdp_buf_puts(out, KC_MDP_STYLE_HEADING_GAP);
}

/**
 * Opens a terminal blockquote.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_quote_open(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_QUOTE_OPEN);
}

/**
 * Closes a terminal blockquote.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_quote_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_QUOTE_CLOSE);
    kc_mdp_buf_puts(out, KC_MDP_STYLE_QUOTE_GAP);
}

/**
 * Opens a terminal list.
 * @param out Target buffer.
 * @param ordered Non-zero for ordered lists.
 * @return None.
 */
static void ansi_list_open(mdp_buf_t *out, int ordered) {
    (void)out;
    (void)ordered;
}

/**
 * Closes a terminal list.
 * @param out Target buffer.
 * @param ordered Non-zero for ordered lists.
 * @return None.
 */
static void ansi_list_close(mdp_buf_t *out, int ordered) {
    (void)ordered;
    kc_mdp_buf_puts(out, KC_MDP_STYLE_LIST_GAP);
}

/**
 * Opens a terminal list item with its semantic marker.
 * @param out Target buffer.
 * @param ordered Non-zero for ordered lists.
 * @param number Ordered item number.
 * @param task_state -1 normal, 0 unchecked, 1 checked.
 * @return None.
 */
static void ansi_list_item_open(mdp_buf_t *out, int ordered,
    unsigned int number, int task_state) {
    char number_buf[32];

    if (task_state == 0) {
        kc_mdp_buf_puts(out, KC_MDP_STYLE_TASK_OFF);
    } else if (task_state == 1) {
        kc_mdp_buf_puts(out, KC_MDP_STYLE_TASK_ON);
    } else if (ordered) {
        snprintf(number_buf, sizeof(number_buf), "  %u. ", number);
        kc_mdp_buf_puts(out, number_buf);
    } else {
        kc_mdp_buf_puts(out, KC_MDP_STYLE_LIST_BULLET);
    }
}

/**
 * Closes a terminal list item.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_list_item_close(mdp_buf_t *out) {
    kc_mdp_buf_putc(out, '\n');
}

/**
 * Opens a terminal fenced code block.
 * @param out Target buffer.
 * @param language Optional language identifier.
 * @param len Language byte count.
 * @return None.
 */
static void ansi_code_open(mdp_buf_t *out, const char *language, size_t len) {
    (void)language;
    (void)len;
    kc_mdp_buf_puts(out, KC_MDP_STYLE_CODE_BLOCK_OPEN);
}

/**
 * Renders one terminal code line.
 * @param out Target buffer.
 * @param text Code text.
 * @param len Byte count.
 * @return None.
 */
static void ansi_code_text(mdp_buf_t *out, const char *text, size_t len) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_CODE_INDENT);
    kc_mdp_buf_write(out, text, len);
    kc_mdp_buf_putc(out, '\n');
}

/**
 * Closes a terminal code block.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_code_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_CODE_BLOCK_CLOSE);
    kc_mdp_buf_puts(out, KC_MDP_STYLE_CODE_BLOCK_GAP);
}

/**
 * Renders a terminal horizontal rule.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_horizontal_rule(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_HORIZONTAL_RULE);
    kc_mdp_buf_puts(out, KC_MDP_STYLE_HORIZONTAL_RULE_GAP);
}

/**
 * Opens a terminal table.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_table_open(mdp_buf_t *out) {
    (void)out;
}

/**
 * Closes a terminal table.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_table_close(mdp_buf_t *out) {
    kc_mdp_buf_puts(out, KC_MDP_STYLE_TABLE_GAP);
}

/**
 * Opens a terminal table row.
 * @param out Target buffer.
 * @param header Non-zero for header rows.
 * @return None.
 */
static void ansi_table_row_open(mdp_buf_t *out, int header) {
    if (header) {
        kc_mdp_buf_puts(out, KC_MDP_STYLE_TABLE_HEADER_OPEN);
    }
}

/**
 * Closes a terminal table row.
 * @param out Target buffer.
 * @param header Non-zero for header rows.
 * @return None.
 */
static void ansi_table_row_close(mdp_buf_t *out, int header) {
    if (header) {
        kc_mdp_buf_puts(out, KC_MDP_STYLE_TABLE_HEADER_CLOSE);
    }
    kc_mdp_buf_putc(out, '\n');
}

/**
 * Opens a terminal table cell.
 * @param out Target buffer.
 * @param header Non-zero for header cells.
 * @param first Non-zero for the first cell in a row.
 * @return None.
 */
static void ansi_table_cell_open(mdp_buf_t *out, int header, int first) {
    (void)header;
    if (!first) {
        kc_mdp_buf_puts(out, KC_MDP_STYLE_TABLE_SEPARATOR);
    }
}

/**
 * Closes a terminal table cell.
 * @param out Target buffer.
 * @param header Non-zero for header cells.
 * @return None.
 */
static void ansi_table_cell_close(mdp_buf_t *out, int header) {
    (void)out;
    (void)header;
}

/**
 * Emits visible content from a raw HTML block.
 * @param out Target buffer.
 * @param text Raw HTML line.
 * @param len Byte count.
 * @return None.
 */
static void ansi_raw_block(mdp_buf_t *out, const char *text, size_t len) {
    ansi_raw_inline(out, text, len);
    kc_mdp_buf_putc(out, '\n');
}

static const kc_mdp_renderer_t renderer = {
    ansi_text,
    ansi_strong_open,
    ansi_strong_close,
    ansi_em_open,
    ansi_em_close,
    ansi_strike_open,
    ansi_strike_close,
    ansi_code_inline,
    ansi_link_open,
    ansi_link_close,
    ansi_image,
    ansi_raw_inline,
    ansi_paragraph_open,
    ansi_paragraph_close,
    ansi_heading_open,
    ansi_heading_close,
    ansi_quote_open,
    ansi_quote_close,
    ansi_list_open,
    ansi_list_close,
    ansi_list_item_open,
    ansi_list_item_close,
    ansi_code_open,
    ansi_code_text,
    ansi_code_close,
    ansi_horizontal_rule,
    ansi_table_open,
    ansi_table_close,
    ansi_table_row_open,
    ansi_table_row_close,
    ansi_table_cell_open,
    ansi_table_cell_close,
    ansi_raw_block
};

/**
 * Renders Markdown through the ANSI terminal backend.
 * @param body Markdown body text.
 * @param out Target output buffer.
 * @return MDP_OK on success, MDP_ERROR on allocation failure.
 */
int kc_mdp_render_ansi(const char *body, mdp_buf_t *out) {
    return kc_mdp_parse(body, out, &renderer);
}
