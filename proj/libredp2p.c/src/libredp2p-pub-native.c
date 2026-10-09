/**
 * libredp2p-pub.c - REDP2P.
 * Summary: Publisher persistence, registration and service sessions.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libredp2p-peer.h"

#include "monocypher.h"
#include "parson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

#define REDP2P_IPV4_LOOPBACK             0x7f000001u

/* Native publisher implementation is intentionally kept in this module. */
#include "libredp2p-pub-native-body.inc"
