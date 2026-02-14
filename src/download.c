/* download.c - wget download wrapper
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <unistd.h>

#include <string.h>

#include "aept/aept.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/util.h"

int aept_download(const char *url, const char *dest)
{
    const char *argv[] = {"wget", "-q", "-O", dest, url, NULL};

    if (strncmp(url, "https://", 8) != 0)
        log_warning("downloading over insecure connection: %s", url);

    log_info("downloading %s", url);

    unlink(dest);

    int r = xsystem(argv);
    if (r != 0) {
        log_error("failed to download '%s'", url);
        unlink(dest);
        return -1;
    }

    return 0;
}
