/* aept.h - public API for libaept
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef AEPT_H_7BF97F
#define AEPT_H_7BF97F

/* --- Visibility ---------------------------------------------------------- */

/* libaept is compiled with -fvisibility=hidden, so nothing leaves the
 * shared object unless it is marked AEPT_API.  The ABI is therefore
 * what these markings say it is, rather than whatever happened to be
 * named aept_* -- which used to export 139 symbols for a header that
 * declares 29.
 *
 * Everything in this header is exported.  Three utilities outside it
 * are too, because a C caller linking against libaept needs them and
 * the aept CLI is exactly that caller: aept_log() (backing the
 * aept_log_*() macros in msg.h) and aept_malloc()/aept_asprintf() from
 * util.h.  Nothing else is: the library's own modules call each other
 * through the unmarked headers in include/aept/.
 *
 * Tests reaching internals link the static archive (-static in
 * tests/Makefile.am), where hidden visibility does not apply. */
#ifndef AEPT_API
#if defined(__GNUC__) || defined(__clang__)
#define AEPT_API __attribute__((visibility("default")))
#else
#define AEPT_API
#endif
#endif

/* --- Opaque context handle ----------------------------------------------- */

/* An aept_ctx_t is not thread-safe: all calls on a given context must be
 * serialized by the caller.  aept_cancel() is the sole exception (safe to
 * call from any thread or signal handler).  Different threads may operate
 * on independent contexts concurrently (e.g. different offline roots). */
typedef struct aept_ctx aept_ctx_t;

/* --- Lifecycle ----------------------------------------------------------- */

AEPT_API aept_ctx_t *aept_init(void);
AEPT_API void aept_cleanup(aept_ctx_t *ctx);

/* --- Configuration ------------------------------------------------------- */

AEPT_API int aept_load_config(aept_ctx_t *ctx, const char *path);
AEPT_API void aept_set_offline_root(aept_ctx_t *ctx, const char *path);
AEPT_API void aept_set_verbosity(aept_ctx_t *ctx, int level);

/*
 * Seconds a single network wait may take before the transfer is
 * abandoned; 0 waits indefinitely.  Defaults to 120, and to whatever
 * `option network_timeout` says once a config file is loaded.
 *
 * This is an idle timeout: the clock restarts whenever the peer sends
 * something, so a slow transfer runs to completion and only a silent
 * one is cut off.  It exists so that an embedding application gets
 * control back from a peer that stops responding without having to
 * arrange a signal, which would act on the whole process rather than on
 * this call.  A cut-off call reports AEPT_ERR_TIMEOUT.
 *
 * Name resolution is not covered: getaddrinfo(3) cannot be interrupted
 * or bounded from here, and is limited only by the resolver's own
 * configuration.
 */
AEPT_API void aept_set_network_timeout(aept_ctx_t *ctx, int seconds);

/* --- Error reporting ----------------------------------------------------- */

enum {
    AEPT_ERR_NONE = 0,
    AEPT_ERR_GENERAL,
    AEPT_ERR_TIMEOUT,
    /* The operation succeeded, but one or more trigger scripts
     * failed.  The failure is recorded in the status area and retried
     * on the next transaction or by aept_triggers(); reported here
     * because the call itself returns 0 -- the transaction's own work
     * is complete and pretending otherwise would be the lie. */
    AEPT_ERR_TRIGGER,
};

/* Why the most recent call on this context failed.  Meaningful
 * immediately after a call that returned non-zero. */
AEPT_API int aept_last_error(aept_ctx_t *ctx);

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
    AEPT_FLAG_KEEP_GOING,
};

AEPT_API void aept_set_flag(aept_ctx_t *ctx, int flag, int value);
AEPT_API int aept_get_flag(aept_ctx_t *ctx, int flag);

/* --- Callbacks ----------------------------------------------------------- */

