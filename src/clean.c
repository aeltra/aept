/* clean.c - cache cleanup
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/clean.h"
#include "aept/config.h"
#include "aept/msg.h"
#include "aept/util.h"

/* Whether this root is a build-box target.  /etc/target is the file
 * build-box writes into every one of them, and inside such a target a
 * kept cache is the arrangement working as designed rather than
 * anything the user needs telling about. */
static int is_build_box_target(const struct aept_config *cfg)
{
    char *marker = aept_config_root_path(cfg, "/etc/target");
    int found = aept_file_exists(marker);

    free(marker);
    return found;
}

int aept_op_clean(struct aept_ctx *ctx)
{
    DIR *d;
    struct dirent *ent;
    int errors = 0;

    if (!ctx->config.clean_cache) {
        if (!is_build_box_target(&ctx->config))
            aept_log_info("cache cleaning is disabled, keeping '%s'", ctx->config.cache_dir);
        return 0;
    }

    d = opendir(ctx->config.cache_dir);
    if (!d) {
        if (errno == ENOENT)
            return 0;
        aept_log_error("cannot open cache directory '%s': %s", ctx->config.cache_dir,
                       strerror(errno));
        return -1;
    }

    while ((ent = readdir(d)) != NULL) {
        char *path = NULL;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        aept_asprintf(&path, "%s/%s", ctx->config.cache_dir, ent->d_name);

        if (unlink(path) < 0) {
            aept_log_error("cannot remove '%s': %s", path, strerror(errno));
            errors++;
        }

        free(path);
    }

    closedir(d);

    return errors ? -1 : 0;
}
