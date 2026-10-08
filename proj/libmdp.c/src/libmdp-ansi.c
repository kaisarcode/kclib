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
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *text;
    size_t width;
} ansi_cell_t;

typedef struct {
    ansi_cell_t *cells;
    size_t count;
    size_t cap;
    int header;
} ansi_row_t;

typedef struct {
    ansi_row_t *rows;
    size_t count;
    size_t cap;
    ansi_row_t row;
    mdp_buf_t cell;
    int capture;
} ansi_state_t;

/**
 * Returns ANSI renderer state attached to an output buffer.
 * @param out Target buffer.
 * @return Renderer state pointer.
 */
static ansi_state_t *ansi_state(mdp_buf_t *out) {
    return (ansi_state_t *)out->renderer_data;
}

/**
 * Returns the active destination for normal text writes.
 * @param out Target buffer.
 * @return Main output or current table-cell buffer.
 */
static mdp_buf_t *ansi_target(mdp_buf_t *out) {
    ansi_state_t *state = ansi_state(out);

    if (state && state->capture) {
        return &state->cell;
    }
    return out;
}

/**
 * Writes bytes to the active ANSI destination.
 * @param out Target buffer.
 * @param data Bytes to write.
 * @param len Byte count.
 * @return None.
 */
static void ansi_write(mdp_buf_t *out, const void *data, size_t len) {
    mdp_buf_t *target = ansi_target(out);

    kc_mdp_buf_write(target, data, len);
    if (target->oom) {
        out->oom = 1;
    }
}

/**
 * Writes a string to the active ANSI destination.
 * @param out Target buffer.
 * @param text String to write.
 * @return None.
 */
static void ansi_puts(mdp_buf_t *out, const char *text) {
    if (text) {
        ansi_write(out, text, strlen(text));
    }
}

/**
 * Writes one byte to the active ANSI destination.
 * @param out Target buffer.
 * @param c Byte to write.
 * @return None.
 */
static void ansi_putc(mdp_buf_t *out, char c) {
    ansi_write(out, &c, 1);
}

/**
 * Measures visible terminal width while skipping ANSI escapes.
 * UTF-8 continuation bytes do not consume additional columns.
 * @param text ANSI-decorated cell text.
 * @return Approximate terminal column width.
 */
static size_t ansi_visible_width(const char *text) {
    const unsigned char *s = (const unsigned char *)text;
    size_t width = 0;
    size_t i = 0;

    while (s[i]) {
        if (s[i] == 0x1b && s[i + 1] == '[') {
            i += 2;
            while (s[i] && (s[i] < 0x40 || s[i] > 0x7e)) {
                i++;
            }
            if (s[i]) i++;
            continue;
        }
        if (s[i] == 0x1b && s[i + 1] == ']') {
            i += 2;
            while (s[i]) {
                if (s[i] == 0x07) {
                    i++;
                    break;
                }
                if (s[i] == 0x1b && s[i + 1] == '\\') {
                    i += 2;
                    break;
                }
                i++;
            }
            continue;
        }
        if ((s[i] & 0xc0) != 0x80) {
            width++;
        }
        i++;
    }
    return width;
}

/**
 * Releases all cells owned by one stored table row.
 * @param row Row to release.
 * @return None.
 */
static void ansi_row_free(ansi_row_t *row) {
    size_t i;

    for (i = 0; i < row->count; i++) {
        free(row->cells[i].text);
    }
    free(row->cells);
    memset(row, 0, sizeof(*row));
}

/**
 * Releases all temporary table layout state.
 * @param state Renderer state.
 * @return None.
 */
static void ansi_table_reset(ansi_state_t *state) {
    size_t i;

    for (i = 0; i < state->count; i++) {
        ansi_row_free(&state->rows[i]);
    }
    free(state->rows);
    ansi_row_free(&state->row);
    free(state->cell.data);
    memset(state, 0, sizeof(*state));
}

