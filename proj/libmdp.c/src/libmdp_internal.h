#ifndef KC_MDP_INTERNAL_H
#define KC_MDP_INTERNAL_H

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
    void (*list_item_open)(mdp_buf_t *out, int ordered, unsigned int number,
        int task_state);
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

void kc_mdp_buf_write(mdp_buf_t *buf, const void *data, size_t len);
void kc_mdp_buf_puts(mdp_buf_t *buf, const char *text);
void kc_mdp_buf_putc(mdp_buf_t *buf, char c);
int kc_mdp_parse(const char *body, mdp_buf_t *out,
    const kc_mdp_renderer_t *renderer);
int kc_mdp_render_html(const char *body, mdp_buf_t *out);
int kc_mdp_render_ansi(const char *body, mdp_buf_t *out);

#endif
