/**
 * test.c - kc public CLI contract tests.
 * Summary: Exercise the generic launcher and package manager in one suite.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU General Public License v3.0
 */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

#ifndef KC_CLI_PATH
#define KC_CLI_PATH "kc"
#endif

static char kc_test_root[128];
static char kc_test_probe[512];

/**
 * Run a fixed CLI test command.
 * @param arguments Arguments to pass to kc.
 * @return Child process status.
 */
static int kc_run(const char *arguments) {
    char command[4096];
#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s >NUL 2>&1\"",
        KC_CLI_PATH, arguments);
#else
    snprintf(command, sizeof(command), "\"%s\" %s >/dev/null 2>&1",
        KC_CLI_PATH, arguments);
#endif
    return system(command);
}

/**
 * Return an exit code from a command launched through the test shell.
 * @param status Result of system().
 * @return Exit code or -1 on abnormal termination.
 */
static int kc_status(int status) {
#if defined(_WIN32)
    return status;
#else
    return status >= 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

/**
 * Capture the actual child's stdout, optionally supplying stdin from a file.
 * @param arguments Fixed kc command arguments.
 * @param input Path for stdin or NULL.
 * @param output Path for captured stdout.
 * @return Exit status of the child.
 */
static int kc_capture(const char *arguments, const char *input,
    const char *output) {
    char command[4096];
#if defined(_WIN32)
    if (input) snprintf(command, sizeof(command),
        "\"\"%s\" %s < \"%s\" > \"%s\" 2>NUL\"",
        KC_CLI_PATH, arguments, input, output);
    else snprintf(command, sizeof(command),
        "\"\"%s\" %s > \"%s\" 2>NUL\"",
        KC_CLI_PATH, arguments, output);
#else
    if (input) snprintf(command, sizeof(command),
        "\"%s\" %s < \"%s\" > \"%s\" 2>/dev/null",
        KC_CLI_PATH, arguments, input, output);
    else snprintf(command, sizeof(command),
        "\"%s\" %s > \"%s\" 2>/dev/null",
        KC_CLI_PATH, arguments, output);
#endif
    return kc_status(system(command));
}

/**
 * Compare a captured text file with the expected content.
 * @param path Capture location.
 * @param expected Expected output.
 * @return Nonzero on an exact match.
 */
static int kc_file_is(const char *path, const char *expected) {
    FILE *file = fopen(path, "r");
    char contents[512];
    size_t count;
    if (!file) return 0;
    count = fread(contents, 1, sizeof(contents) - 1, file);
    if (ferror(file)) {
        fclose(file);
        return 0;
    }
    contents[count] = '\0';
    fclose(file);
    return !strcmp(contents, expected);
}

/**
 * Make one private test directory.
 * @param path Directory path.
 * @return Nonzero on success.
 */
static int kc_mkdir(const char *path) {
#if defined(_WIN32)
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0700) == 0;
#endif
}

/**
 * Remove a private test directory.
 * @param path Directory path.
 * @return None.
 */
static void kc_rmdir(const char *path) {
#if defined(_WIN32)
    _rmdir(path);
#else
    rmdir(path);
#endif
}

/**
 * Identify the native target, matching the installed kcpkg file layout.
 * @return Native platform/architecture.
 */
static const char *kc_native_target(void) {
#if defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
    return "windows/aarch64";
#elif defined(_M_IX86) || defined(__i386__)
    return "windows/i686";
#else
    return "windows/x86_64";
#endif
#elif defined(__APPLE__)
#if defined(__aarch64__)
    return "macos/aarch64";
#else
    return "macos/x86_64";
#endif
#elif defined(__aarch64__)
    return "linux/aarch64";
#elif defined(__i386__)
    return "linux/i686";
#elif defined(__arm__)
    return "linux/armv7";
#elif defined(__riscv) && __riscv_xlen == 64
    return "linux/riscv64";
#elif defined(__powerpc64__)
    return "linux/powerpc64le";
#elif defined(__s390x__)
    return "linux/s390x";
#else
    return "linux/x86_64";
#endif
}

/**
 * Copy this test executable into a pretend kcpkg installation.
 * The copy doubles as a dummy CLI and does not require a third-party compiler.
 * @param source Path to this test executable.
 * @return Nonzero when the private installation is ready.
 */
static int kc_test_init(const char *source) {
    char path[512], parent[512];
    char buffer[8192];
    FILE *in, *out;
    size_t size;
    int i;
    unsigned long pid;
#if defined(_WIN32)
    pid = (unsigned long)_getpid();
#else
    pid = (unsigned long)getpid();
#endif
    for (i = 0; i < 1000; i++) {
        snprintf(kc_test_root, sizeof(kc_test_root),
            "kc-fixture-%lu-%d", pid, i);
        if (kc_mkdir(kc_test_root)) break;
        if (errno != EEXIST) return 0;
    }
    if (i == 1000) return 0;
    snprintf(path, sizeof(path), "%s/libprobe.c", kc_test_root);
    if (!kc_mkdir(path)) return 0;
    snprintf(path, sizeof(path), "%s/libprobe.c/%s", kc_test_root,
        kc_native_target());
    {
        char *slash = strrchr(path, '/');
        if (!slash) return 0;
        *slash = '\0';
        if (!kc_mkdir(path)) return 0;
        *slash = '/';
    }
    if (!kc_mkdir(path)) return 0;
    snprintf(parent, sizeof(parent), "%s/libprobe.c/%s", kc_test_root,
        kc_native_target());
#if defined(_WIN32)
    i = snprintf(kc_test_probe, sizeof(kc_test_probe), "%s/probe.exe", parent);
#else
    i = snprintf(kc_test_probe, sizeof(kc_test_probe), "%s/probe", parent);
#endif
    if (i < 0 || (size_t)i >= sizeof(kc_test_probe)) return 0;
    in = fopen(source, "rb");
    if (!in) return 0;
    out = fopen(kc_test_probe, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while ((size = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, size, out) != size) break;
    }
    i = ferror(in) || ferror(out) || !feof(in);
    fclose(in);
    if (fclose(out)) i = 1;
    if (i) return 0;
#if !defined(_WIN32)
    if (chmod(kc_test_probe, 0700) != 0) return 0;
    return setenv("KCPKG_HOME", kc_test_root, 1) == 0;
#else
    return _putenv_s("KCPKG_HOME", kc_test_root) == 0;
#endif
}

