/* main.c - CLI entry point
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/aept.h"
#include "aept/clean.h"
#include "aept/config.h"
#include "aept/install.h"
#include "aept/msg.h"
#include "aept/remove.h"
#include "aept/update.h"
#include "aept/util.h"

#define DEFAULT_CONF "/etc/aept/aept.conf"

static void usage(void)
{
    fprintf(stderr,
        "Usage: aept <command> [options] [packages...]\n"
        "\n"
        "Commands:\n"
        "  update                Fetch package lists from repositories\n"
        "  install <pkgs...>     Install packages\n"
        "  remove <pkgs...>      Remove packages\n"
        "  upgrade               Upgrade all installed packages\n"
        "  clean                 Remove cached package files\n"
        "  list                  List available packages\n"
        "  info <pkg>            Show package information\n"
        "\n"
        "Options:\n"
        "  -c, --conf <file>     Configuration file (default: %s)\n"
        "  -o, --offline-root <dir>  Install to an offline root directory\n"
        "  -f, --force-depends   Ignore dependency errors\n"
        "  -d, --download-only   Only download, do not install\n"
        "  -n, --noaction        Dry run, show what would be done\n"
        "  -v, --verbose         Increase verbosity\n"
        "  -h, --help            Show this help\n",
        DEFAULT_CONF
    );
}

static struct option long_options[] = {
    {"conf",          required_argument, NULL, 'c'},
    {"offline-root",  required_argument, NULL, 'o'},
    {"force-depends", no_argument,       NULL, 'f'},
    {"download-only", no_argument,       NULL, 'd'},
    {"noaction",      no_argument,       NULL, 'n'},
    {"verbose",       no_argument,       NULL, 'v'},
    {"help",          no_argument,       NULL, 'h'},
    {NULL, 0, NULL, 0}
};

int main(int argc, char *argv[])
{
    const char *conf_file = DEFAULT_CONF;
    const char *offline_root = NULL;
    int opt;
    int r;

    aept_log_init();

    while ((opt = getopt_long(argc, argv, "+c:o:fdnvh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            conf_file = optarg;
            break;
        case 'o':
            offline_root = optarg;
            break;
        case 'f':
            cfg->force_depends = 1;
            break;
        case 'd':
            cfg->download_only = 1;
            break;
        case 'n':
            cfg->noaction = 1;
            break;
        case 'v':
            cfg->verbosity++;
            break;
        case 'h':
            usage();
            return 0;
        default:
            usage();
            return 1;
        }
    }

    if (optind >= argc) {
        usage();
        return 1;
    }

    const char *command = argv[optind];
    optind++;

    r = config_load(conf_file);
    if (r < 0)
        return 1;

    /* Command-line offline_root overrides config file */
    if (offline_root) {
        free(cfg->offline_root);
        cfg->offline_root = xstrdup(offline_root);
    }

    if (strcmp(command, "update") == 0) {
        r = aept_update();
    } else if (strcmp(command, "install") == 0) {
        if (optind >= argc) {
            log_error("install requires at least one package name");
            r = 1;
        } else {
            r = aept_install((const char **)&argv[optind], argc - optind);
        }
    } else if (strcmp(command, "remove") == 0) {
        if (optind >= argc) {
            log_error("remove requires at least one package name");
            r = 1;
        } else {
            r = aept_remove((const char **)&argv[optind], argc - optind);
        }
    } else if (strcmp(command, "upgrade") == 0) {
        r = aept_install(NULL, 0);
    } else if (strcmp(command, "clean") == 0) {
        r = aept_clean();
    } else {
        log_error("unknown command '%s'", command);
        usage();
        r = 1;
    }

    config_free();

    return r ? 1 : 0;
}
