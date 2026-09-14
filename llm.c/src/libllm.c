/**
 * libllm.c - Local generative inference library.
 * Summary: Core implementation for loading GGUF models and generating text.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include "libllm.h"

#include "fit.h"
#include "llama.h"
#include "chat.h"
#include "log.h"
#include "sampling.h"
#include "speculative.h"
#include "mtmd.h"
#include "mtmd-helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#endif

#ifndef KC_LLM_BUILD_VERSION
#define KC_LLM_BUILD_VERSION 0
#endif

#ifndef KC_LLAMACPP_COMMIT
#define KC_LLAMACPP_COMMIT "unknown"
#endif

#define KC_LLM_MAX_IMAGES 16
#define KC_LLM_MAX_LORAS 16

struct kc_llm {
    struct llama_model *model;
    struct llama_context *ctx;
    struct llama_model *draft_model;
    struct llama_context *draft_ctx;
    struct llama_sampler *sampler;
    struct mtmd_context *mctx;
    struct llama_adapter_lora *adapters[KC_LLM_MAX_LORAS];
    const struct llama_vocab *vocab;
    kc_llm_options_t opts;
    char error[1024];
    size_t n_tokens;
    llama_pos pos;
    int stop_requested;
    int has_vision;
    int n_adapters;
    int model_only;
    char *info_architecture;
    char *info_name;
};

typedef struct {
    float *tensor_split;
    struct llama_model_tensor_buft_override *tensor_buft_overrides;
    size_t *margins;
} kc_llm_fit_buffers_t;

typedef struct {
    common_params_speculative spec;
} kc_llm_speculative_t;

typedef struct {
    char *prompt;
    int64_t token_count;
    int64_t position_count;
} kc_llm_request_usage_t;

/**
 * kc_llm_strdup - Duplicate a string.
 * @param s Source string.
 * @return Allocated copy, or NULL on failure.
 */
