/* cmd_transact.c - commands that change what is installed
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * install, remove, upgrade, autoremove -- the commands that
 * run a transaction against the root.
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/aept.h"
#include "aept/cli.h"
#include "aept/msg.h"
#include "aept/util.h"

static void usage_install(FILE *out)
{
    fprintf(out, "Usage: aept install [options] <packages|paths...>\n"
                 "\n"
                 "Install packages and their dependencies.\n"
                 "Arguments starting with ./ or / are treated as local .aeltra files.\n"
                 "\n"
                 "Options:\n"
                 "  -f, --force-depends   Ignore dependency errors\n"
                 "  -d, --download-only   Only download, do not install\n"
                 "  -n, --noaction        Dry run, show what would be done\n"
                 "  -h, --help            Show this help\n"
                 "\n"
                 "  --non-interactive     Do not prompt; implies --force-confold\n"
                 "  --allow-downgrade     Allow package downgrades\n"
                 "  --reinstall           Reinstall already installed packages\n"
                 "  --no-cache            Download, install, and delete each package\n"
                 "  --force-confnew       Always install new conffiles without asking\n"
                 "  --force-confold       Always keep old conffiles without asking\n"
                 "  --keep-going          Continue past per-package errors\n");
}

static void usage_remove(FILE *out)
{
    fprintf(out, "Usage: aept remove [options] <packages...>\n"
                 "\n"
                 "Remove installed packages.\n"
                 "\n"
                 "Options:\n"
                 "  -f, --force-depends   Ignore dependency errors\n"
                 "  -n, --noaction        Dry run, show what would be done\n"
                 "  -h, --help            Show this help\n"
                 "\n"
                 "  --non-interactive     Do not prompt\n"
                 "  --purge               Also remove modified conffiles\n"
                 "  --keep-going          Continue past per-package errors\n");
}

static void usage_autoremove(FILE *out)
{
    fprintf(out, "Usage: aept autoremove [options]\n"
                 "\n"
                 "Remove auto-installed packages that are no longer needed.\n"
                 "\n"
                 "Options:\n"
                 "  -f, --force-depends   Ignore dependency errors\n"
                 "  -n, --noaction        Dry run, show what would be done\n"
                 "  -h, --help            Show this help\n"
                 "\n"
                 "  --non-interactive     Do not prompt\n"
                 "  --purge               Also remove modified conffiles\n"
                 "  --keep-going          Continue past per-package errors\n");
}

static void usage_upgrade(FILE *out)
{
    fprintf(out, "Usage: aept upgrade [options]\n"
                 "\n"
                 "Upgrade all installed packages.\n"
                 "\n"
                 "Options:\n"
                 "  -f, --force-depends   Ignore dependency errors\n"
                 "  -d, --download-only   Only download, do not install\n"
                 "  -n, --noaction        Dry run, show what would be done\n"
                 "  -h, --help            Show this help\n"
                 "\n"
                 "  --non-interactive     Do not prompt; implies --force-confold\n"
                 "  --allow-downgrade     Allow package downgrades\n"
                 "  --no-cache            Download, install, and delete each package\n"
                 "  --force-confnew       Always install new conffiles without asking\n"
                 "  --force-confold       Always keep old conffiles without asking\n"
                 "  --keep-going          Continue past per-package errors\n");
}

static struct option install_options[] = {
    {"force-depends",   no_argument, NULL, 'f'  },
    {"download-only",   no_argument, NULL, 'd'  },
    {"noaction",        no_argument, NULL, 'n'  },
    {"help",            no_argument, NULL, 'h'  },
    {"allow-downgrade", no_argument, NULL, 0x100},
    {"reinstall",       no_argument, NULL, 0x101},
    {"no-cache",        no_argument, NULL, 0x102},
    {"force-confnew",   no_argument, NULL, 0x103},
    {"force-confold",   no_argument, NULL, 0x104},
    {"non-interactive", no_argument, NULL, 0x105},
    {"keep-going",      no_argument, NULL, 0x106},
    {NULL,              0,           NULL, 0    }
};

static struct option autoremove_options[] = {
    {"force-depends",   no_argument, NULL, 'f'  },
    {"noaction",        no_argument, NULL, 'n'  },
    {"help",            no_argument, NULL, 'h'  },
    {"purge",           no_argument, NULL, 0x100},
    {"non-interactive", no_argument, NULL, 0x101},
    {"keep-going",      no_argument, NULL, 0x102},
    {NULL,              0,           NULL, 0    }
};

static struct option remove_options[] = {
    {"force-depends",   no_argument, NULL, 'f'  },
    {"noaction",        no_argument, NULL, 'n'  },
    {"help",            no_argument, NULL, 'h'  },
    {"purge",           no_argument, NULL, 0x100},
    {"non-interactive", no_argument, NULL, 0x101},
    {"keep-going",      no_argument, NULL, 0x102},
    {NULL,              0,           NULL, 0    }
};

/* upgrade reuses install_options */

