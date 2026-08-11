/* internal.h - context structure and configuration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef INTERNAL_H_7BF97F
#define INTERNAL_H_7BF97F

#include <stdatomic.h>

#include "aept/aept.h"

typedef struct {
    char *name;
    char *url;
    int gzip;
} aept_source_t;

typedef struct aept_config {
    aept_source_t *sources;
    int nsources;

    char *offline_root;    /* NULL or path */
    char *info_dir;        /* default "/var/lib/aept/info" */
    char *lists_dir;       /* default "/var/lib/aept/lists" */
    char *cache_dir;       /* default "/var/cache/aept" */
    char *tmp_dir;         /* default "/tmp" */
    char *lock_file;       /* default "/var/lib/aept/lock" */
    char *usign_keydir;    /* default "/etc/aept/usign/trustdb" */
    char *auto_file;       /* default "/var/lib/aept/auto-installed" */
    char *pin_file;        /* default "/var/lib/aept/pinned-packages" */
    char *ssl_client_cert; /* NULL or path to client certificate */
    char *ssl_client_key;  /* NULL or path to client private key */

    char **archs;
    int narchs;

    int check_signature; /* default 1 */
    int ignore_uid;      /* default 0 */
    int allow_downgrade;
    int force_depends;
    int noaction;
    int download_only;
    int reinstall;
    int no_cache;
    int force_confnew;
    int force_confold;
    int purge;
    int non_interactive;
    int keep_going;
    int verbosity;
} aept_config_t;

/* Forward declarations */
struct aept_solver;
struct libfetch_ctx;

/* Full definition of the opaque context handle (aept_ctx_t). */
struct aept_ctx {
    aept_config_t config;
    struct aept_solver *solver; /* NULL until aept_solver_init() */
    struct libfetch_ctx *http;  /* connection cache + TLS config */
    int lock_fd;

    /* Callbacks — set once, read-only after init */
    aept_log_fn log_fn;
    void *log_userdata;
    aept_display_fn display_fn;
    void *display_userdata;
    aept_confirm_fn confirm_fn;
    void *confirm_userdata;

    _Atomic int cancelled;
    int use_color;
    int config_loaded;
};

/*
 * Absolute paths to the helpers aept execs.
 *
 * aept normally runs as root, so no exec may resolve through PATH: an
 * attacker who can influence the environment would otherwise choose
 * which binary runs with those privileges.  Note that execvp() only
 * searches PATH for names containing no slash, so passing these as
 * argv[0] is sufficient — /bin/sh in script.c and trigger.c is already
 * safe for the same reason.
 *
 * Deliberately compile-time constants rather than configure-time
 * AC_PATH_PROG results: aept is cross-built for its target, so probing
 * the build machine would bake in paths that need not hold there.
 */
#define AEPT_USIGN_BIN "/usr/bin/usign"
#define AEPT_RM_BIN "/bin/rm"
#define AEPT_DIFF_BIN "/usr/bin/diff"
#define AEPT_SH_BIN "/bin/sh"

/* Child process exit codes */
#define AEPT_EXIT_EXEC_FAILED 255
#define AEPT_EXIT_SETUP_FAILED 254

#endif
