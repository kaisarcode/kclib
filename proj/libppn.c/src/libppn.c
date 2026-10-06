/**
 * libppn.c - Native popup notifications.
 * Summary: Displays native notifications on Linux, Windows, and macOS.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libppn.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_PPN_BUILD_VERSION
#define KC_PPN_BUILD_VERSION 0
#endif

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#import <Foundation/Foundation.h>
#else
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#if !defined(_WIN32) && !defined(__APPLE__)

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} kc_ppn_buffer_t;

/**
 * Reserve space in a byte buffer.
 * @param buffer Byte buffer.
 * @param extra Additional byte count.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_buffer_reserve(kc_ppn_buffer_t *buffer, size_t extra) {
    size_t capacity;
    unsigned char *data;

    if (extra > SIZE_MAX - buffer->size) {
        return KC_PPN_ERROR;
    }
    if (buffer->size + extra <= buffer->capacity) {
        return KC_PPN_OK;
    }

    capacity = buffer->capacity == 0 ? 128 : buffer->capacity;
    while (capacity < buffer->size + extra) {
        if (capacity > SIZE_MAX / 2) {
            capacity = buffer->size + extra;
            break;
        }
        capacity *= 2;
    }

    data = (unsigned char *)realloc(buffer->data, capacity);
    if (data == NULL) {
        return KC_PPN_ERROR;
    }

    buffer->data = data;
    buffer->capacity = capacity;
    return KC_PPN_OK;
}

/**
 * Append bytes to a byte buffer.
 * @param buffer Byte buffer.
 * @param data Bytes to append.
 * @param size Byte count.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_buffer_append(
    kc_ppn_buffer_t *buffer,
    const void *data,
    size_t size
) {
    if (kc_ppn_buffer_reserve(buffer, size) != KC_PPN_OK) {
        return KC_PPN_ERROR;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return KC_PPN_OK;
}

/**
 * Pad a byte buffer to the requested alignment.
 * @param buffer Byte buffer.
 * @param alignment Required alignment.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_buffer_align(kc_ppn_buffer_t *buffer, size_t alignment) {
    static const unsigned char zero[8] = {0};
    size_t padding;

    padding = (alignment - (buffer->size % alignment)) % alignment;
    return kc_ppn_buffer_append(buffer, zero, padding);
}

/**
 * Append one little-endian 32-bit integer.
 * @param buffer Byte buffer.
 * @param value Integer value.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_buffer_u32(kc_ppn_buffer_t *buffer, uint32_t value) {
    unsigned char raw[4];

    raw[0] = (unsigned char)(value & 0xffU);
    raw[1] = (unsigned char)((value >> 8) & 0xffU);
    raw[2] = (unsigned char)((value >> 16) & 0xffU);
    raw[3] = (unsigned char)((value >> 24) & 0xffU);
    return kc_ppn_buffer_append(buffer, raw, sizeof(raw));
}

/**
 * Append one little-endian 64-bit integer.
 * @param buffer Destination byte buffer.
 * @param value Integer value.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_buffer_u64(kc_ppn_buffer_t *buffer, uint64_t value) {
    unsigned char raw[8];
    size_t index;

    for (index = 0; index < sizeof(raw); index++) {
        raw[index] = (unsigned char)(value >> (index * 8U));
    }
    return kc_ppn_buffer_append(buffer, raw, sizeof(raw));
}

/**
 * Append one D-Bus string or object path.
 * @param buffer Byte buffer.
 * @param value String value.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_buffer_string(kc_ppn_buffer_t *buffer, const char *value) {
    uint32_t length;

    if (value == NULL || strlen(value) > UINT32_MAX) {
        return KC_PPN_ERROR;
    }
    length = (uint32_t)strlen(value);

    if (kc_ppn_buffer_align(buffer, 4) != KC_PPN_OK ||
        kc_ppn_buffer_u32(buffer, length) != KC_PPN_OK ||
        kc_ppn_buffer_append(buffer, value, length + 1) != KC_PPN_OK) {
        return KC_PPN_ERROR;
    }
    return KC_PPN_OK;
}

/**
 * Append one D-Bus signature value.
 * @param buffer Byte buffer.
 * @param value Signature value.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_buffer_signature(kc_ppn_buffer_t *buffer, const char *value) {
    unsigned char length;

    if (value == NULL || strlen(value) > 255) {
        return KC_PPN_ERROR;
    }
    length = (unsigned char)strlen(value);

    if (kc_ppn_buffer_append(buffer, &length, 1) != KC_PPN_OK ||
        kc_ppn_buffer_append(buffer, value, length + 1) != KC_PPN_OK) {
        return KC_PPN_ERROR;
    }
    return KC_PPN_OK;
}

/**
 * Append one D-Bus header field.
 * @param fields Header field buffer.
 * @param code Header field code.
 * @param type Variant type signature.
 * @param value Header field value.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_header_field(
    kc_ppn_buffer_t *fields,
    unsigned char code,
    const char *type,
    const char *value
) {
    if (kc_ppn_buffer_align(fields, 8) != KC_PPN_OK ||
        kc_ppn_buffer_append(fields, &code, 1) != KC_PPN_OK ||
        kc_ppn_buffer_signature(fields, type) != KC_PPN_OK) {
        return KC_PPN_ERROR;
    }

    if (strcmp(type, "g") == 0) {
        return kc_ppn_buffer_signature(fields, value);
    }
    return kc_ppn_buffer_string(fields, value);
}

/**
 * Decode percent escapes used by D-Bus addresses.
 * @param value Encoded address fragment.
 * @param length Encoded fragment length.
 * @return Allocated decoded string, or NULL on failure.
 */
