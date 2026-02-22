/* aept.h - public API for libaept
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef AEPT_H_7BF97F
#define AEPT_H_7BF97F

/* --- Lifecycle ----------------------------------------------------------- */

int  aept_init(void);
void aept_cleanup(void);

/* --- Configuration ------------------------------------------------------- */

int  aept_load_config(const char *path);
void aept_set_offline_root(const char *path);
void aept_set_verbosity(int level);

/* --- Flags --------------------------------------------------------------- */

enum {
    AEPT_FLAG_FORCE_DEPENDS,
    AEPT_FLAG_DOWNLOAD_ONLY,
    AEPT_FLAG_NOACTION,
    AEPT_FLAG_ALLOW_DOWNGRADE,
    AEPT_FLAG_REINSTALL,
    AEPT_FLAG_NO_CACHE,
    AEPT_FLAG_FORCE_CONFNEW,
    AEPT_FLAG_FORCE_CONFOLD,
    AEPT_FLAG_PURGE,
    AEPT_FLAG_NON_INTERACTIVE,
    AEPT_FLAG_CHECK_SIGNATURE,
    AEPT_FLAG_IGNORE_UID,
};

void aept_set_flag(int flag, int value);
int  aept_get_flag(int flag);

/* --- Callbacks ----------------------------------------------------------- */

enum {
    AEPT_LOG_ERROR   = 0,
    AEPT_LOG_WARNING = 1,
    AEPT_LOG_INFO    = 2,
    AEPT_LOG_DEBUG   = 3,
};

typedef void (*aept_log_fn)(int level, const char *msg, void *userdata);
void aept_set_log_fn(aept_log_fn fn, void *userdata);

typedef struct aept_transaction {
    const char **install;   int n_install;
    const char **upgrade;   int n_upgrade;
    const char **reinstall; int n_reinstall;
    const char **remove;    int n_remove;
} aept_transaction_t;

typedef void (*aept_display_fn)(const aept_transaction_t *txn, void *userdata);
void aept_set_display_fn(aept_display_fn fn, void *userdata);

/* Return non-zero to proceed, 0 to abort. */
typedef int (*aept_confirm_fn)(void *userdata);
void aept_set_confirm_fn(aept_confirm_fn fn, void *userdata);

/* --- Cancellation -------------------------------------------------------- */

void aept_cancel(void);

/* --- Mutating operations ------------------------------------------------- */

int aept_update(void);
int aept_install(const char **names, int name_count,
                 const char **local_paths, int local_count);
int aept_upgrade(void);
int aept_remove(const char **names, int count);
int aept_autoremove(void);
int aept_clean(void);

int aept_pin(const char **specs, int count);
int aept_unpin(const char **names, int count);
int aept_mark_auto(const char **names, int count);
int aept_mark_manual(const char **names, int count);
int aept_mark_manual_all(void);

/* --- Query: list --------------------------------------------------------- */

typedef struct {
    char *name;
    char *version;
    char *summary;
    int   installed;
    int   upgradable;
} aept_pkg_entry_t;

typedef struct {
    aept_pkg_entry_t *entries;
    int count;
} aept_pkg_list_t;

int  aept_list(const char *pattern,
               int filter_installed, int filter_upgradable,
               aept_pkg_list_t *out);
void aept_pkg_list_free(aept_pkg_list_t *list);

/* --- Query: show --------------------------------------------------------- */

typedef struct {
    char *name;
    char *version;
    char *architecture;
    unsigned long long installed_size;
    char *depends;
    char *pre_depends;
    char *recommends;
    char *suggests;
    char *provides;
    char *conflicts;
    char *replaces;
    char *homepage;
    char *filename;
    char *summary;
    char *description;
    int   is_installed;
} aept_pkg_info_t;

int  aept_show(const char *name, aept_pkg_info_t *out);
void aept_pkg_info_free(aept_pkg_info_t *info);

/* --- Query: files / owns / architectures --------------------------------- */

int aept_files(const char *name, char ***paths_out, int *count_out);

int aept_owns(const char *path, char ***owners_out, int *count_out);

int aept_architectures(char ***archs_out, int *count_out);

#endif