/**
 * Adds the current captured cell to the current table row.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_store_cell(mdp_buf_t *out) {
    ansi_state_t *state = ansi_state(out);
    ansi_cell_t *grown;
    char *text;

    if (!state || out->oom) {
        return;
    }
    if (state->row.count == state->row.cap) {
        size_t cap = state->row.cap ? state->row.cap * 2 : 4;

        grown = (ansi_cell_t *)realloc(state->row.cells,
            cap * sizeof(*grown));
        if (!grown) {
            out->oom = 1;
            return;
        }
        state->row.cells = grown;
        state->row.cap = cap;
    }
    if (state->cell.data) {
        text = (char *)state->cell.data;
    } else {
        text = (char *)malloc(1);
        if (!text) {
            out->oom = 1;
            return;
        }
        text[0] = '\0';
    }
    state->row.cells[state->row.count].text = text;
    state->row.cells[state->row.count].width = ansi_visible_width(text);
    state->row.count++;
    memset(&state->cell, 0, sizeof(state->cell));
}

/**
 * Adds the current completed row to the stored table.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_store_row(mdp_buf_t *out) {
    ansi_state_t *state = ansi_state(out);
    ansi_row_t *grown;

    if (!state || out->oom) {
        return;
    }
    if (state->count == state->cap) {
        size_t cap = state->cap ? state->cap * 2 : 4;

        grown = (ansi_row_t *)realloc(state->rows,
            cap * sizeof(*grown));
        if (!grown) {
            out->oom = 1;
            return;
        }
        state->rows = grown;
        state->cap = cap;
    }
    state->rows[state->count++] = state->row;
    memset(&state->row, 0, sizeof(state->row));
}

/**
 * Writes spaces directly to the main output buffer.
 * @param out Target buffer.
 * @param count Number of spaces.
 * @return None.
 */
static void ansi_pad(mdp_buf_t *out, size_t count) {
    while (count--) {
        kc_mdp_buf_putc(out, ' ');
    }
}

/**
 * Renders all stored table rows with aligned column widths.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_render_table(mdp_buf_t *out) {
    ansi_state_t *state = ansi_state(out);
    size_t *widths;
    size_t columns = 0;
    size_t r;
    size_t c;

    if (!state || !state->count || out->oom) {
        return;
    }
    for (r = 0; r < state->count; r++) {
        if (state->rows[r].count > columns) {
            columns = state->rows[r].count;
        }
    }
    widths = (size_t *)calloc(columns, sizeof(*widths));
    if (!widths) {
        out->oom = 1;
        return;
    }
    for (r = 0; r < state->count; r++) {
        for (c = 0; c < state->rows[r].count; c++) {
            if (state->rows[r].cells[c].width > widths[c]) {
                widths[c] = state->rows[r].cells[c].width;
            }
        }
    }
    for (r = 0; r < state->count; r++) {
        ansi_row_t *row = &state->rows[r];

        if (row->header) {
            kc_mdp_buf_puts(out, KC_MDP_STYLE_TABLE_HEADER_OPEN);
        }
        for (c = 0; c < columns; c++) {
            size_t width = 0;

            if (c < row->count) {
                kc_mdp_buf_puts(out, row->cells[c].text);
                width = row->cells[c].width;
            }
            ansi_pad(out, widths[c] - width);
            if (c + 1 < columns) {
                kc_mdp_buf_puts(out, KC_MDP_STYLE_TABLE_SEPARATOR);
            }
        }
        if (row->header) {
            kc_mdp_buf_puts(out, KC_MDP_STYLE_TABLE_HEADER_CLOSE);
        }
        kc_mdp_buf_putc(out, '\n');
    }
    free(widths);
}

/**
 * Writes terminal text without HTML escaping.
 * @param out Target buffer.
 * @param text Source text.
 * @param len Byte count.
 * @return None.
 */
static void ansi_text(mdp_buf_t *out, const char *text, size_t len) {
    ansi_write(out, text, len);
}