static char *kc_ppn_dbus_unescape(const char *value, size_t length) {
    char *decoded;
    size_t source;
    size_t target;

    decoded = (char *)malloc(length + 1);
    if (decoded == NULL) {
        return NULL;
    }

    target = 0;
    for (source = 0; source < length; source++) {
        if (value[source] == '%' && source + 2 < length) {
            char hex[3];
            char *end = NULL;
            long byte;

            hex[0] = value[source + 1];
            hex[1] = value[source + 2];
            hex[2] = '\0';
            byte = strtol(hex, &end, 16);
            if (end != hex + 2 || byte < 0 || byte > 255) {
                free(decoded);
                return NULL;
            }
            decoded[target++] = (char)byte;
            source += 2;
        } else {
            decoded[target++] = value[source];
        }
    }
    decoded[target] = '\0';
    return decoded;
}

/**
 * Connect to the current D-Bus session socket.
 * @return Connected socket descriptor, or -1 on failure.
 */
static int kc_ppn_dbus_connect(void) {
    const char *address;
    const char *entry;
    const char *end;
    const char *value;
    size_t length;
    int abstract_address;
    int socket_fd;
    struct sockaddr_un socket_address;
    socklen_t socket_length;
    char *decoded;

    address = getenv("DBUS_SESSION_BUS_ADDRESS");
    if (address == NULL || *address == '\0') {
        return -1;
    }

    entry = strstr(address, "unix:");
    if (entry == NULL) {
        return -1;
    }
    entry += 5;
    end = strchr(entry, ';');
    if (end == NULL) {
        end = entry + strlen(entry);
    }

    value = strstr(entry, "path=");
    abstract_address = 0;
    if (value == NULL || value >= end) {
        value = strstr(entry, "abstract=");
        abstract_address = 1;
        if (value == NULL || value >= end) {
            return -1;
        }
        value += 9;
    } else {
        value += 5;
    }

    length = strcspn(value, ",;");
    if (value + length > end) {
        length = (size_t)(end - value);
    }
    decoded = kc_ppn_dbus_unescape(value, length);
    if (decoded == NULL) {
        return -1;
    }

    if (strlen(decoded) + (abstract_address ? 1U : 0U) >=
        sizeof(socket_address.sun_path)) {
        free(decoded);
        return -1;
    }

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        free(decoded);
        return -1;
    }

    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.sun_family = AF_UNIX;
    if (abstract_address) {
        socket_address.sun_path[0] = '\0';
        memcpy(socket_address.sun_path + 1, decoded, strlen(decoded));
        socket_length = (socklen_t)(
            offsetof(struct sockaddr_un, sun_path) + 1 + strlen(decoded)
        );
    } else {
        memcpy(socket_address.sun_path, decoded, strlen(decoded) + 1);
        socket_length = (socklen_t)(
            offsetof(struct sockaddr_un, sun_path) + strlen(decoded) + 1
        );
    }
    free(decoded);

    if (connect(
        socket_fd,
        (const struct sockaddr *)&socket_address,
        socket_length
    ) != 0) {
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

/**
 * Write all bytes to a socket without raising SIGPIPE.
 * @param socket_fd Connected socket descriptor.
 * @param data Bytes to write.
 * @param size Byte count.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_socket_write(int socket_fd, const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;

    while (size > 0) {
        ssize_t written = send(socket_fd, bytes, size, MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return KC_PPN_ERROR;
        }
        bytes += written;
        size -= (size_t)written;
    }
    return KC_PPN_OK;
}

/**
 * Read all requested bytes from a socket.
 * @param socket_fd Connected socket descriptor.
 * @param data Destination buffer.
 * @param size Byte count.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_socket_read(int socket_fd, void *data, size_t size) {
    unsigned char *bytes = (unsigned char *)data;

    while (size > 0) {
        ssize_t count = recv(socket_fd, bytes, size, 0);
        if (count <= 0) {
            if (count < 0 && errno == EINTR) {
                continue;
            }
            return KC_PPN_ERROR;
        }
        bytes += count;
        size -= (size_t)count;
    }
    return KC_PPN_OK;
}

/**
 * Read one CRLF-terminated D-Bus authentication line.
 * @param socket_fd Connected socket descriptor.
 * @param line Destination line buffer.
 * @param capacity Destination capacity.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_dbus_auth_line(int socket_fd, char *line, size_t capacity) {
    size_t used = 0;

    while (used + 1 < capacity) {
        char byte;
        ssize_t count = recv(socket_fd, &byte, 1, 0);

        if (count <= 0) {
            return KC_PPN_ERROR;
        }
        line[used++] = byte;
        if (used >= 2 && line[used - 2] == '\r' && line[used - 1] == '\n') {
            line[used] = '\0';
            return KC_PPN_OK;
        }
    }
    return KC_PPN_ERROR;
}

/**
 * Authenticate on the D-Bus session connection.
 * @param socket_fd Connected socket descriptor.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_dbus_authenticate(int socket_fd) {
    char uid[32];
    char request[160];
    char response[256];
    char encoded[64];
    size_t index;
    size_t offset = 0;
    int count;
    size_t uid_length;

    count = snprintf(uid, sizeof(uid), "%lu", (unsigned long)getuid());
    if (count <= 0 || (size_t)count >= sizeof(uid)) {
        return KC_PPN_ERROR;
    }
    uid_length = (size_t)count;

    for (index = 0; index < uid_length; index++) {
        count = snprintf(
            encoded + offset,
            sizeof(encoded) - offset,
            "%02x",
            (unsigned char)uid[index]
        );
        if (count != 2) {
            return KC_PPN_ERROR;
        }
        offset += 2;
    }
    encoded[offset] = '\0';

    request[0] = '\0';
    count = snprintf(
        request + 1,
        sizeof(request) - 1,
        "AUTH EXTERNAL %s\r\n",
        encoded
    );
    if (count <= 0 || (size_t)count >= sizeof(request) - 1 ||
        kc_ppn_socket_write(
            socket_fd, request, (size_t)count + 1
        ) != KC_PPN_OK ||
        kc_ppn_dbus_auth_line(
            socket_fd, response, sizeof(response)
        ) != KC_PPN_OK ||
        strncmp(response, "OK ", 3) != 0) {
        return KC_PPN_ERROR;
    }

    if (kc_ppn_socket_write(socket_fd, "BEGIN\r\n", 7) != KC_PPN_OK) {
        return KC_PPN_ERROR;
    }
    return KC_PPN_OK;
}

/**
 * Build one D-Bus method-call message.
 * @param message Destination message buffer.
 * @param serial Message serial.
 * @param flags D-Bus message flags.
 * @param path Object path.
 * @param interface Interface name.
 * @param member Method name.
 * @param destination Destination bus name.
 * @param signature Optional body signature.
 * @param body Optional body buffer.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_dbus_message(
    kc_ppn_buffer_t *message,
    uint32_t serial,
    unsigned char flags,
    const char *path,
    const char *interface,
    const char *member,
    const char *destination,
    const char *signature,
    const kc_ppn_buffer_t *body
) {
    kc_ppn_buffer_t fields = {0};
    unsigned char fixed[16] = {'l', 1, 0, 1};
    uint32_t body_size;
    uint32_t fields_size;
    int result = KC_PPN_ERROR;

    fixed[2] = flags;
    if (kc_ppn_header_field(&fields, 1, "o", path) != KC_PPN_OK ||
        kc_ppn_header_field(&fields, 2, "s", interface) != KC_PPN_OK ||
        kc_ppn_header_field(&fields, 3, "s", member) != KC_PPN_OK ||
        kc_ppn_header_field(&fields, 6, "s", destination) != KC_PPN_OK ||
        (signature != NULL &&
            kc_ppn_header_field(&fields, 8, "g", signature) != KC_PPN_OK) ||
        fields.size > UINT32_MAX ||
        (body != NULL && body->size > UINT32_MAX)) {
        goto cleanup;
    }

    body_size = body == NULL ? 0 : (uint32_t)body->size;
    fields_size = (uint32_t)fields.size;
    fixed[4] = (unsigned char)(body_size & 0xffU);
    fixed[5] = (unsigned char)((body_size >> 8) & 0xffU);
    fixed[6] = (unsigned char)((body_size >> 16) & 0xffU);
    fixed[7] = (unsigned char)((body_size >> 24) & 0xffU);
    fixed[8] = (unsigned char)(serial & 0xffU);
    fixed[9] = (unsigned char)((serial >> 8) & 0xffU);
    fixed[10] = (unsigned char)((serial >> 16) & 0xffU);
    fixed[11] = (unsigned char)((serial >> 24) & 0xffU);
    fixed[12] = (unsigned char)(fields_size & 0xffU);
    fixed[13] = (unsigned char)((fields_size >> 8) & 0xffU);
    fixed[14] = (unsigned char)((fields_size >> 16) & 0xffU);
    fixed[15] = (unsigned char)((fields_size >> 24) & 0xffU);

    if (kc_ppn_buffer_append(message, fixed, sizeof(fixed)) != KC_PPN_OK ||
        kc_ppn_buffer_append(message, fields.data, fields.size) != KC_PPN_OK ||
        kc_ppn_buffer_align(message, 8) != KC_PPN_OK ||
        (body != NULL && kc_ppn_buffer_append(
            message, body->data, body->size
        ) != KC_PPN_OK)) {
        goto cleanup;
    }
    result = KC_PPN_OK;

cleanup:
    free(fields.data);
    return result;
}

/**
 * Read and discard one complete D-Bus message.
 * @param socket_fd Connected socket descriptor.
 * @return Message type on success, or -1 on failure.
 */
static int kc_ppn_dbus_read_message(int socket_fd) {
    unsigned char fixed[16];
    unsigned char discard[256];
    uint32_t body_size;
    uint32_t fields_size;
    size_t header_size;
    size_t remaining;

    if (kc_ppn_socket_read(socket_fd, fixed, sizeof(fixed)) != KC_PPN_OK ||
        fixed[0] != 'l' || fixed[3] != 1) {
        return -1;
    }

    body_size = (uint32_t)fixed[4] |
        ((uint32_t)fixed[5] << 8) |
        ((uint32_t)fixed[6] << 16) |
        ((uint32_t)fixed[7] << 24);
    fields_size = (uint32_t)fixed[12] |
        ((uint32_t)fixed[13] << 8) |
        ((uint32_t)fixed[14] << 16) |
        ((uint32_t)fixed[15] << 24);

    header_size = 16U + (size_t)fields_size;
    header_size = (header_size + 7U) & ~(size_t)7U;
    remaining = header_size - 16U + (size_t)body_size;

    while (remaining > 0) {
        size_t chunk = remaining < sizeof(discard) ? remaining : sizeof(discard);
        if (kc_ppn_socket_read(socket_fd, discard, chunk) != KC_PPN_OK) {
            return -1;
        }
        remaining -= chunk;
    }
    return fixed[1];
}

/**
 * Register the authenticated connection with the D-Bus daemon.
 * @param socket_fd Connected socket descriptor.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_dbus_hello(int socket_fd) {
    kc_ppn_buffer_t message = {0};
    int result;

    result = kc_ppn_dbus_message(
        &message,
        1,
        1,
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "Hello",
        "org.freedesktop.DBus",
        NULL,
        NULL
    );
    if (result == KC_PPN_OK) {
        result = kc_ppn_socket_write(socket_fd, message.data, message.size);
    }
    free(message.data);
    return result;
}

/**
 * Append the standard Linux notification hints.
 * @param body Notification body buffer.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_dbus_hints(kc_ppn_buffer_t *body) {
    size_t length_offset;
    size_t payload_offset;
    size_t payload_size;
    uint32_t length;
    unsigned char normal_urgency = 1;

    if (kc_ppn_buffer_align(body, 4) != KC_PPN_OK) {
        return KC_PPN_ERROR;
    }
    length_offset = body->size;
    if (kc_ppn_buffer_u32(body, 0) != KC_PPN_OK ||
        kc_ppn_buffer_align(body, 8) != KC_PPN_OK) {
        return KC_PPN_ERROR;
    }

    payload_offset = body->size;
    if (kc_ppn_buffer_string(body, "urgency") != KC_PPN_OK ||
        kc_ppn_buffer_signature(body, "y") != KC_PPN_OK ||
        kc_ppn_buffer_append(body, &normal_urgency, sizeof(normal_urgency)) !=
            KC_PPN_OK ||
        kc_ppn_buffer_align(body, 8) != KC_PPN_OK ||
        kc_ppn_buffer_string(body, "sender-pid") != KC_PPN_OK ||
        kc_ppn_buffer_signature(body, "x") != KC_PPN_OK ||
        kc_ppn_buffer_align(body, 8) != KC_PPN_OK ||
        kc_ppn_buffer_u64(body, (uint64_t)getpid()) != KC_PPN_OK) {
        return KC_PPN_ERROR;
    }

    payload_size = body->size - payload_offset;
    if (payload_size > UINT32_MAX) {
        return KC_PPN_ERROR;
    }
    length = (uint32_t)payload_size;
    body->data[length_offset] = (unsigned char)(length & 0xffU);
    body->data[length_offset + 1] = (unsigned char)((length >> 8) & 0xffU);
    body->data[length_offset + 2] = (unsigned char)((length >> 16) & 0xffU);
    body->data[length_offset + 3] = (unsigned char)((length >> 24) & 0xffU);
    return KC_PPN_OK;
}

/**
 * Construct the Freedesktop Notify method body.
 * @param body Destination body buffer.
 * @param notification Notification values.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_dbus_body(
    kc_ppn_buffer_t *body,
    const kc_ppn_notification_t *notification
) {
    if (kc_ppn_buffer_string(body, "Application") != KC_PPN_OK ||
        kc_ppn_buffer_u32(body, 0) != KC_PPN_OK ||
        kc_ppn_buffer_string(body, "") != KC_PPN_OK ||
        kc_ppn_buffer_string(body, notification->title) != KC_PPN_OK ||
        kc_ppn_buffer_string(body, notification->message) != KC_PPN_OK ||
        kc_ppn_buffer_align(body, 4) != KC_PPN_OK ||
        kc_ppn_buffer_u32(body, 0) != KC_PPN_OK ||
        kc_ppn_dbus_hints(body) != KC_PPN_OK ||
        kc_ppn_buffer_align(body, 8) != KC_PPN_OK ||
        kc_ppn_buffer_u32(body, UINT32_MAX) != KC_PPN_OK) {
        return KC_PPN_ERROR;
    }
    return KC_PPN_OK;
}

/**
 * Send one notification through org.freedesktop.Notifications.
 * @param notification Notification values.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_show_linux(const kc_ppn_notification_t *notification) {
    kc_ppn_buffer_t body = {0};
    kc_ppn_buffer_t message = {0};
    struct timeval timeout;
    int socket_fd = -1;
    int result = KC_PPN_ERROR;
    int index;

    if (kc_ppn_dbus_body(&body, notification) != KC_PPN_OK ||
        kc_ppn_dbus_message(
            &message,
            2,
            0,
            "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications",
            "Notify",
            "org.freedesktop.Notifications",
            "susssasa{sv}i",
            &body
        ) != KC_PPN_OK) {
        goto cleanup;
    }

    socket_fd = kc_ppn_dbus_connect();
    if (socket_fd < 0 || kc_ppn_dbus_authenticate(socket_fd) != KC_PPN_OK) {
        goto cleanup;
    }

    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    if (setsockopt(
            socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)
        ) != 0 ||
        kc_ppn_dbus_hello(socket_fd) != KC_PPN_OK ||
        kc_ppn_socket_write(
            socket_fd, message.data, message.size
        ) != KC_PPN_OK) {
        goto cleanup;
    }

    for (index = 0; index < 4; index++) {
        int type = kc_ppn_dbus_read_message(socket_fd);
        if (type == 2) {
            result = KC_PPN_OK;
            break;
        }
        if (type < 0 || type == 3) {
            break;
        }
    }

cleanup:
    if (socket_fd >= 0) {
        close(socket_fd);
    }
    free(body.data);
    free(message.data);
    return result;
}
#endif

#if defined(_WIN32)

typedef struct {
    wchar_t title[64];
    wchar_t message[256];
    HANDLE ready;
    LONG success;
} kc_ppn_windows_notification_t;

/**
 * Convert UTF-8 text into a fixed UTF-16 buffer.
 * @param source UTF-8 source string.
 * @param target UTF-16 destination buffer.
 * @param capacity Destination capacity.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_windows_text(
    const char *source,
    wchar_t *target,
    int capacity
) {
    int count;
    wchar_t *wide;

    count = MultiByteToWideChar(CP_UTF8, 0, source, -1, NULL, 0);
    if (count <= 0) {
        return KC_PPN_ERROR;
    }
    wide = (wchar_t *)calloc((size_t)count, sizeof(wchar_t));
    if (wide == NULL) {
        return KC_PPN_ERROR;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, source, -1, wide, count) <= 0) {
        free(wide);
        return KC_PPN_ERROR;
    }
    lstrcpynW(target, wide, capacity);
    free(wide);
    return KC_PPN_OK;
}

/**
 * Own the temporary shell icon used for a Windows notification.
 * @param userdata Notification state.
 * @return Thread exit code.
 */
static DWORD WINAPI kc_ppn_windows_thread(void *userdata) {
    kc_ppn_windows_notification_t *notification = userdata;
    NOTIFYICONDATAW icon;
    HWND window;

    window = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        NULL,
        GetModuleHandleW(NULL),
        NULL
    );
    if (window == NULL) {
        SetEvent(notification->ready);
        Sleep(2500);
        CloseHandle(notification->ready);
        free(notification);
        return 0;
    }

    memset(&icon, 0, sizeof(icon));
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_TIP;
    icon.hIcon = LoadIconW(NULL, IDI_INFORMATION);
    lstrcpynW(icon.szTip, notification->title, ARRAYSIZE(icon.szTip));

    if (Shell_NotifyIconW(NIM_ADD, &icon)) {
        icon.uFlags = NIF_INFO;
        icon.dwInfoFlags = NIIF_INFO;
        lstrcpynW(
            icon.szInfoTitle,
            notification->title,
            ARRAYSIZE(icon.szInfoTitle)
        );
        lstrcpynW(icon.szInfo, notification->message, ARRAYSIZE(icon.szInfo));
        if (Shell_NotifyIconW(NIM_MODIFY, &icon)) {
            InterlockedExchange(&notification->success, 1);
        }
    }

    SetEvent(notification->ready);
    Sleep(10000);
    icon.uFlags = 0;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    DestroyWindow(window);
    CloseHandle(notification->ready);
    free(notification);
    return 0;
}

