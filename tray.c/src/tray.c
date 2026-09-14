/**
 * tray.c - Native system tray CLI.
 * Summary: Runs a local program when a tray menu item is activated.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtray.h"

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KC_TRAY_CLI_QUIT_ACTION "kc:tray:quit"

#define TRAY_PARSE_OK       0
#define TRAY_PARSE_ERROR    1
#define TRAY_PARSE_HELP     2
#define TRAY_PARSE_VERSION  3

typedef struct {
    char *label;
    char *action;
    char *exec;
    char **args;
    int arg_count;
    int is_separator;
} tray_cli_item_t;

typedef struct {
    tray_cli_item_t *items;
    int count;
    int capacity;
} tray_cli_list_t;

typedef struct {
    kc_tray_t *ctx;
    tray_cli_list_t *items;
} tray_cli_app_t;

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void tray_cli_help(const char *name) {
    printf("Usage: %s [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    --icon <path>            Icon file path or icon name\n");
    printf("    --tooltip <text>         Tooltip text\n");
    printf("    --item <label> <action>  Add a menu item that runs a program\n");
    printf("    --exec <path>            Program to run when a menu item is activated\n");
    printf("    --arg <value>            One exact argument, repeatable in order\n");
    printf("    --sep                    Add a menu separator\n");
    printf("    --quit <label>           Add a quit item for the tray process\n");
    printf("    -h, --help               Show this help\n");
    printf("    -v, --version            Show build version\n");
}

/**
 * Append one item slot to a CLI item list.
 * @param list Item list.
 * @param is_separator Non-zero when the slot is a separator.
 * @return Index of the new slot, or -1 on allocation failure.
 */
static int tray_cli_list_push(tray_cli_list_t *list, int is_separator) {
    tray_cli_item_t *items;
    int capacity;

    if (list->count == list->capacity) {
        capacity = list->capacity ? list->capacity * 2 : 8;
        items = (tray_cli_item_t *)realloc(list->items,
            (size_t)capacity * sizeof(tray_cli_item_t));
        if (!items) {
            return -1;
        }
        list->items = items;
        list->capacity = capacity;
    }
    memset(&list->items[list->count], 0, sizeof(tray_cli_item_t));
    list->items[list->count].is_separator = is_separator;
    list->count++;
    return list->count - 1;
}

/**
 * Replace one owned CLI item string.
 * @param field Destination string field.
 * @param value New string value.
 * @param out_status Receives 1 when an allocation failure occurred.
 * @return None.
 */
static void tray_cli_item_set_string(char **field, const char *value, int *out_status) {
    char *copy;

    copy = strdup(value);
    if (!copy) {
        *out_status = 1;
        return;
    }
    free(*field);
    *field = copy;
}

/**
 * Append one argument to a CLI item argument list.
 * @param item CLI item.
 * @param value Argument value.
 * @return 0 on success, 1 on failure.
 */
static int tray_cli_item_add_arg(tray_cli_item_t *item, const char *value) {
    char **args;

    args = (char **)realloc(item->args, (size_t)(item->arg_count + 1) * sizeof(char *));
    if (!args) {
        return 1;
    }
    item->args = args;
    item->args[item->arg_count] = strdup(value);
    if (!item->args[item->arg_count]) {
        return 1;
    }
    item->arg_count++;
    return 0;
}

/**
 * Parse CLI arguments into options and menu items.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param opts Cluster options handle.
 * @param list Destination CLI item list.
 * @return TRAY_PARSE_OK, ERROR, HELP or VERSION.
 */
