/**
 * libppn.h - Native popup notifications.
 * Summary: Public API for displaying operating-system notifications.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_PPN_H
#define KC_PPN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_PPN_OK 0
#define KC_PPN_ERROR -1

typedef struct {
    const char *title;
    const char *message;
} kc_ppn_notification_t;

/**
 * Display one native operating-system notification.
 * @param notification Notification title and message.
 * @return KC_PPN_OK on success, or KC_PPN_ERROR on failure.
 */
int kc_ppn_show(const kc_ppn_notification_t *notification);

/**
 * Return the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_ppn_version(void);

#ifdef __cplusplus
}
#endif

#endif
