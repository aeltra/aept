/* cmd_query.c - commands that only read
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * list, show, files, owns, print-architecture -- nothing here
 * changes the root, so none of it needs the lock.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/aept.h"
#include "aept/cli.h"
#include "aept/msg.h"
#include "aept/util.h"

static void usage_list(FILE *out)
{
    fprintf(out, "Usage: aept list [options] [pattern]\n"
                 "\n"
                 "List packages. With no arguments, list all available packages.\n"
                 "An optional glob pattern filters by package name.\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help    Show this help\n"
                 "\n"
                 "  --installed   Only show installed packages\n"
                 "  --upgradable  Only show upgradable packages\n");
}

static void usage_owns(FILE *out)
{
    fprintf(out, "Usage: aept owns [options] <path>\n"
                 "\n"
                 "Find which installed package owns a file.\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help  Show this help\n");
}

static void usage_files(FILE *out)
{
    fprintf(out, "Usage: aept files [options] <package>\n"
                 "\n"
                 "List files belonging to an installed package.\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help  Show this help\n");
}

static void usage_show(FILE *out)
{
    fprintf(out, "Usage: aept show [options] <package>\n"
                 "\n"
                 "Show package information.  Without -a this is the candidate:\n"
                 "the best version any source offers, or the installed one when\n"
                 "no source offers it.\n"
                 "\n"
                 "Options:\n"
                 "  -a, --all   Show every version, newest first\n"
                 "  -h, --help  Show this help\n");
}

static void usage_print_architecture(FILE *out)
{
    fprintf(out, "Usage: aept print-architecture [options]\n"
                 "\n"
                 "Show configured architectures.\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help  Show this help\n");
}

static struct option list_options[] = {
    {"help",       no_argument, NULL, 'h'  },
    {"installed",  no_argument, NULL, 0x100},
    {"upgradable", no_argument, NULL, 0x101},
    {NULL,         0,           NULL, 0    }
};

static struct option show_options[] = {
    {"all",  no_argument, NULL, 'a'},
    {"help", no_argument, NULL, 'h'},
    {NULL,   0,           NULL, 0  }
};

static struct option files_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL,   0,           NULL, 0  }
};

static struct option owns_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL,   0,           NULL, 0  }
};

static struct option print_arch_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL,   0,           NULL, 0  }
};

/* ── command handlers ──────────────────────────────────────────────── */

int cmd_list(int argc, char *argv[])
{
    const char *pattern = NULL;
    int filter_installed = 0, filter_upgradable = 0;
    aept_pkg_list_t list;
    int opt, r, i;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), list_options, NULL)) != -1) {
        switch (opt) {
        case 0x100:
            filter_installed = 1;
            break;
        case 0x101:
            filter_upgradable = 1;
            break;
        case 'h':
            usage_list(stdout);
            return 0;
        default:
            usage_list(stderr);
            return 1;
        }
    }

    if (optind < argc)
        pattern = argv[optind];

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    r = aept_list(ctx, pattern, filter_installed, filter_upgradable, &list);
    if (r < 0) {
        cli_cleanup(ctx);
        return 1;
    }

    for (i = 0; i < list.count; i++) {
        aept_pkg_entry_t *e = &list.entries[i];

        printf("%s - %s", e->name, e->version);

        if (e->summary)
            printf(" - %s", e->summary);

        if (e->installed) {
            if (e->upgradable)
                printf(" [installed,upgradable]");
            else
                printf(" [installed]");
        }

        printf("\n");
    }

    aept_pkg_list_free(&list);
    cli_cleanup(ctx);
    return 0;
}