static int tray_cli_parse(int argc, char **argv, kc_tray_options_t opts, tray_cli_list_t *list) {
    tray_cli_item_t *current = NULL;
    int state = 0;
    int status = 0;
    int i;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (state == 2) {
            if (strcmp(arg, "--arg") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "tray: missing value for --arg\n");
                    return TRAY_PARSE_ERROR;
                }
                if (tray_cli_item_add_arg(current, argv[i])) {
                    fprintf(stderr, "tray: allocation failed\n");
                    return TRAY_PARSE_ERROR;
                }
                continue;
            }
            current = NULL;
            state = 0;
            i--;
            continue;
        }
        if (state == 1) {
            if (strcmp(arg, "--exec") == 0) {
                int fail = 0;
                if (++i >= argc) {
                    fprintf(stderr, "tray: missing value for --exec\n");
                    return TRAY_PARSE_ERROR;
                }
                tray_cli_item_set_string(&current->exec, argv[i], &fail);
                if (fail) {
                    fprintf(stderr, "tray: allocation failed\n");
                    return TRAY_PARSE_ERROR;
                }
                state = 2;
                continue;
            }
            fprintf(stderr, "tray: missing --exec after --item\n");
            return TRAY_PARSE_ERROR;
        }

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            tray_cli_help(argv[0]);
            return TRAY_PARSE_HELP;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            printf("tray build %llu\n", (unsigned long long)kc_tray_version());
            return TRAY_PARSE_VERSION;
        }
        if (strcmp(arg, "--icon") == 0 || strcmp(arg, "--tooltip") == 0) {
            const char *key = arg + 2;
            if (++i >= argc) {
                fprintf(stderr, "tray: missing value for %s\n", arg);
                return TRAY_PARSE_ERROR;
            }
            if (kc_tray_options_set(opts, key, argv[i]) != KC_TRAY_OK) {
                fprintf(stderr, "tray: allocation failed\n");
                return TRAY_PARSE_ERROR;
            }
            continue;
        }
        if (strcmp(arg, "--item") == 0 || strcmp(arg, "--quit") == 0) {
            int index;
            int fail = 0;
            if (strcmp(arg, "--item") == 0) {
                if (i + 2 >= argc) {
                    fprintf(stderr, "tray: --item requires a label and an action\n");
                    return TRAY_PARSE_ERROR;
                }
                if (!argv[i + 1][0] || !argv[i + 2][0]) {
                    fprintf(stderr, "tray: --item label and action must not be empty\n");
                    return TRAY_PARSE_ERROR;
                }
                index = tray_cli_list_push(list, 0);
                if (index < 0) {
                    fprintf(stderr, "tray: allocation failed\n");
                    return TRAY_PARSE_ERROR;
                }
                current = &list->items[index];
                tray_cli_item_set_string(&current->label, argv[i + 1], &fail);
                tray_cli_item_set_string(&current->action, argv[i + 2], &fail);
                i += 2;
            } else {
                if (++i >= argc) {
                    fprintf(stderr, "tray: --quit requires a label\n");
                    return TRAY_PARSE_ERROR;
                }
                if (!argv[i][0]) {
                    fprintf(stderr, "tray: --quit label must not be empty\n");
                    return TRAY_PARSE_ERROR;
                }
                index = tray_cli_list_push(list, 0);
                if (index < 0) {
                    fprintf(stderr, "tray: allocation failed\n");
                    return TRAY_PARSE_ERROR;
                }
                current = NULL;
                tray_cli_item_set_string(&list->items[index].label, argv[i], &fail);
                tray_cli_item_set_string(&list->items[index].action,
                    KC_TRAY_CLI_QUIT_ACTION, &fail);
            }
            if (fail) {
                fprintf(stderr, "tray: allocation failed\n");
                return TRAY_PARSE_ERROR;
            }
            if (strcmp(arg, "--item") == 0) {
                int j;

                if (strcmp(current->action, KC_TRAY_CLI_QUIT_ACTION) == 0) {
                    fprintf(stderr,
                        "tray: action '%s' is reserved for the quit item\n",
                        current->action);
                    return TRAY_PARSE_ERROR;
                }
                for (j = 0; j < index; j++) {
                    if (!list->items[j].is_separator &&
                        strcmp(list->items[j].action, current->action) == 0) {
                        fprintf(stderr, "tray: duplicate action '%s'\n",
                            current->action);
                        return TRAY_PARSE_ERROR;
                    }
                }
                state = 1;
            }
            continue;
        }
        if (strcmp(arg, "--sep") == 0) {
            if (tray_cli_list_push(list, 1) < 0) {
                fprintf(stderr, "tray: allocation failed\n");
                return TRAY_PARSE_ERROR;
            }
            continue;
        }
        fprintf(stderr, "tray: unknown option '%s'\n", arg);
        return TRAY_PARSE_ERROR;
    }

    if (state == 1) {
        fprintf(stderr, "tray: missing --exec after --item\n");
        return TRAY_PARSE_ERROR;
    }
    if (list->count == 0) {
        fprintf(stderr, "tray: no menu items configured\n");
        return TRAY_PARSE_ERROR;
    }
    return status;
}

/**
 * Build the library menu item array from the CLI item list.
 * @param list CLI item list.
 * @param out_items Receives the caller-owned item array.
 * @param out_count Receives the item count.
 * @return 0 on success, 1 on failure.
 */
static int tray_cli_build_menu(const tray_cli_list_t *list, kc_tray_item_t **out_items, int *out_count) {
    kc_tray_item_t *items;
    int i;

    items = (kc_tray_item_t *)calloc((size_t)list->count, sizeof(kc_tray_item_t));
    if (!items) {
        return 1;
    }
    for (i = 0; i < list->count; i++) {
        if (list->items[i].is_separator) {
            items[i].label = NULL;
            items[i].action = NULL;
        } else {
            items[i].label = list->items[i].label;
            items[i].action = list->items[i].action;
        }
    }
    *out_items = items;
    *out_count = list->count;
    return 0;
}

