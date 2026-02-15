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
#include "aept/autoremove.h"
#include "aept/clean.h"
#include "aept/config.h"
#include "aept/install.h"
#include "aept/msg.h"
#include "aept/query.h"
#include "aept/remove.h"
#include "aept/update.h"
#include "aept/util.h"

#define DEFAULT_CONF "/etc/aept/aept.conf"

static const char *conf_file = DEFAULT_CONF;
static const char *offline_root;
static int conf_explicit;

/* ── shared config helpers ─────────────────────────────────────────── */

static const char *resolve_conf(void)
{
    char *path;

    if (!offline_root || conf_explicit)
        return conf_file;

    xasprintf(&path, "%s%s", offline_root, DEFAULT_CONF);
    return path;
}

static int load_config(void)
{
    const char *cf = resolve_conf();

    if (access(cf, R_OK) < 0 && !conf_explicit && errno == ENOENT) {
        log_warning("config file '%s' not found, using defaults", cf);
        config_set_defaults();
    } else if (config_load(cf) < 0) {
        if (cf != conf_file)
            free((char *)cf);
        return -1;
    }

    if (cf != conf_file)
        free((char *)cf);

    if (offline_root) {
        free(cfg->offline_root);
        cfg->offline_root = xstrdup(offline_root);
    }

    config_apply_offline_root();
    return 0;
}

static int setup_config(void)
{
    if (load_config() < 0)
        return -1;

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
        "Usage: aept [-c <file>] [-o <dir>] [-v] <command> [options] [args...]\n"
        "\n"
        "Global options:\n"
        "  -c, --conf <file>         Configuration file (default: %s)\n"
        "  -o, --offline-root <dir>  Use <dir> as the package root\n"
        "  -v, --verbose             Increase verbosity\n"
        "  -h, --help                Show this help\n"
        "\n"
        "Commands:\n"
        "  update              Fetch package lists from repositories\n"
        "  install <pkgs...>   Install packages\n"
        "  remove <pkgs...>    Remove packages\n"
        "  autoremove          Remove unneeded auto-installed packages\n"
        "  upgrade             Upgrade all installed packages\n"
        "  list [pattern]      List packages\n"
        "  show <pkg>          Show package information\n"
        "  clean               Remove cached package files\n"
        "  files <pkg>         List files of an installed package\n"
        "  owns <path>         Find which package owns a file\n"
        "  print-architecture  Show configured architectures\n"
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
        "  -h, --help  Show this help\n"
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
        "  -f, --force-depends   Ignore dependency errors\n"
        "  -d, --download-only   Only download, do not install\n"
        "  -n, --noaction        Dry run, show what would be done\n"
        "  -h, --help            Show this help\n"
        "\n"
        "  --allow-downgrade     Allow package downgrades\n"
        "  --reinstall           Reinstall already installed packages\n"
        "  --no-cache            Download, install, and delete each package\n"
        "  --force-confnew       Always install new conffiles without asking\n"
        "  --force-confold       Always keep old conffiles without asking\n"
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
        "  -f, --force-depends   Ignore dependency errors\n"
        "  -n, --noaction        Dry run, show what would be done\n"
        "  -h, --help            Show this help\n"
        "\n"
        "  --purge               Also remove modified conffiles\n"
    );
}

static void usage_autoremove(FILE *out)
{
    fprintf(out,
        "Usage: aept autoremove [options]\n"
        "\n"
        "Remove auto-installed packages that are no longer needed.\n"
        "\n"
        "Options:\n"
        "  -f, --force-depends   Ignore dependency errors\n"
        "  -n, --noaction        Dry run, show what would be done\n"
        "  -h, --help            Show this help\n"
        "\n"
        "  --purge               Also remove modified conffiles\n"
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
        "  -f, --force-depends   Ignore dependency errors\n"
        "  -d, --download-only   Only download, do not install\n"
        "  -n, --noaction        Dry run, show what would be done\n"
        "  -h, --help            Show this help\n"
        "\n"
        "  --allow-downgrade     Allow package downgrades\n"
        "  --no-cache            Download, install, and delete each package\n"
        "  --force-confnew       Always install new conffiles without asking\n"
        "  --force-confold       Always keep old conffiles without asking\n"
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
        "  -h, --help  Show this help\n"
    );
}