/**
 * Clean up only the files and directories created by the contract tests.
 * @return None.
 */
static void kc_test_cleanup(void) {
    char parent[512], path[512];
    remove("kc-help-output.txt");
    remove("kc-probe-output.txt");
    remove("kc-probe-input.txt");
    remove(kc_test_probe);
    snprintf(parent, sizeof(parent), "%s/libprobe.c/%s", kc_test_root,
        kc_native_target());
    kc_rmdir(parent);
    snprintf(path, sizeof(path), "%s/libprobe.c/%s", kc_test_root,
        kc_native_target());
    {
        char *slash = strrchr(path, '/');
        if (slash) {
            *slash = '\0';
            kc_rmdir(path);
        }
    }
    snprintf(path, sizeof(path), "%s/libprobe.c", kc_test_root);
    kc_rmdir(path);
    kc_rmdir(kc_test_root);
}

/**
 * Verify the standard kclib CLI help and version options.
 * @return Nonzero when they work.
 */
static int kc_help_contract(void) {
    FILE *input;
    char buffer[2048];
    size_t length;
    if (kc_capture("-h", NULL, "kc-help-output.txt") != 0) return 0;
    input = fopen("kc-help-output.txt", "r");
    if (!input) return 0;
    length = fread(buffer, 1, sizeof(buffer) - 1, input);
    fclose(input);
    buffer[length] = '\0';
    return strstr(buffer, "Usage: kc <command> [arguments]") != NULL &&
        strstr(buffer, "Commands:") != NULL &&
        strstr(buffer, "  kc install <name>") != NULL &&
        strstr(buffer, "  kc update [name]") != NULL &&
        strstr(buffer, "  kc remove <name>") != NULL &&
        strstr(buffer, "  kc list") != NULL &&
        strstr(buffer, "  kc info <name>") != NULL &&
        strstr(buffer, "  kc <name> [args]") != NULL &&
        strstr(buffer, "Options:") != NULL &&
        kc_run("--help") == 0 && kc_run("-v") == 0 &&
        kc_run("--version") == 0;
}

/**
 * Verify a child receives -h unchanged and writes its own help to stdout.
 * @return Nonzero when dispatch is transparent.
 */
static int kc_dispatch_help(void) {
    return kc_capture("probe -h", NULL, "kc-probe-output.txt") == 0 &&
        kc_file_is("kc-probe-output.txt", "PROBE HELP\n");
}

/**
 * Verify the child receives its argument and kc preserves its exit code.
 * @return Nonzero when forwarding works.
 */
static int kc_dispatch_status(void) {
    return kc_capture("probe kc-probe-hello", NULL, "kc-probe-output.txt") == 23 &&
        kc_file_is("kc-probe-output.txt", "PROBE:kc-probe-hello\n");
}

/**
 * Verify stdin and stdout are handed to the child without interference.
 * @return Nonzero when redirected data survives dispatch.
 */
static int kc_dispatch_stdio(void) {
    FILE *file = fopen("kc-probe-input.txt", "wb");
    if (!file) return 0;
    if (fputs("hello from stdin\n", file) < 0) {
        fclose(file);
        return 0;
    }
    if (fclose(file)) return 0;
    return kc_capture("probe kc-probe-stdin", "kc-probe-input.txt",
        "kc-probe-output.txt") == 0 &&
        kc_file_is("kc-probe-output.txt", "hello from stdin\n");
}

/**
 * Ensure standalone application CLIs use the same generic launcher contract.
 * The test binary is installed under a plain project directory, not lib*.c.
 * @return Nonzero when arguments reach the installed application's executable.
 */