static char *kc_llm_strdup(const char *s) {
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
 * kc_llm_strdup_n - Duplicate a bounded string.
 * @param s Source string.
 * @param len Number of bytes to copy.
 * @return Allocated copy, or NULL on failure.
 */
static char *kc_llm_strdup_n(const char *s, size_t len) {
    char *copy;
    if (!s) return NULL;
    copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

/**
 * kc_llm_replace_string - Replace an owned string with a copy.
 * @param dst Destination pointer.
 * @param src Source string.
 * @return 0 on success, -1 on failure.
 */
static int kc_llm_replace_string(char **dst, const char *src) {
    char *copy;
    if (!dst) return -1;
    copy = src ? kc_llm_strdup(src) : NULL;
    if (src && !copy) return -1;
    free(*dst);
    *dst = copy;
    return 0;
}

/**
 * kc_llm_set_err - Store a formatted error on a context.
 * @param m Context to update.
 * @param fmt Format string.
 * @return None.
 */
static void kc_llm_set_err(kc_llm_t *m, const char *fmt, ...) {
    if (!m) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(m->error, sizeof(m->error), fmt, args);
    va_end(args);
}

/**
 * kc_llm_error - Return the current context error string.
 * @param ctx Context pointer.
 * @return Error string.
 */
const char *kc_llm_error(const kc_llm_t *ctx) {
    return ctx ? ctx->error : "null context";
}

/**
 * g_llama_log_error - Last llama.cpp error line captured for failure paths.
 * llama.cpp logging is process-global, so the capture buffer is too.
 */
static char g_llama_log_error[1024];

/**
 * llm_log_callback - Capture llama.cpp error lines for failure reporting.
 * @param level Log level.
 * @param text Log text.
 * @param user User pointer.
 * @return None.
 */
static void llm_log_callback(enum ggml_log_level level, const char *text, void *user) {
    size_t len;
    (void)user;
    if (level != GGML_LOG_LEVEL_ERROR || !text || !text[0]) return;
    len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) len--;
    if (len == 0) return;
    if (len >= sizeof(g_llama_log_error)) len = sizeof(g_llama_log_error) - 1;
    memcpy(g_llama_log_error, text, len);
    g_llama_log_error[len] = '\0';
}

/**
 * parse_int - Parse an integer string.
 * @param str Input string.
 * @param out Parsed value.
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
 * parse_float - Parse a float string.
 * @param str Input string.
 * @param out Parsed value.
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
 * env_int - Read an integer environment variable.
 * @param name Variable name.
 * @param out Parsed value.
 * @return 0 on success, -1 on missing or invalid value.
 */
static int env_int(const char *name, int *out) {
    const char *val = getenv(name);
    if (!val) return -1;
    return parse_int(val, out);
}

/**
 * env_float - Read a float environment variable.
 * @param name Variable name.
 * @param out Parsed value.
 * @return 0 on success, -1 on missing or invalid value.
 */
static int env_float(const char *name, float *out) {
    const char *val = getenv(name);
    if (!val) return -1;
    return parse_float(val, out);
}

/**
 * env_str - Read a string environment variable.
 * @param name Variable name.
 * @return Variable value, or NULL when unset.
 */
static const char *env_str(const char *name) {
    return getenv(name);
}

/**
 * kc_llm_role_valid - Check whether a role is supported.
 * @param role Role string.
 * @return 1 when the role is valid, 0 otherwise.
 */
static int kc_llm_role_valid(const char *role) {
    if (!role) return 0;
    if (strcmp(role, "system") == 0) return 1;
    if (strcmp(role, "user") == 0) return 1;
    if (strcmp(role, "assistant") == 0) return 1;
    return 0;
}

/**
 * kc_llm_fit_buffers_free - Release temporary parameter-fit buffers.
 * @param bufs Buffers to release.
 * @return None.
 */
static void kc_llm_fit_buffers_free(kc_llm_fit_buffers_t *bufs) {
    if (!bufs) return;
    free(bufs->tensor_split);
    free(bufs->tensor_buft_overrides);
    free(bufs->margins);
    bufs->tensor_split = NULL;
    bufs->tensor_buft_overrides = NULL;
    bufs->margins = NULL;
}

/**
 * build_common_sampling - Convert libllm options into common sampling.
 * @param opts Generation options.
 * @return Initialized common sampling parameters.
 */
static common_params_sampling build_common_sampling(const kc_llm_options_t *opts) {
    common_params_sampling params;
    params.seed = opts->seed >= 0 ? (uint32_t)opts->seed : LLAMA_DEFAULT_SEED;
    params.top_k = opts->top_k;
    params.top_p = opts->top_p;
    params.min_p = opts->min_p;
    params.temp = opts->temp;
    params.penalty_last_n = opts->repeat_last_n;
    params.penalty_repeat = opts->repeat_penalty;
    params.no_perf = true;
    return params;
}

/**
 * apply_flash_attention - Apply flash-attention selection to context params.
 * @param opts Runtime options.
 * @param cparams Context parameters to update.
 * @return None.
 */
static void apply_flash_attention(const kc_llm_options_t *opts,
    struct llama_context_params *cparams) {
    if (!opts || !cparams) return;
    if (opts->fattn < 0) {
        cparams->flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;
    } else if (opts->fattn == 0) {
        cparams->flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
    } else {
        cparams->flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;
    }
}

/**
 * build_speculative_params - Map libllm options into speculative params.
 * @param ctx Active libllm context.
 * @return Speculative parameters.
 */
static kc_llm_speculative_t build_speculative_params(kc_llm_t *ctx) {
    kc_llm_speculative_t data = {};
    data.spec.types = { COMMON_SPECULATIVE_TYPE_DRAFT_MTP };
    data.spec.draft.n_max = ctx->opts.mtp_tokens;
    data.spec.draft.n_min = ctx->opts.mtp_min;
    data.spec.draft.p_split = ctx->opts.mtp_p_split;
    data.spec.draft.p_min = ctx->opts.mtp_p_min;
    data.spec.draft.n_gpu_layers = ctx->opts.mtp_gpu_layers;
    data.spec.draft.ctx_tgt = ctx->ctx;
    data.spec.draft.ctx_dft = ctx->draft_ctx;
    return data;
}

/**
 * kc_llm_options_default - Return default generation options.
 * @return Default options.
 */
kc_llm_options_t kc_llm_options_default(void) {
    kc_llm_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.predict = -1;
    opts.gpu = -1;
    opts.gpu_layers = -1;
    opts.think = 1;
    opts.mtp_tokens = 4;
    opts.mtp_min = 0;
    opts.mtp_p_split = 0.10f;
    opts.mtp_p_min = 0.0f;
    opts.mtp_threads = 0;
    opts.mtp_gpu_layers = 0;
    opts.fattn = -1;
    opts.seed = -1;
    opts.temp = 0.80f;
    opts.top_k = 40;
    opts.top_p = 0.95f;
    opts.min_p = 0.0f;
    opts.repeat_penalty = 1.10f;
    opts.repeat_last_n = 64;
    opts.until = 4;
    return opts;
}

/**
 * kc_llm_options_load_env - Load KC_LLM environment overrides.
 * @param opts Options to update.
 * @return None.
 */
void kc_llm_options_load_env(kc_llm_options_t *opts) {
    const char *model;
    const char *lora;
    const char *lora_scale;
    const char *image;
    const char *mmproj;
    if (!opts) return;
    env_int("KC_LLM_CTX", &opts->ctx);
    env_int("KC_LLM_PREDICT", &opts->predict);
    env_int("KC_LLM_THREADS", &opts->threads);
    env_int("KC_LLM_GPU", &opts->gpu);
    env_int("KC_LLM_GPU_LAYERS", &opts->gpu_layers);
    env_int("KC_LLM_SEED", &opts->seed);
    env_float("KC_LLM_TEMP", &opts->temp);
    env_int("KC_LLM_TOP_K", &opts->top_k);
    env_float("KC_LLM_TOP_P", &opts->top_p);
    env_float("KC_LLM_MIN_P", &opts->min_p);
    env_float("KC_LLM_REPEAT_PENALTY", &opts->repeat_penalty);
    env_int("KC_LLM_REPEAT_LAST_N", &opts->repeat_last_n);
    env_int("KC_LLM_UNTIL", &opts->until);
    model = env_str("KC_LLM_MODEL");
    if (model) {
        free(opts->model_path);
        opts->model_path = kc_llm_strdup(model);
    }
    mmproj = env_str("KC_LLM_MMPROJ");
    if (mmproj) {
        free(opts->mmproj_path);
        opts->mmproj_path = kc_llm_strdup(mmproj);
    }
    {
        const char *kv_load = env_str("KC_LLM_KV_LOAD");
        const char *kv_save = env_str("KC_LLM_KV_SAVE");
        if (kv_load) {
            free(opts->kv_load_path);
            opts->kv_load_path = kc_llm_strdup(kv_load);
        }
        if (kv_save) {
            free(opts->kv_save_path);
            opts->kv_save_path = kc_llm_strdup(kv_save);
        }
    }
    lora = env_str("KC_LLM_LORA");
    if (lora && opts->n_loras < KC_LLM_MAX_LORAS) {
        char **paths = (char **)realloc(opts->lora_paths, (size_t)(opts->n_loras + 1) * sizeof(char *));
        float *scales = (float *)realloc(opts->lora_scales, (size_t)(opts->n_loras + 1) * sizeof(float));
        if (paths && scales) {
            opts->lora_paths = paths;
            opts->lora_scales = scales;
            opts->lora_paths[opts->n_loras] = kc_llm_strdup(lora);
            opts->lora_scales[opts->n_loras] = 1.0f;
            lora_scale = env_str("KC_LLM_LORA_SCALE");
            if (lora_scale) {
                float scale_val = 1.0f;
                if (parse_float(lora_scale, &scale_val) == 0) {
                    opts->lora_scales[opts->n_loras] = scale_val;
                }
            }
            opts->n_loras++;
        } else {
            free(paths);
            free(scales);
        }
    }
    image = env_str("KC_LLM_IMAGE");
    if (image && opts->n_images < KC_LLM_MAX_IMAGES) {
        char **images = (char **)realloc(opts->images, (size_t)(opts->n_images + 1) * sizeof(char *));
        if (images) {
            opts->images = images;
            opts->images[opts->n_images++] = kc_llm_strdup(image);
        }
    }
}

/**
 * kc_llm_options_free - Free dynamic fields owned by options.
 * @param opts Options to clear.
 * @return None.
 */
void kc_llm_options_free(kc_llm_options_t *opts) {
    int i;
    if (!opts) return;
    free(opts->model_path);
    free(opts->kv_load_path);
    free(opts->kv_save_path);
    free(opts->mmproj_path);
    free(opts->role);
    free(opts->mtp_path);
    for (i = 0; i < opts->n_loras; i++) {
        free(opts->lora_paths[i]);
    }
    free(opts->lora_paths);
    free(opts->lora_scales);
    for (i = 0; i < opts->n_images; i++) {
        free(opts->images[i]);
    }
    free(opts->images);
    memset(opts, 0, sizeof(*opts));
}

/**
 * kc_llm_request_usage_free - Release prepared request accounting state.
 * @param usage Usage structure to clear.
 * @return None.
 */
static void kc_llm_request_usage_free(kc_llm_request_usage_t *usage) {
    if (!usage) return;
    free(usage->prompt);
    usage->prompt = NULL;
    usage->token_count = 0;
    usage->position_count = 0;
}

/**
 * kc_llm_info_field_needs_request - Check whether a field depends on input.
 * @param field Inspection field.
 * @return 1 when request content is required, 0 otherwise.
 */
static int kc_llm_info_field_needs_request(kc_llm_info_field_t field) {
    return field == KC_LLM_INFO_INPUT_TOKENS || field == KC_LLM_INFO_CONTEXT_AFTER;
}

/**
 * kc_llm_info_field_needs_runtime - Check whether a field depends on
 * runtime state.
 * @param field Inspection field.
 * @return 1 when a llama_context is required, 0 otherwise.
 */
static int kc_llm_info_field_needs_runtime(kc_llm_info_field_t field) {
    return field == KC_LLM_INFO_CONTEXT_USED ||
        field == KC_LLM_INFO_CONTEXT_SIZE ||
        field == KC_LLM_INFO_CONTEXT_FREE ||
        field == KC_LLM_INFO_CONTEXT_AFTER;
}

/**
 * kc_llm_info_field_needs_usage - Check whether a field needs prepared
 * request accounting.
 * @param field Inspection field.
 * @return 1 when request usage must be computed, 0 otherwise.
 */
static int kc_llm_info_field_needs_usage(kc_llm_info_field_t field) {
    return field == KC_LLM_INFO_INPUT_TOKENS || field == KC_LLM_INFO_CONTEXT_AFTER;
}

/**
 * kc_llm_context_used - Return the current effective runtime occupancy.
 * @param ctx Context pointer.
 * @param out Output occupancy in positions.
 * @return 0 on success, -1 on failure.
 */
static int kc_llm_context_used(kc_llm_t *ctx, int64_t *out) {
    llama_memory_t mem;
    llama_pos pos_min;
    llama_pos pos_max;
    if (!ctx || !ctx->ctx || !out) return -1;
    mem = llama_get_memory(ctx->ctx);
    pos_max = llama_memory_seq_pos_max(mem, 0);
    if (pos_max < 0) {
        *out = 0;
        return 0;
    }
    pos_min = llama_memory_seq_pos_min(mem, 0);
    if (pos_min < 0 || pos_min > pos_max) return -1;
    *out = (int64_t)pos_max - (int64_t)pos_min + 1;
    return 0;
}

/**
 * kc_llm_context_size - Return the effective runtime context capacity.
 * @param ctx Context pointer.
 * @param out Output context size.
 * @return 0 on success, -1 on failure.
 */
static int kc_llm_context_size(kc_llm_t *ctx, int64_t *out) {
    uint32_t n_ctx;
    if (!ctx || !ctx->ctx || !out) return -1;
    n_ctx = llama_n_ctx(ctx->ctx);
    *out = (int64_t)n_ctx;
    return 0;
}

/**
 * kc_llm_info_store_meta - Cache a metadata string on the context.
 * @param model Loaded model.
 * @param key Metadata key name.
 * @param dst Destination owned string.
 * @return 0 on success, -1 on allocation failure.
 */
static int kc_llm_info_store_meta(const struct llama_model *model, const char *key,
    char **dst) {
    int32_t needed;
    char *buf;
    if (!model || !key || !dst) return -1;
    needed = llama_model_meta_val_str(model, key, NULL, 0);
    if (needed < 0) {
        free(*dst);
        *dst = NULL;
        return 0;
    }
    buf = (char *)malloc((size_t)needed + 1);
    if (!buf) return -1;
    if (llama_model_meta_val_str(model, key, buf, (size_t)needed + 1) < 0) {
        free(buf);
        free(*dst);
        *dst = NULL;
        return 0;
    }
    free(*dst);
    *dst = buf;
    return 0;
}

/**
 * kc_llm_info_refresh_static_strings - Refresh cached metadata strings.
 * @param ctx Context pointer.
 * @return 0 on success, -1 on failure.
 */
static int kc_llm_info_refresh_static_strings(kc_llm_t *ctx) {
    if (!ctx || !ctx->model) return -1;
    if (kc_llm_info_store_meta(ctx->model, "general.architecture",
            &ctx->info_architecture) != 0) {
        return -1;
    }
    if (kc_llm_info_store_meta(ctx->model, "general.name", &ctx->info_name) != 0) {
        return -1;
    }
    return 0;
}

/**
 * kc_llm_model_file_size - Return the source model file size.
 * @param path Model file path.
 * @param out Output size in bytes.
 * @return 0 on success, -1 on failure.
 */
static int kc_llm_model_file_size(const char *path, uint64_t *out) {
    struct stat st;
    if (!path || !out) return -1;
    if (stat(path, &st) != 0) return -1;
    if (st.st_size < 0) return -1;
    *out = (uint64_t)st.st_size;
    return 0;
}

/**
 * kc_llm_version - Return the build version.
 * @return Build version timestamp.
 */
uint64_t kc_llm_version(void) {
    return (uint64_t)KC_LLM_BUILD_VERSION;
}

/**
 * get_cpu_count - Return the number of online CPU cores.
 * @return CPU count, or a fallback value.
 */
static int get_cpu_count(void) {
#ifdef __EMSCRIPTEN__
    return 1;
#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (si.dwNumberOfProcessors > 0) return (int)si.dwNumberOfProcessors;
#elif defined(__APPLE__)
    int n = 0;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.ncpu", &n, &len, NULL, 0) == 0 && n > 0) return n;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) return (int)n;
#endif
    return 4;
}

/**
 * apply_gpu_mode - Apply GPU offload settings to model params.
 * @param opts Options with GPU settings.
 * @param mparams Model parameters to update.
 * @return 0 on success, -1 when requested GPU is unavailable.
 */
static int apply_gpu_mode(const kc_llm_options_t *opts, struct llama_model_params *mparams) {
    int supports_gpu;

    if (opts->gpu == 0) {
        mparams->n_gpu_layers = 0;
        return 0;
    }

    supports_gpu = llama_supports_gpu_offload() ? 1 : 0;

    if (opts->gpu < 0) {
        if (supports_gpu) {
            mparams->n_gpu_layers = opts->gpu_layers;
        } else {
            mparams->n_gpu_layers = 0;
        }
        return 0;
    }

    if (opts->gpu > 0) {
        if (!supports_gpu) {
            return -1;
        }
        mparams->n_gpu_layers = opts->gpu_layers;
        return 0;
    }

    mparams->n_gpu_layers = 0;
    return 0;
}

/**
 * build_params - Build llama.cpp model and context parameters.
 * @param opts Options containing runtime settings.
 * @param mparams Output model parameters.
 * @param cparams Output context parameters.
 * @param fit_bufs Temporary buffers needed by llama.cpp parameter fitting.
 * @param err Error output buffer.
 * @param err_size Error output buffer size.
 * @return 0 on success, -1 on failure.
 */
static int build_params(const kc_llm_options_t *opts,
    struct llama_model_params *mparams,
    struct llama_context_params *cparams,
    kc_llm_fit_buffers_t *fit_bufs,
    char *err, size_t err_size) {
    common_params_fit_status fit_status;
    size_t n_devices;

    if (!opts || !mparams || !cparams || !fit_bufs) return -1;

    *mparams = llama_model_default_params();
    *cparams = llama_context_default_params();
    memset(fit_bufs, 0, sizeof(*fit_bufs));
    if (opts->ctx > 0) {
        cparams->n_ctx = (uint32_t)opts->ctx;
    }
    cparams->n_batch = 2048;
    cparams->n_ubatch = 512;
    cparams->n_threads = opts->threads > 0 ? opts->threads : get_cpu_count();
    cparams->n_threads_batch = cparams->n_threads;
    cparams->no_perf = 1;
    apply_flash_attention(opts, cparams);

    if (apply_gpu_mode(opts, mparams) != 0) {
        snprintf(err, err_size, "gpu mode %d requires a GPU, but none is available", opts->gpu);
        return -1;
    }

    if (mparams->n_gpu_layers < 0 && llama_supports_gpu_offload()) {
        n_devices = llama_max_devices();
        fit_bufs->tensor_split = (float *)calloc(n_devices > 0 ? n_devices : 1, sizeof(float));
        fit_bufs->tensor_buft_overrides = (struct llama_model_tensor_buft_override *)calloc(
            llama_max_tensor_buft_overrides(), sizeof(struct llama_model_tensor_buft_override));
        fit_bufs->margins = (size_t *)calloc(n_devices > 0 ? n_devices : 1, sizeof(size_t));
        if (!fit_bufs->tensor_split || !fit_bufs->tensor_buft_overrides || !fit_bufs->margins) {
            kc_llm_fit_buffers_free(fit_bufs);
            snprintf(err, err_size, "out of memory");
            return -1;
        }
        for (size_t i = 0; i < n_devices; i++) {
            fit_bufs->margins[i] = 1024U * 1024U * 1024U;
        }
        fit_status = common_fit_params(opts->model_path, mparams, cparams,
            fit_bufs->tensor_split, fit_bufs->tensor_buft_overrides,
            fit_bufs->margins, 4096, NULL, GGML_LOG_LEVEL_ERROR);
        if (fit_status == COMMON_PARAMS_FIT_STATUS_ERROR) {
            kc_llm_fit_buffers_free(fit_bufs);
            snprintf(err, err_size, "failed to fit model parameters to device memory");
            return -1;
        }
    }

    return 0;
}

/**
 * build_draft_params - Build model and context params for a draft model.
 * @param opts Main runtime options.
 * @param mparams Output model params.
 * @param cparams Output context params.
 * @param fit_bufs Temporary fit buffers.
 * @param err Error output buffer.
 * @param err_size Error output buffer size.
 * @return 0 on success, -1 on failure.
 */
static int build_draft_params(const kc_llm_options_t *opts,
    struct llama_model_params *mparams,
    struct llama_context_params *cparams,
    kc_llm_fit_buffers_t *fit_bufs,
    char *err, size_t err_size) {
    if (!opts || !opts->mtp_path || !mparams || !cparams || !fit_bufs) return -1;
    *mparams = llama_model_default_params();
    *cparams = llama_context_default_params();
    memset(fit_bufs, 0, sizeof(*fit_bufs));
    if (opts->ctx > 0) {
        cparams->n_ctx = (uint32_t)opts->ctx;
    }
    cparams->n_batch = 2048;
    cparams->n_ubatch = 512;
    cparams->n_threads = opts->mtp_threads > 0 ? opts->mtp_threads :
        (opts->threads > 0 ? opts->threads : get_cpu_count());
    cparams->n_threads_batch = cparams->n_threads;
    cparams->no_perf = 1;
    apply_flash_attention(opts, cparams);

    if (opts->gpu == 0) {
        mparams->n_gpu_layers = 0;
    } else if (llama_supports_gpu_offload()) {
        mparams->n_gpu_layers = opts->mtp_gpu_layers;
    } else if (opts->gpu > 0) {
        snprintf(err, err_size, "gpu mode %d requires a GPU, but none is available", opts->gpu);
        return -1;
    }

    return 0;
}

/**
 * load_model - Load a llama.cpp model from disk.
 * @param opts Options containing the model path.
 * @param mparams Prepared model parameters.
 * @param err Error output buffer.
 * @param err_size Error output buffer size.
 * @return Loaded model, or NULL on failure.
 */
static struct llama_model *load_model(const kc_llm_options_t *opts,
    const struct llama_model_params *mparams, char *err, size_t err_size) {
    struct llama_model *model;
    model = llama_model_load_from_file(opts->model_path, *mparams);
    if (!model) {
        snprintf(err, err_size, "failed to load model: %s", opts->model_path);
        return NULL;
    }
    return model;
}

/**
 * create_context - Create a llama.cpp inference context.
 * @param model Loaded model.
 * @param opts Options containing context settings.
 * @param err Error output buffer.
 * @param err_size Error output buffer size.
 * @return Created context, or NULL on failure.
 */
static struct llama_context *create_context(struct llama_model *model,
    const struct llama_context_params *cparams, char *err, size_t err_size) {
    struct llama_context *ctx;
    ctx = llama_init_from_model(model, *cparams);
    if (!ctx) {
        snprintf(err, err_size, "failed to create context");
        return NULL;
    }
    return ctx;
}

/**
 * apply_lora - Load and apply configured LoRA adapters.
 * @param ctx Library context.
 * @param opts Options containing LoRA paths.
 * @param err Error output buffer.
 * @param err_size Error output buffer size.
 * @return 0 on success, -1 on failure.
 */
static int apply_lora(kc_llm_t *ctx, const kc_llm_options_t *opts, char *err, size_t err_size) {
    float scales[KC_LLM_MAX_LORAS];
    int i;
    if (opts->n_loras == 0) return 0;
    for (i = 0; i < opts->n_loras; i++) {
        ctx->adapters[i] = llama_adapter_lora_init(ctx->model, opts->lora_paths[i]);
        if (!ctx->adapters[i]) {
            snprintf(err, err_size, "failed to load LoRA adapter: %s", opts->lora_paths[i]);
            while (--i >= 0) {
                llama_adapter_lora_free(ctx->adapters[i]);
                ctx->adapters[i] = NULL;
            }
            return -1;
        }
        scales[i] = opts->lora_scales[i];
        ctx->n_adapters++;
    }
    if (llama_set_adapters_lora(ctx->ctx, ctx->adapters, (size_t)opts->n_loras, scales) != 0) {
        snprintf(err, err_size, "failed to apply LoRA adapters");
        for (i = 0; i < ctx->n_adapters; i++) {
            llama_adapter_lora_free(ctx->adapters[i]);
            ctx->adapters[i] = NULL;
        }
        ctx->n_adapters = 0;
        return -1;
    }
    return 0;
}

/**
 * build_sampler - Build a llama.cpp sampler chain.
 * @param vocab Model vocabulary.
 * @param opts Sampling options.
 * @return Sampler chain, or NULL on failure.
 */
static struct llama_sampler *build_sampler(const struct llama_vocab *vocab, const kc_llm_options_t *opts) {
    struct llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    struct llama_sampler *chain;
    uint32_t seed;
    (void)vocab;
    sparams.no_perf = 1;
    chain = llama_sampler_chain_init(sparams);
    if (!chain) return NULL;
    if (opts->repeat_penalty != 1.0f) {
        int32_t last_n = opts->repeat_last_n;
        if (last_n < 0) last_n = -1;
        llama_sampler_chain_add(chain, llama_sampler_init_penalties(llama_vocab_n_tokens(vocab), last_n, opts->repeat_penalty, 0.0f, 0.0f));
    }
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(opts->top_k));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(opts->top_p, 1));
    if (opts->min_p > 0.0f) {
        llama_sampler_chain_add(chain, llama_sampler_init_min_p(opts->min_p, 1));
    }
    llama_sampler_chain_add(chain, llama_sampler_init_temp(opts->temp));
    seed = opts->seed >= 0 ? (uint32_t)opts->seed : (uint32_t)time(NULL);
    llama_sampler_chain_add(chain, llama_sampler_init_dist(seed));
    return chain;
}

