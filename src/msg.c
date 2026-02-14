/* msg.c - logging
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdarg.h>

#include "aept/aept.h"
#include "aept/msg.h"

static const char *level_prefix[] = {
    "ERROR",
    "NOTICE",
    "INFO",
    "DEBUG"
};

void aept_msg(int level, const char *fmt, ...)
{
    va_list ap;

    if (cfg && level > cfg->verbosity)
        return;

    fprintf(stderr, "aept: %s: ", level_prefix[level]);

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
