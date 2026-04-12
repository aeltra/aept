/* config.c - configuration file parsing
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/config.h"
#include "aept/msg.h"
#include "aept/util.h"

void aept_config_set_defaults(struct aept_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->info_dir = aept_strdup("/var/lib/aept/info");
    cfg->lists_dir = aept_strdup("/var/lib/aept/lists");
    cfg->cache_dir = aept_strdup("/var/cache/aept");
    cfg->tmp_dir = aept_strdup("/tmp");
    cfg->lock_file = aept_strdup("/var/lib/aept/lock");
    cfg->usign_keydir = aept_strdup("/etc/aept/usign/trustdb");
    cfg->auto_file = aept_strdup("/var/lib/aept/auto-installed");
    cfg->pin_file = aept_strdup("/var/lib/aept/pinned-packages");

    cfg->check_signature = 1;
    cfg->verbosity = AEPT_INFO;
}

static void add_source(struct aept_config *cfg, const char *name,
                        const char *url, int gzip)
{
    if (!aept_pkg_name_is_safe(name)) {
        aept_log_warning("ignoring source with unsafe name '%s'", name);
        return;
    }

    cfg->nsources++;
    cfg->sources = aept_realloc(cfg->sources,
                            cfg->nsources * sizeof(aept_source_t));

    aept_source_t *src = &cfg->sources[cfg->nsources - 1];
    src->name = aept_strdup(name);
    src->url = aept_strdup(url);
    src->gzip = gzip;
}

static void add_arch(struct aept_config *cfg, const char *arch)
{
    cfg->narchs++;
    cfg->archs = aept_realloc(cfg->archs, cfg->narchs * sizeof(char *));
    cfg->archs[cfg->narchs - 1] = aept_strdup(arch);
}

/*
 * Parse a boolean option value.  Accepts the usual aliases for
 * truthiness and falsiness.  On unrecognized input, logs a warning and
 * returns `safe_default` — callers pass the security-preserving value
 * (e.g. 1 for check_signature) so a typo never silently weakens the
 * configuration.
 */
static int parse_bool(const char *key, const char *value, int safe_default)
{
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
            strcmp(value, "yes") == 0 || strcmp(value, "on") == 0)
        return 1;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
            strcmp(value, "no") == 0 || strcmp(value, "off") == 0)
        return 0;

    aept_log_warning("invalid boolean value '%s' for option '%s', "
                "using default '%d'", value, key, safe_default);
    return safe_default;
}

static void set_option(struct aept_config *cfg, const char *key,
                        const char *value)
{
    char **strp = NULL;

    if (strcmp(key, "offline_root") == 0)
        strp = &cfg->offline_root;
    else if (strcmp(key, "info_dir") == 0)
        strp = &cfg->info_dir;
    else if (strcmp(key, "lists_dir") == 0)
        strp = &cfg->lists_dir;
    else if (strcmp(key, "cache_dir") == 0)
        strp = &cfg->cache_dir;
    else if (strcmp(key, "tmp_dir") == 0)
        strp = &cfg->tmp_dir;
    else if (strcmp(key, "lock_file") == 0)
        strp = &cfg->lock_file;
    else if (strcmp(key, "usign_keydir") == 0)
        strp = &cfg->usign_keydir;
    else if (strcmp(key, "auto_file") == 0)
        strp = &cfg->auto_file;
    else if (strcmp(key, "pin_file") == 0)
        strp = &cfg->pin_file;
    else if (strcmp(key, "ssl_client_cert") == 0)
        strp = &cfg->ssl_client_cert;
    else if (strcmp(key, "ssl_client_key") == 0)
        strp = &cfg->ssl_client_key;
    else if (strcmp(key, "check_signature") == 0) {
        cfg->check_signature = parse_bool(key, value, 1);
        return;
    } else if (strcmp(key, "ignore_uid") == 0) {
        cfg->ignore_uid = parse_bool(key, value, 0);
        return;
    } else if (strcmp(key, "allow_downgrade") == 0) {
        cfg->allow_downgrade = parse_bool(key, value, 0);
        return;
    } else {
        aept_log_warning("unknown option '%s'", key);
        return;
    }

    free(*strp);
    *strp = aept_strdup(value);
}

void aept_config_apply_offline_root(struct aept_config *cfg)
{
    char *tmp;

    if (!cfg->offline_root)
        return;

    aept_asprintf(&tmp, "%s%s", cfg->offline_root, cfg->lists_dir);
    free(cfg->lists_dir);
    cfg->lists_dir = tmp;

    aept_asprintf(&tmp, "%s%s", cfg->offline_root, cfg->cache_dir);
    free(cfg->cache_dir);
    cfg->cache_dir = tmp;

    aept_asprintf(&tmp, "%s%s", cfg->offline_root, cfg->info_dir);
    free(cfg->info_dir);
    cfg->info_dir = tmp;

    aept_asprintf(&tmp, "%s%s", cfg->offline_root, cfg->lock_file);
    free(cfg->lock_file);
    cfg->lock_file = tmp;

    aept_asprintf(&tmp, "%s%s", cfg->offline_root, cfg->auto_file);
    free(cfg->auto_file);
    cfg->auto_file = tmp;

    aept_asprintf(&tmp, "%s%s", cfg->offline_root, cfg->pin_file);
    free(cfg->pin_file);
    cfg->pin_file = tmp;
}

