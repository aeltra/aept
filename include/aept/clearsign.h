/* clearsign.h - signify clearsigned document splitting
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef CLEARSIGN_H_7BF97F
#define CLEARSIGN_H_7BF97F

#include <stddef.h>

/* Message and signature regions of a clearsigned document.  Both point
 * into the caller's buffer and are only valid while it lives. */
typedef struct {
    const char *msg;
    size_t      msg_len;
    const char *sig;
    size_t      sig_len;
} aept_clearsign_t;

/* Locate the message and signature inside a clearsigned document.
 * Returns 0 on success, -1 if the document is malformed. */
int aept_clearsign_parse(const char *buf, size_t len, aept_clearsign_t *out);

/* Write the two regions out as separate files, so that the signature can
 * be checked with the usual detached-signature tooling.  Returns 0 on
 * success, -1 on error (leaving neither file behind). */
int aept_clearsign_write(const aept_clearsign_t *cs, const char *msg_path,
                         const char *sig_path);

#endif