static void usage_list(FILE *out)
{
    fprintf(out,
        "Usage: aept list [options] [pattern]\n"
        "\n"
        "List packages. With no arguments, list all available packages.\n"
        "An optional glob pattern filters by package name.\n"
        "\n"
        "Options:\n"
        "  -h, --help    Show this help\n"
        "\n"
        "  --installed   Only show installed packages\n"
        "  --upgradable  Only show upgradable packages\n"
    );
}

static void usage_owns(FILE *out)
{
    fprintf(out,
        "Usage: aept owns [options] <path>\n"
        "\n"
        "Find which installed package owns a file.\n"
        "\n"
        "Options:\n"
        "  -h, --help  Show this help\n"
    );
}

static void usage_files(FILE *out)
{
    fprintf(out,
        "Usage: aept files [options] <package>\n"
        "\n"
        "List files belonging to an installed package.\n"
        "\n"
        "Options:\n"
        "  -h, --help  Show this help\n"
    );
}

static void usage_show(FILE *out)
{
    fprintf(out,
        "Usage: aept show [options] <package>\n"
        "\n"
        "Show package information.\n"
        "\n"
        "Options:\n"
        "  -h, --help  Show this help\n"
    );
}

static void usage_print_architecture(FILE *out)
{
    fprintf(out,
        "Usage: aept print-architecture [options]\n"
        "\n"
        "Show configured architectures.\n"
        "\n"
        "Options:\n"
        "  -h, --help  Show this help\n"
    );
}

/* ── per-command option tables ─────────────────────────────────────── */

static struct option update_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

static struct option install_options[] = {
    {"force-depends",   no_argument, NULL, 'f'},
    {"download-only",   no_argument, NULL, 'd'},
    {"noaction",        no_argument, NULL, 'n'},
    {"help",            no_argument, NULL, 'h'},
    {"allow-downgrade", no_argument, NULL, 0x100},
    {"reinstall",       no_argument, NULL, 0x101},
    {"no-cache",        no_argument, NULL, 0x102},
    {"force-confnew",   no_argument, NULL, 0x103},
    {"force-confold",   no_argument, NULL, 0x104},
    {NULL, 0, NULL, 0}
};

static struct option autoremove_options[] = {
    {"force-depends", no_argument, NULL, 'f'},
    {"noaction",      no_argument, NULL, 'n'},
    {"help",          no_argument, NULL, 'h'},
    {"purge",         no_argument, NULL, 0x100},
    {NULL, 0, NULL, 0}
};

static struct option remove_options[] = {
    {"force-depends", no_argument, NULL, 'f'},
    {"noaction",      no_argument, NULL, 'n'},
    {"help",          no_argument, NULL, 'h'},
    {"purge",         no_argument, NULL, 0x100},
    {NULL, 0, NULL, 0}
};

/* upgrade reuses install_options */

static struct option clean_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

static struct option list_options[] = {
    {"help",       no_argument, NULL, 'h'},
    {"installed",  no_argument, NULL, 0x100},
    {"upgradable", no_argument, NULL, 0x101},
    {NULL, 0, NULL, 0}
};

static struct option show_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

static struct option files_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

static struct option owns_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

static struct option print_arch_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

/* ── command handlers ──────────────────────────────────────────────── */

static int cmd_update(int argc, char *argv[])
{
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", update_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_update(stdout); return 0;
        default:  usage_update(stderr); return 1;
        }
    }

    if (setup_config() < 0)
        return 1;

    r = aept_update();
    teardown_config();
    return r;
}

