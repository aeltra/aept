/* msg.c - logging
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/msg.h"

static int use_color;

/* Callback globals (set by API entry points, NULL by default) */
aept_log_fn     aept_log_cb;
void           *aept_log_cb_data;
aept_confirm_fn aept_confirm_cb;
void           *aept_confirm_cb_data;
const struct aept_transaction *aept_confirm_txn;

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

    if (aept_cfg && level > aept_cfg->verbosity)
        return;

    if (aept_log_cb) {
        char buf[1024];
        int n;

        va_start(ap, fmt);
        n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);

        if (level == AEPT_DEBUG && file && n >= 0
                && (size_t)n < sizeof(buf) - 1) {
            snprintf(buf + n, sizeof(buf) - n, " (%s:%d)", file, line);
        }

        aept_log_cb(level, buf, aept_log_cb_data);
        return;
    }

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

int aept_confirm_continue(void)
{
    struct termios old_tio, new_tio;
    int ch;

    if (aept_cfg->non_interactive)
        return 1;

    if (aept_confirm_cb)
        return aept_confirm_cb(aept_confirm_txn, aept_confirm_cb_data);

    printf("Do you want to continue? [Y/n] ");
    fflush(stdout);

    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    new_tio.c_cc[VMIN] = 1;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    putchar('\n');

    if (ch == 'n' || ch == 'N')
        return 0;

    return 1;
}

void aept_print_heading(const char *fmt, ...)
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

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

void aept_print_names(const char **list, int count)
{
    int i, col, width, len;

    qsort(list, count, sizeof(char *), cmp_str);
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