static void print_info(const aept_pkg_info_t *info)
{
    printf("Package: %s\n", info->name);
    printf("Version: %s\n", info->version);
    printf("Architecture: %s\n", info->architecture);

    if (info->section)
        printf("Section: %s\n", info->section);
    if (info->source)
        printf("Source: %s\n", info->source);
    if (info->maintainer)
        printf("Maintainer: %s\n", info->maintainer);

    /* The field is bytes; Debian's Installed-Size is kB, which is what
     * the control file said and what this label promises. */
    if (info->installed_size)
        printf("Installed-Size: %llu kB\n", info->installed_size / 1024);

    if (info->depends)
        printf("Depends: %s\n", info->depends);
    if (info->pre_depends)
        printf("Pre-Depends: %s\n", info->pre_depends);
    if (info->recommends)
        printf("Recommends: %s\n", info->recommends);
    if (info->suggests)
        printf("Suggests: %s\n", info->suggests);
    if (info->provides)
        printf("Provides: %s\n", info->provides);
    if (info->conflicts)
        printf("Conflicts: %s\n", info->conflicts);
    if (info->replaces)
        printf("Replaces: %s\n", info->replaces);

    if (info->homepage)
        printf("Homepage: %s\n", info->homepage);

    /* Only an index carries Size, so this is absent for something that
     * is merely installed -- as is Filename, beside it. */
    if (info->download_size)
        printf("Download-Size: %llu kB\n", info->download_size / 1024);

    if (info->filename)
        printf("Filename: %s\n", info->filename);

    if (info->summary) {
        printf("Description: %s\n", info->summary);
        /*
         * libsolv's DESCRIPTION holds the continuation lines -- but the
         * summary itself when a package has none, which is every stanza
         * in an archive whose descriptions are one line.  Printing both
         * then says the same sentence twice.
         */
        if (info->description && strcmp(info->description, info->summary) != 0) {
            const char *p = info->description;
            while (*p) {
                const char *eol = strchr(p, '\n');
                if (eol) {
                    printf(" %.*s\n", (int)(eol - p), p);
                    p = eol + 1;
                } else {
                    printf(" %s\n", p);
                    break;
                }
            }
        }
    }

    if (info->is_installed)
        printf("Status: install ok installed\n");
}

int cmd_show(int argc, char *argv[])
{
    aept_pkg_info_t info;
    aept_pkg_info_list_t list;
    int opt, r, all = 0, i;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("ah"), show_options, NULL)) != -1) {
        switch (opt) {
        case 'a':
            all = 1;
            break;
        case 'h':
            usage_show(stdout);
            return 0;
        default:
            usage_show(stderr);
            return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("show requires a package name");
        return 1;
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    if (!all) {
        r = aept_show(ctx, argv[optind], &info);
        if (r != 0) {
            if (r > 0)
                aept_log_error("package '%s' not found", argv[optind]);
            cli_cleanup(ctx);
            return 1;
        }
        print_info(&info);
        aept_pkg_info_free(&info);
        cli_cleanup(ctx);
        return 0;
    }

    r = aept_show_all(ctx, argv[optind], &list);
    if (r != 0) {
        if (r > 0)
            aept_log_error("package '%s' not found", argv[optind]);
        cli_cleanup(ctx);
        return 1;
    }

    /* One stanza per version, blank-line separated, as a Packages file
     * is -- so the output can be fed to something that reads one. */
    for (i = 0; i < list.count; i++) {
        if (i > 0)
            printf("\n");
        print_info(&list.entries[i]);
    }

    aept_pkg_info_list_free(&list);
    cli_cleanup(ctx);
    return 0;
}

int cmd_files(int argc, char *argv[])
{
    char **paths;
    int count;
    int opt, r, i;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), files_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_files(stdout);
            return 0;
        default:
            usage_files(stderr);
            return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("files requires a package name");
        return 1;
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    r = aept_files(ctx, argv[optind], &paths, &count);
    if (r != 0) {
        if (r > 0)
            aept_log_error("package '%s' is not installed", argv[optind]);
        cli_cleanup(ctx);
        return 1;
    }

    for (i = 0; i < count; i++) {
        printf("%s\n", paths[i]);
        free(paths[i]);
    }
    free(paths);

    cli_cleanup(ctx);
    return 0;
}

int cmd_owns(int argc, char *argv[])
{
    char **owners;
    int count;
    int opt, r, i;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), owns_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_owns(stdout);
            return 0;
        default:
            usage_owns(stderr);
            return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("owns requires a file path");
        return 1;
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    r = aept_owns(ctx, argv[optind], &owners, &count);
    if (r != 0) {
        cli_cleanup(ctx);
        return 1;
    }

    for (i = 0; i < count; i++) {
        printf("%s\n", owners[i]);
        free(owners[i]);
    }
    free(owners);

    cli_cleanup(ctx);
    return 0;
}

int cmd_print_architecture(int argc, char *argv[])
{
    char **archs;
    int count;
    int opt, r, i;

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_LEAF("h"), print_arch_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage_print_architecture(stdout);
            return 0;
        default:
            usage_print_architecture(stderr);
            return 1;
        }
    }

    aept_ctx_t *ctx = init_aept();
    if (!ctx)
        return 1;

    r = aept_architectures(ctx, &archs, &count);
    if (r < 0) {
        cli_cleanup(ctx);
        return 1;
    }

    for (i = 0; i < count; i++) {
        printf("%s\n", archs[i]);
        free(archs[i]);
    }
    free(archs);

    cli_cleanup(ctx);
    return 0;
}

/* ── main ──────────────────────────────────────────────────────────── */