/**
 * Display one Windows shell notification.
 * @param source Notification values.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_show_windows(const kc_ppn_notification_t *source) {
    kc_ppn_windows_notification_t *notification;
    HANDLE thread;
    DWORD wait_result;
    LONG success;

    notification = calloc(1, sizeof(*notification));
    if (notification == NULL) {
        return KC_PPN_ERROR;
    }
    notification->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (notification->ready == NULL ||
        kc_ppn_windows_text(
            source->title,
            notification->title,
            ARRAYSIZE(notification->title)
        ) != KC_PPN_OK ||
        kc_ppn_windows_text(
            source->message,
            notification->message,
            ARRAYSIZE(notification->message)
        ) != KC_PPN_OK) {
        if (notification->ready != NULL) {
            CloseHandle(notification->ready);
        }
        free(notification);
        return KC_PPN_ERROR;
    }

    thread = CreateThread(
        NULL,
        0,
        kc_ppn_windows_thread,
        notification,
        0,
        NULL
    );
    if (thread == NULL) {
        CloseHandle(notification->ready);
        free(notification);
        return KC_PPN_ERROR;
    }

    wait_result = WaitForSingleObject(notification->ready, 2000);
    success = InterlockedCompareExchange(&notification->success, 0, 0);
    CloseHandle(thread);

    if (wait_result != WAIT_OBJECT_0) {
        return KC_PPN_ERROR;
    }
    return success != 0 ? KC_PPN_OK : KC_PPN_ERROR;
}
#endif

#if defined(__APPLE__)
/**
 * Display one notification through macOS Notification Center.
 * @param notification Notification values.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
static int kc_ppn_show_macos(const kc_ppn_notification_t *notification) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    @autoreleasepool {
        NSString *title;
        NSString *message;
        NSUserNotification *native;

        title = [NSString stringWithUTF8String:notification->title];
        message = [NSString stringWithUTF8String:notification->message];
        if (title == nil || message == nil) {
            return KC_PPN_ERROR;
        }

        native = [[NSUserNotification alloc] init];
        if (native == nil) {
            return KC_PPN_ERROR;
        }
        native.title = title;
        native.informativeText = message;
        [[NSUserNotificationCenter defaultUserNotificationCenter]
            deliverNotification:native];
    }
#pragma clang diagnostic pop
    return KC_PPN_OK;
}
#endif

/**
 * Display one native operating-system notification.
 * @param notification Notification values.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
int kc_ppn_show(const kc_ppn_notification_t *notification) {
    if (notification == NULL || notification->title == NULL ||
        notification->message == NULL || notification->title[0] == '\0' ||
        notification->message[0] == '\0') {
        return KC_PPN_ERROR;
    }

#if defined(_WIN32)
    return kc_ppn_show_windows(notification);
#elif defined(__APPLE__)
    return kc_ppn_show_macos(notification);
#else
    return kc_ppn_show_linux(notification);
#endif
}

/**
 * Return the generated build version.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_ppn_version(void) {
    return (uint64_t)KC_PPN_BUILD_VERSION;
}
