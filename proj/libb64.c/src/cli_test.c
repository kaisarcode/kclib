/**
 * test.c - b64 CLI contract tests.
 * Summary: Validates Base64 modes and input precedence.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_CLI_PATH
#define KC_CLI_PATH "b64"
#endif

static int test_total = 0;
static int test_failed = 0;

/**
 * Writes one fixture file.
 * @param path Output file path.
 * @param text Text to write.
 * @return Nonzero on success.
 */
static int write_text_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    size_t size = strlen(text);
    int ok;

    if (!file) return 0;
    ok = fwrite(text, 1, size, file) == size;
    fclose(file);
    return ok;
}

/**
 * Executes the CLI and compares captured stdout exactly.
 * @param args CLI arguments.
 * @param stdin_text Optional stdin content.
 * @param expected Expected stdout.
 * @return Nonzero on success.
 */
static int output_equals(
    const char *args,
    const char *stdin_text,
    const char *expected
)
{
    const char *input_path = "b64-cli-stdin.txt";
    const char *output_path = "b64-cli-output.txt";
    char command[4096];
    char buffer[4096];
    FILE *file;
    size_t size;
    int status;

    if (stdin_text && !write_text_file(input_path, stdin_text)) return 0;

#ifdef _WIN32
    snprintf(command, sizeof(command),
        "\"\"%s\" %s %s> %s 2>NUL\"",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< b64-cli-stdin.txt " : "",
        output_path);
#else
    snprintf(command, sizeof(command),
        "\"%s\" %s %s> %s 2>/dev/null",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< b64-cli-stdin.txt " : "",
        output_path);
#endif

    status = system(command);
    if (stdin_text) remove(input_path);
    if (status != 0) {
        remove(output_path);
        return 0;
    }

    file = fopen(output_path, "rb");
    if (!file) return 0;
    size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    remove(output_path);
    buffer[size] = '\0';
    return strcmp(buffer, expected) == 0;
}

/**
 * Executes the CLI with output discarded.
 * @param args Arguments appended after executable.
 * @return Child process status from system().
 */
static int run_cli(const char *args)
{
    char command[4096];

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > NUL 2>&1\"",
        KC_CLI_PATH, args ? args : "");
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        KC_CLI_PATH, args ? args : "");
#endif
    return system(command);
}

/**
 * Prints one contract-test result.
 * @param name Test name.
 * @param detail Behavior under test.
 * @param ok Nonzero on success.
 * @return None.
 */
static void result(const char *name, const char *detail, int ok)
{
    test_total++;
    if (!ok) test_failed++;
    printf("[%d/13] [%s] %s: %s\n", test_total, ok ? "PASS" : "FAIL",
        name, detail);
}

/**
 * Runs all b64 CLI contract tests.
 * @return Zero when all tests pass.
 */
int main(void)
{
    const char *file_path = "b64-cli-file.txt";
    int fixture_ok;

    result("help", "--help succeeds", run_cli("--help") == 0);
    result("version", "--version succeeds", run_cli("--version") == 0);
    result("encode arg", "-e encodes text argument",
        output_equals("-e \"hello world\"", NULL, "aGVsbG8gd29ybGQ="));
    result("encode stdin", "-e reads stdin when argument is absent",
        output_equals("-e", "hello world", "aGVsbG8gd29ybGQ="));

    fixture_ok = write_text_file(file_path, "file");
    result("encode file", "-e -i reads file",
        fixture_ok && output_equals("-e -i b64-cli-file.txt", NULL,
            "ZmlsZQ=="));
    result("stdin priority", "stdin takes priority over -i",
        fixture_ok && output_equals("-e -i b64-cli-file.txt", "stdin",
            "c3RkaW4="));
    result("argument priority", "argument takes priority over stdin and -i",
        fixture_ok && output_equals(
            "-e \"argument\" -i b64-cli-file.txt", "stdin",
            "YXJndW1lbnQ="));

    result("decode arg", "-d decodes Base64 argument",
        output_equals("-d aGVsbG8=", NULL, "hello"));
    result("decode stdin", "-d reads Base64 from stdin",
        output_equals("-d", "aGVsbG8=", "hello"));
    fixture_ok = fixture_ok && write_text_file(file_path, "aGVsbG8=");
    result("decode file", "-d -i reads Base64 file",
        fixture_ok && output_equals("-d -i b64-cli-file.txt", NULL, "hello"));
    remove(file_path);

    result("missing mode", "encode or decode mode is required", run_cli("") != 0);
    result("invalid", "invalid Base64 fails", run_cli("-d invalid") != 0);
    result("unknown", "unknown option fails", run_cli("-e --unknown") != 0);

    printf("\n%d passed, %d failed\n", test_total - test_failed, test_failed);
    return test_failed ? 1 : 0;
}
