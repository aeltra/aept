/* cmd_state.c - commands that change a package's standing
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * mark, pin, unpin -- these rewrite aept's own records about a
 * package without touching the package itself.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/aept.h"
#include "aept/cli.h"
#include "aept/msg.h"
#include "aept/util.h"

static void usage_mark(FILE *out)
{
    fprintf(out, "Usage: aept mark manual [--all] <packages...>\n"
                 "       aept mark auto <packages...>\n"
                 "\n"
                 "Control auto-installed package marks.\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help  Show this help\n"
                 "\n"
                 "  --all       Mark all packages as manually installed\n");
}

static void usage_pin(FILE *out)
{
    fprintf(out, "Usage: aept pin <packages...>\n"
                 "       aept unpin <packages...>\n"
                 "\n"
                 "Pin packages to their currently installed version.\n"
                 "Use name=version to pin to a specific version.\n"
                 "Pinned packages are held back during upgrade.\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help  Show this help\n");
}

static struct option mark_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL,   0,           NULL, 0  }
};

static struct option mark_manual_options[] = {
    {"help", no_argument, NULL, 'h'  },
    {"all",  no_argument, NULL, 0x100},
    {NULL,   0,           NULL, 0    }
};

static struct option mark_auto_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL,   0,           NULL, 0  }
};

static struct option pin_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL,   0,           NULL, 0  }
};

static int cmd_mark_manual(int argc, char *argv[])
{
    int all = 0;
    int opt, r;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), mark_manual_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_mark(stdout);
            return 0;
        case 0x100:
            all = 1;
            break;
        default:
            usage_mark(stderr);
            return 1;
        }
    }

    if (!all && optind >= argc) {
        aept_log_error("mark manual requires package names or --all");
        return 1;
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    if (all) {
        r = aept_mark_manual_all(ctx);
    } else {
        r = aept_mark_manual(ctx, (const char **)&argv[optind], argc - optind);
    }

    cli_cleanup(ctx);
    return r != 0 ? 1 : 0;
}

static int cmd_mark_auto(int argc, char *argv[])
{
    int opt, r;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), mark_auto_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_mark(stdout);
            return 0;
        default:
            usage_mark(stderr);
            return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("mark auto requires package names");
        return 1;
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    r = aept_mark_auto(ctx, (const char **)&argv[optind], argc - optind);

    cli_cleanup(ctx);
    return r != 0 ? 1 : 0;
}

int cmd_mark(int argc, char *argv[])
{
    const char *action;
    int opt;

    /* Dispatching, so OPTS_DISPATCH: everything after the action word
     * belongs to cmd_mark_manual() or cmd_mark_auto(). */
    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_DISPATCH("h"), mark_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_mark(stdout);
            return 0;
        default:
            usage_mark(stderr);
            return 1;
        }
    }

    if (optind >= argc) {
        usage_mark(stderr);
        return 1;
    }

    action = argv[optind];

    if (strcmp(action, "manual") == 0)
        return cmd_mark_manual(argc - optind, argv + optind);
    if (strcmp(action, "auto") == 0)
        return cmd_mark_auto(argc - optind, argv + optind);

    aept_log_error("unknown mark action '%s'", action);
    usage_mark(stderr);
    return 1;
}

int cmd_pin(int argc, char *argv[])
{
    int opt, r;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), pin_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_pin(stdout);
            return 0;
        default:
            usage_pin(stderr);
            return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("pin requires at least one package name");
        usage_pin(stderr);
        return 1;
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    r = aept_pin(ctx, (const char **)&argv[optind], argc - optind);

    cli_cleanup(ctx);
    return r != 0 ? 1 : 0;
}

int cmd_unpin(int argc, char *argv[])
{
    int opt, r;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), pin_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_pin(stdout);
            return 0;
        default:
            usage_pin(stderr);
            return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("unpin requires at least one package name");
        usage_pin(stderr);
        return 1;
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    r = aept_unpin(ctx, (const char **)&argv[optind], argc - optind);

    cli_cleanup(ctx);
    return r != 0 ? 1 : 0;
}
