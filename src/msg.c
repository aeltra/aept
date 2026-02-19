/* msg.c - logging
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
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
    [AEPT_INFO]    = "\033[32m",
    [AEPT_DEBUG]   = "\033[34m"
};

void aept_log_init(void)
{
    use_color = isatty(STDOUT_FILENO) && isatty(STDERR_FILENO);
}

void aept_log(int level, const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    FILE *out;

    if (level < 0 || level > AEPT_DEBUG)
        return;

    if (cfg && level > cfg->verbosity)
        return;

    out = (level <= AEPT_WARNING) ? stderr : stdout;

    if (use_color) {
        fprintf(out, "\033[1maept\033[0m: %s\033[1m%s\033[0m: ",
                level_color[level], level_name[level]);
    } else {
        fprintf(out, "aept: %s: ", level_name[level]);
    }

    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    if (level == AEPT_DEBUG && file)
        fprintf(out, " (%s:%d)", file, line);

    fputc('\n', out);
}

void print_heading(const char *fmt, ...)
{
    va_list ap;

    if (use_color)
        printf("\033[1m");

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    if (use_color)
        printf("\033[0m");

    putchar('\n');
}

static int terminal_width(void)
{
    struct winsize ws;

    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0
            && ws.ws_col > 0)
        return ws.ws_col;

    return 80;
}

#define INDENT 2

void print_names(const char **list, int count)
{
    int i, col, width, len;

    width = terminal_width();
    col = INDENT;
    printf("%*s", INDENT, "");

    for (i = 0; i < count; i++) {
        len = strlen(list[i]);

        if (i > 0 && col + 1 + len > width) {
            printf("\n%*s", INDENT, "");
            col = INDENT;
        } else if (i > 0) {
            putchar(' ');
            col++;
        }

        printf("%s", list[i]);
        col += len;
    }

    putchar('\n');
}
