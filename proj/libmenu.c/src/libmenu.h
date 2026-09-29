/**
 * libmenu.h - Native application menu entries.
 * Summary: Public API for adding and deleting application menu entries.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_MENU_H
#define KC_MENU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_MENU_OK 0
#define KC_MENU_ERROR -1

typedef struct {
    const char *id;
    const char *name;
    const char *description;
    const char *command;
    const char *icon;
    const char *category;
} kc_menu_entry_t;

/**
 * Add one application menu entry.
 * Platform-specific menu registration is selected internally.
 * @param entry Application menu entry.
 * @return KC_MENU_OK on success, or KC_MENU_ERROR on failure.
 */
int kc_menu_add(const kc_menu_entry_t *entry);

/**
 * Delete one application menu entry.
 * Missing entries are a successful no-op.
 * @param id Application menu entry identifier.
 * @return KC_MENU_OK on success, or KC_MENU_ERROR on failure.
 */
int kc_menu_delete(const char *id);

/**
 * Return the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_menu_version(void);

#ifdef __cplusplus
}
#endif

#endif
