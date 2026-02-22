/* main.c - CLI entry point
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/aept.h"
#include "aept/msg.h"
#include "aept/util.h"

#define DEFAULT_CONF "/etc/aept/aept.conf"

static const char *conf_file = DEFAULT_CONF;
static const char *offline_root;
static int conf_explicit;
static int verbose_count;

/* ── signal handling ──────────────────────────────────────────────── */

static void signal_handler(int sig)
{
    (void)sig;
    aept_cancel();
}

static void setup_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* ── shared helpers ───────────────────────────────────────────────── */

static const char *resolve_conf(void)
{
    char *path;

    if (!offline_root || conf_explicit)
        return conf_file;

    aept_asprintf(&path, "%s%s", offline_root, DEFAULT_CONF);
    return path;
}

static int init_aept(void)
{
    const char *cf;

    aept_init();

    if (offline_root)
        aept_set_offline_root(offline_root);

    cf = resolve_conf();

    if (conf_explicit && access(cf, R_OK) < 0) {
        aept_log_error("cannot access config file '%s': %s",
                  cf, strerror(errno));
        aept_cleanup();
        return -1;
    }

    if (!conf_explicit && access(cf, R_OK) < 0 && errno == ENOENT)
        aept_log_warning("config file '%s' not found, using defaults", cf);

    if (aept_load_config(cf) < 0) {
        if (cf != conf_file)
            free((char *)cf);
        aept_cleanup();
        return -1;
    }

    if (cf != conf_file)
        free((char *)cf);

    aept_set_verbosity(AEPT_LOG_INFO + verbose_count);

    if (!offline_root && access("/etc/aeltra_version", F_OK) != 0) {
        aept_log_error("not running on Aeltra OS; use -o to set an offline root");
        aept_cleanup();
        return -1;
    }

    return 0;
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
        "  mark <action>       Control auto-installed package marks\n"
        "  pin <pkgs...>       Pin packages to a specific version\n"
        "  unpin <pkgs...>     Remove version pins\n"
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
        "Usage: aept install [options] <packages|paths...>\n"
        "\n"
        "Install packages and their dependencies.\n"
        "Arguments starting with ./ or / are treated as local .ipk files.\n"
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
        "  --non-interactive     Do not prompt\n"
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
        "  --non-interactive     Do not prompt\n"
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
        "  --non-interactive     Do not prompt; implies --force-confold\n"
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

static void usage_mark(FILE *out)
{
    fprintf(out,
        "Usage: aept mark manual [--all] <packages...>\n"
        "       aept mark auto <packages...>\n"
        "\n"
        "Control auto-installed package marks.\n"
        "\n"
        "Options:\n"
        "  -h, --help  Show this help\n"
        "\n"
        "  --all       Mark all packages as manually installed\n"
    );
}

static void usage_pin(FILE *out)
{
    fprintf(out,
        "Usage: aept pin <packages...>\n"
        "       aept unpin <packages...>\n"
        "\n"
        "Pin packages to their currently installed version.\n"
        "Use name=version to pin to a specific version.\n"
        "Pinned packages are held back during upgrade.\n"
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
    {"non-interactive", no_argument, NULL, 0x105},
    {NULL, 0, NULL, 0}
};

static struct option autoremove_options[] = {
    {"force-depends",   no_argument, NULL, 'f'},
    {"noaction",        no_argument, NULL, 'n'},
    {"help",            no_argument, NULL, 'h'},
    {"purge",           no_argument, NULL, 0x100},
    {"non-interactive", no_argument, NULL, 0x101},
    {NULL, 0, NULL, 0}
};

static struct option remove_options[] = {
    {"force-depends",   no_argument, NULL, 'f'},
    {"noaction",        no_argument, NULL, 'n'},
    {"help",            no_argument, NULL, 'h'},
    {"purge",           no_argument, NULL, 0x100},
    {"non-interactive", no_argument, NULL, 0x101},
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

static struct option mark_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

static struct option mark_manual_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"all",  no_argument, NULL, 0x100},
    {NULL, 0, NULL, 0}
};

static struct option mark_auto_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

static struct option pin_options[] = {
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

    if (init_aept() < 0)
        return 1;

    r = aept_update();
    aept_cleanup();
    return r;
}

static int cmd_install(int argc, char *argv[])
{
    int force_depends = 0, download_only = 0, noaction = 0;
    int allow_downgrade = 0, reinstall = 0, no_cache = 0;
    int force_confnew = 0, force_confold = 0, non_interactive = 0;
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
        case 0x105: non_interactive = 1; break;
        case 'h': usage_install(stdout); return 0;
        default:  usage_install(stderr); return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("install requires at least one package name or .ipk path");
        return 1;
    }

    /* Partition arguments into package names and local .ipk paths */
    int nargs = argc - optind;
    const char **pkg_names = aept_malloc(nargs * sizeof(char *));
    const char **local_paths = aept_malloc(nargs * sizeof(char *));
    int n_names = 0, n_locals = 0;

    for (int j = optind; j < argc; j++) {
        if (argv[j][0] == '/' ||
                (argv[j][0] == '.' && argv[j][1] == '/')) {
            if (access(argv[j], R_OK) < 0) {
                aept_log_error("cannot access '%s': %s",
                          argv[j], strerror(errno));
                free(pkg_names);
                free(local_paths);
                return 1;
            }
            local_paths[n_locals++] = argv[j];
        } else {
            pkg_names[n_names++] = argv[j];
        }
    }

    if (init_aept() < 0) {
        free(pkg_names);
        free(local_paths);
        return 1;
    }

    non_interactive = non_interactive || !isatty(STDIN_FILENO);

    aept_set_flag(AEPT_FLAG_FORCE_DEPENDS, force_depends);
    aept_set_flag(AEPT_FLAG_DOWNLOAD_ONLY, download_only);
    aept_set_flag(AEPT_FLAG_NOACTION, noaction);
    aept_set_flag(AEPT_FLAG_NON_INTERACTIVE, non_interactive);
    aept_set_flag(AEPT_FLAG_ALLOW_DOWNGRADE, allow_downgrade);
    aept_set_flag(AEPT_FLAG_REINSTALL, reinstall);
    aept_set_flag(AEPT_FLAG_NO_CACHE, no_cache);
    aept_set_flag(AEPT_FLAG_FORCE_CONFNEW, force_confnew);
    aept_set_flag(AEPT_FLAG_FORCE_CONFOLD, force_confold);
    if (non_interactive && !force_confnew)
        aept_set_flag(AEPT_FLAG_FORCE_CONFOLD, 1);

    r = aept_install(n_names > 0 ? pkg_names : NULL, n_names,
                     n_locals > 0 ? local_paths : NULL, n_locals);
    free(pkg_names);
    free(local_paths);
    aept_cleanup();
    return r;
}