int aept_config_load(struct aept_config *cfg, const char *filename)
{
    FILE *fp;
    char buf[4096];

    aept_config_set_defaults(cfg);

    fp = fopen(filename, "r");
    if (!fp) {
        aept_log_error("cannot open config file '%s': %s",
                  filename, strerror(errno));
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp)) {
        char *line = buf;
        char *token;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_log_warning("config line too long, skipping");
            aept_fgets_drain_line(fp);
            continue;
        }

        /* strip trailing newline */
        line[strcspn(line, "\n")] = '\0';

        /* skip leading whitespace */
        while (isspace((unsigned char)*line))
            line++;

        /* skip comments and blank lines */
        if (*line == '#' || *line == '\0')
            continue;

        token = strsep(&line, " \t");

        if (strcmp(token, "src/gz") == 0) {
            char *name = strsep(&line, " \t");
            char *url = strsep(&line, " \t");
            if (name && url)
                add_source(cfg, name, url, 1);
        } else if (strcmp(token, "src") == 0) {
            char *name = strsep(&line, " \t");
            char *url = strsep(&line, " \t");
            if (name && url)
                add_source(cfg, name, url, 0);
        } else if (strcmp(token, "option") == 0) {
            char *key = strsep(&line, " \t");
            char *value = strsep(&line, " \t");
            if (key && value)
                set_option(cfg, key, value);
        } else if (strcmp(token, "arch") == 0) {
            char *arch = strsep(&line, " \t");
            if (arch)
                add_arch(cfg, arch);
        } else {
            aept_log_warning("unknown config directive '%s'", token);
        }
    }

    fclose(fp);

    return 0;
}

static int validate_dir(const char *name, const char *path)
{
    if (aept_file_exists(path) && !aept_file_is_dir(path)) {
        aept_log_error("'%s' (%s) exists but is not a directory", name, path);
        return -1;
    }
    return 0;
}

int aept_config_validate(const struct aept_config *cfg)
{
    int r = 0;

    if (cfg->offline_root) {
        if (!aept_file_exists(cfg->offline_root)) {
            aept_log_error("offline_root '%s' does not exist",
                      cfg->offline_root);
            return -1;
        }
        if (!aept_file_is_dir(cfg->offline_root)) {
            aept_log_error("offline_root '%s' is not a directory",
                      cfg->offline_root);
            return -1;
        }
    }

    r |= validate_dir("info_dir", cfg->info_dir);
    r |= validate_dir("lists_dir", cfg->lists_dir);
    r |= validate_dir("cache_dir", cfg->cache_dir);
    r |= validate_dir("tmp_dir", cfg->tmp_dir);
    r |= validate_dir("usign_keydir", cfg->usign_keydir);

    if (aept_file_exists(AEPT_USIGN_BIN) && aept_file_is_dir(AEPT_USIGN_BIN)) {
        aept_log_error("usign_bin '%s' is a directory", AEPT_USIGN_BIN);
        r = -1;
    }

    return r;
}

void aept_config_free(struct aept_config *cfg)
{
    int i;

    for (i = 0; i < cfg->nsources; i++) {
        free(cfg->sources[i].name);
        free(cfg->sources[i].url);
    }
    free(cfg->sources);

    for (i = 0; i < cfg->narchs; i++)
        free(cfg->archs[i]);
    free(cfg->archs);

    free(cfg->offline_root);
    free(cfg->info_dir);
    free(cfg->lists_dir);
    free(cfg->cache_dir);
    free(cfg->tmp_dir);
    free(cfg->lock_file);
    free(cfg->usign_keydir);
    free(cfg->auto_file);
    free(cfg->pin_file);
    free(cfg->ssl_client_cert);
    free(cfg->ssl_client_key);

    memset(cfg, 0, sizeof(*cfg));
}

char *aept_config_root_path(const struct aept_config *cfg, const char *path)
{
    char *result;
    aept_asprintf(&result, "%s%s",
              cfg->offline_root ? cfg->offline_root : "", path);
    return result;
}

int aept_config_lock(struct aept_ctx *ctx)
{
    char *dir = aept_strdup(ctx->config.lock_file);
    aept_file_mkdir_hier(dirname(dir), 0755);
    free(dir);

    ctx->lock_fd = open(ctx->config.lock_file, O_CREAT | O_RDWR, 0644);
    if (ctx->lock_fd < 0) {
        aept_log_error("cannot open lock file '%s': %s",
                  ctx->config.lock_file, strerror(errno));
        return -1;
    }

    if (flock(ctx->lock_fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK)
            aept_log_error("another aept instance is running");
        else
            aept_log_error("cannot lock '%s': %s",
                      ctx->config.lock_file, strerror(errno));
        close(ctx->lock_fd);
        ctx->lock_fd = -1;
        return -1;
    }

    return 0;
}

void aept_config_unlock(struct aept_ctx *ctx)
{
    if (ctx->lock_fd >= 0) {
        flock(ctx->lock_fd, LOCK_UN);
        close(ctx->lock_fd);
        ctx->lock_fd = -1;
    }
}
