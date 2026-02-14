/* download.c - wget download wrapper
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <unistd.h>

#include "aept/aept.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/util.h"

int aept_download(const char *url, const char *dest)
{
    const char *argv[] = {"wget", "-q", "-O", dest, url, NULL};

    aept_msg(AEPT_INFO, "downloading %s\n", url);

    unlink(dest);

    int r = xsystem(argv);
    if (r != 0) {
        aept_msg(AEPT_ERROR, "failed to download '%s'\n", url);
        unlink(dest);
        return -1;
    }

    return 0;
}