static int kc_dispatch_app(void) {
    const char *target = kc_native_target();
    const char *slash = strchr(target, '/');
    char path[512], platform[512], directory[512], command[512];
    unsigned char buffer[8192];
    FILE *input, *output;
    size_t n;
    int ok = 1;
    if (!slash) return 0;
    snprintf(path, sizeof(path), "%s/probeapp", kc_test_root);
    if (!kc_mkdir(path)) return 0;
    snprintf(platform, sizeof(platform), "%s/probeapp/%.*s", kc_test_root,
        (int)(slash - target), target);
    if (!kc_mkdir(platform)) ok = 0;
    snprintf(directory, sizeof(directory), "%s/probeapp/%s", kc_test_root,
        target);
    if (ok && !kc_mkdir(directory)) ok = 0;
#if defined(_WIN32)
    snprintf(command, sizeof(command), "%s/probeapp/%s/probeapp.exe",
        kc_test_root, target);
#else
    snprintf(command, sizeof(command), "%s/probeapp/%s/probeapp",
        kc_test_root, target);
#endif
    if (ok) {
        input = fopen(kc_test_probe, "rb");
        output = fopen(command, "wb");
        if (!input || !output) ok = 0;
        if (input && output) {
            while ((n = fread(buffer, 1, sizeof(buffer), input)) != 0) {
                if (fwrite(buffer, 1, n, output) != n) ok = 0;
            }
            if (ferror(input)) ok = 0;
        }
        if (input) fclose(input);
        if (output && fclose(output)) ok = 0;
    }
#if !defined(_WIN32)
    if (ok && chmod(command, 0700)) ok = 0;
#endif
    if (ok) ok = kc_capture("probeapp -h", NULL, "kc-probe-output.txt") == 0 &&
        kc_file_is("kc-probe-output.txt", "PROBE HELP\n");
    remove(command);
    kc_rmdir(directory);
    kc_rmdir(platform);
    kc_rmdir(path);
    return ok;
}

/**
 * Print a contract result using the kclib test format.
 * @param index One-based case position.
 * @param name Stable case name.
 * @param detail Description of the case.
 * @param result Nonzero on success.
 * @return 0 for success, 1 on failure.
 */
static int kc_case(int index, const char *name, const char *detail, int result) {
    printf("[%d/25] [%s] %s: %s\n",
        index, result ? "PASS" : "FAIL", name, detail);
    return result ? 0 : 1;
}

/**
 * Run launcher tests, or act as the dummy CLI used by the tests themselves.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Contract or dummy CLI status.
 */
static char pkg_test_root[256];
static char pkg_test_source[320];
static char pkg_test_home[320];
static char pkg_test_bad_home[320];

/**
 * Execute the package manager with test-owned arguments.
 * @param arguments Fixed command arguments.
 * @return Child exit status.
 */
static int pkg_run(const char *arguments) {
    char command[4096];
#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s >NUL\"",
        KC_CLI_PATH, arguments);
#else
    snprintf(command, sizeof(command), "\"%s\" %s >/dev/null",
        KC_CLI_PATH, arguments);
#endif
    return system(command);
}

/**
 * Verify the standard Commands and Options help sections and version flags.
 * @return Nonzero when the package CLI help contract is satisfied.
 */
static int pkg_help_contract(void) {
    char command[4096], buffer[2048];
    size_t size;
    FILE *input;
#ifdef _WIN32
    snprintf(command, sizeof(command),
        "\"\"%s\" -h > kc-packages-help-output.txt 2>NUL\"", KC_CLI_PATH);
#else
    snprintf(command, sizeof(command),
        "\"%s\" -h > kc-packages-help-output.txt 2>/dev/null", KC_CLI_PATH);
#endif
    if (system(command) != 0) return 0;
    input = fopen("kc-packages-help-output.txt", "rb");
    if (!input) return 0;
    size = fread(buffer, 1, sizeof(buffer) - 1, input);
    fclose(input);
    remove("kc-packages-help-output.txt");
    buffer[size] = '\0';
    return !strncmp(buffer, "Usage: kc <command> [arguments]",
        strlen("Usage: kc <command> [arguments]")) &&
        strstr(buffer, "Commands:") != NULL &&
        strstr(buffer, "Options:") != NULL &&
        strstr(buffer, "  kc info <name>") != NULL &&
        strstr(buffer, "  kc install <name>") != NULL &&
        strstr(buffer, "  -h, --help") != NULL &&
        strstr(buffer, "  -v, --version") != NULL &&
        pkg_run("--help") == 0 && pkg_run("-v") == 0 &&
        pkg_run("--version") == 0;
}

/**
 * Create a test directory, allowing it to already exist.
 * @param path Local directory path.
 * @return Nonzero on success.
 */
static int pkg_dir(const char *path) {
#if defined(_WIN32)
    return _mkdir(path) == 0 || errno == EEXIST;
#else
    return mkdir(path, 0700) == 0 || errno == EEXIST;
#endif
}

/**
 * Return the native target used by this test executable.
 * @return Platform/architecture pair.
 */
static const char *pkg_native_target(void) {
#if defined(_WIN32)
    return "windows/x86_64";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos/aarch64";
#elif defined(__APPLE__)
    return "macos/x86_64";
#elif defined(__aarch64__)
    return "linux/aarch64";
#elif defined(__i386__)
    return "linux/i686";
#else
    return "linux/x86_64";
#endif
}

/**
 * Allocate a fresh, exclusive fixture root for each test process.
 * @return Nonzero when the workspace is reserved.
 */
