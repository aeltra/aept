/* main.c - CLI entry point
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/aept.h"
#include "aept/cli.h"
#include "aept/msg.h"
#include "aept/util.h"

#define DEFAULT_CONF "/etc/aept/aept.conf"

static const char *conf_file = DEFAULT_CONF;
static const char *offline_root;
static const char *cache_dir_override;
static int conf_explicit;
static int verbose_count;

/* ── signal handling ──────────────────────────────────────────────── */

/*
 * Read by the signal handler, so atomic rather than a plain pointer: a
 * handler may touch only a lock-free atomic or a volatile sig_atomic_t,
 * and this is the same reason ctx->cancelled is _Atomic.
 *
 * It must be cleared before the context it names is freed, or a signal
 * arriving between the free and process exit hands aept_cancel() memory
 * that is gone.  cli_cleanup() below is the only thing that frees a
 * context here, and clearing first is enough: a handler runs to
 * completion against a suspended main thread, so it sees the pointer
 * either wholly before or wholly after.
 */
static aept_ctx_t *_Atomic g_ctx;
static volatile sig_atomic_t g_signum;

static void signal_handler(int sig)
{
    aept_ctx_t *ctx = g_ctx;

    g_signum = sig;
    if (ctx)
        aept_cancel(ctx);
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
    sigaction(SIGHUP, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

/* ── shared helpers ───────────────────────────────────────────────── */

/* Finish with a context: unpublish it before it is freed.  Every
 * aept_cleanup() in this file goes through here. */
void cli_cleanup(aept_ctx_t *ctx)
{
    g_ctx = NULL;
    aept_cleanup(ctx);
}

static const char *resolve_conf(void)
{
    char *path;

    if (!offline_root || conf_explicit)
        return conf_file;

    aept_asprintf(&path, "%s%s", offline_root, DEFAULT_CONF);
    return path;
}

aept_ctx_t *init_aept(void)
{
    const char *cf;

    aept_ctx_t *ctx = aept_init();
    if (!ctx)
        return NULL;

    g_ctx = ctx;

    if (offline_root && aept_set_offline_root(ctx, offline_root) < 0) {
        cli_cleanup(ctx);
        return NULL;
    }

    cf = resolve_conf();

    if (conf_explicit && access(cf, R_OK) < 0) {
        aept_log_error("cannot access config file '%s': %s", cf, strerror(errno));
        cli_cleanup(ctx);
        return NULL;
    }

    if (!conf_explicit && access(cf, R_OK) < 0 && errno == ENOENT)
        aept_log_warning("config file '%s' not found, using defaults", cf);

    if (aept_load_config(ctx, cf) < 0) {
        if (cf != conf_file)
            free((char *)cf);
        cli_cleanup(ctx);
        return NULL;
    }

    if (cf != conf_file)
        free((char *)cf);

    /*
     * A --cache-dir on the command line replaces whatever the config
     * said, verbatim.  It is applied AFTER aept_load_config() so the
     * offline-root prefixing done by that call does not touch it: the
     * CLI value is a host path and must remain literal.
     */
    if (cache_dir_override && aept_set_cache_dir(ctx, cache_dir_override) < 0) {
        cli_cleanup(ctx);
        return NULL;
    }

    aept_set_verbosity(ctx, AEPT_LOG_INFO + verbose_count);

    if (!offline_root && access("/etc/aeltra_version", F_OK) != 0) {
        aept_log_error("not running on Aeltra OS; use -o to set an offline root");
        cli_cleanup(ctx);
        return NULL;
    }

    return ctx;
}

/* ── usage functions ───────────────────────────────────────────────── */

static void usage_main(FILE *out)
{
    fprintf(out,
            "Usage: aept [-c <file>] [-o <dir>] [-C <dir>] [-v] <command> [options] [args...]\n"
            "\n"
            "Global options:\n"
            "  -c, --conf <file>         Configuration file (default: %s)\n"
            "  -o, --offline-root <dir>  Use <dir> as the package root\n"
            "  -C, --cache-dir <dir>     Override the cache directory (host path,\n"
            "                            not prefixed with the offline root).\n"
            "                            Also read from AEPT_CACHE_DIR if unset.\n"
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
            "  mark <action>       Mark packages auto, manual or protected\n"
            "  pin <pkgs...>       Pin packages to a specific version\n"
            "  unpin <pkgs...>     Remove version pins\n"
            "  clean               Remove cached package files\n"
            "  triggers            Retry trigger scripts that failed earlier\n"
            "  files <pkg>         List files of an installed package\n"
            "  owns <path>         Find which package owns a file\n"
            "  verify [pkgs...]    Check installed files against their records\n"
            "  print-architecture  Show configured architectures\n"
            "\n"
            "Run 'aept <command> --help' for command-specific options.\n",
            DEFAULT_CONF);
}

/*
 * The exit status a completed transaction earns.  2 is deliberate and
 * documented: the transaction's own work succeeded, but a trigger
 * script failed -- recorded on disk and retried by the next
 * transaction or `aept triggers` -- and a caller scripting aept gets
 * to tell that apart from both success and failure.
 */
#define EXIT_TRIGGER_FAILED 2

int transaction_exit(aept_ctx_t *ctx, int r)
{
    if (r != 0)
        return 1;
    if (aept_last_error(ctx) == AEPT_ERR_TRIGGER)
        return EXIT_TRIGGER_FAILED;
    return 0;
}

static struct option global_options[] = {
    {"conf",         required_argument, NULL, 'c'},
    {"offline-root", required_argument, NULL, 'o'},
    {"cache-dir",    required_argument, NULL, 'C'},
    {"verbose",      no_argument,       NULL, 'v'},
    {"help",         no_argument,       NULL, 'h'},
    {NULL,           0,                 NULL, 0  }
};

int main(int argc, char *argv[])
{
    const char *command;
    int opt, rc;
    int sub_argc;
    char **sub_argv;

    setup_signals();

    optind = 0;
    while ((opt = getopt_long(argc, argv, OPTS_DISPATCH("c:o:C:vh"), global_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            conf_file = optarg;
            conf_explicit = 1;
            break;
        case 'o':
            offline_root = optarg;
            break;
        case 'C':
            cache_dir_override = optarg;
            break;
        case 'v':
            verbose_count++;
            break;
        case 'h':
            usage_main(stdout);
            return 0;
        default:
            usage_main(stderr);
            return 1;
        }
    }

    if (!cache_dir_override) {
        const char *env_cache_dir = getenv("AEPT_CACHE_DIR");
        if (env_cache_dir && *env_cache_dir)
            cache_dir_override = env_cache_dir;
    }

    if (optind >= argc) {
        usage_main(stderr);
        return 1;
    }

    command = argv[optind];
    sub_argc = argc - optind;
    sub_argv = argv + optind;

    if (strcmp(command, "update") == 0)
        rc = cmd_update(sub_argc, sub_argv);
    else if (strcmp(command, "install") == 0)
        rc = cmd_install(sub_argc, sub_argv);
    else if (strcmp(command, "remove") == 0)
        rc = cmd_remove(sub_argc, sub_argv);
    else if (strcmp(command, "autoremove") == 0)
        rc = cmd_autoremove(sub_argc, sub_argv);
    else if (strcmp(command, "upgrade") == 0)
        rc = cmd_upgrade(sub_argc, sub_argv);
    else if (strcmp(command, "clean") == 0)
        rc = cmd_clean(sub_argc, sub_argv);
    else if (strcmp(command, "triggers") == 0)
        rc = cmd_triggers(sub_argc, sub_argv);
    else if (strcmp(command, "list") == 0)
        rc = cmd_list(sub_argc, sub_argv);
    else if (strcmp(command, "show") == 0)
        rc = cmd_show(sub_argc, sub_argv);
    else if (strcmp(command, "files") == 0)
        rc = cmd_files(sub_argc, sub_argv);
    else if (strcmp(command, "verify") == 0)
        rc = cmd_verify(sub_argc, sub_argv);
    else if (strcmp(command, "owns") == 0)
        rc = cmd_owns(sub_argc, sub_argv);
    else if (strcmp(command, "mark") == 0)
        rc = cmd_mark(sub_argc, sub_argv);
    else if (strcmp(command, "pin") == 0)
        rc = cmd_pin(sub_argc, sub_argv);
    else if (strcmp(command, "unpin") == 0)
        rc = cmd_unpin(sub_argc, sub_argv);
    else if (strcmp(command, "print-architecture") == 0)
        rc = cmd_print_architecture(sub_argc, sub_argv);
    else {
        aept_log_error("unknown command '%s'", command);
        usage_main(stderr);
        return 1;
    }

    /* If a fatal signal was caught, re-raise it under the default
     * disposition so the parent sees the conventional 128+signum
     * exit status instead of a generic error. */
    if (g_signum) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(g_signum, &sa, NULL);
        raise(g_signum);
    }

    return rc;
}