static int cmd_install(int argc, char *argv[])
{
    int force_depends = 0, download_only = 0, noaction = 0;
    int allow_downgrade = 0, reinstall = 0, no_cache = 0;
    int force_confnew = 0, force_confold = 0;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "fdnh", install_options, NULL)) != -1) {
        switch (opt) {
        case 'f': force_depends = 1; break;
        case 'd': download_only = 1; break;
        case 'n': noaction = 1; break;
        case 0x100: allow_downgrade = 1; break;
        case 0x101: reinstall = 1; break;
        case 0x102: no_cache = 1; break;
        case 0x103: force_confnew = 1; break;
        case 0x104: force_confold = 1; break;
        case 'h': usage_install(stdout); return 0;
        default:  usage_install(stderr); return 1;
        }
    }

    if (optind >= argc) {
        log_error("install requires at least one package name");
        return 1;
    }

    if (setup_config() < 0)
        return 1;

    cfg->force_depends = force_depends;
    cfg->download_only = download_only;
    cfg->noaction = noaction;
    cfg->allow_downgrade = allow_downgrade;
    cfg->reinstall = reinstall;
    cfg->no_cache = no_cache;
    cfg->force_confnew = force_confnew;
    cfg->force_confold = force_confold;

    r = aept_install((const char **)&argv[optind], argc - optind);
    teardown_config();
    return r;
}

static int cmd_autoremove(int argc, char *argv[])
{
    int force_depends = 0, noaction = 0, purge = 0;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "fnh", autoremove_options, NULL)) != -1) {
        switch (opt) {
        case 'f': force_depends = 1; break;
        case 'n': noaction = 1; break;
        case 0x100: purge = 1; break;
        case 'h': usage_autoremove(stdout); return 0;
        default:  usage_autoremove(stderr); return 1;
        }
    }

    if (setup_config() < 0)
        return 1;

    cfg->force_depends = force_depends;
    cfg->noaction = noaction;
    cfg->purge = purge;

    r = aept_autoremove();
    teardown_config();
    return r;
}

static int cmd_remove(int argc, char *argv[])
{
    int force_depends = 0, noaction = 0, purge = 0;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "fnh", remove_options, NULL)) != -1) {
        switch (opt) {
        case 'f': force_depends = 1; break;
        case 'n': noaction = 1; break;
        case 0x100: purge = 1; break;
        case 'h': usage_remove(stdout); return 0;
        default:  usage_remove(stderr); return 1;
        }
    }

    if (optind >= argc) {
        log_error("remove requires at least one package name");
        return 1;
    }

    if (setup_config() < 0)
        return 1;

    cfg->force_depends = force_depends;
    cfg->noaction = noaction;
    cfg->purge = purge;

    r = aept_remove((const char **)&argv[optind], argc - optind);
    teardown_config();
    return r;
}

static int cmd_upgrade(int argc, char *argv[])
{
    int force_depends = 0, download_only = 0, noaction = 0;
    int allow_downgrade = 0, no_cache = 0;
    int force_confnew = 0, force_confold = 0;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "fdnh", install_options, NULL)) != -1) {
        switch (opt) {
        case 'f': force_depends = 1; break;
        case 'd': download_only = 1; break;
        case 'n': noaction = 1; break;
        case 0x100: allow_downgrade = 1; break;
        case 0x101: break; /* --reinstall: ignored for upgrade */
        case 0x102: no_cache = 1; break;
        case 0x103: force_confnew = 1; break;
        case 0x104: force_confold = 1; break;
        case 'h': usage_upgrade(stdout); return 0;
        default:  usage_upgrade(stderr); return 1;
        }
    }

    if (setup_config() < 0)
        return 1;

    cfg->force_depends = force_depends;
    cfg->download_only = download_only;
    cfg->noaction = noaction;
    cfg->allow_downgrade = allow_downgrade;
    cfg->no_cache = no_cache;
    cfg->force_confnew = force_confnew;
    cfg->force_confold = force_confold;

    r = aept_install(NULL, 0);
    teardown_config();
    return r;
}

