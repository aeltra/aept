/* main.c - CLI entry point
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/aept.h"
#include "aept/clean.h"
#include "aept/config.h"
#include "aept/install.h"
#include "aept/msg.h"
#include "aept/remove.h"
#include "aept/update.h"
#include "aept/util.h"

#define DEFAULT_CONF "/etc/aept/aept.conf"

static const char *conf_file = DEFAULT_CONF;
static int conf_explicit;

/* ── shared config helpers ─────────────────────────────────────────── */

static int setup_config(const char *offline_root)
{
    if (access(conf_file, R_OK) < 0 && !conf_explicit && errno == ENOENT) {
        log_warning("config file '%s' not found, using defaults", conf_file);
        config_set_defaults();
    } else if (config_load(conf_file) < 0) {
        return -1;
    }

    if (offline_root) {
        free(cfg->offline_root);
        cfg->offline_root = xstrdup(offline_root);
    }

    config_apply_offline_root();

    if (config_validate() < 0) {
        config_free();
        return -1;
    }

    if (config_lock() < 0) {
        config_free();
        return -1;
    }

    return 0;
}

static void teardown_config(void)
{
    config_unlock();
    config_free();
}

/* ── usage functions ───────────────────────────────────────────────── */

static void usage_main(FILE *out)
{
    fprintf(out,
        "Usage: aept [-c <file>] [-v] <command> [options] [args...]\n"
        "\n"
        "Global options:\n"
        "  -c, --conf <file>   Configuration file (default: %s)\n"
        "  -v, --verbose       Increase verbosity\n"
        "  -h, --help          Show this help\n"
        "\n"
        "Commands:\n"
        "  update              Fetch package lists from repositories\n"
        "  install <pkgs...>   Install packages\n"
        "  remove <pkgs...>    Remove packages\n"
        "  upgrade             Upgrade all installed packages\n"
        "  clean               Remove cached package files\n"
        "\n"
        "Run 'aept <command> --help' for command-specific options.\n",
        DEFAULT_CONF
    );
}

static void usage_update(FILE *out)
{
    fprintf(out,
        "Usage: aept update [options]\n"
        "\n"
        "Fetch package lists from repositories.\n"
        "\n"
        "Options:\n"
        "  -o, --offline-root <dir>  Install to an offline root directory\n"
        "  -h, --help                Show this help\n"
    );
}

static void usage_install(FILE *out)
{
    fprintf(out,
        "Usage: aept install [options] <packages...>\n"
        "\n"
        "Install packages and their dependencies.\n"
        "\n"
        "Options:\n"
        "  -o, --offline-root <dir>  Install to an offline root directory\n"
        "  -f, --force-depends       Ignore dependency errors\n"
        "  -d, --download-only       Only download, do not install\n"
        "  -n, --noaction            Dry run, show what would be done\n"
        "  -h, --help                Show this help\n"
        "\n"
        "  --allow-downgrade         Allow package downgrades\n"
    );
}

static void usage_remove(FILE *out)
{
    fprintf(out,
        "Usage: aept remove [options] <packages...>\n"
        "\n"
        "Remove installed packages.\n"
        "\n"
        "Options:\n"
        "  -o, --offline-root <dir>  Install to an offline root directory\n"
        "  -f, --force-depends       Ignore dependency errors\n"
        "  -n, --noaction            Dry run, show what would be done\n"
        "  -h, --help                Show this help\n"
    );
}

static void usage_upgrade(FILE *out)
{
    fprintf(out,
        "Usage: aept upgrade [options]\n"
        "\n"
        "Upgrade all installed packages.\n"
        "\n"
        "Options:\n"
        "  -o, --offline-root <dir>  Install to an offline root directory\n"
        "  -f, --force-depends       Ignore dependency errors\n"
        "  -d, --download-only       Only download, do not install\n"
        "  -n, --noaction            Dry run, show what would be done\n"
        "  -h, --help                Show this help\n"
        "\n"
        "  --allow-downgrade         Allow package downgrades\n"
    );
}

static void usage_clean(FILE *out)
{
    fprintf(out,
        "Usage: aept clean [options]\n"
        "\n"
        "Remove cached package files.\n"
        "\n"
        "Options:\n"
        "  -o, --offline-root <dir>  Install to an offline root directory\n"
        "  -h, --help                Show this help\n"
    );
}

/* ── per-command option tables ─────────────────────────────────────── */

static struct option update_options[] = {
    {"offline-root", required_argument, NULL, 'o'},
    {"help",         no_argument,       NULL, 'h'},
    {NULL, 0, NULL, 0}
};

static struct option install_options[] = {
    {"offline-root",    required_argument, NULL, 'o'},
    {"force-depends",   no_argument,       NULL, 'f'},
    {"download-only",   no_argument,       NULL, 'd'},
    {"noaction",        no_argument,       NULL, 'n'},
    {"help",            no_argument,       NULL, 'h'},
    {"allow-downgrade", no_argument,       NULL, 0x100},
    {NULL, 0, NULL, 0}
};

