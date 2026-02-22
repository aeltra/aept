/* script.h - maintainer script execution
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef SCRIPT_H_7BF97F
#define SCRIPT_H_7BF97F

/* Run a maintainer script.
 *
 * script_dir: directory containing the script
 * pkg_name: package name (NULL if script has no pkg prefix)
 * script: "preinst", "postinst", "prerm", "postrm"
 * action: action argument (e.g. "install", "configure", "upgrade")
 * version: version argument (NULL if not applicable)
 *
 * If pkg_name is non-NULL, looks for {script_dir}/{pkg_name}.{script}.
 * If pkg_name is NULL, looks for {script_dir}/{script}.
 *
 * Returns 0 on success, script exit code on failure,
 * 0 if script does not exist. */
int aept_run_script(const char *script_dir, const char *pkg_name,
               const char *script, const char *action,
               const char *version);

#endif