/**
 * Opens bold terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_strong_open(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_STRONG_OPEN);
}

/**
 * Closes bold terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_strong_close(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_STRONG_CLOSE);
}

/**
 * Opens italic terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_em_open(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_EM_OPEN);
}

/**
 * Closes italic terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_em_close(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_EM_CLOSE);
}

/**
 * Opens strike terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_strike_open(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_STRIKE_OPEN);
}

/**
 * Closes strike terminal styling.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_strike_close(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_STRIKE_CLOSE);
}

/**
 * Renders inline code using the configured terminal style.
 * @param out Target buffer.
 * @param text Code text.
 * @param len Byte count.
 * @return None.
 */
static void ansi_code_inline(mdp_buf_t *out, const char *text, size_t len) {
    ansi_puts(out, KC_MDP_STYLE_CODE_INLINE_OPEN);
    ansi_write(out, text, len);
    ansi_puts(out, KC_MDP_STYLE_CODE_INLINE_CLOSE);
}

/**
 * Opens an OSC 8 terminal hyperlink.
 * @param out Target buffer.
 * @param url Link destination.
 * @param len URL byte count.
 * @return None.
 */
static void ansi_link_open(mdp_buf_t *out, const char *url, size_t len) {
    ansi_puts(out, KC_MDP_STYLE_LINK_OPEN);
    ansi_puts(out, "\x1b]8;;");
    ansi_write(out, url, len);
    ansi_puts(out, "\x1b\\");
}

/**
 * Closes an OSC 8 terminal hyperlink.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_link_close(mdp_buf_t *out) {
    ansi_puts(out, "\x1b]8;;\x1b\\");
    ansi_puts(out, KC_MDP_STYLE_LINK_CLOSE);
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
    ansi_puts(out, "[image: ");
    ansi_write(out, alt, alt_len);
    ansi_putc(out, ']');
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
            ansi_putc(out, text[i]);
        }
    }
}

/**
 * Opens a terminal paragraph.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_paragraph_open(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_COLOR_TEXT);
}

/**
 * Closes a terminal paragraph.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_paragraph_close(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_COLOR_RESET);
    ansi_puts(out, KC_MDP_STYLE_PARAGRAPH_GAP);
}

/**
 * Opens a terminal heading style for one Markdown level.
 * @param out Target buffer.
 * @param level Heading level.
 * @return None.
 */
static void ansi_heading_open(mdp_buf_t *out, int level) {
    const char *style;

    switch (level) {
        case 1: style = KC_MDP_STYLE_H1_OPEN; break;
        case 2: style = KC_MDP_STYLE_H2_OPEN; break;
        case 3: style = KC_MDP_STYLE_H3_OPEN; break;
        case 4: style = KC_MDP_STYLE_H4_OPEN; break;
        case 5: style = KC_MDP_STYLE_H5_OPEN; break;
        default: style = KC_MDP_STYLE_H6_OPEN; break;
    }
    ansi_puts(out, style);
}

/**
 * Closes a terminal heading style for one Markdown level.
 * @param out Target buffer.
 * @param level Heading level.
 * @return None.
 */
static void ansi_heading_close(mdp_buf_t *out, int level) {
    const char *style;

    switch (level) {
        case 1: style = KC_MDP_STYLE_H1_CLOSE; break;
        case 2: style = KC_MDP_STYLE_H2_CLOSE; break;
        case 3: style = KC_MDP_STYLE_H3_CLOSE; break;
        case 4: style = KC_MDP_STYLE_H4_CLOSE; break;
        case 5: style = KC_MDP_STYLE_H5_CLOSE; break;
        default: style = KC_MDP_STYLE_H6_CLOSE; break;
    }
    ansi_puts(out, style);
    ansi_puts(out, KC_MDP_STYLE_HEADING_GAP);
}

