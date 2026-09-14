/**
 * llm.c - One-shot local generative inference CLI.
 * Summary: Reads prompts from stdin, loads a GGUF model, and streams generated text to stdout.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L

#include "libllm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

#ifndef KC_LLAMACPP_COMMIT
#define KC_LLAMACPP_COMMIT "unknown"
#endif

#define CLI_MAX_IMAGES 16
#define CLI_MAX_LORAS 16
#define CLI_MAX_INFO_FIELDS 32

typedef struct {
    const char *name;
    int field;
} cli_info_selector_t;

static const cli_info_selector_t g_info_selectors[] = {
    {"architecture", 0},
    {"name", 1},
    {"parameters", 2},
    {"size", 3},
    {"vocabulary", 4},
    {"context-max", 5},
    {"embedding-size", 6},
    {"layers", 7},
    {"heads", 8},
    {"kv-heads", 9},
    {"input-tokens", 10},
    {"context-used", 11},
    {"context-size", 12},
    {"context-free", 13},
    {"context-after", 14},
};

/**
 * cli_strdup - Duplicate a string.
 * @param s Source string.
 * @return Allocated copy, or NULL on failure.
 */
static char *cli_strdup(const char *s) {
    size_t len;
    char *copy;
    if (!s) return NULL;
    len = strlen(s) + 1;
    copy = (char *)malloc(len);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    return copy;
}

/**
 * parse_int - Parse an integer from a string.
 * @param str Input string.
 * @param out Output integer.
 * @return 0 on success, -1 on failure.
 */
static int parse_int(const char *str, int *out) {
    char *end = NULL;
    long val;
    if (!str || !out) return -1;
    val = strtol(str, &end, 10);
    if (!end || *end != '\0' || end == str) return -1;
    *out = (int)val;
    return 0;
}

/**
 * parse_float - Parse a float from a string.
 * @param str Input string.
 * @param out Output float.
 * @return 0 on success, -1 on failure.
 */
static int parse_float(const char *str, float *out) {
    char *end = NULL;
    float val;
    if (!str || !out) return -1;
    val = strtof(str, &end);
    if (!end || *end != '\0' || end == str) return -1;
    *out = val;
    return 0;
}

/**
 * parse_bool01 - Parse a boolean flag value encoded as 0 or 1.
 * @param str Input string.
 * @param out Output integer.
 * @return 0 on success, -1 on failure.
 */
static int parse_bool01(const char *str, int *out) {
    int value;
    if (parse_int(str, &value) != 0) return -1;
    if (value != 0 && value != 1) return -1;
    *out = value;
    return 0;
}

/**
 * parse_tri_state - Parse 0, 1, or auto into a tri-state integer.
 * @param str Input string.
 * @param out Output integer.
 * @return 0 on success, -1 on failure.
 */
static int parse_tri_state(const char *str, int *out) {
    if (!str || !out) return -1;
    if (strcmp(str, "auto") == 0) {
        *out = -1;
        return 0;
    }
    return parse_bool01(str, out);
}

/**
 * push_lora - Append a LoRA adapter path to the options.
 * @param opts Options to update.
 * @param path LoRA adapter file path.
 * @return 0 on success, -1 on failure.
 */
static int push_lora(kc_llm_options_t *opts, const char *path) {
    char **paths;
    float *scales;
    char *copy;
    if (opts->n_loras >= CLI_MAX_LORAS) return -1;
    copy = cli_strdup(path);
    if (!copy) return -1;
    paths = (char **)realloc(opts->lora_paths, (size_t)(opts->n_loras + 1) * sizeof(char *));
    scales = (float *)realloc(opts->lora_scales, (size_t)(opts->n_loras + 1) * sizeof(float));
    if (!paths || !scales) {
        free(copy);
        free(paths);
        free(scales);
        return -1;
    }
    opts->lora_paths = paths;
    opts->lora_scales = scales;
    opts->lora_paths[opts->n_loras] = copy;
    opts->lora_scales[opts->n_loras] = 1.0f;
    opts->n_loras++;
    return 0;
}

/**
 * set_option_string - Replace an option string with an allocated copy.
 * @param dst Pointer to the string field to update.
 * @param value New value.
 * @return 0 on success, -1 on failure.
 */
static int set_option_string(char **dst, const char *value) {
    char *copy;
    if (!dst) return -1;
    copy = cli_strdup(value);
    if (value && !copy) return -1;
    free(*dst);
    *dst = copy;
    return 0;
}