enum {
    AEPT_LOG_ERROR = 0,
    AEPT_LOG_WARNING = 1,
    AEPT_LOG_INFO = 2,
    AEPT_LOG_DEBUG = 3,
};

typedef void (*aept_log_fn)(int level, const char *msg, void *userdata);
AEPT_API void aept_set_log_fn(aept_ctx_t *ctx, aept_log_fn fn, void *userdata);

typedef struct aept_transaction {
    const char **install;
    int n_install;
    const char **upgrade;
    int n_upgrade;
    const char **reinstall;
    int n_reinstall;
    const char **remove;
    int n_remove;
} aept_transaction_t;

typedef void (*aept_display_fn)(const aept_transaction_t *txn, void *userdata);
AEPT_API void aept_set_display_fn(aept_ctx_t *ctx, aept_display_fn fn, void *userdata);

/* Return non-zero to proceed, 0 to abort. */
typedef int (*aept_confirm_fn)(void *userdata);
AEPT_API void aept_set_confirm_fn(aept_ctx_t *ctx, aept_confirm_fn fn, void *userdata);

/* --- Cancellation -------------------------------------------------------- */

AEPT_API void aept_cancel(aept_ctx_t *ctx);

/* --- Mutating operations ------------------------------------------------- */

AEPT_API int aept_update(aept_ctx_t *ctx);
AEPT_API int aept_install(aept_ctx_t *ctx, const char **names, int name_count,
                          const char **local_paths, int local_count);
AEPT_API int aept_upgrade(aept_ctx_t *ctx);
AEPT_API int aept_remove(aept_ctx_t *ctx, const char **names, int count);
AEPT_API int aept_autoremove(aept_ctx_t *ctx);
AEPT_API int aept_clean(aept_ctx_t *ctx);

/* Retry trigger scripts whose earlier run failed (recorded per package
 * as {info_dir}/{name}.triggers-pending).  Returns 0 when every
 * pending trigger ran clean or none was pending, -1 when any failed
 * again -- for this call the trigger IS the operation. */
AEPT_API int aept_triggers(aept_ctx_t *ctx);

AEPT_API int aept_pin(aept_ctx_t *ctx, const char **specs, int count);
AEPT_API int aept_unpin(aept_ctx_t *ctx, const char **names, int count);
AEPT_API int aept_mark_auto(aept_ctx_t *ctx, const char **names, int count);
AEPT_API int aept_mark_manual(aept_ctx_t *ctx, const char **names, int count);
AEPT_API int aept_mark_manual_all(aept_ctx_t *ctx);

/* --- Query: list --------------------------------------------------------- */

typedef struct {
    char *name;
    char *version;
    char *summary;
    int installed;
    int upgradable;
} aept_pkg_entry_t;

typedef struct {
    aept_pkg_entry_t *entries;
    int count;
} aept_pkg_list_t;

AEPT_API int aept_list(aept_ctx_t *ctx, const char *pattern, int filter_installed,
                       int filter_upgradable, aept_pkg_list_t *out);
AEPT_API void aept_pkg_list_free(aept_pkg_list_t *list);

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
    int is_installed;
} aept_pkg_info_t;

/* Returns 0 on success, 1 if not found, -1 on error. */
AEPT_API int aept_show(aept_ctx_t *ctx, const char *name, aept_pkg_info_t *out);
AEPT_API void aept_pkg_info_free(aept_pkg_info_t *info);

/* --- Query: files / owns / architectures --------------------------------- */

/* Returns 0 on success, 1 if not found, -1 on error. */
AEPT_API int aept_files(aept_ctx_t *ctx, const char *name, char ***paths_out, int *count_out);

/* Returns 0 on success, 1 if not found, -1 on error. */
AEPT_API int aept_owns(aept_ctx_t *ctx, const char *path, char ***owners_out, int *count_out);

AEPT_API int aept_architectures(aept_ctx_t *ctx, char ***archs_out, int *count_out);

#endif