static int cmd_autoremove(int argc, char *argv[])
{
    int force_depends = 0, noaction = 0, purge = 0, non_interactive = 0;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "fnh", autoremove_options, NULL)) != -1) {
        switch (opt) {
        case 'f': force_depends = 1; break;
        case 'n': noaction = 1; break;
        case 0x100: purge = 1; break;
        case 0x101: non_interactive = 1; break;
        case 'h': usage_autoremove(stdout); return 0;
        default:  usage_autoremove(stderr); return 1;
        }
    }

    if (init_aept() < 0)
        return 1;

    non_interactive = non_interactive || !isatty(STDIN_FILENO);

    aept_set_flag(AEPT_FLAG_FORCE_DEPENDS, force_depends);
    aept_set_flag(AEPT_FLAG_NOACTION, noaction);
    aept_set_flag(AEPT_FLAG_NON_INTERACTIVE, non_interactive);
    aept_set_flag(AEPT_FLAG_PURGE, purge);

    r = aept_autoremove();
    aept_cleanup();
    return r;
}

static int cmd_remove(int argc, char *argv[])
{
    int force_depends = 0, noaction = 0, purge = 0, non_interactive = 0;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "fnh", remove_options, NULL)) != -1) {
        switch (opt) {
        case 'f': force_depends = 1; break;
        case 'n': noaction = 1; break;
        case 0x100: purge = 1; break;
        case 0x101: non_interactive = 1; break;
        case 'h': usage_remove(stdout); return 0;
        default:  usage_remove(stderr); return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("remove requires at least one package name");
        return 1;
    }

    if (init_aept() < 0)
        return 1;

    non_interactive = non_interactive || !isatty(STDIN_FILENO);

    aept_set_flag(AEPT_FLAG_FORCE_DEPENDS, force_depends);
    aept_set_flag(AEPT_FLAG_NOACTION, noaction);
    aept_set_flag(AEPT_FLAG_NON_INTERACTIVE, non_interactive);
    aept_set_flag(AEPT_FLAG_PURGE, purge);

    r = aept_remove((const char **)&argv[optind], argc - optind);
    aept_cleanup();
    return r;
}

