/* clean.c - cache cleanup
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/clean.h"
#include "aept/msg.h"
#include "aept/util.h"

int aept_op_clean(void)
{
    DIR *d;
    struct dirent *ent;
    int errors = 0;

    d = opendir(aept_cfg->cache_dir);
    if (!d) {
        if (errno == ENOENT)
            return 0;
        aept_log_error("cannot open cache directory '%s': %s",
                  aept_cfg->cache_dir, strerror(errno));
        return -1;
    }

    while ((ent = readdir(d)) != NULL) {
        char *path = NULL;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        aept_asprintf(&path, "%s/%s", aept_cfg->cache_dir, ent->d_name);

        if (unlink(path) < 0) {
            aept_log_error("cannot remove '%s': %s", path, strerror(errno));
            errors++;
        }

        free(path);
    }

    closedir(d);

    return errors ? -1 : 0;
}