static int pkg_test_init(void) {
    unsigned long pid;
    int i;
#if defined(_WIN32)
    pid = (unsigned long)_getpid();
#else
    pid = (unsigned long)getpid();
#endif
    for (i = 0; i < 1000; i++) {
        snprintf(pkg_test_root, sizeof(pkg_test_root),
            "kc-packages-fixture-%lu-%d", pid, i);
#if defined(_WIN32)
        if (_mkdir(pkg_test_root) == 0) break;
#else
        if (mkdir(pkg_test_root, 0700) == 0) break;
#endif
        if (errno != EEXIST) return 0;
    }
    if (i == 1000) return 0;
    snprintf(pkg_test_source, sizeof(pkg_test_source), "%s/source", pkg_test_root);
    snprintf(pkg_test_home, sizeof(pkg_test_home), "%s/home", pkg_test_root);
    snprintf(pkg_test_bad_home, sizeof(pkg_test_bad_home), "%s/bad", pkg_test_root);
    return pkg_dir(pkg_test_source);
}

/**
 * Create an offline fixture with one known SHA-256 artifact.
 * @param data File contents.
 * @param digest Expected SHA-256.
 * @return Nonzero when the source is ready.
 */
static int pkg_fixture_content(const char *data, const char *digest) {
    const char *target = pkg_native_target();
    const char *platform_end = strchr(target, '/');
    const char *arch = platform_end ? platform_end + 1 : "";
    char path[768];
    FILE *file;
    static const char *platforms[] = {"linux", "windows", "macos"};
    size_t i;
    if (!platform_end) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c", pkg_test_source);
    if (!pkg_dir(path)) return 0;
    for (i = 0; i < sizeof(platforms) / sizeof(platforms[0]); i++) {
        snprintf(path, sizeof(path), "%s/libnetl.c/%s", pkg_test_source,
            platforms[i]);
        if (!pkg_dir(path)) return 0;
    }
    snprintf(path, sizeof(path), "%s/libnetl.c/%s", pkg_test_source, target);
    if (!pkg_dir(path)) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c/%s/libnetl.h", pkg_test_source, target);
    file = fopen(path, "wb");
    if (!file) return 0;
    if (fwrite(data, 1, strlen(data), file) != strlen(data)) {
        fclose(file);
        return 0;
    }
    if (fclose(file)) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c/manifest.json", pkg_test_source);
    file = fopen(path, "wb");
    if (!file) return 0;
    fprintf(file, "{\"metadata\":{\"name\":\"libnetl\","
        "\"description\":\"Network sockets\",\"author\":\"KaisarCode\","
        "\"license\":\"LGPL-3.0-only\","
        "\"website\":\"https://kaisarcode.com\","
        "\"repository\":\"https://github.com/kaisarcode/kclib\"},"
        "\"artifacts\":[{\"platform\":\"%.*s\","
        "\"arch\":\"%s\",\"file\":\"libnetl.h\","
        "\"path\":\"libnetl.c/%s/libnetl.h\","
        "\"sha256\":\"%s\"}]}\n",
        (int)(platform_end - target), target, arch, target, digest);
    if (fclose(file)) return 0;
#if defined(_WIN32)
    return _putenv_s("KCDIST_DIR", pkg_test_source) == 0 &&
        _putenv_s("KCPKG_HOME", pkg_test_home) == 0;
#else
    return setenv("KCDIST_DIR", pkg_test_source, 1) == 0 &&
        setenv("KCPKG_HOME", pkg_test_home, 1) == 0;
#endif
}

/**
 * Create the standard SHA-256 verified test distribution.
 * @return Nonzero on successful fixture setup.
 */