static int cmd_upgrade(int argc, char *argv[])
{
    int force_depends = 0, download_only = 0, noaction = 0;
    int allow_downgrade = 0, no_cache = 0;
    int force_confnew = 0, force_confold = 0, non_interactive = 0;
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
        case 0x105: non_interactive = 1; break;
        case 'h': usage_upgrade(stdout); return 0;
        default:  usage_upgrade(stderr); return 1;
        }
    }

    if (init_aept() < 0)
        return 1;

    non_interactive = non_interactive || !isatty(STDIN_FILENO);

    aept_set_flag(AEPT_FLAG_FORCE_DEPENDS, force_depends);
    aept_set_flag(AEPT_FLAG_DOWNLOAD_ONLY, download_only);
    aept_set_flag(AEPT_FLAG_NOACTION, noaction);
    aept_set_flag(AEPT_FLAG_NON_INTERACTIVE, non_interactive);
    aept_set_flag(AEPT_FLAG_ALLOW_DOWNGRADE, allow_downgrade);
    aept_set_flag(AEPT_FLAG_NO_CACHE, no_cache);
    aept_set_flag(AEPT_FLAG_FORCE_CONFNEW, force_confnew);
    aept_set_flag(AEPT_FLAG_FORCE_CONFOLD, force_confold);
    if (non_interactive && !force_confnew)
        aept_set_flag(AEPT_FLAG_FORCE_CONFOLD, 1);

    r = aept_upgrade();
    aept_cleanup();
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

    if (init_aept() < 0)
        return 1;

    r = aept_clean();
    aept_cleanup();
    return r;
}

static int cmd_list(int argc, char *argv[])
{
    const char *pattern = NULL;
    int filter_installed = 0, filter_upgradable = 0;
    aept_pkg_list_t list;
    int opt, r, i;

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

    if (init_aept() < 0)
        return 1;

    r = aept_list(pattern, filter_installed, filter_upgradable, &list);
    if (r < 0) {
        aept_cleanup();
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
    aept_cleanup();
    return 0;
}

static int cmd_show(int argc, char *argv[])
{
    aept_pkg_info_t info;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", show_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_show(stdout); return 0;
        default:  usage_show(stderr); return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("show requires a package name");
        return 1;
    }

    if (init_aept() < 0)
        return 1;

    r = aept_show(argv[optind], &info);
    if (r != 0) {
        if (r > 0)
            aept_log_error("package '%s' not found", argv[optind]);
        aept_cleanup();
        return 1;
    }

    printf("Package: %s\n", info.name);
    printf("Version: %s\n", info.version);
    printf("Architecture: %s\n", info.architecture);

    if (info.installed_size)
        printf("Installed-Size: %llu kB\n", info.installed_size);

    if (info.depends)
        printf("Depends: %s\n", info.depends);
    if (info.pre_depends)
        printf("Pre-Depends: %s\n", info.pre_depends);
    if (info.recommends)
        printf("Recommends: %s\n", info.recommends);
    if (info.suggests)
        printf("Suggests: %s\n", info.suggests);
    if (info.provides)
        printf("Provides: %s\n", info.provides);
    if (info.conflicts)
        printf("Conflicts: %s\n", info.conflicts);
    if (info.replaces)
        printf("Replaces: %s\n", info.replaces);

    if (info.homepage)
        printf("Homepage: %s\n", info.homepage);

    if (info.filename)
        printf("Filename: %s\n", info.filename);

    if (info.summary) {
        printf("Description: %s\n", info.summary);
        if (info.description) {
            const char *p = info.description;
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

    if (info.is_installed)
        printf("Status: install ok installed\n");

    aept_pkg_info_free(&info);
    aept_cleanup();
    return 0;
}

static int cmd_files(int argc, char *argv[])
{
    char **paths;
    int count;
    int opt, r, i;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", files_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_files(stdout); return 0;
        default:  usage_files(stderr); return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("files requires a package name");
        return 1;
    }

    if (init_aept() < 0)
        return 1;

    r = aept_files(argv[optind], &paths, &count);
    if (r != 0) {
        if (r > 0)
            aept_log_error("package '%s' is not installed", argv[optind]);
        aept_cleanup();
        return 1;
    }

    for (i = 0; i < count; i++) {
        printf("%s\n", paths[i]);
        free(paths[i]);
    }
    free(paths);

    aept_cleanup();
    return 0;
}

static int cmd_owns(int argc, char *argv[])
{
    char **owners;
    int count;
    int opt, r, i;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", owns_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_owns(stdout); return 0;
        default:  usage_owns(stderr); return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("owns requires a file path");
        return 1;
    }

    if (init_aept() < 0)
        return 1;

    r = aept_owns(argv[optind], &owners, &count);
    if (r != 0) {
        aept_cleanup();
        return 1;
    }

    for (i = 0; i < count; i++) {
        printf("%s\n", owners[i]);
        free(owners[i]);
    }
    free(owners);

    aept_cleanup();
    return 0;
}

