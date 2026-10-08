/**
 * mdp.h - Markdown Parser
 * Summary: Public API for the mdp library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_MDP_H
#define KC_MDP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_mdp kc_mdp_t;

#define KC_MDP_OK      0
#define KC_MDP_ERROR  -1

/**
 * Create a reusable Markdown document.
 * Frontmatter is split from the body once during this call.
 * @param out Pointer to receive the document.
 * @param input Null-terminated Markdown input.
 * @return KC_MDP_OK on success, KC_MDP_ERROR on failure.
 */
int kc_mdp_open(kc_mdp_t **out, const char *input);

/**
 * Render and cache the document body as an HTML fragment.
 * HTML is the default presentation backend for mdp. The returned pointer
 * belongs to the document and remains valid until kc_mdp_close().
 * @param mdp Document returned by kc_mdp_open().
 * @return Cached NUL-terminated HTML fragment, or NULL on failure.
 */
const char *kc_mdp_html(kc_mdp_t *mdp);

/**
 * Render and cache the document body as ANSI terminal text.
 * The ANSI backend consumes the same parsed Markdown semantics as HTML. The
 * returned pointer belongs to the document and remains valid until close.
 * @param mdp Document returned by kc_mdp_open().
 * @return Cached NUL-terminated ANSI terminal text, or NULL on failure.
 */
const char *kc_mdp_ansi(kc_mdp_t *mdp);

/**
 * Return the document body after recognized frontmatter.
 * The returned pointer belongs to the document and remains valid until
 * kc_mdp_close().
 * @param mdp Document returned by kc_mdp_open().
 * @return NUL-terminated body text, or NULL for an invalid document.
 */
const char *kc_mdp_body(const kc_mdp_t *mdp);

/**
 * Return recognized raw frontmatter content.
 * The returned pointer belongs to the document and remains valid until
 * kc_mdp_close().
 * @param mdp Document returned by kc_mdp_open().
 * @return NUL-terminated metadata text, or NULL for an invalid document.
 */
const char *kc_mdp_meta(const kc_mdp_t *mdp);

/**
 * Release a Markdown document. Accepts NULL.
 * @param mdp Document returned by kc_mdp_open(), or NULL.
 * @return None.
 */
void kc_mdp_close(kc_mdp_t *mdp);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_mdp_version(void);

#ifdef KC_MDP_PRIVATE

#include <stddef.h>

#define MDP_OK     0
#define MDP_ERROR -1

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
    int oom;
} mdp_buf_t;

typedef struct {
    void (*text)(mdp_buf_t *out, const char *text, size_t len);
    void (*strong_open)(mdp_buf_t *out);
    void (*strong_close)(mdp_buf_t *out);
    void (*em_open)(mdp_buf_t *out);
    void (*em_close)(mdp_buf_t *out);
    void (*strike_open)(mdp_buf_t *out);
    void (*strike_close)(mdp_buf_t *out);
    void (*code_inline)(mdp_buf_t *out, const char *text, size_t len);
    void (*link_open)(mdp_buf_t *out, const char *url, size_t len);
    void (*link_close)(mdp_buf_t *out);
    void (*image)(mdp_buf_t *out, const char *alt, size_t alt_len,
        const char *url, size_t url_len);
    void (*raw_inline)(mdp_buf_t *out, const char *text, size_t len);
    void (*paragraph_open)(mdp_buf_t *out);
    void (*paragraph_close)(mdp_buf_t *out);
    void (*heading_open)(mdp_buf_t *out, int level);
    void (*heading_close)(mdp_buf_t *out, int level);
    void (*quote_open)(mdp_buf_t *out);
    void (*quote_close)(mdp_buf_t *out);
    void (*list_open)(mdp_buf_t *out, int ordered);
    void (*list_close)(mdp_buf_t *out, int ordered);
    void (*list_item_open)(mdp_buf_t *out, int ordered,
        unsigned int number, int task_state);
    void (*list_item_close)(mdp_buf_t *out);
    void (*code_open)(mdp_buf_t *out, const char *language, size_t len);
    void (*code_text)(mdp_buf_t *out, const char *text, size_t len);
    void (*code_close)(mdp_buf_t *out);
    void (*horizontal_rule)(mdp_buf_t *out);
    void (*table_open)(mdp_buf_t *out);
    void (*table_close)(mdp_buf_t *out);
    void (*table_row_open)(mdp_buf_t *out, int header);
    void (*table_row_close)(mdp_buf_t *out, int header);
    void (*table_cell_open)(mdp_buf_t *out, int header, int first);
    void (*table_cell_close)(mdp_buf_t *out, int header);
    void (*raw_block)(mdp_buf_t *out, const char *text, size_t len);
} kc_mdp_renderer_t;

/**
 * Appends bytes to a growable renderer buffer.
 * @param buf Target buffer.
 * @param data Bytes to append.
 * @param len Byte count.
 * @return None.
 */
void kc_mdp_buf_write(mdp_buf_t *buf, const void *data, size_t len);

/**
 * Appends a NUL-terminated string to a renderer buffer.
 * @param buf Target buffer.
 * @param text String to append.
 * @return None.
 */
void kc_mdp_buf_puts(mdp_buf_t *buf, const char *text);

/**
 * Appends one character to a renderer buffer.
 * @param buf Target buffer.
 * @param c Character to append.
 * @return None.
 */
void kc_mdp_buf_putc(mdp_buf_t *buf, char c);

/**
 * Parses Markdown once and emits semantic renderer callbacks.
 * @param body Markdown body text.
 * @param out Target output buffer.
 * @param renderer Renderer callback table.
 * @return MDP_OK on success, MDP_ERROR on allocation failure.
 */
int kc_mdp_parse(const char *body, mdp_buf_t *out,
    const kc_mdp_renderer_t *renderer);

/**
 * Renders Markdown through the HTML backend.
 * @param body Markdown body text.
 * @param out Target output buffer.
 * @return MDP_OK on success, MDP_ERROR on allocation failure.
 */
int kc_mdp_render_html(const char *body, mdp_buf_t *out);

/**
 * Renders Markdown through the ANSI terminal backend.
 * @param body Markdown body text.
 * @param out Target output buffer.
 * @return MDP_OK on success, MDP_ERROR on allocation failure.
 */
int kc_mdp_render_ansi(const char *body, mdp_buf_t *out);

#endif

#ifdef __cplusplus
}
#endif

#endif