/**
 * copy_options - Deep-copy public options into context-owned storage.
 * @param dst Destination options.
 * @param src Source options.
 * @return 0 on success, -1 on allocation failure.
 */
static int copy_options(kc_llm_options_t *dst, const kc_llm_options_t *src) {
    int i;
    *dst = *src;
    dst->model_path = src->model_path ? kc_llm_strdup(src->model_path) : NULL;
    dst->kv_load_path = src->kv_load_path ? kc_llm_strdup(src->kv_load_path) : NULL;
    dst->kv_save_path = src->kv_save_path ? kc_llm_strdup(src->kv_save_path) : NULL;
    dst->mmproj_path = src->mmproj_path ? kc_llm_strdup(src->mmproj_path) : NULL;
    dst->role = src->role ? kc_llm_strdup(src->role) : NULL;
    dst->mtp_path = src->mtp_path ? kc_llm_strdup(src->mtp_path) : NULL;
    dst->lora_paths = NULL;
    dst->lora_scales = NULL;
    dst->images = NULL;
    if (src->n_loras > 0) {
        dst->lora_paths = (char **)calloc(src->n_loras, sizeof(char *));
        dst->lora_scales = (float *)calloc(src->n_loras, sizeof(float));
        if (!dst->lora_paths || !dst->lora_scales) return -1;
        for (i = 0; i < src->n_loras; i++) {
            dst->lora_paths[i] = src->lora_paths[i] ? kc_llm_strdup(src->lora_paths[i]) : NULL;
            dst->lora_scales[i] = src->lora_scales[i];
        }
    }
    if (src->n_images > 0) {
        dst->images = (char **)calloc(src->n_images, sizeof(char *));
        if (!dst->images) return -1;
        for (i = 0; i < src->n_images; i++) {
            dst->images[i] = src->images[i] ? kc_llm_strdup(src->images[i]) : NULL;
        }
    }
    dst->n_loras = src->n_loras;
    dst->n_images = src->n_images;
    return 0;
}

