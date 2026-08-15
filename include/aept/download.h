/* download.h - HTTP download and package retrieval
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef DOWNLOAD_H_7BF97F
#define DOWNLOAD_H_7BF97F

#include <solv/pool.h>

struct aept_ctx;
struct libfetch_validators;

/* Download url to dest file. Returns 0 on success, -1 on error. */
int aept_download(struct aept_ctx *ctx, const char *url, const char *dest, const char *name);

/*
 * The same, revalidating a copy that is already held.
 *
 * "have" is what was recorded for this url when it was last fetched, or
 * NULL to fetch unconditionally; "got" receives what the server offered
 * for what it sent, and may be NULL.  When the server answers that the
 * copy held is still current, *unchanged is set and dest is not written
 * at all -- so the caller must have something to keep.  "unchanged" is
 * required whenever "have" is given.
 *
 * Returns 0 on success, -1 on error.
 */
int aept_download_cond(struct aept_ctx *ctx, const char *url, const char *dest, const char *name,
                       const struct libfetch_validators *have, struct libfetch_validators *got,
                       int *unchanged);

/* Download a package identified by solvable p.
 * Uses cache if available and checksum matches.
 * On success, *dest_out is set to the local path (caller frees).
 * Returns 0 on success, -1 on error. */
int aept_download_package(struct aept_ctx *ctx, Id p, Pool *pool, char **dest_out);

#endif