static int cmd_clean(int argc, char *argv[])
{
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", clean_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_clean(stdout); return 0;
        default:  usage_clean(stderr); return 1;
        }
    }

    if (setup_config() < 0)
        return 1;

    r = aept_clean();
    teardown_config();
    return r;
}

static int cmd_list(int argc, char *argv[])
{
    const char *pattern = NULL;
    int filter_installed = 0, filter_upgradable = 0;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", list_options, NULL)) != -1) {
        switch (opt) {
        case 0x100: filter_installed = 1; break;
        case 0x101: filter_upgradable = 1; break;
        case 'h': usage_list(stdout); return 0;
        default:  usage_list(stderr); return 1;
        }
    }

    if (optind < argc)
        pattern = argv[optind];

    if (load_config() < 0)
        return 1;

    r = aept_list(pattern, filter_installed, filter_upgradable);
    config_free();
    return r;
}

static int cmd_show(int argc, char *argv[])
{
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", show_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_show(stdout); return 0;
        default:  usage_show(stderr); return 1;
        }
    }

    if (optind >= argc) {
        log_error("show requires a package name");
        return 1;
    }

    if (load_config() < 0)
        return 1;

    r = aept_show(argv[optind]);
    config_free();
    return r;
}

static int cmd_files(int argc, char *argv[])
{
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", files_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_files(stdout); return 0;
        default:  usage_files(stderr); return 1;
        }
    }

    if (optind >= argc) {
        log_error("files requires a package name");
        return 1;
    }

    if (load_config() < 0)
        return 1;

    r = aept_files(argv[optind]);
    config_free();
    return r;
}

static int cmd_owns(int argc, char *argv[])
{
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", owns_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_owns(stdout); return 0;
        default:  usage_owns(stderr); return 1;
        }
    }

    if (optind >= argc) {
        log_error("owns requires a file path");
        return 1;
    }

    if (load_config() < 0)
        return 1;

    r = aept_owns(argv[optind]);
    config_free();
    return r;
}

static int cmd_print_architecture(int argc, char *argv[])
{
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", print_arch_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_print_architecture(stdout); return 0;
        default:  usage_print_architecture(stderr); return 1;
        }
    }

    if (load_config() < 0)
        return 1;

    r = aept_print_architecture();
    config_free();
    return r;
}

/* ── main ──────────────────────────────────────────────────────────── */

static struct option global_options[] = {
    {"conf",         required_argument, NULL, 'c'},
    {"offline-root", required_argument, NULL, 'o'},
    {"verbose",      no_argument,       NULL, 'v'},
    {"help",         no_argument,       NULL, 'h'},
    {NULL, 0, NULL, 0}
};

int main(int argc, char *argv[])
{
    const char *command;
    int opt;

    aept_log_init();

    while ((opt = getopt_long(argc, argv, "+c:o:vh", global_options, NULL)) != -1) {
        switch (opt) {
        case 'c': conf_file = optarg; conf_explicit = 1; break;
        case 'o': offline_root = optarg; break;
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
    if (strcmp(command, "autoremove") == 0)
        return cmd_autoremove(argc - optind, argv + optind);
    if (strcmp(command, "upgrade") == 0)
        return cmd_upgrade(argc - optind, argv + optind);
    if (strcmp(command, "clean") == 0)
        return cmd_clean(argc - optind, argv + optind);
    if (strcmp(command, "list") == 0)
        return cmd_list(argc - optind, argv + optind);
    if (strcmp(command, "show") == 0)
        return cmd_show(argc - optind, argv + optind);
    if (strcmp(command, "files") == 0)
        return cmd_files(argc - optind, argv + optind);
    if (strcmp(command, "owns") == 0)
        return cmd_owns(argc - optind, argv + optind);
    if (strcmp(command, "print-architecture") == 0)
        return cmd_print_architecture(argc - optind, argv + optind);

    log_error("unknown command '%s'", command);
    usage_main(stderr);
    return 1;
}
