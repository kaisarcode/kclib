/**
 * kc.c - KaisarCode command launcher.
 * Summary: Manage installed projects and dispatch their CLIs generically.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU General Public License v3.0
 */

#include "packages.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#else
#include <errno.h>
#include <unistd.h>
#include <sys/utsname.h>
#endif

#define KC_PATH_SIZE 1024

/**
 * Print launcher options without interpreting arguments of child commands.
 * @return None.
 */
static void kc_help(void) {
    printf("Usage: kc <command> [arguments]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  kc install <name>  Install a project\n");
    printf("  kc update [name]   Update one or all projects\n");
    printf("  kc remove <name>   Remove an installed project\n");
    printf("  kc list            List installed projects\n");
    printf("  kc info <name>     Show project metadata and installation\n");
    printf("  kc <name> [args]   Execute an installed CLI\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show build version\n");
}

/**
 * Validate a command name as one plain directory and executable component.
 * @param name Requested command.
 * @return Nonzero if the name is safe.
 */
static int kc_valid_command(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    if (!*p) return 0;
    for (; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') ||
            (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')
            continue;
        return 0;
    }
    return 1;
}

/**
 * Locate the root shared with kcpkg, honoring an explicit KCPKG_HOME.
 * @param output Result buffer.
 * @param capacity Result capacity.
 * @return 0 when resolved.
 */
static int kc_home(char *output, size_t capacity) {
    const char *root = getenv("KCPKG_HOME");
    const char *suffix = "";
    int length;
    if (!root || !*root) {
#if defined(_WIN32)
        root = getenv("LOCALAPPDATA");
        suffix = "/KaisarCode/kclib";
#else
        root = getenv("XDG_DATA_HOME");
        if (root && *root) suffix = "/kaisarcode/kclib";
        else {
            root = getenv("HOME");
            suffix = "/.local/share/kaisarcode/kclib";
        }
#endif
    }
    if (!root || !*root) return 1;
    length = snprintf(output, capacity, "%s%s", root, suffix);
    return length < 0 || (size_t)length >= capacity;
}

/**
 * Select the native distribution platform and architecture.
 * @param platform Output platform.
 * @param arch Output architecture.
 * @return 0 if the platform and architecture are supported.
 */
static int kc_target(const char **platform, const char **arch) {
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    *platform = "windows";
    switch (info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: *arch = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: *arch = "i686"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: *arch = "aarch64"; break;
        default: return 1;
    }
#else
    struct utsname info;
    if (uname(&info)) return 1;
#if defined(__APPLE__)
    *platform = "macos";
#else
    *platform = "linux";
#endif
    if (!strcmp(info.machine, "x86_64") || !strcmp(info.machine, "amd64"))
        *arch = "x86_64";
    else if (!strcmp(info.machine, "i386") || !strcmp(info.machine, "i686"))
        *arch = "i686";
    else if (!strcmp(info.machine, "aarch64") || !strcmp(info.machine, "arm64"))
        *arch = "aarch64";
    else if (!strcmp(info.machine, "armv7l") || !strcmp(info.machine, "armv7"))
        *arch = "armv7";
    else if (!strcmp(info.machine, "ppc64le") || !strcmp(info.machine, "powerpc64le"))
        *arch = "powerpc64le";
    else if (!strcmp(info.machine, "riscv64"))
        *arch = "riscv64";
    else if (!strcmp(info.machine, "mips"))
        *arch = "mips";
    else if (!strcmp(info.machine, "mipsel"))
        *arch = "mipsel";
    else if (!strcmp(info.machine, "mips64el"))
        *arch = "mips64el";
    else if (!strcmp(info.machine, "s390x"))
        *arch = "s390x";
    else if (!strcmp(info.machine, "loongarch64"))
        *arch = "loongarch64";
    else return 1;
#endif
    return 0;
}

/**
 * Locate the CLI of an installed library or standalone application.
 * @param output Destination path.
 * @param capacity Destination capacity.
 * @param command Command name.
 * @return 0 on a valid bounded path.
 */
static int kc_command_path(char *output, size_t capacity, const char *command) {
    char home[KC_PATH_SIZE];
    const char *platform, *arch;
    int length;
    if (!kc_valid_command(command) || kc_home(home, sizeof(home)) ||
        kc_target(&platform, &arch)) return 1;
#if defined(_WIN32)
    length = snprintf(output, capacity, "%s/lib%s.c/%s/%s/%s.exe",
        home, command, platform, arch, command);
#else
    length = snprintf(output, capacity, "%s/lib%s.c/%s/%s/%s",
        home, command, platform, arch, command);
#endif
    if (length < 0 || (size_t)length >= capacity) return 1;
    {
        struct stat info;
        if (stat(output, &info) == 0) return 0;
    }
#if defined(_WIN32)
    length = snprintf(output, capacity, "%s/%s/%s/%s/%s.exe",
        home, command, platform, arch, command);
#else
    length = snprintf(output, capacity, "%s/%s/%s/%s/%s",
        home, command, platform, arch, command);
#endif
    return length < 0 || (size_t)length >= capacity;
}

/**
 * Return whether the first argument is a reserved package-management command.
 * @return Nonzero when the command is reserved.
 */
static int kc_is_package_command(const char *command) {
    return !strcmp(command, "install") || !strcmp(command, "update") ||
        !strcmp(command, "remove") || !strcmp(command, "list") ||
        !strcmp(command, "info");
}

/**
 * Handle the reserved package-management commands in-process, not by
 * dispatching another CLI. Any invalid reserved invocation stays reserved.
 * @return Command exit status.
 */
static int kc_package_command(int argc, char **argv) {
    const char *platform, *arch;
    const char *command = argv[1];
    if (argc > 3 || argc < 2 ||
        (argc == 2 && strcmp(command, "list") &&
            strcmp(command, "update")) ||
        (argc == 3 && !strcmp(command, "list"))) {
        kc_help();
        return 2;
    }
    if (kc_target(&platform, &arch)) {
        fputs("kc: unsupported native target\n", stderr);
        return 1;
    }
    if (!strcmp(command, "list"))
        return kc_packages_each(platform, arch, 0);
    if (!strcmp(command, "update"))
        return argc == 2 ? kc_packages_each(platform, arch, 1) :
            kc_packages_update(argv[2], platform, arch);
    if (!strcmp(command, "install"))
        return kc_packages_install(argv[2], platform, arch);
    if (!strcmp(command, "remove"))
        return kc_packages_remove(argv[2], platform, arch);
    return kc_packages_info(argv[2], platform, arch);
}

/**
 * Replace this process with the installed CLI or relay its exit code.
 * @param argc Argument count.
 * @param argv Launcher command and arguments.
 * @return Child exit status, or 127 when the CLI cannot be executed.
 */
int main(int argc, char **argv) {
    char path[KC_PATH_SIZE];
    struct stat info;
    if (argc == 1 || (argc == 2 &&
        (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
        kc_help();
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "-v") ||
        !strcmp(argv[1], "--version"))) {
        printf("kc build %s %s\n", __DATE__, __TIME__);
        return 0;
    }
    if (kc_is_package_command(argv[1]))
        return kc_package_command(argc, argv);
    if (kc_command_path(path, sizeof(path), argv[1])) {
        fprintf(stderr, "kc: invalid command or unsupported native target: %s\n",
            argv[1]);
        return 127;
    }
    if (stat(path, &info) != 0 ||
#if defined(_WIN32)
        (info.st_mode & _S_IFREG) == 0
#else
        !S_ISREG(info.st_mode)
#endif
    ) {
        fprintf(stderr, "kc: command not installed: %s\n", argv[1]);
        return 127;
    }
#if defined(_WIN32)
    {
        int status = _spawnv(_P_WAIT, path, (const char *const *)(argv + 1));
        if (status >= 0) return status;
    }
#else
    execv(path, argv + 1);
#endif
    perror("kc: cannot execute installed command");
    return 127;
}