int cmd_install(int argc, char *argv[])
{
    int force_depends = 0, download_only = 0, noaction = 0;
    int allow_downgrade = 0, reinstall = 0, no_cache = 0;
    int force_confnew = 0, force_confold = 0, non_interactive = 0;
    int keep_going = 0;
    int opt, r, rc;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("fdnh"), install_options, NULL)) != -1) {
        switch (opt) {
        case 'f':
            force_depends = 1;
            break;
        case 'd':
            download_only = 1;
            break;
        case 'n':
            noaction = 1;
            break;
        case 0x100:
            allow_downgrade = 1;
            break;
        case 0x101:
            reinstall = 1;
            break;
        case 0x102:
            no_cache = 1;
            break;
        case 0x103:
            force_confnew = 1;
            break;
        case 0x104:
            force_confold = 1;
            break;
        case 0x105:
            non_interactive = 1;
            break;
        case 0x106:
            keep_going = 1;
            break;
        case 'h':
            usage_install(stdout);
            return 0;
        default:
            usage_install(stderr);
            return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("install requires at least one package name or .aeltra path");
        return 1;
    }

    /* Partition arguments into package names and local .aeltra paths */
    int nargs = argc - optind;
    const char **pkg_names = aept_malloc(nargs * sizeof(char *));
    const char **local_paths = aept_malloc(nargs * sizeof(char *));
    int n_names = 0, n_locals = 0;

    for (int j = optind; j < argc; j++) {
        if (argv[j][0] == '/' || (argv[j][0] == '.' && argv[j][1] == '/')) {
            if (access(argv[j], R_OK) < 0) {
                aept_log_error("cannot access '%s': %s", argv[j], strerror(errno));
                free(pkg_names);
                free(local_paths);
                return 1;
            }
            local_paths[n_locals++] = argv[j];
        } else {
            pkg_names[n_names++] = argv[j];
        }
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx) {
        free(pkg_names);
        free(local_paths);
        return 1;
    }

    non_interactive = non_interactive || !isatty(STDIN_FILENO);

    aept_set_flag(ctx, AEPT_FLAG_FORCE_DEPENDS, force_depends);
    aept_set_flag(ctx, AEPT_FLAG_DOWNLOAD_ONLY, download_only);
    aept_set_flag(ctx, AEPT_FLAG_NOACTION, noaction);
    aept_set_flag(ctx, AEPT_FLAG_NON_INTERACTIVE, non_interactive);
    aept_set_flag(ctx, AEPT_FLAG_ALLOW_DOWNGRADE, allow_downgrade);
    aept_set_flag(ctx, AEPT_FLAG_REINSTALL, reinstall);
    aept_set_flag(ctx, AEPT_FLAG_NO_CACHE, no_cache);
    aept_set_flag(ctx, AEPT_FLAG_FORCE_CONFNEW, force_confnew);
    aept_set_flag(ctx, AEPT_FLAG_FORCE_CONFOLD, force_confold);
    aept_set_flag(ctx, AEPT_FLAG_KEEP_GOING, keep_going);
    if (non_interactive && !force_confnew)
        aept_set_flag(ctx, AEPT_FLAG_FORCE_CONFOLD, 1);

    r = aept_install(ctx, n_names > 0 ? pkg_names : NULL, n_names,
                     n_locals > 0 ? local_paths : NULL, n_locals);
    free(pkg_names);
    free(local_paths);
    rc = transaction_exit(ctx, r);
    cli_cleanup(ctx);
    return rc;
}