static struct option remove_options[] = {
    {"offline-root",  required_argument, NULL, 'o'},
    {"force-depends", no_argument,       NULL, 'f'},
    {"noaction",      no_argument,       NULL, 'n'},
    {"help",          no_argument,       NULL, 'h'},
    {NULL, 0, NULL, 0}
};

/* upgrade reuses install_options */

static struct option clean_options[] = {
    {"offline-root", required_argument, NULL, 'o'},
    {"help",         no_argument,       NULL, 'h'},
    {NULL, 0, NULL, 0}
};

/* ── command handlers ──────────────────────────────────────────────── */

static int cmd_update(int argc, char *argv[])
{
    const char *offline_root = NULL;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:h", update_options, NULL)) != -1) {
        switch (opt) {
        case 'o': offline_root = optarg; break;
        case 'h': usage_update(stdout); return 0;
        default:  usage_update(stderr); return 1;
        }
    }

    if (setup_config(offline_root) < 0)
        return 1;

    r = aept_update();
    teardown_config();
    return r;
}

static int cmd_install(int argc, char *argv[])
{
    const char *offline_root = NULL;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:fdnh", install_options, NULL)) != -1) {
        switch (opt) {
        case 'o': offline_root = optarg; break;
        case 'f': cfg->force_depends = 1; break;
        case 'd': cfg->download_only = 1; break;
        case 'n': cfg->noaction = 1; break;
        case 'v': cfg->verbosity++; break;
        case 0x100: cfg->allow_downgrade = 1; break;
        case 'h': usage_install(stdout); return 0;
        default:  usage_install(stderr); return 1;
        }
    }

    if (optind >= argc) {
        log_error("install requires at least one package name");
        return 1;
    }

    if (setup_config(offline_root) < 0)
        return 1;

    r = aept_install((const char **)&argv[optind], argc - optind);
    teardown_config();
    return r;
}

static int cmd_remove(int argc, char *argv[])
{
    const char *offline_root = NULL;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:fnh", remove_options, NULL)) != -1) {
        switch (opt) {
        case 'o': offline_root = optarg; break;
        case 'f': cfg->force_depends = 1; break;
        case 'n': cfg->noaction = 1; break;
        case 'h': usage_remove(stdout); return 0;
        default:  usage_remove(stderr); return 1;
        }
    }

    if (optind >= argc) {
        log_error("remove requires at least one package name");
        return 1;
    }

    if (setup_config(offline_root) < 0)
        return 1;

    r = aept_remove((const char **)&argv[optind], argc - optind);
    teardown_config();
    return r;
}

static int cmd_upgrade(int argc, char *argv[])
{
    const char *offline_root = NULL;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:fdnh", install_options, NULL)) != -1) {
        switch (opt) {
        case 'o': offline_root = optarg; break;
        case 'f': cfg->force_depends = 1; break;
        case 'd': cfg->download_only = 1; break;
        case 'n': cfg->noaction = 1; break;
        case 'v': cfg->verbosity++; break;
        case 0x100: cfg->allow_downgrade = 1; break;
        case 'h': usage_upgrade(stdout); return 0;
        default:  usage_upgrade(stderr); return 1;
        }
    }

    if (setup_config(offline_root) < 0)
        return 1;

    r = aept_install(NULL, 0);
    teardown_config();
    return r;
}

static int cmd_clean(int argc, char *argv[])
{
    const char *offline_root = NULL;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:h", clean_options, NULL)) != -1) {
        switch (opt) {
        case 'o': offline_root = optarg; break;
        case 'h': usage_clean(stdout); return 0;
        default:  usage_clean(stderr); return 1;
        }
    }

    if (setup_config(offline_root) < 0)
        return 1;

    r = aept_clean();
    teardown_config();
    return r;
}

/* ── main ──────────────────────────────────────────────────────────── */

static struct option global_options[] = {
    {"conf",    required_argument, NULL, 'c'},
    {"verbose", no_argument,       NULL, 'v'},
    {"help",    no_argument,       NULL, 'h'},
    {NULL, 0, NULL, 0}
};

int main(int argc, char *argv[])
{
    const char *command;
    int opt;

    aept_log_init();

    while ((opt = getopt_long(argc, argv, "+c:vh", global_options, NULL)) != -1) {
        switch (opt) {
        case 'c': conf_file = optarg; conf_explicit = 1; break;
        case 'v': cfg->verbosity++; break;
        case 'h': usage_main(stdout); return 0;
        default:  usage_main(stderr); return 1;
        }
    }

    if (optind >= argc) {
        usage_main(stderr);
        return 1;
    }

    command = argv[optind];

    if (strcmp(command, "update") == 0)
        return cmd_update(argc - optind, argv + optind);
    if (strcmp(command, "install") == 0)
        return cmd_install(argc - optind, argv + optind);
    if (strcmp(command, "remove") == 0)
        return cmd_remove(argc - optind, argv + optind);
    if (strcmp(command, "upgrade") == 0)
        return cmd_upgrade(argc - optind, argv + optind);
    if (strcmp(command, "clean") == 0)
        return cmd_clean(argc - optind, argv + optind);

    log_error("unknown command '%s'", command);
    usage_main(stderr);
    return 1;
}