static int pkg_fixture(void) {
    return pkg_fixture_content("abc",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

/**
 * Check the installed artifact contents.
 * @param data Expected three-byte content.
 * @return Nonzero when the installed library matches.
 */
static int pkg_installed_is(const char *data) {
    char path[768];
    FILE *file;
    char content[4] = {0};
    snprintf(path, sizeof(path), "%s/libnetl.c/%s/libnetl.h",
        pkg_test_home, pkg_native_target());
    file = fopen(path, "rb");
    if (!file) return 0;
    if (fread(content, 1, 3, file) != 3) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return memcmp(content, data, 3) == 0;
}

/**
 * Ensure installation kept the library's own manifest.
 * @return Nonzero when an installed-file receipt exists.
 */
static int pkg_has_manifest(void) {
    char path[768];
    struct stat info;
    snprintf(path, sizeof(path), "%s/libnetl.c/%s/manifest.json",
        pkg_test_home, pkg_native_target());
    return stat(path, &info) == 0;
}

/**
 * Normalize captured console output so the same assertions work on Windows
 * (CRLF) and Unix (LF). This changes test data only, not CLI output.
 * @param output Captured, NUL-terminated text.
 * @return None.
 */
static void pkg_normalize_lines(char *output) {
    char *read = output, *write = output;
    while (*read) {
        if (*read == '\r' && read[1] == '\n') read++;
        *write++ = *read++;
    }
    *write = '\0';
}

/**
 * Query package metadata and verify the installed-state report.
 * @param installed Whether a managed installation should be present.
 * @return Nonzero when all fields and the installation status are correct.
 */
static int pkg_info_case(int installed) {
    char command[4096], path[768], output[4096];
    FILE *file;
    size_t length;
    if (!pkg_fixture()) return 0;
    snprintf(path, sizeof(path), "%s/info-output.txt", pkg_test_root);
#if defined(_WIN32)
    snprintf(command, sizeof(command),
        "\"\"%s\" info netl > \"%s\" 2>NUL\"", KC_CLI_PATH, path);
#else
    snprintf(command, sizeof(command),
        "\"%s\" info netl > \"%s\" 2>/dev/null", KC_CLI_PATH, path);
#endif
    if (system(command) != 0) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    length = fread(output, 1, sizeof(output) - 1, file);
    if (ferror(file)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    remove(path);
    output[length] = '\0';
    pkg_normalize_lines(output);
    if (!strstr(output, "Name: libnetl\n") ||
        !strstr(output, "Description: Network sockets\n") ||
        !strstr(output, "Author: KaisarCode\n") ||
        !strstr(output, "License: LGPL-3.0-only\n") ||
        !strstr(output, "Website: https://kaisarcode.com\n") ||
        !strstr(output, "Repository: https://github.com/kaisarcode/kclib\n"))
        return 0;
    if (installed == 0)
        return strstr(output, "Installed: no\n") != NULL &&
            strstr(output, "Location:") == NULL;
    snprintf(path, sizeof(path), "%s/libnetl.c/%s",
        pkg_test_home, pkg_native_target());
    snprintf(command, sizeof(command), "Location: %s\n", path);
    if (installed == 1)
        return strstr(output, "Installed: yes\n") != NULL &&
            strstr(output, command) != NULL;
    return strstr(output, "Installed: incomplete\n") != NULL &&
        strstr(output, "Reason: Package manifest or declared files are missing or changed\n") != NULL &&
        strstr(output, command) != NULL;
}

/**
 * Confirm installed metadata is available without a distribution source.
 * @return Nonzero if info reads the installed project's local manifest.
 */
static int pkg_info_offline_case(void) {
    char missing[512];
    int ok;
    snprintf(missing, sizeof(missing), "%s/unavailable", pkg_test_root);
#if defined(_WIN32)
    if (_putenv_s("KCDIST_DIR", missing) != 0) return 0;
#else
    if (setenv("KCDIST_DIR", missing, 1) != 0) return 0;
#endif
    ok = pkg_run("info netl") == 0;
    return pkg_fixture() && ok;
}

/**
 * Update the installed native-target kclib from a newer fixture.
 * @return Nonzero when only the expected new content is installed.
 */
static int pkg_update_case(void) {
    return pkg_fixture_content("def",
        "cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34") &&
        pkg_run("update libnetl") == 0 && pkg_installed_is("def");
}

/**
 * Check that package listing and bulk update are available.
 * @return Nonzero when both operations succeed.
 */
static int pkg_list_case(void) {
    return pkg_run("list") == 0 && pkg_run("update") == 0 &&
        pkg_installed_is("def");
}

/**
 * Remove an installed library's owned files.
 * @return Nonzero when the installed file disappears.
 */
static int pkg_remove_case(void) {
    return pkg_run("remove libnetl") == 0 && !pkg_installed_is("def");
}

static void pkg_test_rmdir(const char *path);

/**
 * Prepare an existing package directory without a local manifest.
 * @param content Existing artifact bytes.
 * @return Nonzero when the directory contains the artifact and a user file.
 */
static int pkg_existing_without_manifest(const char *content) {
    const char *target = pkg_native_target();
    const char *slash = strchr(target, '/');
    char path[768];
    FILE *file;
    if (!slash || !pkg_fixture()) return 0;
    if (!pkg_dir(pkg_test_home)) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c", pkg_test_home);
    if (!pkg_dir(path)) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c/%.*s",
        pkg_test_home, (int)(slash - target), target);
    if (!pkg_dir(path)) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c/%s", pkg_test_home, target);
    if (!pkg_dir(path)) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c/%s/libnetl.h",
        pkg_test_home, target);
    file = fopen(path, "wb");
    if (!file) return 0;
    if (fwrite(content, 1, 3, file) != 3) {
        fclose(file);
        return 0;
    }
    if (fclose(file)) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c/%s/user-notes.txt",
        pkg_test_home, target);
    file = fopen(path, "wb");
    if (!file) return 0;
    if (fwrite("keep", 1, 4, file) != 4) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

/**
 * Ensure package operations can recover a previously populated directory
 * even if its manifest is missing, preserving unrelated files.
 * @param complete Nonzero when the existing package artifact is valid.
 * @return Nonzero if installation and removal both preserve unrelated data.
 */
static int pkg_recover_missing_manifest(int complete) {
    char file[768], directory[768], bytes[5] = {0};
    FILE *input;
    int ok;
    if (!pkg_existing_without_manifest(complete ? "abc" : "bad")) return 0;
    if (complete && !pkg_info_case(1)) return 0;
    if (pkg_run("install libnetl") != 0 ||
        !pkg_has_manifest() || !pkg_installed_is("abc"))
        return 0;
    if (pkg_run("remove libnetl") != 0) return 0;
    snprintf(directory, sizeof(directory), "%s/libnetl.c/%s",
        pkg_test_home, pkg_native_target());
    snprintf(file, sizeof(file), "%s/libnetl.c/%s/user-notes.txt",
        pkg_test_home, pkg_native_target());
    input = fopen(file, "rb");
    if (!input) return 0;
    ok = fread(bytes, 1, 4, input) == 4 && !strcmp(bytes, "keep");
    fclose(input);
    remove(file);
    pkg_test_rmdir(directory);
    return ok;
}

/**
 * Restore an existing package through update, preserving user files.
 * @return Nonzero when updated data and user files both survive.
 */
static int pkg_update_missing_manifest_case(void) {
    char file[768], directory[768], bytes[5] = {0};
    FILE *input;
    int ok;
    if (!pkg_existing_without_manifest("abc") ||
        !pkg_fixture_content("def",
            "cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34"))
        return 0;
    if (pkg_run("update libnetl") != 0 || !pkg_has_manifest() ||
        !pkg_installed_is("def")) return 0;
    if (pkg_run("remove libnetl") != 0) return 0;
    snprintf(directory, sizeof(directory), "%s/libnetl.c/%s",
        pkg_test_home, pkg_native_target());
    snprintf(file, sizeof(file), "%s/libnetl.c/%s/user-notes.txt",
        pkg_test_home, pkg_native_target());
    input = fopen(file, "rb");
    if (!input) return 0;
    ok = fread(bytes, 1, 4, input) == 4 && !strcmp(bytes, "keep");
    fclose(input);
    remove(file);
    pkg_test_rmdir(directory);
    return ok;
}

/**
 * Confirm extra files survive updates and are not deleted on removal.
 * @return Nonzero when only manifest files are managed.
 */
static int pkg_extra_files_case(void) {
    char path[768], target[768];
    FILE *file;
    int ok;
    if (!pkg_fixture() || pkg_run("install libnetl")) return 0;
    snprintf(target, sizeof(target), "%s/libnetl.c/%s",
        pkg_test_home, pkg_native_target());
    snprintf(path, sizeof(path), "%s/libnetl.c/%s/user-notes.txt",
        pkg_test_home, pkg_native_target());
    file = fopen(path, "wb");
    if (!file) return 0;
    if (fwrite("keep", 1, 4, file) != 4) {
        fclose(file);
        return 0;
    }
    if (fclose(file)) return 0;
    ok = pkg_fixture_content("def",
        "cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34") &&
        pkg_run("update libnetl") == 0 && pkg_installed_is("def");
    file = fopen(path, "rb");
    if (!file) return 0;
    {
        char bytes[5] = {0};
        ok = ok && fread(bytes, 1, 4, file) == 4 &&
            !strcmp(bytes, "keep");
    }
    fclose(file);
    if (pkg_run("remove libnetl")) return 0;
    file = fopen(path, "rb");
    ok = ok && file != NULL;
    if (file) fclose(file);
    remove(path);
    pkg_test_rmdir(target);
    return ok;
}

/**
 * Confirm that a failed update leaves the old installation intact.
 * @return Nonzero if SHA-256 failure does not replace installed data.
 */
static int pkg_update_rollback_case(void) {
    if (!pkg_fixture() || pkg_run("install libnetl") != 0 ||
        !pkg_installed_is("abc")) return 0;
    if (!pkg_fixture_content("bad",
        "cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34"))
        return 0;
    return pkg_run("update libnetl") != 0 && pkg_installed_is("abc") &&
        pkg_run("remove libnetl") == 0;
}

/**
 * Check that the full native package is installed from a local source.
 * @return Nonzero for successful verified installation.
 */
static int pkg_install_case(void) {
    return pkg_fixture() && pkg_run("install libnetl") == 0 &&
        pkg_installed_is("abc") && pkg_has_manifest();
}

/**
 * Verify a corrupted artifact is never installed.
 * @return Nonzero when the incorrect SHA-256 is rejected.
 */
static int pkg_bad_hash_case(void) {
    FILE *file;
    struct stat info;
    char path[768];
    if (!pkg_fixture()) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c/%s/libnetl.h",
        pkg_test_source, pkg_native_target());
    file = fopen(path, "wb");
    if (!file) return 0;
    if (fwrite("bad", 1, 3, file) != 3) {
        fclose(file);
        return 0;
    }
    if (fclose(file)) return 0;
#if defined(_WIN32)
    if (_putenv_s("KCPKG_HOME", pkg_test_bad_home) != 0) return 0;
#else
    if (setenv("KCPKG_HOME", pkg_test_bad_home, 1) != 0) return 0;
#endif
    if (pkg_run("install libnetl") == 0) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c/%s",
        pkg_test_bad_home, pkg_native_target());
    return stat(path, &info) != 0;
}

