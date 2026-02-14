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
    if (!cfg->offline_root)
        return path;

    size_t len = strlen(cfg->offline_root);
    if (strncmp(path, cfg->offline_root, len) == 0)
        return path + len;

    return path;
}

int run_script(const char *script_dir, const char *pkg_name,
               const char *script, const char *args)
{
    char *path = NULL;
    char *cmd = NULL;
    int r;

    if (pkg_name)
        xasprintf(&path, "%s/%s.%s", script_dir, pkg_name, script);
    else
        xasprintf(&path, "%s/%s", script_dir, script);

    if (!file_exists(path)) {
        free(path);
        return 0;
    }

    log_info("running %s %s", script, args ? args : "");

    const char *run_path = path;
    if (cfg->offline_root)
        run_path = strip_offline_root(path);

    if (args)
        xasprintf(&cmd, "%s %s", run_path, args);
    else
        cmd = xstrdup(run_path);

    const char *argv[] = {"/bin/sh", "-c", cmd, NULL};
    r = xsystem_offline_root(argv);

    free(path);
    free(cmd);

    if (r != 0) {
        log_error("%s script failed with exit code %d", script, r);
        return r;
    }

    return 0;
}
