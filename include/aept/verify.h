/* verify.h - usign signature verification
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef VERIFY_H_7BF97F
#define VERIFY_H_7BF97F

struct aept_ctx;

/* Verify file signature using usign. Returns 0 on success, -1 on failure. */
int aept_verify_signature(struct aept_ctx *ctx, const char *file,
                          const char *sigfile);

#endif