/**
 * Ensure large project manifests do not exhaust a fixed token array.
 * @return Nonzero when a valid package can be installed from a large inventory.
 */
static int pkg_large_manifest_case(void) {
    char path[768];
    const char *target = pkg_native_target();
    const char *slash = strchr(target, '/');
    FILE *file;
    int i;
    if (!slash || !pkg_fixture()) return 0;
    snprintf(path, sizeof(path), "%s/libnetl.c/manifest.json", pkg_test_source);
    file = fopen(path, "wb");
    if (!file) return 0;
    fprintf(file, "{\"metadata\":{\"name\":\"libnetl\"},\"artifacts\":[{"
        "\"platform\":\"%.*s\",\"arch\":\"%s\","
        "\"file\":\"libnetl.h\","
        "\"path\":\"libnetl.c/%s/libnetl.h\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\""
        "}],\"other\":[",
        (int)(slash - target), target, slash + 1, target);
    for (i = 0; i < 3000; i++) {
        if (i) fputc(',', file);
        fputs("{\"a\":\"b\",\"c\":1,\"d\":true,\"e\":null,\"f\":[0,1,2]}", file);
    }
    fputs("]}\n", file);
    if (fclose(file)) return 0;
    return pkg_run("install libnetl") == 0 &&
        pkg_installed_is("abc") && pkg_run("remove libnetl") == 0;
}

