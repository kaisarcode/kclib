/**
 * sha256.h - SHA-256 file verification interface.
 * Summary: Declares portable file digest calculation.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU General Public License v3.0
 */

#ifndef KC_SHA256_H
#define KC_SHA256_H

int kc_sha256_file(const char *filename, char digest[65]);

#endif