/**
 * push_image - Append an image attachment path to the options.
 * @param opts Options to update.
 * @param path Image file path.
 * @return 0 on success, -1 on failure.
 */
static int push_image(kc_llm_options_t *opts, const char *path) {
    char **images;
    char *copy;
    if (opts->n_images >= CLI_MAX_IMAGES) return -1;
    copy = cli_strdup(path);
    if (!copy) return -1;
    images = (char **)realloc(opts->images, (size_t)(opts->n_images + 1) * sizeof(char *));
    if (!images) {
        free(copy);
        return -1;
    }
    opts->images = images;
    opts->images[opts->n_images++] = copy;
    return 0;
}

/**
 * info_field_needs_request - Check whether a CLI selector needs stdin
 * content.
 * @param field Inspection field.
 * @return 1 when the field requires request content, 0 otherwise.
 */
static int info_field_needs_request(int field) {
    return field == 10 || field == 14;
}

/**
 * expand_info_group - Expand one CLI info group into concrete fields.
 * @param token Group name.
 * @param fields Destination array.
 * @param count Current field count.
 * @return 1 when token matched a group, 0 when not a group, -1 on overflow.
 */
static int expand_info_group(const char *token, int *fields, size_t *count) {
    static const int model_fields[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    static const int context_fields[] = {10, 11, 12, 13, 14, 5};
    size_t i;
    if (!token || !fields || !count) return -1;
    if (strcmp(token, "model") == 0) {
        for (i = 0; i < sizeof(model_fields) / sizeof(model_fields[0]); i++) {
            if (*count >= CLI_MAX_INFO_FIELDS) return -1;
            fields[(*count)++] = model_fields[i];
        }
        return 1;
    }
    if (strcmp(token, "context") == 0) {
        for (i = 0; i < sizeof(context_fields) / sizeof(context_fields[0]); i++) {
            if (*count >= CLI_MAX_INFO_FIELDS) return -1;
            fields[(*count)++] = context_fields[i];
        }
        return 1;
    }
    if (strcmp(token, "all") == 0) {
        for (i = 0; i < sizeof(g_info_selectors) / sizeof(g_info_selectors[0]); i++) {
            if (*count >= CLI_MAX_INFO_FIELDS) return -1;
            fields[(*count)++] = (int)i;
        }
        return 1;
    }
    return 0;
}

/**
 * parse_info_selectors - Parse and expand a comma-separated info selector list.
 * @param text Selector text.
 * @param fields Destination array.
 * @param count Output field count.
 * @return 0 on success, -1 on failure.
 */
static int parse_info_selectors(const char *text, int *fields, size_t *count) {
    const char *p;
    if (!text || !text[0] || !fields || !count) return -1;
    *count = 0;
    p = text;
    while (*p) {
        const char *start = p;
        const char *end;
        size_t len;
        char *token;
        size_t i;
        int group_rc;
        while (*p && *p != ',') p++;
        end = p;
        len = (size_t)(end - start);
        if (len == 0) return -1;
        token = cli_strdup(start);
        if (!token) return -1;
        token[len] = '\0';
        group_rc = expand_info_group(token, fields, count);
        if (group_rc < 0) {
            free(token);
            return -1;
        }
        if (group_rc == 0) {
            int matched = 0;
            for (i = 0; i < sizeof(g_info_selectors) / sizeof(g_info_selectors[0]); i++) {
                if (strcmp(token, g_info_selectors[i].name) == 0) {
                    if (*count >= CLI_MAX_INFO_FIELDS) {
                        free(token);
                        return -1;
                    }
                    fields[*count] = (int)i;
                    (*count)++;
                    matched = 1;
                    break;
                }
            }
            if (!matched) {
                free(token);
                return -1;
            }
        }
        free(token);
        if (*p == ',') {
            p++;
            if (*p == '\0') return -1;
        }
    }
    return *count > 0 ? 0 : -1;
}

/**
 * read_stdin - Read all data from stdin into a buffer.
 * @param out Pointer to receive the allocated buffer.
 * @return 0 on success, -1 on failure.
 */
static int read_stdin(char **out) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int ch;
    if (!out) return -1;
    *out = NULL;
    while ((ch = fgetc(stdin)) != EOF) {
        if (len + 1 >= cap) {
            size_t next = cap ? cap * 2 : 4096;
            char *p = (char *)realloc(buf, next + 1);
            if (!p) { free(buf); return -1; }
            buf = p;
            cap = next;
        }
        buf[len++] = (char)ch;
    }
    if (ferror(stdin)) { free(buf); return -1; }
    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) return -1;
    }
    buf[len] = '\0';
    *out = buf;
    return 0;
}