/**
 * kc_llm_apply_model_chat_template - Render one message through
 * the model template.
 * @param ctx Context to use.
 * @param role Message role string.
 * @param content Message content string.
 * @return Allocated prompt string, or NULL when templating is unavailable.
 */
static char *kc_llm_apply_model_chat_template(kc_llm_t *ctx, const char *role,
    const char *content) {
    common_chat_templates_ptr tmpls;
    common_chat_templates_inputs inputs;
    common_chat_msg msg;
    std::string prompt;
    char *out;
    size_t len;
    int verbosity_thold = common_log_get_verbosity_thold();

    if (!ctx || !ctx->model || !content) return NULL;

    try {
        if (verbosity_thold >= LOG_LEVEL_WARN) {
            common_log_set_verbosity_thold(LOG_LEVEL_ERROR);
        }
        tmpls = common_chat_templates_init(ctx->model, "");
        if (verbosity_thold >= LOG_LEVEL_WARN) {
            common_log_set_verbosity_thold(verbosity_thold);
        }
        if (!tmpls) return NULL;

        msg.role = role ? role : "user";
        msg.content = content;
        inputs.messages.push_back(msg);
        inputs.add_generation_prompt = true;
        inputs.use_jinja = true;
        inputs.enable_thinking = ctx->opts.think != 0;
        if (verbosity_thold >= LOG_LEVEL_WARN) {
            common_log_set_verbosity_thold(LOG_LEVEL_ERROR);
        }
        prompt = common_chat_templates_apply(tmpls.get(), inputs).prompt;
        if (verbosity_thold >= LOG_LEVEL_WARN) {
            common_log_set_verbosity_thold(verbosity_thold);
        }
    } catch (...) {
        if (verbosity_thold >= LOG_LEVEL_WARN) {
            common_log_set_verbosity_thold(verbosity_thold);
        }
        return NULL;
    }

    len = prompt.size();
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, prompt.c_str(), len + 1);
    return out;
}

/**
 * kc_llm_prepare_prompt - Render chat content through model
 * templating when available.
 * @param ctx Context to use.
 * @param role Message role string.
 * @param content Message content text.
 * @return Allocated prompt string, or NULL on failure.
 */
static char *kc_llm_prepare_prompt(kc_llm_t *ctx, const char *role,
    const char *content) {
    char *buf;

    if (!ctx || !content) return NULL;
    if (!role) role = "user";
    if (!kc_llm_role_valid(role)) {
        kc_llm_set_err(ctx, "invalid role: %s", role);
        return NULL;
    }

    buf = kc_llm_apply_model_chat_template(ctx, role, content);
    if (buf) return buf;
    buf = kc_llm_strdup(content);
    if (!buf) kc_llm_set_err(ctx, "out of memory");
    return buf;
}

/**
 * kc_llm_open_internal - Allocate and initialize a model or runtime context.
 * @param out Pointer to receive the context.
 * @param opts Options to use.
 * @param model_only Non-zero to load model metadata without runtime state.
 * @param errbuf Optional buffer receiving the failure reason.
 * @param errcap Size of errbuf.
 * @return KC_LLM_OK on success, or KC_LLM_ERROR on failure.
 */
static int kc_llm_open_internal(kc_llm_t **out, const kc_llm_options_t *opts,
    int model_only, char *errbuf, size_t errcap) {
    kc_llm_t *ctx;
    struct llama_model_params mparams;
    struct llama_model_params draft_mparams;
    struct llama_context_params cparams;
    struct llama_context_params draft_cparams;
    kc_llm_fit_buffers_t fit_bufs;
    kc_llm_fit_buffers_t draft_fit_bufs = {};
    if (!out || !opts) return KC_LLM_ERROR;
    *out = NULL;
    if (!opts->model_path || !opts->model_path[0]) return KC_LLM_ERROR;

    ctx = (kc_llm_t *)calloc(1, sizeof(kc_llm_t));
    if (!ctx) return KC_LLM_ERROR;

    if (copy_options(&ctx->opts, opts) != 0) {
        free(ctx);
        return KC_LLM_ERROR;
    }
    ctx->model_only = model_only != 0;

    g_llama_log_error[0] = '\0';
    llama_log_set(llm_log_callback, NULL);
    llama_backend_init();

    if (model_only) {
        mparams = llama_model_default_params();
        ctx->model = load_model(opts, &mparams, ctx->error, sizeof(ctx->error));
        if (!ctx->model) goto fail;
        ctx->vocab = llama_model_get_vocab(ctx->model);
        if (kc_llm_info_refresh_static_strings(ctx) != 0) {
            kc_llm_set_err(ctx, "out of memory");
            goto fail;
        }
        *out = ctx;
        return KC_LLM_OK;
    }

    if (build_params(opts, &mparams, &cparams, &fit_bufs, ctx->error, sizeof(ctx->error)) != 0) {
        goto fail;
    }

    ctx->model = load_model(opts, &mparams, ctx->error, sizeof(ctx->error));
    kc_llm_fit_buffers_free(&fit_bufs);
    if (!ctx->model) goto fail;

    if (kc_llm_info_refresh_static_strings(ctx) != 0) {
        kc_llm_set_err(ctx, "out of memory");
        goto fail;
    }

    ctx->ctx = create_context(ctx->model, &cparams, ctx->error, sizeof(ctx->error));
    if (!ctx->ctx) goto fail;

    if (opts->mtp_path && opts->mtp_path[0]) {
        kc_llm_options_t draft_opts = *opts;

        if (build_draft_params(opts, &draft_mparams, &draft_cparams,
                &draft_fit_bufs, ctx->error, sizeof(ctx->error)) != 0) {
            goto fail;
        }
        draft_opts.model_path = opts->mtp_path;
        ctx->draft_model = load_model(&draft_opts, &draft_mparams,
            ctx->error, sizeof(ctx->error));
        kc_llm_fit_buffers_free(&draft_fit_bufs);
        if (!ctx->draft_model) goto fail;

        draft_cparams.ctx_type = LLAMA_CONTEXT_TYPE_MTP;
        draft_cparams.n_rs_seq = 0;
        draft_cparams.n_outputs_max = 1;
        draft_cparams.ctx_other = ctx->ctx;

        ctx->draft_ctx = create_context(ctx->draft_model, &draft_cparams,
            ctx->error, sizeof(ctx->error));
        if (!ctx->draft_ctx) goto fail;
    }

    if (apply_lora(ctx, opts, ctx->error, sizeof(ctx->error)) != 0) goto fail;

    ctx->vocab = llama_model_get_vocab(ctx->model);
    ctx->sampler = build_sampler(ctx->vocab, opts);
    if (!ctx->sampler) {
        kc_llm_set_err(ctx, "failed to create sampler chain");
        goto fail;
    }

    ctx->has_vision = (opts->n_images > 0 && opts->mmproj_path);
    if (ctx->has_vision && opts->mtp_path && opts->mtp_path[0]) {
        kc_llm_set_err(ctx, "MTP is not supported with image inputs yet");
        goto fail;
    }
    if (ctx->has_vision) {
        struct mtmd_context_params mparams = mtmd_context_params_default();
        mparams.n_threads = opts->threads > 0 ? opts->threads : 4;
        mparams.use_gpu = (opts->gpu > 0);
        mparams.flash_attn_type = opts->fattn < 0 ? LLAMA_FLASH_ATTN_TYPE_AUTO :
            (opts->fattn == 0 ? LLAMA_FLASH_ATTN_TYPE_DISABLED : LLAMA_FLASH_ATTN_TYPE_ENABLED);
        mtmd_helper_log_set(llm_log_callback, NULL);
        ctx->mctx = mtmd_init_from_file(opts->mmproj_path, ctx->model, mparams);
        if (!ctx->mctx) {
            kc_llm_set_err(ctx, "failed to load mmproj: %s", opts->mmproj_path);
            goto fail;
        }
        if (!mtmd_support_vision(ctx->mctx)) {
            kc_llm_set_err(ctx, "mmproj does not support vision: %s", opts->mmproj_path);
            goto fail;
        }
    }

    if (opts->kv_load_path) {
        size_t n_loaded = 0;
        size_t loaded_bytes;
        size_t n_ctx = (size_t)llama_n_ctx(ctx->ctx);
        llama_token *tokens = (llama_token *)malloc(sizeof(llama_token) * n_ctx);
        if (!tokens) {
            kc_llm_set_err(ctx, "out of memory");
            goto fail;
        }
        loaded_bytes = llama_state_seq_load_file(ctx->ctx, opts->kv_load_path, 0, tokens, n_ctx, &n_loaded);
        free(tokens);
        if (loaded_bytes == 0) {
            kc_llm_set_err(ctx, "failed to load KV state: %s", opts->kv_load_path);
            goto fail;
        }
        ctx->n_tokens = n_loaded;
        ctx->pos = (llama_pos)n_loaded;
    }

    *out = ctx;
    return KC_LLM_OK;

fail:
    if (errbuf && errcap > 0) {
        if (ctx && ctx->error[0]) snprintf(errbuf, errcap, "%s", ctx->error);
        else if (g_llama_log_error[0]) snprintf(errbuf, errcap, "%s", g_llama_log_error);
        else errbuf[0] = '\0';
    }
    kc_llm_close(ctx);
    return KC_LLM_ERROR;
}