/**
 * Remove a directory from this process-owned test workspace.
 * @param path Directory path.
 * @return None.
 */
static void pkg_test_rmdir(const char *path) {
#if defined(_WIN32)
    _rmdir(path);
#else
    rmdir(path);
#endif
}

/**
 * Existing directories without an installation record must be explained.
 * @return Nonzero when info displays the unverified state and directory.
 */
static int pkg_info_unrecognized_case(void) {
    char path[768];
    int passed;
    snprintf(path, sizeof(path), "%s/libnetl.c/%s",
        pkg_test_home, pkg_native_target());
    if (!pkg_dir(path)) return 0;
    passed = pkg_info_case(2);
    pkg_test_rmdir(path);
    return passed;
}

/**
 * Clean test-owned files after a successful run.
 * @return None.
 */
static void pkg_test_cleanup(void) {
    const char *roots[] = {pkg_test_source, pkg_test_home, pkg_test_bad_home};
    static const char *platforms[] = {"linux", "windows", "macos"};
    char path[768];
    size_t i, j;
    const char *target = pkg_native_target();
    const char *platform_end = strchr(target, '/');
    if (!platform_end) return;
    for (i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        snprintf(path, sizeof(path), "%s/libnetl.c/%s/libnetl.h",
            roots[i], target);
        remove(path);
        snprintf(path, sizeof(path), "%s/libnetl.c/%s/manifest.json",
            roots[i], target);
        remove(path);
        snprintf(path, sizeof(path), "%s/libnetl.c/%s/user-notes.txt",
            roots[i], target);
        remove(path);
        snprintf(path, sizeof(path), "%s/libnetl.c/%s",
            roots[i], target);
        pkg_test_rmdir(path);
        for (j = 0; j < sizeof(platforms) / sizeof(platforms[0]); j++) {
            snprintf(path, sizeof(path), "%s/libnetl.c/%s",
                roots[i], platforms[j]);
            pkg_test_rmdir(path);
        }
        snprintf(path, sizeof(path), "%s/libnetl.c/manifest.json", roots[i]);
        remove(path);
        snprintf(path, sizeof(path), "%s/libnetl.c", roots[i]);
        pkg_test_rmdir(path);
        pkg_test_rmdir(roots[i]);
    }
    for (i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        const char *binary;
#if defined(_WIN32)
        binary = "kc.exe";
#else
        binary = "kc";
#endif
        snprintf(path, sizeof(path), "%s/kc/%s/%s", roots[i], target, binary);
        remove(path);
        snprintf(path, sizeof(path), "%s/kc/%s/manifest.json", roots[i], target);
        remove(path);
        snprintf(path, sizeof(path), "%s/kc/%s", roots[i], target);
        pkg_test_rmdir(path);
        for (j = 0; j < sizeof(platforms) / sizeof(platforms[0]); j++) {
            snprintf(path, sizeof(path), "%s/kc/%s", roots[i], platforms[j]);
            pkg_test_rmdir(path);
        }
        snprintf(path, sizeof(path), "%s/kc/manifest.json", roots[i]);
        remove(path);
        snprintf(path, sizeof(path), "%s/kc", roots[i]);
        pkg_test_rmdir(path);
    }
    pkg_test_rmdir(pkg_test_root);
}

/**
 * A standalone kcapp project uses the same manifest schema and shared
 * distribution root as a kclib, but does not need a lib prefix or .c suffix.
 * @return Nonzero when its installation, metadata and removal work.
 */
static int pkg_application_case(void) {
    const char *target = pkg_native_target();
    const char *separator = strchr(target, '/');
    char path[768], installed[768];
    const char *binary;
    FILE *file;
    struct stat info;
    if (!separator || !pkg_fixture()) return 0;
#if defined(_WIN32)
    binary = "kc.exe";
#else
    binary = "kc";
#endif
    snprintf(path, sizeof(path), "%s/kc", pkg_test_source);
    if (!pkg_dir(path)) return 0;
    snprintf(path, sizeof(path), "%s/kc/%.*s", pkg_test_source,
        (int)(separator - target), target);
    if (!pkg_dir(path)) return 0;
    snprintf(path, sizeof(path), "%s/kc/%s", pkg_test_source, target);
    if (!pkg_dir(path)) return 0;
    snprintf(path, sizeof(path), "%s/kc/%s/%s", pkg_test_source, target,
        binary);
    file = fopen(path, "wb");
    if (!file) return 0;
    if (fwrite("abc", 1, 3, file) != 3) {
        fclose(file);
        return 0;
    }
    if (fclose(file)) return 0;
    snprintf(path, sizeof(path), "%s/kc/manifest.json", pkg_test_source);
    file = fopen(path, "wb");
    if (!file) return 0;
    fprintf(file,
        "{\"metadata\":{\"name\":\"kc\",\"description\":\"CLI launcher\","
        "\"author\":\"KaisarCode\",\"license\":\"GPL-3.0-only\"},"
        "\"artifacts\":[{\"platform\":\"%.*s\",\"arch\":\"%s\","
        "\"file\":\"%s\",\"path\":\"kc/%s/%s\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}]}\n",
        (int)(separator - target), target, separator + 1,
        binary, target, binary);
    if (fclose(file)) return 0;
    if (pkg_run("info kc") || pkg_run("install kc") ||
        pkg_run("info kc") || pkg_run("list")) return 0;
    snprintf(installed, sizeof(installed), "%s/kc/%s/%s", pkg_test_home,
        target, binary);
    if (stat(installed, &info) != 0 || pkg_run("remove kc")) return 0;
    return stat(installed, &info) != 0;
}