/**
 * Opens a terminal blockquote.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_quote_open(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_QUOTE_OPEN);
}

/**
 * Closes a terminal blockquote.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_quote_close(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_QUOTE_CLOSE);
    ansi_puts(out, KC_MDP_STYLE_QUOTE_GAP);
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
    ansi_puts(out, KC_MDP_STYLE_LIST_GAP);
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
        ansi_puts(out, KC_MDP_STYLE_TASK_OFF);
    } else if (task_state == 1) {
        ansi_puts(out, KC_MDP_STYLE_TASK_ON);
    } else if (ordered) {
        snprintf(number_buf, sizeof(number_buf), "  %u. ", number);
        ansi_puts(out, number_buf);
    } else {
        ansi_puts(out, KC_MDP_STYLE_LIST_BULLET);
    }
}

/**
 * Closes a terminal list item.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_list_item_close(mdp_buf_t *out) {
    ansi_putc(out, '\n');
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
    ansi_puts(out, KC_MDP_STYLE_CODE_BLOCK_OPEN);
}

/**
 * Renders one terminal code line.
 * @param out Target buffer.
 * @param text Code text.
 * @param len Byte count.
 * @return None.
 */
static void ansi_code_text(mdp_buf_t *out, const char *text, size_t len) {
    ansi_puts(out, KC_MDP_STYLE_CODE_INDENT);
    ansi_write(out, text, len);
    ansi_putc(out, '\n');
}

/**
 * Closes a terminal code block.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_code_close(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_CODE_BLOCK_CLOSE);
    ansi_puts(out, KC_MDP_STYLE_CODE_BLOCK_GAP);
}

/**
 * Renders a terminal horizontal rule.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_horizontal_rule(mdp_buf_t *out) {
    ansi_puts(out, KC_MDP_STYLE_HORIZONTAL_RULE);
    ansi_puts(out, KC_MDP_STYLE_HORIZONTAL_RULE_GAP);
}

/**
 * Starts collecting a terminal table for aligned rendering.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_table_open(mdp_buf_t *out) {
    ansi_state_t *state = ansi_state(out);

    if (state) {
        ansi_table_reset(state);
        out->renderer_data = state;
    }
}

/**
 * Renders and releases one collected terminal table.
 * @param out Target buffer.
 * @return None.
 */
static void ansi_table_close(mdp_buf_t *out) {
    ansi_state_t *state = ansi_state(out);

    ansi_render_table(out);
    kc_mdp_buf_puts(out, KC_MDP_STYLE_TABLE_GAP);
    if (state) {
        ansi_table_reset(state);
        out->renderer_data = state;
    }
}

/**
 * Starts collecting one terminal table row.
 * @param out Target buffer.
 * @param header Non-zero for header rows.
 * @return None.
 */
static void ansi_table_row_open(mdp_buf_t *out, int header) {
    ansi_state_t *state = ansi_state(out);

    if (!state) return;
    ansi_row_free(&state->row);
    state->row.header = header;
}

/**
 * Stores one completed terminal table row.
 * @param out Target buffer.
 * @param header Non-zero for header rows.
 * @return None.
 */
static void ansi_table_row_close(mdp_buf_t *out, int header) {
    (void)header;
    ansi_store_row(out);
}

/**
 * Starts capturing one terminal table cell.
 * @param out Target buffer.
 * @param header Non-zero for header cells.
 * @param first Non-zero for the first cell in a row.
 * @return None.
 */
static void ansi_table_cell_open(mdp_buf_t *out, int header, int first) {
    ansi_state_t *state = ansi_state(out);

    (void)header;
    (void)first;
    if (!state) return;
    free(state->cell.data);
    memset(&state->cell, 0, sizeof(state->cell));
    state->capture = 1;
}

/**
 * Stores one completed terminal table cell.
 * @param out Target buffer.
 * @param header Non-zero for header cells.
 * @return None.
 */
static void ansi_table_cell_close(mdp_buf_t *out, int header) {
    ansi_state_t *state = ansi_state(out);

    (void)header;
    if (!state) return;
    state->capture = 0;
    ansi_store_cell(out);
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
    ansi_putc(out, '\n');
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
    ansi_state_t state;
    int rc;

    memset(&state, 0, sizeof(state));
    out->renderer_data = &state;
    rc = kc_mdp_parse(body, out, &renderer);
    ansi_table_reset(&state);
    out->renderer_data = NULL;
    return rc;
}