/**
 * kc_llm_open - Allocate and initialize an LLM runtime context.
 * @param out Pointer to receive the context.
 * @param opts Options to use.
 * @return KC_LLM_OK on success, or KC_LLM_ERROR on failure.
 */
int kc_llm_open(kc_llm_t **out, const kc_llm_options_t *opts) {
    return kc_llm_open_internal(out, opts, 0, NULL, 0);
}

/**
 * kc_llm_open_model - Allocate and initialize a model-only context.
 * @param out Pointer to receive the context.
 * @param opts Options to use.
 * @return KC_LLM_OK on success, or KC_LLM_ERROR on failure.
 */
int kc_llm_open_model(kc_llm_t **out, const kc_llm_options_t *opts) {
    return kc_llm_open_internal(out, opts, 1, NULL, 0);
}

/**
 * kc_llm_close - Release an LLM context.
 * @param ctx Context to release.
 * @return None.
 */
void kc_llm_close(kc_llm_t *ctx) {
    int i;
    if (!ctx) return;
    if (ctx->opts.kv_save_path && ctx->ctx) {
        size_t n_ctx = (size_t)llama_n_ctx(ctx->ctx);
        llama_token *tokens = (llama_token *)malloc(sizeof(llama_token) * (n_ctx > 0 ? n_ctx : 1));
        if (tokens) {
            llama_state_seq_save_file(ctx->ctx, ctx->opts.kv_save_path, 0, tokens, ctx->n_tokens);
            free(tokens);
        }
    }
    free(ctx->info_architecture);
    free(ctx->info_name);
    kc_llm_options_free(&ctx->opts);
    if (ctx->mctx) mtmd_free(ctx->mctx);
    if (ctx->sampler) llama_sampler_free(ctx->sampler);
    if (ctx->draft_ctx) llama_free(ctx->draft_ctx);
    if (ctx->ctx) llama_free(ctx->ctx);
    for (i = 0; i < ctx->n_adapters; i++) {
        if (ctx->adapters[i]) llama_adapter_lora_free(ctx->adapters[i]);
    }
    if (ctx->draft_model) llama_model_free(ctx->draft_model);
    if (ctx->model) llama_model_free(ctx->model);
    llama_backend_free();
    free(ctx);
}

/**
 * kc_llm_stop - Request generation stop for a context.
 * @param ctx Context to stop.
 * @return KC_LLM_OK on success, KC_LLM_ERROR on failure.
 */
int kc_llm_stop(kc_llm_t *ctx) {
    if (!ctx) return KC_LLM_ERROR;
    ctx->stop_requested = 1;
    return KC_LLM_OK;
}

/**
 * kc_llm_stop_requested - Return whether stop was requested.
 * @param ctx Context pointer.
 * @return 1 when stop was requested, or 0 otherwise.
 */
int kc_llm_stop_requested(kc_llm_t *ctx) {
    if (!ctx) return 0;
    return ctx->stop_requested ? 1 : 0;
}

/**
 * kc_llm_kv_save - Write the active KV state to one file.
 * @param ctx Context pointer.
 * @param path Destination file path.
 * @return KC_LLM_OK on success, or KC_LLM_ERROR on failure.
 */
static int kc_llm_kv_save(kc_llm_t *ctx, const char *path) {
    size_t n_ctx;
    llama_token *tokens;

    if (!ctx || !ctx->ctx || !path || !path[0]) return KC_LLM_ERROR;

    n_ctx = (size_t)llama_n_ctx(ctx->ctx);
    tokens = (llama_token *)malloc(sizeof(llama_token) * (n_ctx > 0 ? n_ctx : 1));
    if (!tokens) {
        kc_llm_set_err(ctx, "out of memory");
        return KC_LLM_ERROR;
    }

    if (llama_state_seq_save_file(ctx->ctx, path, 0, tokens, ctx->n_tokens) == 0) {
        free(tokens);
        kc_llm_set_err(ctx, "failed to save KV state: %s", path);
        return KC_LLM_ERROR;
    }

    free(tokens);
    return KC_LLM_OK;
}

/**
 * kc_llm_kv_load - Load one KV state file into the active runtime.
 * @param ctx Context pointer.
 * @param path Source file path.
 * @return KC_LLM_OK on success, or KC_LLM_ERROR on failure.
 */
static int kc_llm_kv_load(kc_llm_t *ctx, const char *path) {
    size_t n_loaded = 0;
    size_t loaded_bytes;
    size_t n_ctx;
    llama_token *tokens;

    if (!ctx || !ctx->ctx || !path || !path[0]) return KC_LLM_ERROR;

    if (kc_llm_memory_clear(ctx) != KC_LLM_OK) return KC_LLM_ERROR;

    n_ctx = (size_t)llama_n_ctx(ctx->ctx);
    tokens = (llama_token *)malloc(sizeof(llama_token) * (n_ctx > 0 ? n_ctx : 1));
    if (!tokens) {
        kc_llm_set_err(ctx, "out of memory");
        return KC_LLM_ERROR;
    }

    loaded_bytes = llama_state_seq_load_file(ctx->ctx, path, 0, tokens, n_ctx, &n_loaded);
    free(tokens);
    if (loaded_bytes == 0) {
        kc_llm_set_err(ctx, "failed to load KV state: %s", path);
        return KC_LLM_ERROR;
    }

    if (ctx->sampler) llama_sampler_reset(ctx->sampler);
    ctx->n_tokens = n_loaded;
    ctx->pos = (llama_pos)n_loaded;
    return KC_LLM_OK;
}

/**
 * kc_llm_sampler_rebuild - Rebuild the sampler from current options.
 * @param ctx Context pointer.
 * @return KC_LLM_OK on success, or KC_LLM_ERROR on failure.
 */
static int kc_llm_sampler_rebuild(kc_llm_t *ctx) {
    struct llama_sampler *sampler;

    if (!ctx || !ctx->vocab) return KC_LLM_ERROR;

    sampler = build_sampler(ctx->vocab, &ctx->opts);
    if (!sampler) {
        kc_llm_set_err(ctx, "failed to create sampler chain");
        return KC_LLM_ERROR;
    }

    if (ctx->sampler) llama_sampler_free(ctx->sampler);
    ctx->sampler = sampler;
    return KC_LLM_OK;
}

/**
 * kc_llm_memory_clear - Clear KV cache and sampler state.
 * @param ctx Context to reset.
 * @return KC_LLM_OK on success, KC_LLM_ERROR on failure.
 */
int kc_llm_memory_clear(kc_llm_t *ctx) {
    if (!ctx || !ctx->ctx) return KC_LLM_ERROR;
    llama_memory_clear(llama_get_memory(ctx->ctx), 1);
    if (ctx->draft_ctx) {
        llama_memory_clear(llama_get_memory(ctx->draft_ctx), 1);
    }
    if (ctx->sampler) llama_sampler_reset(ctx->sampler);
    ctx->n_tokens = 0;
    ctx->pos = 0;
    return KC_LLM_OK;
}

/**
 * kc_llm_generate_text - Run text-only generation.
 * @param ctx Context to use.
 * @param prompt Prompt text.
 * @param write Output callback.
 * @param user User pointer for output callback.
 * @return KC_LLM_OK on success, KC_LLM_ERROR or KC_LLM_ESTOP on failure.
 */