#ifdef _WIN32

typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} tray_cli_buf_t;

/**
 * Convert one UTF-8 string to a fresh UTF-16 buffer.
 * @param text UTF-8 source string.
 * @return Owned UTF-16 copy, or NULL on invalid input or allocation failure.
 */
static wchar_t *tray_cli_utf16_from_utf8(const char *text) {
    int length;
    wchar_t *buffer;

    if (!text) {
        return NULL;
    }
    length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (length <= 0) {
        return NULL;
    }
    buffer = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (!buffer) {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, text, -1, buffer, length);
    return buffer;
}

/**
 * Append one byte sequence to a string builder.
 * @param buffer String builder.
 * @param text Byte sequence.
 * @param length Sequence length.
 * @return 0 on success, 1 on allocation failure.
 */
static int tray_cli_buf_append(tray_cli_buf_t *buffer, const char *text, size_t length) {
    char *grown;

    if (buffer->length + length + 1 > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity * 2 : 64;
        while (capacity < buffer->length + length + 1) {
            capacity *= 2;
        }
        grown = (char *)realloc(buffer->text, capacity);
        if (!grown) {
            return 1;
        }
        buffer->text = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->text + buffer->length, text, length);
    buffer->length += length;
    buffer->text[buffer->length] = '\0';
    return 0;
}

/**
 * Append an argument to a command line with Windows quoting rules.
 * @param buffer String builder.
 * @param arg Argument to append.
 * @return 0 on success, 1 on allocation failure.
 */
static int tray_cli_buf_quote_arg(tray_cli_buf_t *buffer, const char *arg) {
    const char *p;
    size_t backslashes = 0;
    size_t count;

    if (arg[0] != '\0' && !strpbrk(arg, " \t\"")) {
        return tray_cli_buf_append(buffer, arg, strlen(arg));
    }
    if (tray_cli_buf_append(buffer, "\"", 1)) {
        return 1;
    }
    for (p = arg; *p; p++) {
        if (*p == '\\') {
            backslashes++;
        } else if (*p == '"') {
            for (count = 0; count < backslashes * 2; count++) {
                if (tray_cli_buf_append(buffer, "\\", 1)) {
                    return 1;
                }
            }
            backslashes = 0;
            if (tray_cli_buf_append(buffer, "\\\"", 2)) {
                return 1;
            }
        } else {
            for (count = 0; count < backslashes; count++) {
                if (tray_cli_buf_append(buffer, "\\", 1)) {
                    return 1;
                }
            }
            backslashes = 0;
            if (tray_cli_buf_append(buffer, p, 1)) {
                return 1;
            }
        }
    }
    for (count = 0; count < backslashes * 2; count++) {
        if (tray_cli_buf_append(buffer, "\\", 1)) {
            return 1;
        }
    }
    return tray_cli_buf_append(buffer, "\"", 1);
}

#else

/**
 * Launch one program detached from the tray process.
 * @param argv Executable identity followed by arguments, NULL-terminated.
 * @return None.
 */
static void tray_cli_spawn(const char *const *argv) {
    pid_t first;
    int fd;

    if (!argv || !argv[0]) {
        return;
    }
    first = fork();
    if (first < 0) {
        fprintf(stderr, "tray: fork failed: %s\n", strerror(errno));
        return;
    }
    if (first == 0) {
        pid_t second = fork();
        if (second < 0) {
            _exit(1);
        }
        if (second > 0) {
            _exit(0);
        }
        setsid();
        fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) {
                close(fd);
            }
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    waitpid(first, NULL, 0);
}

#endif

/**
 * Deliver one menu activation by spawning the matching program.
 * @param userdata Tray CLI application data.
 * @param action Action name of the activated item.
 * @return None.
 */
