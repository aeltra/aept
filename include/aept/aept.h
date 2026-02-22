/* aept.h - public API for libaept
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AEPT_H_7BF97F
#define AEPT_H_7BF97F

/* --- Opaque context ------------------------------------------------------ */

typedef struct aept_ctx aept_ctx_t;

/* --- Lifecycle ----------------------------------------------------------- */

aept_ctx_t *aept_ctx_new(void);
void        aept_ctx_free(aept_ctx_t *ctx);

/* --- Configuration ------------------------------------------------------- */

int  aept_ctx_load_config(aept_ctx_t *ctx, const char *path);
void aept_ctx_set_offline_root(aept_ctx_t *ctx, const char *path);
void aept_ctx_set_verbosity(aept_ctx_t *ctx, int level);

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

void aept_ctx_set_flag(aept_ctx_t *ctx, int flag, int value);
int  aept_ctx_get_flag(aept_ctx_t *ctx, int flag);

/* --- Callbacks ----------------------------------------------------------- */

enum {
    AEPT_LOG_ERROR   = 0,
    AEPT_LOG_WARNING = 1,
    AEPT_LOG_INFO    = 2,
    AEPT_LOG_DEBUG   = 3,
};

typedef void (*aept_log_fn)(int level, const char *msg, void *userdata);
void aept_ctx_set_log_fn(aept_ctx_t *ctx, aept_log_fn fn, void *userdata);

typedef struct {
    const char **install;   int n_install;
    const char **upgrade;   int n_upgrade;
    const char **reinstall; int n_reinstall;
    const char **remove;    int n_remove;
} aept_transaction_t;

/* Return non-zero to proceed, 0 to abort. */
typedef int (*aept_confirm_fn)(const aept_transaction_t *txn, void *userdata);
void aept_ctx_set_confirm_fn(aept_ctx_t *ctx, aept_confirm_fn fn,
                             void *userdata);

/* --- Mutating operations ------------------------------------------------- */

int aept_update(aept_ctx_t *ctx);
int aept_install(aept_ctx_t *ctx, const char **names, int name_count,
                 const char **local_paths, int local_count);
int aept_upgrade(aept_ctx_t *ctx);
int aept_remove(aept_ctx_t *ctx, const char **names, int count);
int aept_autoremove(aept_ctx_t *ctx);
int aept_clean(aept_ctx_t *ctx);

int aept_pin(aept_ctx_t *ctx, const char **specs, int count);
int aept_unpin(aept_ctx_t *ctx, const char **names, int count);
int aept_mark_auto(aept_ctx_t *ctx, const char **names, int count);
int aept_mark_manual(aept_ctx_t *ctx, const char **names, int count);
int aept_mark_manual_all(aept_ctx_t *ctx);

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

int  aept_list(aept_ctx_t *ctx, const char *pattern,
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

int  aept_show(aept_ctx_t *ctx, const char *name, aept_pkg_info_t *out);
void aept_pkg_info_free(aept_pkg_info_t *info);

/* --- Query: files / owns / architectures --------------------------------- */

int aept_files(aept_ctx_t *ctx, const char *name,
               char ***paths_out, int *count_out);

int aept_owns(aept_ctx_t *ctx, const char *path, char **owner_out);

int aept_architectures(aept_ctx_t *ctx,
                       char ***archs_out, int *count_out);

#endif
