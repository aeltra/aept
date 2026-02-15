/* download.h - wget download wrapper
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DOWNLOAD_H_7BF97F
#define DOWNLOAD_H_7BF97F

/* Download url to dest file. Returns 0 on success, -1 on error. */
int aept_download(const char *url, const char *dest, const char *name);

#endif
