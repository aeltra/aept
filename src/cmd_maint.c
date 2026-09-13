/* cmd_maint.c - keeping the root serviceable
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * update, clean, triggers -- indexes, the download cache, and
 * trigger work left owing by an earlier transaction.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/aept.h"
#include "aept/cli.h"
#include "aept/msg.h"
#include "aept/util.h"

static void usage_update(FILE *out)
{
    fprintf(out, "Usage: aept update [options]\n"
                 "\n"
                 "Fetch package lists from repositories.\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help  Show this help\n");
}

static void usage_clean(FILE *out)
{
    fprintf(out, "Usage: aept clean [options]\n"
                 "\n"
                 "Remove cached package files.\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help  Show this help\n");
}

static struct option update_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL,   0,           NULL, 0  }
};

static struct option triggers_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL,   0,           NULL, 0  },
};

static struct option clean_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL,   0,           NULL, 0  }
};

int cmd_update(int argc, char *argv[])
{
    int opt, r;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), update_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_update(stdout);
            return 0;
        default:
            usage_update(stderr);
            return 1;
        }
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    r = aept_update(ctx);
    cli_cleanup(ctx);
    return r != 0 ? 1 : 0;
}

int cmd_clean(int argc, char *argv[])
{
    int opt, r;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), clean_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_clean(stdout);
            return 0;
        default:
            usage_clean(stderr);
            return 1;
        }
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    r = aept_clean(ctx);
    cli_cleanup(ctx);
    return r != 0 ? 1 : 0;
}

static void usage_triggers(FILE *out)
{
    fprintf(out, "Usage: aept triggers\n"
                 "\n"
                 "Retry trigger scripts whose earlier run failed.  Failures are\n"
                 "recorded per package and normally retried by the next\n"
                 "transaction; this runs them on their own.\n");
}

int cmd_triggers(int argc, char *argv[])
{
    int opt, r;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), triggers_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_triggers(stdout);
            return 0;
        default:
            usage_triggers(stderr);
            return 1;
        }
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    r = aept_triggers(ctx);
    cli_cleanup(ctx);
    return r != 0 ? 1 : 0;
}
