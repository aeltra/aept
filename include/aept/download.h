/* download.h - HTTP download
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef DOWNLOAD_H_7BF97F
#define DOWNLOAD_H_7BF97F

struct aept_ctx;

/* Download url to dest file. Returns 0 on success, -1 on error. */
int aept_download(struct aept_ctx *ctx, const char *url, const char *dest,
                  const char *name);

#endif
