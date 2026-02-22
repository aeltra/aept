/* config.c - configuration file parsing
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
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

static aept_config_t _cfg;
aept_config_t *aept_cfg = &_cfg;

void aept_config_set_defaults(void)
{
    memset(aept_cfg, 0, sizeof(*aept_cfg));

    aept_cfg->info_dir = aept_strdup("/var/lib/aept/info");
    aept_cfg->lists_dir = aept_strdup("/var/lib/aept/lists");
    aept_cfg->status_file = aept_strdup("/var/lib/aept/status");
    aept_cfg->cache_dir = aept_strdup("/var/cache/aept");
    aept_cfg->tmp_dir = aept_strdup("/tmp");
    aept_cfg->lock_file = aept_strdup("/var/lib/aept/lock");
    aept_cfg->usign_keydir = aept_strdup("/etc/aept/usign/trustdb");
    aept_cfg->auto_file = aept_strdup("/var/lib/aept/auto-installed");
    aept_cfg->pin_file = aept_strdup("/var/lib/aept/pinned-packages");

    aept_cfg->check_signature = 1;
    aept_cfg->verbosity = AEPT_INFO;
}

static void add_source(const char *name, const char *url, int gzip)
{
    if (!aept_pkg_name_is_safe(name)) {
        aept_log_warning("ignoring source with unsafe name '%s'", name);
        return;
    }

    aept_cfg->nsources++;
    aept_cfg->sources = aept_realloc(aept_cfg->sources,
                            aept_cfg->nsources * sizeof(aept_source_t));

    aept_source_t *src = &aept_cfg->sources[aept_cfg->nsources - 1];
    src->name = aept_strdup(name);
    src->url = aept_strdup(url);
    src->gzip = gzip;
}

static void add_arch(const char *arch)
{
    aept_cfg->narchs++;
    aept_cfg->archs = aept_realloc(aept_cfg->archs, aept_cfg->narchs * sizeof(char *));
    aept_cfg->archs[aept_cfg->narchs - 1] = aept_strdup(arch);
}

static void set_option(const char *key, const char *value)
{
    char **strp = NULL;

    if (strcmp(key, "offline_root") == 0)
        strp = &aept_cfg->offline_root;
    else if (strcmp(key, "info_dir") == 0)
        strp = &aept_cfg->info_dir;
    else if (strcmp(key, "lists_dir") == 0)
        strp = &aept_cfg->lists_dir;
    else if (strcmp(key, "status_file") == 0)
        strp = &aept_cfg->status_file;
    else if (strcmp(key, "cache_dir") == 0)
        strp = &aept_cfg->cache_dir;
    else if (strcmp(key, "tmp_dir") == 0)
        strp = &aept_cfg->tmp_dir;
    else if (strcmp(key, "lock_file") == 0)
        strp = &aept_cfg->lock_file;
    else if (strcmp(key, "usign_keydir") == 0)
        strp = &aept_cfg->usign_keydir;
    else if (strcmp(key, "auto_file") == 0)
        strp = &aept_cfg->auto_file;
    else if (strcmp(key, "pin_file") == 0)
        strp = &aept_cfg->pin_file;
    else if (strcmp(key, "check_signature") == 0) {
        aept_cfg->check_signature = atoi(value);
        return;
    } else if (strcmp(key, "ignore_uid") == 0) {
        aept_cfg->ignore_uid = atoi(value);
        return;
    } else if (strcmp(key, "allow_downgrade") == 0) {
        aept_cfg->allow_downgrade = atoi(value);
        return;
    } else {
        aept_log_warning("unknown option '%s'", key);
        return;
    }

    free(*strp);
    *strp = aept_strdup(value);
}

void aept_config_apply_offline_root(void)
{
    char *tmp;

    if (!aept_cfg->offline_root)
        return;

    aept_asprintf(&tmp, "%s%s", aept_cfg->offline_root, aept_cfg->lists_dir);
    free(aept_cfg->lists_dir);
    aept_cfg->lists_dir = tmp;

    aept_asprintf(&tmp, "%s%s", aept_cfg->offline_root, aept_cfg->cache_dir);
    free(aept_cfg->cache_dir);
    aept_cfg->cache_dir = tmp;

    aept_asprintf(&tmp, "%s%s", aept_cfg->offline_root, aept_cfg->info_dir);
    free(aept_cfg->info_dir);
    aept_cfg->info_dir = tmp;

    aept_asprintf(&tmp, "%s%s", aept_cfg->offline_root, aept_cfg->status_file);
    free(aept_cfg->status_file);
    aept_cfg->status_file = tmp;

    aept_asprintf(&tmp, "%s%s", aept_cfg->offline_root, aept_cfg->lock_file);
    free(aept_cfg->lock_file);
    aept_cfg->lock_file = tmp;

    aept_asprintf(&tmp, "%s%s", aept_cfg->offline_root, aept_cfg->auto_file);
    free(aept_cfg->auto_file);
    aept_cfg->auto_file = tmp;

    aept_asprintf(&tmp, "%s%s", aept_cfg->offline_root, aept_cfg->pin_file);
    free(aept_cfg->pin_file);
    aept_cfg->pin_file = tmp;
}

int aept_config_load(const char *filename)
{
    FILE *fp;
    char buf[4096];

    aept_config_set_defaults();

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
                add_source(name, url, 1);
        } else if (strcmp(token, "src") == 0) {
            char *name = strsep(&line, " \t");
            char *url = strsep(&line, " \t");
            if (name && url)
                add_source(name, url, 0);
        } else if (strcmp(token, "option") == 0) {
            char *key = strsep(&line, " \t");
            char *value = strsep(&line, " \t");
            if (key && value)
                set_option(key, value);
        } else if (strcmp(token, "arch") == 0) {
            char *arch = strsep(&line, " \t");
            if (arch)
                add_arch(arch);
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

int aept_config_validate(void)
{
    int r = 0;

    if (aept_cfg->offline_root) {
        if (!aept_file_exists(aept_cfg->offline_root)) {
            aept_log_error("offline_root '%s' does not exist",
                      aept_cfg->offline_root);
            return -1;
        }
        if (!aept_file_is_dir(aept_cfg->offline_root)) {
            aept_log_error("offline_root '%s' is not a directory",
                      aept_cfg->offline_root);
            return -1;
        }
    }

    r |= validate_dir("info_dir", aept_cfg->info_dir);
    r |= validate_dir("lists_dir", aept_cfg->lists_dir);
    r |= validate_dir("cache_dir", aept_cfg->cache_dir);
    r |= validate_dir("tmp_dir", aept_cfg->tmp_dir);
    r |= validate_dir("usign_keydir", aept_cfg->usign_keydir);

    if (aept_file_exists(AEPT_USIGN_BIN) && aept_file_is_dir(AEPT_USIGN_BIN)) {
        aept_log_error("usign_bin '%s' is a directory", AEPT_USIGN_BIN);
        r = -1;
    }

    return r;
}

void aept_config_free(void)
{
    int i;

    for (i = 0; i < aept_cfg->nsources; i++) {
        free(aept_cfg->sources[i].name);
        free(aept_cfg->sources[i].url);
    }
    free(aept_cfg->sources);

    for (i = 0; i < aept_cfg->narchs; i++)
        free(aept_cfg->archs[i]);
    free(aept_cfg->archs);

    free(aept_cfg->offline_root);
    free(aept_cfg->info_dir);
    free(aept_cfg->lists_dir);
    free(aept_cfg->status_file);
    free(aept_cfg->cache_dir);
    free(aept_cfg->tmp_dir);
    free(aept_cfg->lock_file);
    free(aept_cfg->usign_keydir);
    free(aept_cfg->auto_file);
    free(aept_cfg->pin_file);

    memset(aept_cfg, 0, sizeof(*aept_cfg));
}

char *aept_config_root_path(const char *path)
{
    char *result;
    aept_asprintf(&result, "%s%s",
              aept_cfg->offline_root ? aept_cfg->offline_root : "", path);
    return result;
}

static int lock_fd = -1;

int aept_config_lock(void)
{
    char *dir = aept_strdup(aept_cfg->lock_file);
    aept_file_mkdir_hier(dirname(dir), 0755);
    free(dir);

    lock_fd = open(aept_cfg->lock_file, O_CREAT | O_RDWR, 0644);
    if (lock_fd < 0) {
        aept_log_error("cannot open lock file '%s': %s",
                  aept_cfg->lock_file, strerror(errno));
        return -1;
    }

    if (flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK)
            aept_log_error("another aept instance is running");
        else
            aept_log_error("cannot lock '%s': %s",
                      aept_cfg->lock_file, strerror(errno));
        close(lock_fd);
        lock_fd = -1;
        return -1;
    }

    return 0;
}

void aept_config_unlock(void)
{
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        lock_fd = -1;
    }
}