/**
 * Output one numbered contract test.
 * @param number Case number.
 * @param name Case name.
 * @param detail Behavior exercised.
 * @param passed Nonzero on success.
 * @return 0 when passing.
 */
static int pkg_case(int number, const char *name, const char *detail, int passed) {
    printf("[%d/25] [%s] package_%s: %s\n",
        number + 7, passed ? "PASS" : "FAIL", name, detail);
    return passed ? 0 : 1;
}

/**
 * Run the eighteen package-management cases from the unified test source.
 * @return Number of failed package-management cases.
 */
static int pkg_run_tests(void) {
    int failures = 0;
    if (!pkg_test_init()) {
        fputs("cannot allocate isolated package test fixture\n", stderr);
        return 18;
    }
    failures += pkg_case(1, "cli_help", "shows package commands and options",
        pkg_help_contract());
    failures += pkg_case(2, "cli_unknown", "rejects unsupported commands",
        pkg_run("search libnetl") != 0 && pkg_run("info") != 0);
    failures += pkg_case(3, "info_available", "shows metadata before installation",
        pkg_info_case(0));
    failures += pkg_case(4, "local_install", "installs verified kclib artifacts offline",
        pkg_install_case());
    failures += pkg_case(5, "info_installed", "shows metadata and installed location",
        pkg_info_case(1));
    failures += pkg_case(6, "info_offline", "reads installed metadata without the server",
        pkg_info_offline_case());
    failures += pkg_case(7, "update", "updates an installed library", pkg_update_case());
    failures += pkg_case(8, "list", "lists libraries and updates installed ones", pkg_list_case());
    failures += pkg_case(9, "remove", "removes only managed files", pkg_remove_case());
    failures += pkg_case(10, "info_unverified", "explains an existing unrecognized directory",
        pkg_info_unrecognized_case());
    failures += pkg_case(11, "adopt_manifest",
        "restores a manifest for already verified package files",
        pkg_recover_missing_manifest(1));
    failures += pkg_case(12, "repair_manifest",
        "repairs files without a manifest and preserves unrelated files",
        pkg_recover_missing_manifest(0));
    failures += pkg_case(13, "update_missing_manifest",
        "updates existing files without requiring a local manifest",
        pkg_update_missing_manifest_case());
    failures += pkg_case(14, "extra_files", "preserves user files through update and remove",
        pkg_extra_files_case());
    failures += pkg_case(15, "update_rollback", "keeps old files after failed update",
        pkg_update_rollback_case());
    failures += pkg_case(16, "invalid_hash", "rejects a corrupted artifact",
        pkg_bad_hash_case());
    failures += pkg_case(17, "large_manifest", "accepts a full multi-project inventory",
        pkg_large_manifest_case());
    failures += pkg_case(18, "application_package",
        "installs kcapp project from the same KCDIST_DIR root",
        pkg_application_case());
    if (failures) fprintf(stderr, "kc package test workspace: %s\n", pkg_test_root);
    else pkg_test_cleanup();
    return failures;
}

/**
 * Run the unified kc command and package-management test suite.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return Zero when all test cases pass, otherwise nonzero.
 */
int main(int argc, char **argv) {
    int failures = 0;
    int c;
    if (argc == 2 && !strcmp(argv[1], "-h")) {
        puts("PROBE HELP");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "kc-probe-hello")) {
        puts("PROBE:kc-probe-hello");
        return 23;
    }
    if (argc == 2 && !strcmp(argv[1], "kc-probe-stdin")) {
        while ((c = getchar()) != EOF) putchar(c);
        return ferror(stdin) || ferror(stdout) ? 1 : 0;
    }
    if (argc != 2 || strcmp(argv[1], "all")) {
        fputs("test case: expected all\n", stderr);
        return 2;
    }
    if (!kc_test_init(argv[0])) {
        fputs("test case: cannot create dummy installed CLI\n", stderr);
        return 1;
    }
    failures += kc_case(1, "cli_help", "shows launcher options",
        kc_help_contract());
    failures += kc_case(2, "cli_unknown", "rejects commands not installed",
        kc_run("unknown-command") != 0);
    failures += kc_case(3, "reserved_commands", "handles package commands internally",
        kc_run("list") == 0 && kc_run("install") != 0 &&
        kc_run("info") != 0 && kc_run("remove") != 0 &&
        kc_run("list extra") != 0 && kc_run("update extra extra") != 0);
    failures += kc_case(4, "dispatch_help", "delegates help to the installed CLI",
        kc_dispatch_help());
    failures += kc_case(5, "dispatch_status", "preserves arguments and exit status",
        kc_dispatch_status());
    failures += kc_case(6, "dispatch_stdio", "preserves stdin and stdout",
        kc_dispatch_stdio());
    failures += kc_case(7, "dispatch_app", "delegates application CLIs by project path",
        kc_dispatch_app());
    if (failures) fprintf(stderr, "kc test workspace: %s\n", kc_test_root);
    else kc_test_cleanup();
    failures += pkg_run_tests();
    printf("%d passed, %d failed\n", 25 - failures, failures);
    return failures ? 1 : 0;
}
