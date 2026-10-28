/**
 * packages.h - KaisarCode project package operation interface.
 * Summary: Declares project installation and maintenance operations.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU General Public License v3.0
 */

#ifndef KC_PACKAGES_H
#define KC_PACKAGES_H

int kc_packages_info(const char *name, const char *platform, const char *arch);
int kc_packages_install(const char *name, const char *platform, const char *arch);
int kc_packages_update(const char *name, const char *platform, const char *arch);
int kc_packages_remove(const char *name, const char *platform, const char *arch);
int kc_packages_each(const char *platform, const char *arch, int update);

#endif
