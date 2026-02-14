/* msg.c - logging
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

#include "aept/aept.h"
#include "aept/msg.h"

static int use_color;

static const char *level_name[] = {
    [AEPT_ERROR]   = "error",
    [AEPT_WARNING] = "warning",
    [AEPT_INFO]    = "info",
    [AEPT_DEBUG]   = "debug"
};

static const char *level_color[] = {
    [AEPT_ERROR]   = "\033[31m",
    [AEPT_WARNING] = "\033[33m",
    [AEPT_INFO]    = "",
    [AEPT_DEBUG]   = ""
};

void aept_log_init(void)
{
    use_color = isatty(STDERR_FILENO);
}

void aept_log(int level, const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    if (cfg && level > cfg->verbosity)
        return;

    if (use_color) {
        fprintf(stderr, "\033[1maept\033[0m: %s\033[1m%s\033[0m: ",
                level_color[level], level_name[level]);
    } else {
        fprintf(stderr, "aept: %s: ", level_name[level]);
    }

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    if (level == AEPT_DEBUG && file)
        fprintf(stderr, " (%s:%d)", file, line);

    fputc('\n', stderr);
}