static int cmd_mark_manual(int argc, char *argv[])
{
    int all = 0;
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", mark_manual_options, NULL)) != -1) {
        switch (opt) {
        case 'h':   usage_mark(stdout); return 0;
        case 0x100: all = 1; break;
        default:    usage_mark(stderr); return 1;
        }
    }

    if (!all && optind >= argc) {
        aept_log_error("mark manual requires package names or --all");
        return 1;
    }

    if (init_aept() < 0)
        return 1;

    if (all) {
        r = aept_mark_manual_all();
    } else {
        r = aept_mark_manual((const char **)&argv[optind], argc - optind);
    }

    aept_cleanup();
    return r < 0 ? 1 : 0;
}

static int cmd_mark_auto(int argc, char *argv[])
{
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", mark_auto_options, NULL)) != -1) {
        switch (opt) {
        case 'h':   usage_mark(stdout); return 0;
        default:    usage_mark(stderr); return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("mark auto requires package names");
        return 1;
    }

    if (init_aept() < 0)
        return 1;

    r = aept_mark_auto((const char **)&argv[optind], argc - optind);

    aept_cleanup();
    return r < 0 ? 1 : 0;
}

static int cmd_mark(int argc, char *argv[])
{
    const char *action;
    int opt;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", mark_options, NULL)) != -1) {
        switch (opt) {
        case 'h':   usage_mark(stdout); return 0;
        default:    usage_mark(stderr); return 1;
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

static int cmd_pin(int argc, char *argv[])
{
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", pin_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_pin(stdout); return 0;
        default:  usage_pin(stderr); return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("pin requires at least one package name");
        usage_pin(stderr);
        return 1;
    }

    if (init_aept() < 0)
        return 1;

    r = aept_pin((const char **)&argv[optind], argc - optind);

    aept_cleanup();
    return r < 0 ? 1 : 0;
}

static int cmd_unpin(int argc, char *argv[])
{
    int opt, r;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", pin_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_pin(stdout); return 0;
        default:  usage_pin(stderr); return 1;
        }
    }

    if (optind >= argc) {
        aept_log_error("unpin requires at least one package name");
        usage_pin(stderr);
        return 1;
    }

    if (init_aept() < 0)
        return 1;

    r = aept_unpin((const char **)&argv[optind], argc - optind);

    aept_cleanup();
    return r < 0 ? 1 : 0;
}

static int cmd_print_architecture(int argc, char *argv[])
{
    char **archs;
    int count;
    int opt, r, i;

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", print_arch_options, NULL)) != -1) {
        switch (opt) {
        case 'h': usage_print_architecture(stdout); return 0;
        default:  usage_print_architecture(stderr); return 1;
        }
    }

    if (init_aept() < 0)
        return 1;

    r = aept_architectures(&archs, &count);
    if (r < 0) {
        aept_cleanup();
        return 1;
    }

    for (i = 0; i < count; i++) {
        printf("%s\n", archs[i]);
        free(archs[i]);
    }
    free(archs);

    aept_cleanup();
    return 0;
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
    setup_signals();

    while ((opt = getopt_long(argc, argv, "+c:o:vh", global_options, NULL)) != -1) {
        switch (opt) {
        case 'c': conf_file = optarg; conf_explicit = 1; break;
        case 'o': offline_root = optarg; break;
        case 'v': verbose_count++; break;
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
    if (strcmp(command, "mark") == 0)
        return cmd_mark(argc - optind, argv + optind);
    if (strcmp(command, "pin") == 0)
        return cmd_pin(argc - optind, argv + optind);
    if (strcmp(command, "unpin") == 0)
        return cmd_unpin(argc - optind, argv + optind);
    if (strcmp(command, "print-architecture") == 0)
        return cmd_print_architecture(argc - optind, argv + optind);

    aept_log_error("unknown command '%s'", command);
    usage_main(stderr);
    return 1;
}