static int kc_llm_generate_text(kc_llm_t *ctx, const char *prompt, kc_llm_write_fn write, void *user) {
    struct llama_batch prompt_batch;
    llama_token *tokens = NULL;
    llama_token new_token;
    int32_t n_prompt_tokens;
    int32_t n_piece;
    int n_generated = 0;
    int max_predict;
    int rc = KC_LLM_ERROR;
    size_t n_tokens_cap;
    int i;
    char piece_buf[256];

    if (!ctx || !ctx->ctx || !ctx->sampler || !prompt) return KC_LLM_ERROR;

    max_predict = ctx->opts.predict < 0 ? INT_MAX : ctx->opts.predict;

    n_tokens_cap = ctx->opts.ctx > 0 ? (size_t)ctx->opts.ctx : (size_t)llama_n_ctx(ctx->ctx);
    if (n_tokens_cap == 0) n_tokens_cap = 4096;

    n_prompt_tokens = -llama_tokenize(ctx->vocab, prompt, (int32_t)strlen(prompt), NULL, 0, 1, 1);
    if (n_prompt_tokens <= 0) return KC_LLM_OK;
    if ((size_t)n_prompt_tokens > n_tokens_cap - ctx->n_tokens) {
        kc_llm_set_err(ctx, "context full");
        return KC_LLM_ERROR;
    }

    tokens = (llama_token *)malloc(sizeof(llama_token) * (size_t)n_prompt_tokens);
    if (!tokens) {
        kc_llm_set_err(ctx, "out of memory");
        return KC_LLM_ERROR;
    }

    if (llama_tokenize(ctx->vocab, prompt, (int32_t)strlen(prompt), tokens, n_prompt_tokens, 1, 1) < 0) {
        kc_llm_set_err(ctx, "prompt tokenization failed");
        goto cleanup;
    }

    prompt_batch = llama_batch_init(n_prompt_tokens, 0, 1);
    prompt_batch.n_tokens = n_prompt_tokens;
    for (i = 0; i < n_prompt_tokens; i++) {
        prompt_batch.token[i] = tokens[i];
        prompt_batch.pos[i] = ctx->pos + i;
        prompt_batch.n_seq_id[i] = 1;
        prompt_batch.seq_id[i][0] = 0;
        prompt_batch.logits[i] = 0;
    }
    prompt_batch.logits[n_prompt_tokens - 1] = 1;
    if (llama_decode(ctx->ctx, prompt_batch) != 0) {
        kc_llm_set_err(ctx, "failed to decode prompt");
        llama_batch_free(prompt_batch);
        goto cleanup;
    }
    llama_batch_free(prompt_batch);

    ctx->n_tokens += (size_t)n_prompt_tokens;
    ctx->pos += (llama_pos)n_prompt_tokens;

    new_token = llama_sampler_sample(ctx->sampler, ctx->ctx, -1);
    while (n_generated < max_predict && !llama_vocab_is_eog(ctx->vocab, new_token)) {
        if (ctx->stop_requested) {
            rc = KC_LLM_ESTOP;
            goto cleanup;
        }
        n_piece = llama_token_to_piece(ctx->vocab, new_token, piece_buf, sizeof(piece_buf), 0, 0);
        if (n_piece < 0) n_piece = 0;
        if (n_piece > 0) {
            if (write && write(piece_buf, (size_t)n_piece, user) != 0) {
                kc_llm_set_err(ctx, "failed to write output");
                goto cleanup;
            }
        }
        n_generated++;
        if (ctx->n_tokens >= n_tokens_cap) {
            kc_llm_set_err(ctx, "context full");
            goto cleanup;
        }
        {
            struct llama_batch batch = llama_batch_get_one(&new_token, 1);
            if (llama_decode(ctx->ctx, batch) != 0) {
                kc_llm_set_err(ctx, "failed to decode generated token");
                goto cleanup;
            }
        }
        ctx->n_tokens++;
        ctx->pos++;
        new_token = llama_sampler_sample(ctx->sampler, ctx->ctx, -1);
    }

    rc = KC_LLM_OK;

cleanup:
    free(tokens);
    return rc;
}

/**
 * kc_llm_generate_text_mtp - Run text generation with speculative MTP.
 * @param ctx Context to use.
 * @param prompt Prompt text.
 * @param write Output callback.
 * @param user User pointer for output callback.
 * @return KC_LLM_OK on success, KC_LLM_ERROR or KC_LLM_ESTOP on failure.
 */
static int kc_llm_generate_text_mtp(kc_llm_t *ctx, const char *prompt,
    kc_llm_write_fn write, void *user) {
    common_sampler_ptr smpl;
    common_speculative_ptr spec;
    kc_llm_speculative_t spec_params;
    common_params_sampling sampling_params;
    llama_tokens prompt_toks;
    llama_tokens draft;
    std::vector<llama_token> ids;
    llama_token *tokens = NULL;
    struct llama_batch prefill;
    struct llama_batch batch_tgt;
    llama_token id_last;
    int32_t n_prompt_tokens;
    int max_predict;
    int n_generated = 0;
    int n_past;
    int32_t n_piece;
    int rc = KC_LLM_ERROR;
    size_t n_tokens_cap;
    int i;
    int flush_last = 0;
    char piece_buf[256];

    if (!ctx || !ctx->ctx || !ctx->draft_ctx || !prompt) return KC_LLM_ERROR;

    if (common_context_can_seq_rm(ctx->ctx) == COMMON_CONTEXT_SEQ_RM_TYPE_NO ||
        common_context_can_seq_rm(ctx->draft_ctx) == COMMON_CONTEXT_SEQ_RM_TYPE_NO) {
        kc_llm_set_err(ctx, "MTP is not supported by this context");
        return KC_LLM_ERROR;
    }

    max_predict = ctx->opts.predict < 0 ? INT_MAX : ctx->opts.predict;
    n_tokens_cap = ctx->opts.ctx > 0 ? (size_t)ctx->opts.ctx : (size_t)llama_n_ctx(ctx->ctx);
    if (n_tokens_cap == 0) n_tokens_cap = 4096;

    n_prompt_tokens = -llama_tokenize(ctx->vocab, prompt, (int32_t)strlen(prompt), NULL, 0, 1, 1);
    if (n_prompt_tokens <= 0) return KC_LLM_OK;
    if ((size_t)n_prompt_tokens > n_tokens_cap - ctx->n_tokens) {
        kc_llm_set_err(ctx, "context full");
        return KC_LLM_ERROR;
    }

    tokens = (llama_token *)malloc(sizeof(llama_token) * (size_t)n_prompt_tokens);
    if (!tokens) {
        kc_llm_set_err(ctx, "out of memory");
        return KC_LLM_ERROR;
    }
    if (llama_tokenize(ctx->vocab, prompt, (int32_t)strlen(prompt), tokens,
            n_prompt_tokens, 1, 1) < 0) {
        kc_llm_set_err(ctx, "prompt tokenization failed");
        goto cleanup_mtp;
    }

    sampling_params = build_common_sampling(&ctx->opts);
    smpl.reset(common_sampler_init(ctx->model, sampling_params));
    if (!smpl) {
        kc_llm_set_err(ctx, "failed to create sampler");
        goto cleanup_mtp;
    }

    spec_params = build_speculative_params(ctx);
    spec.reset(common_speculative_init(spec_params.spec, 1));
    if (!spec) {
        kc_llm_set_err(ctx, "failed to initialize MTP");
        goto cleanup_mtp;
    }

    if (n_prompt_tokens > 1) {
        prefill = llama_batch_init(n_prompt_tokens - 1, 0, 1);
        prefill.n_tokens = n_prompt_tokens - 1;
        for (i = 0; i < n_prompt_tokens - 1; i++) {
            prefill.token[i] = tokens[i];
            prefill.pos[i] = ctx->pos + i;
            prefill.n_seq_id[i] = 1;
            prefill.seq_id[i][0] = 0;
            prefill.logits[i] = 1;
            prompt_toks.push_back(tokens[i]);
        }
        if (llama_decode(ctx->ctx, prefill) != 0) {
            kc_llm_set_err(ctx, "failed to decode prompt");
            llama_batch_free(prefill);
            goto cleanup_mtp;
        }
        if (!common_speculative_process(spec.get(), prefill)) {
            kc_llm_set_err(ctx, "failed to process speculative prompt");
            llama_batch_free(prefill);
            goto cleanup_mtp;
        }
        llama_batch_free(prefill);
        ctx->n_tokens += (size_t)(n_prompt_tokens - 1);
        ctx->pos += (llama_pos)(n_prompt_tokens - 1);
    }

    id_last = tokens[n_prompt_tokens - 1];
    n_past = (int)ctx->pos;
    common_speculative_begin(spec.get(), 0, prompt_toks);
    batch_tgt = llama_batch_init(llama_n_batch(ctx->ctx), 0, 1);

    while (n_generated < max_predict) {
        if (ctx->stop_requested) {
            rc = KC_LLM_ESTOP;
            goto cleanup_batch_mtp;
        }

        common_speculative_get_draft_params(spec.get(), 0) = {
            true,
            -1,
            (llama_pos)n_past,
            id_last,
            &prompt_toks,
            &draft,
        };
        common_speculative_draft(spec.get());

        common_batch_clear(batch_tgt);
        common_batch_add(batch_tgt, id_last, n_past++, {0}, true);
        for (size_t j = 0; j < draft.size(); ++j) {
            common_batch_add(batch_tgt, draft[j], n_past + (int)j, {0}, true);
        }

        if (llama_decode(ctx->ctx, batch_tgt) != 0) {
            kc_llm_set_err(ctx, "failed to decode MTP batch");
            goto cleanup_batch_mtp;
        }
        if (!common_speculative_process(spec.get(), batch_tgt)) {
            kc_llm_set_err(ctx, "failed to process MTP batch");
            goto cleanup_batch_mtp;
        }

        ids = common_sampler_sample_and_accept_n(smpl.get(), ctx->ctx, draft);
        common_speculative_accept(spec.get(), 0, (uint16_t)(ids.size() - 1));
        n_past += (int)ids.size() - 1;

        for (size_t j = 0; j < ids.size(); ++j) {
            prompt_toks.push_back(id_last);
            id_last = ids[j];
            if (llama_vocab_is_eog(ctx->vocab, id_last)) {
                rc = KC_LLM_OK;
                goto cleanup_batch_mtp;
            }
            n_piece = llama_token_to_piece(ctx->vocab, id_last, piece_buf,
                sizeof(piece_buf), 0, 0);
            if (n_piece < 0) n_piece = 0;
            if (n_piece > 0) {
                if (write && write(piece_buf, (size_t)n_piece, user) != 0) {
                    kc_llm_set_err(ctx, "failed to write output");
                    goto cleanup_batch_mtp;
                }
            }
            n_generated++;
            if (n_generated >= max_predict) break;
        }

        llama_memory_seq_rm(llama_get_memory(ctx->ctx), 0, n_past, -1);
        llama_memory_seq_rm(llama_get_memory(ctx->draft_ctx), 0, n_past, -1);

        if (n_generated >= max_predict) {
            flush_last = 1;
            rc = KC_LLM_OK;
            goto cleanup_batch_mtp;
        }
        if ((size_t)n_past >= n_tokens_cap) {
            kc_llm_set_err(ctx, "context full");
            goto cleanup_batch_mtp;
        }
        draft.clear();
    }

    rc = KC_LLM_OK;

cleanup_batch_mtp:
    llama_memory_seq_rm(llama_get_memory(ctx->ctx), 0, n_past, -1);
    llama_memory_seq_rm(llama_get_memory(ctx->draft_ctx), 0, n_past, -1);
    if (flush_last && rc == KC_LLM_OK && !llama_vocab_is_eog(ctx->vocab, id_last)) {
        if (llama_decode(ctx->ctx, llama_batch_get_one(&id_last, 1)) == 0) {
            prompt_toks.push_back(id_last);
            n_past++;
        }
    }
    ctx->n_tokens = prompt_toks.size();
    ctx->pos = (llama_pos)prompt_toks.size();
    llama_batch_free(batch_tgt);
cleanup_mtp:
    free(tokens);
    return rc;
}