static void tray_cli_menu_event(void *userdata, const char *action) {
    tray_cli_app_t *app = (tray_cli_app_t *)userdata;
    tray_cli_item_t *item;
    char **argv;
    int i;
    int n;

    if (!app || !action) {
        return;
    }
    if (strcmp(action, KC_TRAY_CLI_QUIT_ACTION) == 0) {
        kc_tray_stop(app->ctx);
        return;
    }
    item = NULL;
    for (i = 0; i < app->items->count; i++) {
        if (!app->items->items[i].is_separator &&
            strcmp(app->items->items[i].action, action) == 0) {
            item = &app->items->items[i];
            break;
        }
    }
    if (!item || !item->exec) {
        return;
    }
    n = item->arg_count + 1;
    argv = (char **)malloc((size_t)(n + 1) * sizeof(char *));
    if (!argv) {
        return;
    }
    argv[0] = item->exec;
    for (i = 0; i < item->arg_count; i++) {
        argv[1 + i] = item->args[i];
    }
    argv[n] = NULL;
#ifdef _WIN32
    {
        wchar_t *exe;
        wchar_t *command_line;

        exe = tray_cli_utf16_from_utf8(item->exec);
        command_line = NULL;
        {
            tray_cli_buf_t buffer = {0};
            int ok;

            ok = tray_cli_buf_quote_arg(&buffer, item->exec);
            for (i = 1; !ok && i <= item->arg_count; i++) {
                if (tray_cli_buf_append(&buffer, " ", 1)) {
                    ok = 1;
                }
                if (!ok && tray_cli_buf_quote_arg(&buffer, argv[i])) {
                    ok = 1;
                }
            }
            if (!ok) {
                command_line = tray_cli_utf16_from_utf8(buffer.text);
            }
            free(buffer.text);
        }
        if (exe && command_line) {
            STARTUPINFOW start_info;
            PROCESS_INFORMATION process_info;

            memset(&start_info, 0, sizeof(start_info));
            start_info.cb = sizeof(start_info);
            if (CreateProcessW(exe, command_line, NULL, NULL, FALSE,
                    DETACHED_PROCESS, NULL, NULL, &start_info, &process_info)) {
                CloseHandle(process_info.hProcess);
                CloseHandle(process_info.hThread);
            } else {
                fprintf(stderr, "tray: failed to start '%s'\n", item->exec);
            }
        } else {
            fprintf(stderr, "tray: failed to start '%s'\n", item->exec);
        }
        free(command_line);
        free(exe);
    }
#else
    tray_cli_spawn((const char *const *)argv);
#endif
    free(argv);
}

/**
 * Release a CLI item list.
 * @param list Item list.
 * @return None.
 */
static void tray_cli_list_free(tray_cli_list_t *list) {
    int i;

    if (!list) {
        return;
    }
    for (i = 0; i < list->count; i++) {
        free(list->items[i].label);
        free(list->items[i].action);
        free(list->items[i].exec);
        {
            int j;
            for (j = 0; j < list->items[i].arg_count; j++) {
                free(list->items[i].args[j]);
            }
        }
        free(list->items[i].args);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/**
 * Executes the command line interface through the public C API.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_tray_options_t opts;
    kc_tray_t *ctx = NULL;
    tray_cli_list_t list;
    tray_cli_app_t app;
    kc_tray_item_t *menu_items = NULL;
    int menu_count = 0;
    const char *env;
    int result;
    int status = 0;

    memset(&list, 0, sizeof(list));
    memset(&app, 0, sizeof(app));

    opts = kc_tray_options_default();
    if (!opts) {
        fprintf(stderr, "tray: allocation failed\n");
        return 1;
    }

    env = getenv("KC_TRAY_ICON");
    if (env && env[0]) {
        if (kc_tray_options_set(opts, "icon", env) != KC_TRAY_OK) {
            fprintf(stderr, "tray: failed to apply KC_TRAY_ICON\n");
            kc_tray_options_free(opts);
            return 1;
        }
    }
    env = getenv("KC_TRAY_TOOLTIP");
    if (env && env[0]) {
        if (kc_tray_options_set(opts, "tooltip", env) != KC_TRAY_OK) {
            fprintf(stderr, "tray: failed to apply KC_TRAY_TOOLTIP\n");
            kc_tray_options_free(opts);
            return 1;
        }
    }

    result = tray_cli_parse(argc, argv, opts, &list);
    if (result == TRAY_PARSE_HELP || result == TRAY_PARSE_VERSION) {
        kc_tray_options_free(opts);
        tray_cli_list_free(&list);
        return 0;
    }
    if (result == TRAY_PARSE_ERROR) {
        kc_tray_options_free(opts);
        tray_cli_list_free(&list);
        return 1;
    }

    if (tray_cli_build_menu(&list, &menu_items, &menu_count) != 0) {
        fprintf(stderr, "tray: allocation failed\n");
        status = 1;
        goto cleanup;
    }

    app.ctx = NULL;
    app.items = &list;
    if (kc_tray_open(&ctx, opts, tray_cli_menu_event, &app) != KC_TRAY_OK) {
        fprintf(stderr, "tray: open failed\n");
        status = 1;
        goto cleanup;
    }
    app.ctx = ctx;

    if (kc_tray_set_menu(ctx, menu_items, menu_count) != KC_TRAY_OK) {
        fprintf(stderr, "tray: failed to set menu\n");
        status = 1;
        goto cleanup;
    }
    if (kc_tray_run(ctx) != KC_TRAY_OK) {
        fprintf(stderr, "tray: event loop failed\n");
        status = 1;
    }

cleanup:
    kc_tray_close(ctx);
    tray_cli_list_free(&list);
    free(menu_items);
    kc_tray_options_free(opts);
    return status;
}