/**
 * read_request - Read one request from stdin up to a delimiter byte.
 * @param until Delimiter byte.
 * @param out Pointer to receive the allocated request string.
 * @param at_eof Pointer to receive EOF status.
 * @return 0 on success, -1 on failure.
 */
static int read_request(int until, char **out, int *at_eof) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int ch;
    if (!out || !at_eof) return -1;
    *out = NULL;
    *at_eof = 0;
    for (;;) {
        ch = fgetc(stdin);
        if (ch == EOF) break;
        if (ch == (unsigned char)until) break;
        if (len + 2 > cap) {
            size_t next = cap ? cap * 2 : 4096;
            char *p = (char *)realloc(buf, next);
            if (!p) { free(buf); return -1; }
            buf = p;
            cap = next;
        }
        buf[len++] = (char)ch;
    }
    if (ch == EOF) {
        if (ferror(stdin)) { free(buf); return -1; }
        *at_eof = 1;
    }
    if (len == 0) { free(buf); *out = NULL; return 0; }
    {
        char *p = (char *)realloc(buf, len + 1);
        if (!p) { free(buf); return -1; }
        p[len] = '\0';
        *out = p;
    }
    return 0;
}

/**
 * write_response_delimiter - Write response delimiter and terminal newline.
 * @param until Response delimiter byte.
 * @return 0 on success, -1 on failure.
 */
static int write_response_delimiter(int until) {
    if (fputc('\n', stdout) == EOF) return -1;
    if (fputc(until, stdout) == EOF) return -1;
    fflush(stdout);
    return 0;
}

/**
 * print_help - Print usage information.
 * @param name Program name.
 * @return None.
 */
static void print_help(const char *name) {
    printf("Usage: %s MODEL [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    --ctx N              Context size in tokens (default: llama.cpp default)\n");
    printf("    --predict N          Max tokens to predict, -1 unlimited (default: -1)\n");
    printf("    --threads N          Number of threads (default: auto)\n");
    printf("    --gpu N              GPU mode: -1 auto, 0 CPU, >0 require GPU (default: -1)\n");
    printf("    --gpu-layers N       Layers to offload to GPU (default: all)\n");
    printf("    --seed N             RNG seed, -1 for random (default: -1)\n");
    printf("    --temp F             Temperature (default: 0.80)\n");
    printf("    --top-k N            Top-k sampling (default: 40)\n");
    printf("    --top-p F            Top-p sampling (default: 0.95)\n");
    printf("    --min-p F            Min-p sampling (default: 0.0)\n");
    printf("    --repeat-penalty F   Repeat penalty (default: 1.10)\n");
    printf("    --repeat-last-n N    Last tokens for penalty (default: 64)\n");
    printf("    --until N            Request/response delimiter byte (default: 4, EOT)\n");
    printf("    --kv-load PATH       Load a KV state snapshot before reading prompts\n");
    printf("    --kv-save PATH       Save the KV state snapshot after requests\n");
    printf("    --lora FILE          LoRA adapter file (repeatable)\n");
    printf("    --lora-scale F       LoRA scale for previous --lora (repeatable)\n");
    printf("    --mmproj FILE        Multimodal projector file\n");
    printf("    --mtp FILE           Draft model file for MTP decoding\n");
    printf("    --mtp-tokens N       Max speculative draft tokens (default: 4)\n");
    printf("    --mtp-min N          Min speculative draft tokens (default: 0)\n");
    printf("    --mtp-psplit F       Draft split probability (default: 0.10)\n");
    printf("    --mtp-pmin F         Draft min probability (default: 0.00)\n");
    printf("    --mtp-threads N      Draft threads (default: same as --threads)\n");
    printf("    --mtp-gpu-layers N   Draft GPU layers, 0 CPU-only (default: 0)\n");
    printf("    --fattn MODE         Flash attention: auto, 1 on, 0 off (default: auto)\n");
    printf("    --think N            Enable model thinking: 1 on, 0 off (default: 1)\n");
    printf("    --role ROLE          Message role: system, user, assistant (default: user)\n");
    printf("    --image FILE         Image attachment (repeatable)\n");
    printf("    --info VALUE         Print inspection fields: selectors, model, context, all\n");
    printf("    -h, --help           Show this help message\n");
    printf("    -v, --version        Show version\n");
}