int cmd_autoremove(int argc, char *argv[])
{
    int force_depends = 0, noaction = 0, purge = 0, non_interactive = 0;
    int keep_going = 0;
    int opt, r, rc;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("fnh"), autoremove_options, NULL)) != -1) {
        switch (opt) {
        case 'f':
            force_depends = 1;
            break;
        case 'n':
            noaction = 1;
            break;
        case 0x100:
            purge = 1;
            break;
        case 0x101:
            non_interactive = 1;
            break;
        case 0x102:
            keep_going = 1;
            break;
        case 'h':
            usage_autoremove(stdout);
            return 0;
        default:
            usage_autoremove(stderr);
            return 1;
        }
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    non_interactive = non_interactive || !isatty(STDIN_FILENO);

    aept_set_flag(ctx, AEPT_FLAG_FORCE_DEPENDS, force_depends);
    aept_set_flag(ctx, AEPT_FLAG_NOACTION, noaction);
    aept_set_flag(ctx, AEPT_FLAG_NON_INTERACTIVE, non_interactive);
    aept_set_flag(ctx, AEPT_FLAG_PURGE, purge);
    aept_set_flag(ctx, AEPT_FLAG_KEEP_GOING, keep_going);

    r = aept_autoremove(ctx);
    rc = transaction_exit(ctx, r);
    cli_cleanup(ctx);
    return rc;
}

int cmd_remove(int argc, char *argv[])
{
    int force_depends = 0, noaction = 0, purge = 0, non_interactive = 0;
    int keep_going = 0;
    int opt, r, rc;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("fnh"), remove_options, NULL)) != -1) {
        switch (opt) {
        case 'f':
            force_depends = 1;
            break;
        case 'n':
            noaction = 1;
            break;
        case 0x100:
            purge = 1;
            break;
        case 0x101:
            non_interactive = 1;
            break;
        case 0x102:
            keep_going = 1;
            break;
        case 'h':
            usage_remove(stdout);
            return 0;
        default:
            usage_remove(stderr);
            return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("remove requires at least one package name");
        return 1;
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    non_interactive = non_interactive || !isatty(STDIN_FILENO);

    aept_set_flag(ctx, AEPT_FLAG_FORCE_DEPENDS, force_depends);
    aept_set_flag(ctx, AEPT_FLAG_NOACTION, noaction);
    aept_set_flag(ctx, AEPT_FLAG_NON_INTERACTIVE, non_interactive);
    aept_set_flag(ctx, AEPT_FLAG_PURGE, purge);
    aept_set_flag(ctx, AEPT_FLAG_KEEP_GOING, keep_going);

    r = aept_remove(ctx, (const char **)&argv[optind], argc - optind);
    rc = transaction_exit(ctx, r);
    cli_cleanup(ctx);
    return rc;
}

int cmd_upgrade(int argc, char *argv[])
{
    int force_depends = 0, download_only = 0, noaction = 0;
    int allow_downgrade = 0, no_cache = 0;
    int force_confnew = 0, force_confold = 0, non_interactive = 0;
    int keep_going = 0;
    int opt, r, rc;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("fdnh"), install_options, NULL)) != -1) {
        switch (opt) {
        case 'f':
            force_depends = 1;
            break;
        case 'd':
            download_only = 1;
            break;
        case 'n':
            noaction = 1;
            break;
        case 0x100:
            allow_downgrade = 1;
            break;
        case 0x101:
            break; /* --reinstall: ignored for upgrade */
        case 0x102:
            no_cache = 1;
            break;
        case 0x103:
            force_confnew = 1;
            break;
        case 0x104:
            force_confold = 1;
            break;
        case 0x105:
            non_interactive = 1;
            break;
        case 0x106:
            keep_going = 1;
            break;
        case 'h':
            usage_upgrade(stdout);
            return 0;
        default:
            usage_upgrade(stderr);
            return 1;
        }
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    non_interactive = non_interactive || !isatty(STDIN_FILENO);

    aept_set_flag(ctx, AEPT_FLAG_FORCE_DEPENDS, force_depends);
    aept_set_flag(ctx, AEPT_FLAG_DOWNLOAD_ONLY, download_only);
    aept_set_flag(ctx, AEPT_FLAG_NOACTION, noaction);
    aept_set_flag(ctx, AEPT_FLAG_NON_INTERACTIVE, non_interactive);
    aept_set_flag(ctx, AEPT_FLAG_ALLOW_DOWNGRADE, allow_downgrade);
    aept_set_flag(ctx, AEPT_FLAG_NO_CACHE, no_cache);
    aept_set_flag(ctx, AEPT_FLAG_FORCE_CONFNEW, force_confnew);
    aept_set_flag(ctx, AEPT_FLAG_FORCE_CONFOLD, force_confold);
    aept_set_flag(ctx, AEPT_FLAG_KEEP_GOING, keep_going);
    if (non_interactive && !force_confnew)
        aept_set_flag(ctx, AEPT_FLAG_FORCE_CONFOLD, 1);

    r = aept_upgrade(ctx);
    rc = transaction_exit(ctx, r);
    cli_cleanup(ctx);
    return rc;
}
