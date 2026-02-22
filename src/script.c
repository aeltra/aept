/* script.c - maintainer script execution
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/aept.h"
#include "aept/msg.h"
#include "aept/script.h"
#include "aept/util.h"

static const char *strip_offline_root(const char *path)
{
    if (!aept_cfg->offline_root)
        return path;

    size_t len = strlen(aept_cfg->offline_root);
    if (strncmp(path, aept_cfg->offline_root, len) == 0)
        return path + len;

    return path;
}

int aept_run_script(const char *script_dir, const char *pkg_name,
               const char *script, const char *action,
               const char *version)
{
    char *path = NULL;
    int r;

    if (pkg_name)
        aept_asprintf(&path, "%s/%s.%s", script_dir, pkg_name, script);
    else
        aept_asprintf(&path, "%s/%s", script_dir, script);

    if (!aept_file_exists(path)) {
        free(path);
        return 0;
    }

    aept_log_debug("running %s for %s %s %s", script,
             pkg_name ? pkg_name : "(none)",
             action ? action : "",
             version ? version : "");

    const char *run_path = path;
    if (aept_cfg->offline_root)
        run_path = strip_offline_root(path);

    if (action && version) {
        const char *argv[] = {"/bin/sh", run_path, action, version, NULL};
        r = aept_system_offline_root(argv);
    } else if (action) {
        const char *argv[] = {"/bin/sh", run_path, action, NULL};
        r = aept_system_offline_root(argv);
    } else {
        const char *argv[] = {"/bin/sh", run_path, NULL};
        r = aept_system_offline_root(argv);
    }

    free(path);

    if (r != 0) {
        aept_log_error("%s script for %s failed with exit code %d",
                  script, pkg_name ? pkg_name : "(none)", r);
        return r;
    }

    return 0;
}