/**
 * prepare_vision_prompt - Ensure a prompt contains image markers.
 * @param prompt Input prompt.
 * @param n_images Number of image attachments.
 * @return Allocated prompt string, or NULL on failure.
 */
static char *prepare_vision_prompt(const char *prompt, int n_images) {
    const char *marker = mtmd_default_marker();
    size_t marker_len = strlen(marker);
    size_t prompt_len = strlen(prompt);
    size_t extra = marker_len * (size_t)n_images;
    char *out;
    size_t pos = 0;
    if (strstr(prompt, marker) != NULL) {
        out = (char *)malloc(prompt_len + 1);
        if (!out) return NULL;
        memcpy(out, prompt, prompt_len + 1);
        return out;
    }
    out = (char *)malloc(extra + prompt_len + 1);
    if (!out) return NULL;
    for (int i = 0; i < n_images; i++) {
        memcpy(out + pos, marker, marker_len);
        pos += marker_len;
    }
    memcpy(out + pos, prompt, prompt_len + 1);
    return out;
}

/**
 * kc_llm_prepare_text_usage - Prepare and tokenize a text request.
 * @param ctx Context pointer.
 * @param role Message role.
 * @param content Message content.
 * @param usage Output usage structure.
 * @return KC_LLM_OK on success, or KC_LLM_ERROR on failure.
 */
static int kc_llm_prepare_text_usage(kc_llm_t *ctx, const char *role,
    const char *content, kc_llm_request_usage_t *usage) {
    std::vector<llama_token> tokens;
    if (!ctx || !ctx->model || !ctx->vocab || !content || !usage) return KC_LLM_ERROR;
    usage->prompt = kc_llm_prepare_prompt(ctx, role, content);
    if (!usage->prompt) return KC_LLM_ERROR;
    try {
        tokens = common_tokenize(ctx->vocab, usage->prompt, true, true);
    } catch (const std::exception &e) {
        kc_llm_set_err(ctx, "%s", e.what());
        kc_llm_request_usage_free(usage);
        return KC_LLM_ERROR;
    } catch (...) {
        kc_llm_set_err(ctx, "prompt tokenization failed");
        kc_llm_request_usage_free(usage);
        return KC_LLM_ERROR;
    }
    usage->token_count = (int64_t)tokens.size();
    usage->position_count = usage->token_count;
    return KC_LLM_OK;
}

/**
 * kc_llm_prepare_vision_usage - Prepare and count a multimodal request.
 * @param ctx Context pointer.
 * @param role Message role.
 * @param content Message content.
 * @param usage Output usage structure.
 * @return KC_LLM_OK on success, or KC_LLM_ERROR on failure.
 */
static int kc_llm_prepare_vision_usage(kc_llm_t *ctx, const char *role,
    const char *content, kc_llm_request_usage_t *usage) {
    struct mtmd_bitmap *bitmaps[KC_LLM_MAX_IMAGES];
    struct mtmd_input_chunks *chunks = NULL;
    struct mtmd_input_text input_text;
    char *prompt_prepared = NULL;
    int rc = KC_LLM_ERROR;
    int i;
    if (!ctx || !ctx->model || !ctx->mctx || !content || !usage) return KC_LLM_ERROR;
    memset(bitmaps, 0, sizeof(bitmaps));
    usage->prompt = kc_llm_prepare_prompt(ctx, role, content);
    if (!usage->prompt) return KC_LLM_ERROR;
    prompt_prepared = prepare_vision_prompt(usage->prompt, ctx->opts.n_images);
    if (!prompt_prepared) {
        kc_llm_set_err(ctx, "out of memory");
        goto cleanup;
    }
    for (i = 0; i < ctx->opts.n_images; i++) {
        struct mtmd_helper_bitmap_wrapper wrapper =
            mtmd_helper_bitmap_init_from_file(ctx->mctx, ctx->opts.images[i], 0, mtmd_helper_init_opt_default());
        if (!wrapper.bitmap) {
            kc_llm_set_err(ctx, "failed to load image: %s", ctx->opts.images[i]);
            goto cleanup;
        }
        bitmaps[i] = wrapper.bitmap;
    }
    input_text.text = prompt_prepared;
    input_text.add_special = 1;
    input_text.parse_special = 1;
    chunks = mtmd_input_chunks_init();
    if (!chunks) {
        kc_llm_set_err(ctx, "failed to init input chunks");
        goto cleanup;
    }
    if (mtmd_tokenize(ctx->mctx, chunks, &input_text,
            (const struct mtmd_bitmap **)bitmaps,
            (size_t)ctx->opts.n_images) != 0) {
        kc_llm_set_err(ctx, "failed to tokenize multimodal input");
        goto cleanup;
    }
    usage->token_count = (int64_t)mtmd_helper_get_n_tokens(chunks);
    usage->position_count = (int64_t)mtmd_helper_get_n_pos(chunks);
    rc = KC_LLM_OK;
cleanup:
    free(prompt_prepared);
    if (chunks) mtmd_input_chunks_free(chunks);
    for (i = 0; i < KC_LLM_MAX_IMAGES; i++) {
        if (bitmaps[i]) mtmd_bitmap_free(bitmaps[i]);
    }
    if (rc != KC_LLM_OK) kc_llm_request_usage_free(usage);
    return rc;
}

/**
 * kc_llm_prepare_request_usage - Prepare request content and count final
 * input usage.
 * @param ctx Context pointer.
 * @param request Request description.
 * @param usage Output usage structure.
 * @return KC_LLM_OK on success, or KC_LLM_ERROR on failure.
 */
static int kc_llm_prepare_request_usage(kc_llm_t *ctx,
    const kc_llm_info_request_t *request, kc_llm_request_usage_t *usage) {
    const char *role;
    if (!ctx || !request || !request->content || !usage) return KC_LLM_ERROR;
    memset(usage, 0, sizeof(*usage));
    role = request->role ? request->role : ctx->opts.role;
    if (ctx->opts.n_images > 0) {
        if (!ctx->mctx) {
            kc_llm_set_err(ctx, "vision request accounting requires full runtime");
            return KC_LLM_ERROR;
        }
        return kc_llm_prepare_vision_usage(ctx, role, request->content, usage);
    }
    return kc_llm_prepare_text_usage(ctx, role, request->content, usage);
}

/**
 * kc_llm_generate_vision - Run multimodal generation.
 * @param ctx Context to use.
 * @param prompt Prompt text.
 * @param write Output callback.
 * @param user User pointer for output callback.
 * @return KC_LLM_OK on success, KC_LLM_ERROR or KC_LLM_ESTOP on failure.
 */
static int kc_llm_generate_vision(kc_llm_t *ctx, const char *prompt, kc_llm_write_fn write, void *user) {
    struct mtmd_bitmap *bitmaps[KC_LLM_MAX_IMAGES];
    struct mtmd_input_chunks *chunks = NULL;
    struct mtmd_input_text input_text;
    char *prompt_prepared = NULL;
    llama_token *dummy_tokens = NULL;
    llama_token new_token;
    llama_pos new_n_past = ctx->pos;
    int32_t n_piece;
    int n_generated = 0;
    int max_predict;
    int rc = KC_LLM_ERROR;
    size_t n_ctx;
    char piece_buf[256];
    int i;

    if (!ctx || !ctx->ctx || !ctx->sampler || !ctx->mctx || !prompt) return KC_LLM_ERROR;

    max_predict = ctx->opts.predict < 0 ? INT_MAX : ctx->opts.predict;

    memset(bitmaps, 0, sizeof(bitmaps));

    n_ctx = (size_t)llama_n_ctx(ctx->ctx);
    dummy_tokens = (llama_token *)malloc(sizeof(llama_token) * (n_ctx > 0 ? n_ctx : 1));
    if (!dummy_tokens) {
        kc_llm_set_err(ctx, "out of memory");
        return KC_LLM_ERROR;
    }

    for (i = 0; i < ctx->opts.n_images; i++) {
        struct mtmd_helper_bitmap_wrapper wrapper = mtmd_helper_bitmap_init_from_file(ctx->mctx, ctx->opts.images[i], 0, mtmd_helper_init_opt_default());
        if (!wrapper.bitmap) {
            kc_llm_set_err(ctx, "failed to load image: %s", ctx->opts.images[i]);
            goto cleanup_vision;
        }
        bitmaps[i] = wrapper.bitmap;
    }

    prompt_prepared = prepare_vision_prompt(prompt, ctx->opts.n_images);
    if (!prompt_prepared) {
        kc_llm_set_err(ctx, "out of memory");
        goto cleanup_vision;
    }

    input_text.text = prompt_prepared;
    input_text.add_special = 1;
    input_text.parse_special = 1;

    chunks = mtmd_input_chunks_init();
    if (!chunks) {
        kc_llm_set_err(ctx, "failed to init input chunks");
        goto cleanup_vision;
    }

    if (mtmd_tokenize(ctx->mctx, chunks, &input_text, (const struct mtmd_bitmap **)bitmaps, (size_t)ctx->opts.n_images) != 0) {
        kc_llm_set_err(ctx, "failed to tokenize multimodal input");
        goto cleanup_vision;
    }

    if (mtmd_helper_eval_chunks(ctx->mctx, ctx->ctx, chunks, new_n_past, 0, 2048, 1, &new_n_past) != 0) {
        kc_llm_set_err(ctx, "failed to evaluate multimodal input");
        goto cleanup_vision;
    }

    ctx->pos = new_n_past;

    new_token = llama_sampler_sample(ctx->sampler, ctx->ctx, -1);
    while (n_generated < max_predict && !llama_vocab_is_eog(ctx->vocab, new_token)) {
        if (ctx->stop_requested) {
            rc = KC_LLM_ESTOP;
            goto cleanup_vision;
        }
        n_piece = llama_token_to_piece(ctx->vocab, new_token, piece_buf, sizeof(piece_buf), 0, 0);
        if (n_piece < 0) n_piece = 0;
        if (n_piece > 0) {
            if (write && write(piece_buf, (size_t)n_piece, user) != 0) {
                kc_llm_set_err(ctx, "failed to write output");
                goto cleanup_vision;
            }
        }
        n_generated++;
        {
            struct llama_batch batch = llama_batch_get_one(&new_token, 1);
            if (llama_decode(ctx->ctx, batch) != 0) {
                kc_llm_set_err(ctx, "failed to decode generated token");
                goto cleanup_vision;
            }
        }
        ctx->pos++;
        new_token = llama_sampler_sample(ctx->sampler, ctx->ctx, -1);
    }

    rc = KC_LLM_OK;

cleanup_vision:
    free(dummy_tokens);
    free(prompt_prepared);
    if (chunks) mtmd_input_chunks_free(chunks);
    for (i = 0; i < KC_LLM_MAX_IMAGES; i++) {
        if (bitmaps[i]) mtmd_bitmap_free(bitmaps[i]);
    }
    return rc;
}

