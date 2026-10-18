/**
 * tray.c - Native system tray command-line interface.
 * Summary: Creates persistent trays and ordered menu items with libtray.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libtray.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#else
#include <time.h>
#endif

#define TRAY_CLI_ACTION_ITEM 1
#define TRAY_CLI_ACTION_SEPARATOR 2
#define TRAY_CLI_ACTION_QUIT 3

typedef struct {
    int type;
    const char *text;
    const char *command;
} tray_cli_action_t;

typedef struct {
    const char *command;
} tray_cli_item_data_t;

static volatile sig_atomic_t tray_cli_stop = 0;

/**
 * Records a termination request.
 * @param sig Signal number.
 * @return None.
 */
static void tray_cli_signal(int sig)
{
    (void)sig;
    tray_cli_stop = 1;
}

/**
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void tray_cli_usage(const char *name)
{
    printf("Usage: %s [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  --icon <icon>            Icon file path or platform icon name\n");
    printf("  --tooltip <text>         Tray tooltip\n");
    printf("  --item <text> <command>  Add a menu item; repeatable\n");
    printf("  --separator              Add a menu separator; repeatable\n");
    printf("  --quit <text>            Add an item that closes the tray\n");
    printf("  -h, --help               Show this help\n");
    printf("  -v, --version            Show version\n");
}

/**
 * Executes one configured menu command.
 * @param item Activated tray item.
 * @param userdata Item callback state.
 * @return None.
 */
static void tray_cli_command(kc_tray_item_t *item, void *userdata)
{
    tray_cli_item_data_t *data = (tray_cli_item_data_t *)userdata;

    (void)item;
    if (!data || !data->command) return;
    (void)system(data->command);
}

/**
 * Requests tray shutdown from one menu item.
 * @param item Activated tray item.
 * @param userdata Unused callback data.
 * @return None.
 */
static void tray_cli_quit(kc_tray_item_t *item, void *userdata)
{
    (void)item;
    (void)userdata;
    tray_cli_stop = 1;
}

/**
 * Sleeps or services the native main loop briefly.
 * @return None.
 */
static void tray_cli_wait(void)
{
#ifdef _WIN32
    Sleep(100);
#elif defined(__APPLE__)
    @autoreleasepool {
        NSDate *until = [NSDate dateWithTimeIntervalSinceNow:0.1];
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
            untilDate:until
            inMode:NSDefaultRunLoopMode
            dequeue:YES];
        if (event) [NSApp sendEvent:event];
        [NSApp updateWindows];
    }
#else
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 100000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Adds one configured action to an opened tray.
 * @param tray Tray handle.
 * @param action Action definition.
 * @param item_data Callback storage array.
 * @param data_index Next callback storage index.
 * @return 0 on success, 1 on failure.
 */
static int tray_cli_add_action(kc_tray_t *tray,
    const tray_cli_action_t *action, tray_cli_item_data_t *item_data,
    size_t *data_index)
{
    kc_tray_item_t *item = NULL;
    int status;

    if (action->type == TRAY_CLI_ACTION_SEPARATOR)
        status = kc_tray_add_separator(tray, &item);
    else if (action->type == TRAY_CLI_ACTION_QUIT)
        status = kc_tray_add_item(tray, &item, action->text,
            tray_cli_quit, NULL);
    else {
        tray_cli_item_data_t *data = &item_data[*data_index];

        data->command = action->command;
        (*data_index)++;
        status = kc_tray_add_item(tray, &item, action->text,
            tray_cli_command, data);
    }

    if (status != KC_TRAY_OK) {
        const char *error = kc_tray_get_error(tray);
        fprintf(stderr, "tray: cannot add menu item%s%s\n",
            error ? ": " : "", error ? error : "");
        return 1;
    }
    return 0;
}

/**
 * Runs the tray CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    kc_tray_options_t options;
    kc_tray_t *tray = NULL;
    tray_cli_action_t *actions;
    tray_cli_item_data_t *item_data;
    size_t action_count = 0;
    size_t command_count = 0;
    size_t data_index = 0;
    const char *icon = NULL;
    const char *tooltip = NULL;
    int result = 0;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        tray_cli_usage(argv[0]);
        return 0;
    }
    if (argc > 1 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_tray_version());
        return 0;
    }

    actions = (tray_cli_action_t *)calloc((size_t)argc, sizeof(*actions));
    if (!actions) return 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--icon") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "tray: --icon requires a value\n");
                result = 1;
                goto done;
            }
            icon = argv[i];
        } else if (strcmp(argv[i], "--tooltip") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "tray: --tooltip requires a value\n");
                result = 1;
                goto done;
            }
            tooltip = argv[i];
        } else if (strcmp(argv[i], "--item") == 0) {
            if (i + 2 >= argc || !argv[i + 1][0] || !argv[i + 2][0]) {
                fprintf(stderr, "tray: --item requires <text> <command>\n");
                result = 1;
                goto done;
            }
            actions[action_count].type = TRAY_CLI_ACTION_ITEM;
            actions[action_count].text = argv[i + 1];
            actions[action_count].command = argv[i + 2];
            action_count++;
            command_count++;
            i += 2;
        } else if (strcmp(argv[i], "--separator") == 0) {
            actions[action_count++].type = TRAY_CLI_ACTION_SEPARATOR;
        } else if (strcmp(argv[i], "--quit") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "tray: --quit requires a label\n");
                result = 1;
                goto done;
            }
            actions[action_count].type = TRAY_CLI_ACTION_QUIT;
            actions[action_count].text = argv[i];
            action_count++;
        } else if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "tray: -h/--help and -v/--version must be used alone\n");
            result = 1;
            goto done;
        } else {
            fprintf(stderr, "tray: unknown option '%s'\n", argv[i]);
            result = 1;
            goto done;
        }
    }

    item_data = command_count ?
        (tray_cli_item_data_t *)calloc(command_count, sizeof(*item_data)) : NULL;
    if (command_count && !item_data) {
        result = 1;
        goto done;
    }

    memset(&options, 0, sizeof(options));
    options.icon = icon;
    options.tooltip = tooltip;
    if (kc_tray_open(&tray, &options) != KC_TRAY_OK) {
        fprintf(stderr, "tray: unable to open native tray\n");
        free(item_data);
        result = 1;
        goto done;
    }

    for (size_t i = 0; i < action_count; i++) {
        if (tray_cli_add_action(tray, &actions[i], item_data,
            &data_index) != 0)
        {
            kc_tray_close(tray);
            free(item_data);
            result = 1;
            goto done;
        }
    }

    tray_cli_stop = 0;
    signal(SIGINT, tray_cli_signal);
    signal(SIGTERM, tray_cli_signal);
    while (!tray_cli_stop) tray_cli_wait();

    kc_tray_close(tray);
    free(item_data);

done:
    free(actions);
    return result;
}
