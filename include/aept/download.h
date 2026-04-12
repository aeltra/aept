/* download.h - HTTP download and package retrieval
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef DOWNLOAD_H_7BF97F
#define DOWNLOAD_H_7BF97F

#include <solv/pool.h>

struct aept_ctx;

/* Download url to dest file. Returns 0 on success, -1 on error. */
int aept_download(struct aept_ctx *ctx, const char *url, const char *dest,
                  const char *name);

/* Download a package identified by solvable p.
 * Uses cache if available and checksum matches.
 * On success, *dest_out is set to the local path (caller frees).
 * Returns 0 on success, -1 on error. */
int aept_download_package(struct aept_ctx *ctx, Id p, Pool *pool,
                          char **dest_out);

#endif
