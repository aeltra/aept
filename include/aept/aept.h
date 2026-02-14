/* aept.h - global configuration and forward declarations
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AEPT_H_7BF97F
#define AEPT_H_7BF97F

typedef struct {
    char *name;
    char *url;
    int gzip;
} aept_source_t;

typedef struct {
    aept_source_t *sources;
    int nsources;

    char *offline_root;     /* NULL or path */
    char *info_dir;         /* default "/var/lib/aept/info" */
    char *lists_dir;        /* default "/var/lib/aept/lists" */
    char *status_file;      /* default "/var/lib/aept/status" */
    char *cache_dir;        /* default "/var/cache/aept" */
    char *tmp_dir;          /* default "/tmp" */
    char *lock_file;        /* default "/var/lib/aept/lock" */
    char *usign_bin;        /* default "usign" */
    char *usign_keydir;     /* default "/etc/aept/usign/trustdb" */

    char **archs;
    int narchs;

    int check_signature;    /* default 1 */
    int ignore_uid;         /* default 0 */
    int force_depends;
    int noaction;
    int download_only;
    int verbosity;
} aept_config_t;

extern aept_config_t *cfg;

/* Child process exit codes */
#define AEPT_EXIT_EXEC_FAILED  255
#define AEPT_EXIT_SETUP_FAILED 254

#endif