/**
 * kc_llm_generate - Generate output for a prompt.
 * @param ctx Context to use.
 * @param prompt Prompt text.
 * @param write Output callback.
 * @param user User pointer for output callback.
 * @return KC_LLM_OK on success, KC_LLM_ERROR or KC_LLM_ESTOP on failure.
 */
int kc_llm_generate(kc_llm_t *ctx, const char *prompt, kc_llm_write_fn write, void *user) {
    return kc_llm_generate_role(ctx, "user", prompt, write, user);
}

/**
 * kc_llm_generate_role - Generate output for a message with an explicit role.
 * @param ctx Context to use.
 * @param role Message role string.
 * @param content Message content.
 * @param write Output callback.
 * @param user User pointer for output callback.
 * @return KC_LLM_OK on success, KC_LLM_ERROR or KC_LLM_ESTOP on failure.
 */
int kc_llm_generate_role(kc_llm_t *ctx, const char *role, const char *content,
    kc_llm_write_fn write, void *user) {
    char *prompt;
    int rc;

    if (!ctx) return KC_LLM_ERROR;
    if (!content) {
        kc_llm_set_err(ctx, "prompt is required");
        return KC_LLM_ERROR;
    }

    prompt = kc_llm_prepare_prompt(ctx, role, content);
    if (!prompt) return KC_LLM_ERROR;

    if (ctx->has_vision) {
        rc = kc_llm_generate_vision(ctx, prompt, write, user);
    } else if (ctx->draft_ctx) {
        rc = kc_llm_generate_text_mtp(ctx, prompt, write, user);
    } else {
        rc = kc_llm_generate_text(ctx, prompt, write, user);
    }
    free(prompt);
    return rc;
}

/**
 * kc_llm_info_query - Query typed inspection values from a model or runtime.
 * @param ctx Context pointer.
 * @param fields Requested field list.
 * @param n_fields Number of fields.
 * @param request Optional request for input-dependent fields.
 * @param out Output array.
 * @return KC_LLM_OK on success, or KC_LLM_ERROR on failure.
 */
int kc_llm_info_query(kc_llm_t *ctx, const kc_llm_info_field_t *fields,
    size_t n_fields, const kc_llm_info_request_t *request, kc_llm_info_t *out) {
    kc_llm_request_usage_t usage;
    int64_t context_used = 0;
    int64_t context_size = 0;
    int have_usage = 0;
    int have_context_used = 0;
    int have_context_size = 0;
    size_t i;
    if (!ctx || !fields || !out || n_fields == 0) return KC_LLM_ERROR;
    memset(&usage, 0, sizeof(usage));
    for (i = 0; i < n_fields; i++) {
        out[i].field = fields[i];
        out[i].type = KC_LLM_INFO_VALUE_NONE;
        if (kc_llm_info_field_needs_request(fields[i]) && (!request || !request->content)) {
            kc_llm_set_err(ctx, "request content is required for this info field");
            return KC_LLM_ERROR;
        }
        if (kc_llm_info_field_needs_runtime(fields[i]) && !ctx->ctx) {
            kc_llm_set_err(ctx, "requested info field requires a full runtime context");
            return KC_LLM_ERROR;
        }
    }
    for (i = 0; i < n_fields; i++) {
        if (kc_llm_info_field_needs_usage(fields[i]) && !have_usage) {
            if (kc_llm_prepare_request_usage(ctx, request, &usage) != KC_LLM_OK) {
                kc_llm_request_usage_free(&usage);
                return KC_LLM_ERROR;
            }
            have_usage = 1;
        }
        if ((fields[i] == KC_LLM_INFO_CONTEXT_USED || fields[i] == KC_LLM_INFO_CONTEXT_FREE ||
                fields[i] == KC_LLM_INFO_CONTEXT_AFTER) && !have_context_used) {
            if (kc_llm_context_used(ctx, &context_used) != 0) {
                kc_llm_set_err(ctx, "failed to query runtime context usage");
                kc_llm_request_usage_free(&usage);
                return KC_LLM_ERROR;
            }
            have_context_used = 1;
        }
        if ((fields[i] == KC_LLM_INFO_CONTEXT_SIZE || fields[i] == KC_LLM_INFO_CONTEXT_FREE ||
                fields[i] == KC_LLM_INFO_CONTEXT_AFTER) && !have_context_size) {
            if (kc_llm_context_size(ctx, &context_size) != 0) {
                kc_llm_set_err(ctx, "failed to query runtime context size");
                kc_llm_request_usage_free(&usage);
                return KC_LLM_ERROR;
            }
            have_context_size = 1;
        }
    }
    for (i = 0; i < n_fields; i++) {
        uint64_t u64_value;
        int64_t i64_value;
        switch (fields[i]) {
            case KC_LLM_INFO_ARCHITECTURE:
                if (ctx->info_architecture) {
                    out[i].type = KC_LLM_INFO_VALUE_STRING;
                    out[i].value.string = ctx->info_architecture;
                }
                break;
            case KC_LLM_INFO_NAME:
                if (ctx->info_name) {
                    out[i].type = KC_LLM_INFO_VALUE_STRING;
                    out[i].value.string = ctx->info_name;
                }
                break;
            case KC_LLM_INFO_PARAMETERS:
                out[i].type = KC_LLM_INFO_VALUE_U64;
                out[i].value.u64 = llama_model_n_params(ctx->model);
                break;
            case KC_LLM_INFO_SIZE:
                if (kc_llm_model_file_size(ctx->opts.model_path, &u64_value) == 0) {
                    out[i].type = KC_LLM_INFO_VALUE_U64;
                    out[i].value.u64 = u64_value;
                }
                break;
            case KC_LLM_INFO_VOCABULARY:
                out[i].type = KC_LLM_INFO_VALUE_U64;
                out[i].value.u64 = (uint64_t)llama_vocab_n_tokens(ctx->vocab);
                break;
            case KC_LLM_INFO_CONTEXT_MAX:
                out[i].type = KC_LLM_INFO_VALUE_U64;
                out[i].value.u64 = (uint64_t)llama_model_n_ctx_train(ctx->model);
                break;
            case KC_LLM_INFO_EMBEDDING_SIZE:
                out[i].type = KC_LLM_INFO_VALUE_U64;
                out[i].value.u64 = (uint64_t)llama_model_n_embd(ctx->model);
                break;
            case KC_LLM_INFO_LAYERS:
                out[i].type = KC_LLM_INFO_VALUE_U64;
                out[i].value.u64 = (uint64_t)llama_model_n_layer(ctx->model);
                break;
            case KC_LLM_INFO_HEADS:
                out[i].type = KC_LLM_INFO_VALUE_U64;
                out[i].value.u64 = (uint64_t)llama_model_n_head(ctx->model);
                break;
            case KC_LLM_INFO_KV_HEADS:
                out[i].type = KC_LLM_INFO_VALUE_U64;
                out[i].value.u64 = (uint64_t)llama_model_n_head_kv(ctx->model);
                break;
            case KC_LLM_INFO_INPUT_TOKENS:
                out[i].type = KC_LLM_INFO_VALUE_I64;
                out[i].value.i64 = usage.token_count;
                break;
            case KC_LLM_INFO_CONTEXT_USED:
                out[i].type = KC_LLM_INFO_VALUE_I64;
                out[i].value.i64 = context_used;
                break;
            case KC_LLM_INFO_CONTEXT_SIZE:
                out[i].type = KC_LLM_INFO_VALUE_I64;
                out[i].value.i64 = context_size;
                break;
            case KC_LLM_INFO_CONTEXT_FREE:
                i64_value = context_size - context_used;
                out[i].type = KC_LLM_INFO_VALUE_I64;
                out[i].value.i64 = i64_value;
                break;
            case KC_LLM_INFO_CONTEXT_AFTER:
                i64_value = context_used + usage.position_count;
                out[i].type = KC_LLM_INFO_VALUE_I64;
                out[i].value.i64 = i64_value;
                break;
        }
    }
    kc_llm_request_usage_free(&usage);
    return KC_LLM_OK;
}
