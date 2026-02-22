/* download.c - wget download wrapper
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/util.h"

int aept_download(const char *url, const char *dest, const char *name)
{
    const char *argv[] = {"wget", "-q", "-O", dest, url, NULL};

    aept_log_info("downloading %s", name);

    unlink(dest);

    int r = aept_system(argv);
    if (r != 0) {
        aept_log_error("failed to download '%s'", url);
        unlink(dest);
        return -1;
    }

    return 0;
}