/**
 * print_version - Print version information.
 * @return None.
 */
static void print_version(void) {
    printf("llm.c %llu\n", (unsigned long long)kc_llm_version());
}

/**
 * validate_options - Validate parsed CLI options.
 * @param opts Options to validate.
 * @return 0 on success, -1 on failure.
 */
static int validate_options(const kc_llm_options_t *opts) {
    if (!opts->model_path || opts->model_path[0] == '\0') {
        fprintf(stderr, "llm: model path is required\n");
        return -1;
    }
    if (opts->n_images > 0 && !opts->mmproj_path) {
        fprintf(stderr, "llm: --image requires --mmproj\n");
        return -1;
    }
    if (opts->mmproj_path && opts->n_images == 0) {
        fprintf(stderr, "llm: --mmproj requires --image\n");
        return -1;
    }
    if (opts->n_images > 0 && opts->mtp_path && opts->mtp_path[0] != '\0') {
        fprintf(stderr, "llm: --image and --mtp options are mutually exclusive\n");
        return -1;
    }
    return 0;
}

/**
 * parse_args - Parse command-line arguments into options.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param opts Options to populate.
 * @return 0 on success, 1 for help, 2 for version, -1 on error.
 */
static int parse_args(int argc, char **argv, kc_llm_options_t *opts,
    char **info_value) {
    int i = 1;
    int explicit_image = 0;
    if (info_value) *info_value = NULL;
    if (argc >= 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            return 1;
        }
        if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
            return 2;
        }
    }
    if (argc < 2 || argv[1][0] == '-') {
        fprintf(stderr, "llm: model path is required\n");
        return -1;
    }
    if (set_option_string(&opts->model_path, argv[1]) != 0) {
        fprintf(stderr, "llm: out of memory\n");
        return -1;
    }
    i = 2;
    while (i < argc) {
        const char *arg = argv[i];
        if (strcmp(arg, "--ctx") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --ctx requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->ctx) != 0) { fprintf(stderr, "llm: invalid --ctx value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--predict") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --predict requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->predict) != 0) { fprintf(stderr, "llm: invalid --predict value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--threads") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --threads requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->threads) != 0) { fprintf(stderr, "llm: invalid --threads value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--gpu") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --gpu requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->gpu) != 0) { fprintf(stderr, "llm: invalid --gpu value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--gpu-layers") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --gpu-layers requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->gpu_layers) != 0) { fprintf(stderr, "llm: invalid --gpu-layers value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--seed") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --seed requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->seed) != 0) { fprintf(stderr, "llm: invalid --seed value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--temp") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --temp requires a value\n"); return -1; }
            if (parse_float(argv[i], &opts->temp) != 0) { fprintf(stderr, "llm: invalid --temp value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--top-k") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --top-k requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->top_k) != 0) { fprintf(stderr, "llm: invalid --top-k value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--top-p") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --top-p requires a value\n"); return -1; }
            if (parse_float(argv[i], &opts->top_p) != 0) { fprintf(stderr, "llm: invalid --top-p value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--min-p") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --min-p requires a value\n"); return -1; }
            if (parse_float(argv[i], &opts->min_p) != 0) { fprintf(stderr, "llm: invalid --min-p value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--repeat-penalty") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --repeat-penalty requires a value\n"); return -1; }
            if (parse_float(argv[i], &opts->repeat_penalty) != 0) { fprintf(stderr, "llm: invalid --repeat-penalty value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--repeat-last-n") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --repeat-last-n requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->repeat_last_n) != 0) { fprintf(stderr, "llm: invalid --repeat-last-n value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--until") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --until requires a value\n"); return -1; }
            {
                int t;
                if (parse_int(argv[i], &t) != 0 || t < 0 || t > 255) { fprintf(stderr, "llm: invalid --until value: %s\n", argv[i]); return -1; }
                opts->until = t;
            }
        } else if (strcmp(arg, "--kv-load") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --kv-load requires a value\n"); return -1; }
            if (set_option_string(&opts->kv_load_path, argv[i]) != 0) {
                fprintf(stderr, "llm: out of memory\n");
                return -1;
            }
        } else if (strcmp(arg, "--kv-save") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --kv-save requires a value\n"); return -1; }
            if (set_option_string(&opts->kv_save_path, argv[i]) != 0) {
                fprintf(stderr, "llm: out of memory\n");
                return -1;
            }
        } else if (strcmp(arg, "--lora") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --lora requires a value\n"); return -1; }
            if (push_lora(opts, argv[i]) != 0) {
                fprintf(stderr, "llm: too many LoRA adapters (max %d)\n", CLI_MAX_LORAS);
                return -1;
            }
        } else if (strcmp(arg, "--lora-scale") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --lora-scale requires a value\n"); return -1; }
            if (opts->n_loras == 0) {
                fprintf(stderr, "llm: --lora-scale requires a previous --lora\n");
                return -1;
            }
            if (parse_float(argv[i], &opts->lora_scales[opts->n_loras - 1]) != 0) { fprintf(stderr, "llm: invalid --lora-scale value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--mmproj") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --mmproj requires a value\n"); return -1; }
            if (set_option_string(&opts->mmproj_path, argv[i]) != 0) {
                fprintf(stderr, "llm: out of memory\n");
                return -1;
            }
        } else if (strcmp(arg, "--mtp") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --mtp requires a value\n"); return -1; }
            if (set_option_string(&opts->mtp_path, argv[i]) != 0) {
                fprintf(stderr, "llm: out of memory\n");
                return -1;
            }
        } else if (strcmp(arg, "--mtp-tokens") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --mtp-tokens requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->mtp_tokens) != 0) { fprintf(stderr, "llm: invalid --mtp-tokens value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--mtp-min") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --mtp-min requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->mtp_min) != 0) { fprintf(stderr, "llm: invalid --mtp-min value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--mtp-psplit") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --mtp-psplit requires a value\n"); return -1; }
            if (parse_float(argv[i], &opts->mtp_p_split) != 0) { fprintf(stderr, "llm: invalid --mtp-psplit value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--mtp-pmin") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --mtp-pmin requires a value\n"); return -1; }
            if (parse_float(argv[i], &opts->mtp_p_min) != 0) { fprintf(stderr, "llm: invalid --mtp-pmin value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--mtp-threads") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --mtp-threads requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->mtp_threads) != 0) { fprintf(stderr, "llm: invalid --mtp-threads value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--mtp-gpu-layers") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --mtp-gpu-layers requires a value\n"); return -1; }
            if (parse_int(argv[i], &opts->mtp_gpu_layers) != 0) { fprintf(stderr, "llm: invalid --mtp-gpu-layers value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--fattn") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --fattn requires a value\n"); return -1; }
            if (parse_tri_state(argv[i], &opts->fattn) != 0) { fprintf(stderr, "llm: invalid --fattn value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--think") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --think requires a value\n"); return -1; }
            if (parse_bool01(argv[i], &opts->think) != 0) { fprintf(stderr, "llm: invalid --think value: %s\n", argv[i]); return -1; }
        } else if (strcmp(arg, "--role") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --role requires a value\n"); return -1; }
            if (set_option_string(&opts->role, argv[i]) != 0) {
                fprintf(stderr, "llm: out of memory\n");
                return -1;
            }
        } else if (strcmp(arg, "--image") == 0) {
            if (++i >= argc) { fprintf(stderr, "llm: --image requires a value\n"); return -1; }
            if (!explicit_image) {
                opts->n_images = 0;
                explicit_image = 1;
            }
            if (push_image(opts, argv[i]) != 0) {
                fprintf(stderr, "llm: too many images (max %d)\n", CLI_MAX_IMAGES);
                return -1;
            }
        } else if (strcmp(arg, "--info") == 0) {
            if (++i >= argc || argv[i][0] == '\0') {
                fprintf(stderr, "llm: --info requires a value\n");
                return -1;
            }
            if (!info_value) {
                fprintf(stderr, "llm: internal error\n");
                return -1;
            }
            if (*info_value) free(*info_value);
            *info_value = cli_strdup(argv[i]);
            if (!*info_value) {
                fprintf(stderr, "llm: out of memory\n");
                return -1;
            }
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            return 1;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            return 2;
        } else {
            fprintf(stderr, "llm: unknown option: %s\n", arg);
            return -1;
        }
        i++;
    }
    return 0;
}

/**
 * cli_stream_write - Write generated tokens to stdout.
 * @param data Output bytes.
 * @param len Number of bytes.
 * @param user User pointer (unused).
 * @return 0 on success, non-zero on failure.
 */
static int cli_stream_write(const char *data, size_t len, void *user) {
    (void)user;
    if (len == 0) return 0;
    if (fwrite(data, 1, len, stdout) != len) return 1;
    fflush(stdout);
    return 0;
}

/**
 * cli_print_info - Print inspection fields in key=value form.
 * @param info Filled info result array.
 * @param fields Field indices in requested order.
 * @param count Field count.
 * @return 0 on success, -1 on failure.
 */
static int cli_print_info(const kc_llm_info_t *info, const int *fields, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        const char *name = g_info_selectors[fields[i]].name;
        const kc_llm_info_t *item = &info[i];
        if (fprintf(stdout, "%s=", name) < 0) return -1;
        switch (item->type) {
            case KC_LLM_INFO_VALUE_STRING:
                if (item->value.string) {
                    if (fprintf(stdout, "%s\n", item->value.string) < 0) return -1;
                } else if (fprintf(stdout, "unavailable\n") < 0) {
                    return -1;
                }
                break;
            case KC_LLM_INFO_VALUE_U64:
                if (fprintf(stdout, "%lld\n", (long long)item->value.u64) < 0) return -1;
                break;
            case KC_LLM_INFO_VALUE_I64:
                if (fprintf(stdout, "%lld\n", (long long)item->value.i64) < 0) return -1;
                break;
            default:
                if (fprintf(stdout, "unavailable\n") < 0) return -1;
                break;
        }
    }
    fflush(stdout);
    return 0;
}

/**
 * main - CLI entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit code.
 */
int main(int argc, char **argv) {
    kc_llm_options_t opts = kc_llm_options_default();
    char *info_value = NULL;
    const char *role = NULL;
    int rc = 1;
    int parse_rc;
    int stateful = 0;
    kc_llm_t *ctx = NULL;
    int ctx_open = 0;

    kc_llm_options_load_env(&opts);
    parse_rc = parse_args(argc, argv, &opts, &info_value);
    if (parse_rc == 1) {
        print_help(argv[0]);
        free(info_value);
        kc_llm_options_free(&opts);
        return 0;
    }
    if (parse_rc == 2) {
        print_version();
        free(info_value);
        kc_llm_options_free(&opts);
        return 0;
    }
    if (parse_rc < 0) {
        print_help(argv[0]);
        free(info_value);
        kc_llm_options_free(&opts);
        return 1;
    }
    if (validate_options(&opts) != 0) {
        print_help(argv[0]);
        free(info_value);
        kc_llm_options_free(&opts);
        return 1;
    }

    stateful = (opts.kv_load_path || opts.kv_save_path);
    role = opts.role ? opts.role : "user";

    if (info_value) {
        int info_fields[CLI_MAX_INFO_FIELDS];
        size_t n_info_fields = 0;
        size_t i;
        int needs_request = 0;
        kc_llm_info_field_t fields[CLI_MAX_INFO_FIELDS];
        kc_llm_info_t info[CLI_MAX_INFO_FIELDS];

        if (parse_info_selectors(info_value, info_fields, &n_info_fields) != 0) {
            fprintf(stderr, "llm: invalid --info selector list: %s\n", info_value);
            free(info_value);
            kc_llm_options_free(&opts);
            return 1;
        }
        for (i = 0; i < n_info_fields; i++) {
            if (info_field_needs_request(info_fields[i])) {
                needs_request = 1;
                break;
            }
        }
        for (i = 0; i < n_info_fields; i++) {
            fields[i] = (kc_llm_info_field_t)info_fields[i];
        }

        if (needs_request) {
            if (kc_llm_open(&ctx, &opts) != KC_LLM_OK) {
                fprintf(stderr, "llm: failed to open model\n");
                goto cleanup_info;
            }
        } else {
            if (kc_llm_open_model(&ctx, &opts) != KC_LLM_OK) {
                fprintf(stderr, "llm: failed to open model\n");
                goto cleanup_info;
            }
        }
        ctx_open = 1;

        if (needs_request) {
            if (opts.n_images > 0) {
                char *prompt = NULL;
                kc_llm_info_request_t req;
                if (read_stdin(&prompt) != 0) {
                    fprintf(stderr, "llm: failed to read stdin\n");
                    goto cleanup_info;
                }
                req.role = role;
                req.content = prompt ? prompt : "";
                if (kc_llm_info_query(ctx, fields, n_info_fields, &req, info) != KC_LLM_OK) {
                    fprintf(stderr, "llm: info query failed: %s\n", kc_llm_error(ctx));
                    free(prompt);
                    goto cleanup_info;
                }
                free(prompt);
                if (cli_print_info(info, info_fields, n_info_fields) != 0 ||
                        write_response_delimiter(opts.until) != 0) {
                    fprintf(stderr, "llm: failed to write info output\n");
                    goto cleanup_info;
                }
            } else {
                for (;;) {
                    char *request = NULL;
                    int at_eof = 0;
                    kc_llm_info_request_t req;
                    if (read_request(opts.until, &request, &at_eof) != 0) {
                        fprintf(stderr, "llm: failed to read request\n");
                        goto cleanup_info;
                    }
                    if (!request && at_eof) break;
                    if (!request) continue;
                    req.role = role;
                    req.content = request;
                    if (kc_llm_info_query(ctx, fields, n_info_fields, &req, info) != KC_LLM_OK) {
                        fprintf(stderr, "llm: info query failed: %s\n", kc_llm_error(ctx));
                        free(request);
                        goto cleanup_info;
                    }
                    if (cli_print_info(info, info_fields, n_info_fields) != 0 ||
                            write_response_delimiter(opts.until) != 0) {
                        fprintf(stderr, "llm: failed to write info output\n");
                        free(request);
                        goto cleanup_info;
                    }
                    free(request);
                    if (at_eof) break;
                }
            }
        } else {
            if (kc_llm_info_query(ctx, fields, n_info_fields, NULL, info) != KC_LLM_OK) {
                fprintf(stderr, "llm: info query failed: %s\n", kc_llm_error(ctx));
                goto cleanup_info;
            }
            if (cli_print_info(info, info_fields, n_info_fields) != 0) {
                fprintf(stderr, "llm: failed to write info output\n");
                goto cleanup_info;
            }
        }

        rc = 0;

    cleanup_info:
        if (ctx_open) kc_llm_close(ctx);
        free(info_value);
        kc_llm_options_free(&opts);
        return rc;
    }

    if (kc_llm_open(&ctx, &opts) != KC_LLM_OK) {
        fprintf(stderr, "llm: failed to open model\n");
        free(info_value);
        kc_llm_options_free(&opts);
        return 1;
    }
    ctx_open = 1;

    if (opts.n_images > 0) {
        char *prompt = NULL;
        if (read_stdin(&prompt) != 0) {
            fprintf(stderr, "llm: failed to read stdin\n");
            goto cleanup;
        }
        if (kc_llm_generate_role(ctx, role, prompt ? prompt : "", cli_stream_write, NULL) != KC_LLM_OK) {
            fprintf(stderr, "llm: generation failed: %s\n", kc_llm_error(ctx));
            free(prompt);
            goto cleanup;
        }
        free(prompt);
        if (write_response_delimiter(opts.until) != 0) {
            fprintf(stderr, "llm: failed to write delimiter\n");
            goto cleanup;
        }
    } else {
        for (;;) {
            char *request = NULL;
            int at_eof = 0;
            if (read_request(opts.until, &request, &at_eof) != 0) {
                fprintf(stderr, "llm: failed to read request\n");
                goto cleanup;
            }
            if (!request && at_eof) break;
            if (!request) continue;
            if (!stateful) {
                if (kc_llm_memory_clear(ctx) != KC_LLM_OK) {
                    fprintf(stderr, "llm: failed to clear memory: %s\n", kc_llm_error(ctx));
                    free(request);
                    goto cleanup;
                }
            }
            if (kc_llm_generate_role(ctx, role, request, cli_stream_write, NULL) != KC_LLM_OK) {
                fprintf(stderr, "llm: generation failed: %s\n", kc_llm_error(ctx));
                free(request);
                goto cleanup;
            }
            if (write_response_delimiter(opts.until) != 0) {
                fprintf(stderr, "llm: failed to write delimiter\n");
                free(request);
                goto cleanup;
            }
            free(request);
            if (at_eof) break;
        }
    }

    rc = 0;

cleanup:
    if (ctx_open) kc_llm_close(ctx);
    free(info_value);
    kc_llm_options_free(&opts);
    return rc;
}
