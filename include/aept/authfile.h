/* authfile.h - source credentials from an auth.conf file
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef AUTHFILE_H_7BF97F
#define AUTHFILE_H_7BF97F

struct aept_config;

/*
 * Give the configured sources the credentials of the first matching
 * entry in an auth.conf file.  Exactly one file is read, the first of:
 *
 *   1. explicit_path, if not NULL -- a host path, never prefixed;
 *   2. inside a build-box target, when not running as root,
 *      <home>/RealHome/.aeltra/auth.conf of the effective user;
 *   3. /etc/aept/auth.conf.
 *
 * 2. and 3. are resolved under the offline root and skipped when
 * absent.  Only https sources are matched, and only those whose url
 * carries no credentials of its own.  Credentials a previous call took
 * from a file are dropped first, so the call can be repeated.
 *
 * cfg->auth_file_used is set to the file read, for the log.
 *
 * Returns 0, or -1 when explicit_path cannot be read.  An implicit file
 * that cannot be read is warned about and skipped.
 */
int aept_auth_apply(struct aept_config *cfg, const char *explicit_path);

#endif
