/**
 * libmenu.c - Native application menu entries.
 * Summary: Linux and Windows backends for application menu registration.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef KC_MENU_BUILD_VERSION
#define KC_MENU_BUILD_VERSION 0
#endif

static int kc_menu_nonempty(const char *value) {
    return value && value[0] != '\0';
}

static int kc_menu_single_line(const char *value) {
    return !value || (strchr(value, '\n') == NULL && strchr(value, '\r') == NULL);
}

static int kc_menu_valid_id(const char *id) {
    const unsigned char *p = (const unsigned char *)id;

    if (!kc_menu_nonempty(id)) return 0;
    for (; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' ||
                *p == '-') {
            continue;
        }
        return 0;
    }
    return 1;
}

static int kc_menu_valid_category(const char *category) {
    static const char *bad = "<>:\"/\\|?*;";
    const unsigned char *p = (const unsigned char *)category;

    if (!category) return 1;
    if (!kc_menu_nonempty(category) || !kc_menu_single_line(category)) return 0;
    for (; *p; p++) {
        if (*p < 32 || strchr(bad, (int)*p)) return 0;
    }
    return 1;
}

static int kc_menu_valid_entry(const kc_menu_entry_t *entry) {
    return entry && kc_menu_valid_id(entry->id) &&
        kc_menu_nonempty(entry->name) && kc_menu_nonempty(entry->command) &&
        kc_menu_single_line(entry->name) &&
        kc_menu_single_line(entry->description) &&
        kc_menu_single_line(entry->command) &&
        kc_menu_single_line(entry->icon) &&
        kc_menu_valid_category(entry->category);
}

#ifdef _WIN32

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>

static wchar_t *kc_menu_wide(const char *value) {
    int count;
    wchar_t *out;

    if (!value) return NULL;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, NULL, 0);
    if (count <= 0) return NULL;
    out = (wchar_t *)calloc((size_t)count, sizeof(wchar_t));
    if (!out) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, out, count)) {
        free(out);
        return NULL;
    }
    return out;
}

static char *kc_menu_utf8(const wchar_t *value) {
    int count;
    char *out;

    count = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
    if (count <= 0) return NULL;
    out = (char *)calloc((size_t)count, 1);
    if (!out) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, 0, value, -1, out, count, NULL, NULL)) {
        free(out);
        return NULL;
    }
    return out;
}

static int kc_menu_mkdir(const wchar_t *path) {
    DWORD attr = GetFileAttributesW(path);
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 0 : -1;
    }
    return CreateDirectoryW(path, NULL) ? 0 : -1;
}

static int kc_menu_windows_name_valid(const char *name) {
    static const char *bad = "<>:\"/\\|?*";
    const unsigned char *p = (const unsigned char *)name;

    if (!kc_menu_nonempty(name)) return 0;
    for (; *p; p++) {
        if (*p < 32 || strchr(bad, (int)*p)) return 0;
    }
    return 1;
}

static wchar_t *kc_menu_join_wide(const wchar_t *a, const wchar_t *b) {
    size_t alen = wcslen(a);
    size_t blen = wcslen(b);
    wchar_t *out = (wchar_t *)calloc(alen + blen + 2U, sizeof(wchar_t));

    if (!out) return NULL;
    memcpy(out, a, alen * sizeof(wchar_t));
    out[alen] = L'\\';
    memcpy(out + alen + 1U, b, (blen + 1U) * sizeof(wchar_t));
    return out;
}

static wchar_t *kc_menu_meta_dir(void) {
    PWSTR base = NULL;
    wchar_t *kaisar = NULL;
    wchar_t *menu = NULL;

    if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, KF_FLAG_CREATE,
            NULL, &base))) {
        return NULL;
    }
    kaisar = kc_menu_join_wide(base, L"kaisarcode");
    CoTaskMemFree(base);
    if (!kaisar) return NULL;
    if (kc_menu_mkdir(kaisar) != 0) {
        free(kaisar);
        return NULL;
    }
    menu = kc_menu_join_wide(kaisar, L"menu.c");
    free(kaisar);
    if (!menu) return NULL;
    if (kc_menu_mkdir(menu) != 0) {
        free(menu);
        return NULL;
    }
    return menu;
}

static wchar_t *kc_menu_meta_path(const char *id) {
    wchar_t *dir = kc_menu_meta_dir();
    wchar_t *wid = kc_menu_wide(id);
    wchar_t *path;

    if (!dir || !wid) {
        free(dir);
        free(wid);
        return NULL;
    }
    path = kc_menu_join_wide(dir, wid);
    free(dir);
    free(wid);
    return path;
}

static int kc_menu_meta_read(const char *id, wchar_t **out_path) {
    wchar_t *meta = kc_menu_meta_path(id);
    FILE *file;
    long size;
    char *data = NULL;
    wchar_t *path = NULL;

    *out_path = NULL;
    if (!meta) return -1;
    file = _wfopen(meta, L"rb");
    free(meta);
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
            fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    data = (char *)calloc((size_t)size + 1U, 1);
    if (!data) {
        fclose(file);
        return -1;
    }
    if (size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return -1;
    }
    fclose(file);
    path = kc_menu_wide(data);
    free(data);
    if (!path) return -1;
    *out_path = path;
    return 1;
}

static int kc_menu_meta_write(const char *id, const wchar_t *shortcut) {
    wchar_t *meta = kc_menu_meta_path(id);
    char *utf8 = kc_menu_utf8(shortcut);
    FILE *file;
    size_t len;
    int ok;

    if (!meta || !utf8) {
        free(meta);
        free(utf8);
        return -1;
    }
    file = _wfopen(meta, L"wb");
    free(meta);
    if (!file) {
        free(utf8);
        return -1;
    }
    len = strlen(utf8);
    ok = fwrite(utf8, 1, len, file) == len;
    if (fclose(file) != 0) ok = 0;
    free(utf8);
    return ok ? 0 : -1;
}

static void kc_menu_quote_arg(wchar_t **cursor, const wchar_t *arg) {
    const wchar_t *p;
    wchar_t *out = *cursor;
    size_t slashes;

    *out++ = L'"';
    p = arg;
    while (*p) {
        slashes = 0;
        while (*p == L'\\') {
            slashes++;
            p++;
        }
        if (*p == L'"') {
            while (slashes--) {
                *out++ = L'\\';
                *out++ = L'\\';
            }
            *out++ = L'\\';
            *out++ = L'"';
            p++;
        } else {
            while (slashes--) *out++ = L'\\';
            if (*p) *out++ = *p++;
        }
    }
    p = arg + wcslen(arg);
    while (p > arg && p[-1] == L'\\') {
        *out++ = L'\\';
        p--;
    }
    *out++ = L'"';
    *cursor = out;
}

static wchar_t *kc_menu_arguments(int argc, wchar_t **argv) {
    size_t total = 1U;
    int i;
    wchar_t *out;
    wchar_t *cursor;

    if (argc <= 1) return _wcsdup(L"");
    for (i = 1; i < argc; i++) total += 2U * wcslen(argv[i]) + 4U;
    out = (wchar_t *)calloc(total, sizeof(wchar_t));
    if (!out) return NULL;
    cursor = out;
    for (i = 1; i < argc; i++) {
        if (i > 1) *cursor++ = L' ';
        kc_menu_quote_arg(&cursor, argv[i]);
    }
    *cursor = L'\0';
    return out;
}

int kc_menu_add(const kc_menu_entry_t *entry) {
    PWSTR programs = NULL;
    wchar_t *wname = NULL, *wcommand = NULL, *wdescription = NULL, *wicon = NULL;
    wchar_t *wcategory = NULL, *target_dir = NULL;
    wchar_t *filename = NULL, *shortcut = NULL, *args = NULL, *old = NULL;
    wchar_t **argv = NULL;
    IShellLinkW *link = NULL;
    IPersistFile *persist = NULL;
    HRESULT hr;
    int argc = 0;
    int coinit = 0;
    int rc = KC_MENU_ERROR;
    size_t len;

    if (!kc_menu_valid_entry(entry) || !kc_menu_windows_name_valid(entry->name)) {
        return KC_MENU_ERROR;
    }

    wname = kc_menu_wide(entry->name);
    wcommand = kc_menu_wide(entry->command);
    wdescription = entry->description ? kc_menu_wide(entry->description) : NULL;
    wicon = entry->icon ? kc_menu_wide(entry->icon) : NULL;
    wcategory = entry->category ? kc_menu_wide(entry->category) : NULL;
    if (!wname || !wcommand || (entry->description && !wdescription) ||
            (entry->icon && !wicon) || (entry->category && !wcategory)) {
        goto done;
    }

    argv = CommandLineToArgvW(wcommand, &argc);
    if (!argv || argc < 1 || argv[0][0] == L'\0') goto done;
    args = kc_menu_arguments(argc, argv);
    if (!args) goto done;

    if (FAILED(SHGetKnownFolderPath(&FOLDERID_Programs, KF_FLAG_CREATE,
            NULL, &programs))) goto done;

    if (wcategory) {
        target_dir = kc_menu_join_wide(programs, wcategory);
        if (!target_dir || kc_menu_mkdir(target_dir) != 0) goto done;
    }

    len = wcslen(wname) + 5U;
    filename = (wchar_t *)calloc(len, sizeof(wchar_t));
    if (!filename) goto done;
    swprintf(filename, len, L"%ls.lnk", wname);
    shortcut = kc_menu_join_wide(target_dir ? target_dir : programs, filename);
    if (!shortcut) goto done;

    if (kc_menu_meta_read(entry->id, &old) > 0 && wcscmp(old, shortcut) != 0) {
        DeleteFileW(old);
    }

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) coinit = 1;
    else if (hr != RPC_E_CHANGED_MODE) goto done;

    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
        &IID_IShellLinkW, (void **)&link);
    if (FAILED(hr)) goto com_done;

    if (FAILED(IShellLinkW_SetPath(link, argv[0]))) goto com_done;
    if (args[0] && FAILED(IShellLinkW_SetArguments(link, args))) goto com_done;
    if (wdescription &&
            FAILED(IShellLinkW_SetDescription(link, wdescription))) goto com_done;
    if (wicon && FAILED(IShellLinkW_SetIconLocation(link, wicon, 0))) goto com_done;

    hr = IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&persist);
    if (FAILED(hr)) goto com_done;
    if (FAILED(IPersistFile_Save(persist, shortcut, TRUE))) goto com_done;
    if (kc_menu_meta_write(entry->id, shortcut) != 0) {
        DeleteFileW(shortcut);
        goto com_done;
    }
    rc = KC_MENU_OK;

com_done:
    if (persist) IPersistFile_Release(persist);
    if (link) IShellLinkW_Release(link);
    if (coinit) CoUninitialize();

done:
    if (argv) LocalFree(argv);
    if (programs) CoTaskMemFree(programs);
    free(old);
    free(args);
    free(shortcut);
    free(filename);
    free(target_dir);
    free(wcategory);
    free(wicon);
    free(wdescription);
    free(wcommand);
    free(wname);
    return rc;
}

int kc_menu_delete(const char *id) {
    wchar_t *shortcut = NULL;
    wchar_t *meta;
    int found;

    if (!kc_menu_valid_id(id)) return KC_MENU_ERROR;
    found = kc_menu_meta_read(id, &shortcut);
    if (found < 0) return KC_MENU_ERROR;
    if (found == 0) return KC_MENU_OK;

    if (!DeleteFileW(shortcut) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        free(shortcut);
        return KC_MENU_ERROR;
    }
    free(shortcut);

    meta = kc_menu_meta_path(id);
    if (!meta) return KC_MENU_ERROR;
    if (!DeleteFileW(meta) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        free(meta);
        return KC_MENU_ERROR;
    }
    free(meta);
    return KC_MENU_OK;
}

#else

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int kc_menu_mkdir(const char *path) {
    struct stat st;

    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    if (mkdir(path, 0755) == 0) return 0;
    return errno == EEXIST ? 0 : -1;
}

static char *kc_menu_data_home(void) {
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home;
    size_t len;
    char *out;

    if (xdg && xdg[0]) return strdup(xdg);
    home = getenv("HOME");
    if (!home || !home[0]) return NULL;
    len = strlen(home) + strlen("/.local/share") + 1U;
    out = (char *)malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s/.local/share", home);
    return out;
}

static char *kc_menu_app_dir(void) {
    char *base = kc_menu_data_home();
    size_t len;
    char *dir;

    if (!base) return NULL;
    if (kc_menu_mkdir(base) != 0) {
        size_t i;
        for (i = 1; base[i]; i++) {
            if (base[i] == '/') {
                char saved = base[i];
                base[i] = '\0';
                if (base[0] && kc_menu_mkdir(base) != 0) {
                    base[i] = saved;
                    free(base);
                    return NULL;
                }
                base[i] = saved;
            }
        }
        if (kc_menu_mkdir(base) != 0) {
            free(base);
            return NULL;
        }
    }
    len = strlen(base) + strlen("/applications") + 1U;
    dir = (char *)malloc(len);
    if (!dir) {
        free(base);
        return NULL;
    }
    snprintf(dir, len, "%s/applications", base);
    free(base);
    if (kc_menu_mkdir(dir) != 0) {
        free(dir);
        return NULL;
    }
    return dir;
}

static char *kc_menu_path(const char *id) {
    char *dir = kc_menu_app_dir();
    size_t len;
    char *path;

    if (!dir) return NULL;
    len = strlen(dir) + strlen(id) + strlen("/.desktop") + 1U;
    path = (char *)malloc(len);
    if (!path) {
        free(dir);
        return NULL;
    }
    snprintf(path, len, "%s/%s.desktop", dir, id);
    free(dir);
    return path;
}

static int kc_menu_write_value(FILE *file, const char *key, const char *value) {
    const unsigned char *p;

    if (!value) return 0;
    if (fprintf(file, "%s=", key) < 0) return -1;
    for (p = (const unsigned char *)value; *p; p++) {
        if (*p == '\\' || *p == '\t') {
            if (fputc('\\', file) == EOF) return -1;
            if (fputc(*p == '\t' ? 't' : '\\', file) == EOF) return -1;
        } else if (fputc(*p, file) == EOF) {
            return -1;
        }
    }
    return fputc('\n', file) == EOF ? -1 : 0;
}

int kc_menu_add(const kc_menu_entry_t *entry) {
    char *path;
    char *tmp;
    size_t len;
    FILE *file;
    int ok;

    if (!kc_menu_valid_entry(entry)) return KC_MENU_ERROR;
    path = kc_menu_path(entry->id);
    if (!path) return KC_MENU_ERROR;
    len = strlen(path) + 5U;
    tmp = (char *)malloc(len);
    if (!tmp) {
        free(path);
        return KC_MENU_ERROR;
    }
    snprintf(tmp, len, "%s.tmp", path);

    file = fopen(tmp, "wb");
    if (!file) {
        free(tmp);
        free(path);
        return KC_MENU_ERROR;
    }

    ok = fprintf(file, "[Desktop Entry]\nType=Application\n") >= 0 &&
        kc_menu_write_value(file, "Name", entry->name) == 0 &&
        (!entry->description ||
            kc_menu_write_value(file, "Comment", entry->description) == 0) &&
        kc_menu_write_value(file, "Exec", entry->command) == 0 &&
        (!entry->icon || kc_menu_write_value(file, "Icon", entry->icon) == 0) &&
        (!entry->category ||
            fprintf(file, "Categories=%s;\n", entry->category) >= 0) &&
        fprintf(file, "Terminal=false\n") >= 0 &&
        fclose(file) == 0;

    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        free(tmp);
        free(path);
        return KC_MENU_ERROR;
    }
    chmod(path, 0644);
    free(tmp);
    free(path);
    return KC_MENU_OK;
}

int kc_menu_delete(const char *id) {
    char *path;

    if (!kc_menu_valid_id(id)) return KC_MENU_ERROR;
    path = kc_menu_path(id);
    if (!path) return KC_MENU_ERROR;
    if (remove(path) != 0 && errno != ENOENT) {
        free(path);
        return KC_MENU_ERROR;
    }
    free(path);
    return KC_MENU_OK;
}

#endif

uint64_t kc_menu_version(void) {
    return (uint64_t)KC_MENU_BUILD_VERSION;
}
