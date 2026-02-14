/* config.c - configuration file parsing
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/aept.h"
#include "aept/config.h"
#include "aept/msg.h"
#include "aept/util.h"

static aept_config_t _cfg;
aept_config_t *cfg = &_cfg;

static void config_set_defaults(void)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->info_dir = xstrdup("/var/lib/aept/info");
    cfg->lists_dir = xstrdup("/var/lib/aept/lists");
    cfg->status_file = xstrdup("/var/lib/aept/status");
    cfg->cache_dir = xstrdup("/var/cache/aept");
    cfg->tmp_dir = xstrdup("/tmp");
    cfg->lock_file = xstrdup("/var/lib/aept/lock");
    cfg->usign_bin = xstrdup("usign");
    cfg->usign_keydir = xstrdup("/etc/aept/usign/trustdb");

    cfg->check_signature = 1;
    cfg->verbosity = AEPT_INFO;
}

static void add_source(const char *name, const char *url, int gzip)
{
    cfg->nsources++;
    cfg->sources = xrealloc(cfg->sources,
                            cfg->nsources * sizeof(aept_source_t));

    aept_source_t *src = &cfg->sources[cfg->nsources - 1];
    src->name = xstrdup(name);
    src->url = xstrdup(url);
    src->gzip = gzip;
}

static void add_arch(const char *arch)
{
    cfg->narchs++;
    cfg->archs = xrealloc(cfg->archs, cfg->narchs * sizeof(char *));
    cfg->archs[cfg->narchs - 1] = xstrdup(arch);
}

static void set_option(const char *key, const char *value)
{
    char **strp = NULL;

    if (strcmp(key, "offline_root") == 0)
        strp = &cfg->offline_root;
    else if (strcmp(key, "info_dir") == 0)
        strp = &cfg->info_dir;
    else if (strcmp(key, "lists_dir") == 0)
        strp = &cfg->lists_dir;
    else if (strcmp(key, "status_file") == 0)
        strp = &cfg->status_file;
    else if (strcmp(key, "cache_dir") == 0)
        strp = &cfg->cache_dir;
    else if (strcmp(key, "tmp_dir") == 0)
        strp = &cfg->tmp_dir;
    else if (strcmp(key, "lock_file") == 0)
        strp = &cfg->lock_file;
    else if (strcmp(key, "usign_bin") == 0)
        strp = &cfg->usign_bin;
    else if (strcmp(key, "usign_keydir") == 0)
        strp = &cfg->usign_keydir;
    else if (strcmp(key, "check_signature") == 0) {
        cfg->check_signature = atoi(value);
        return;
    } else if (strcmp(key, "ignore_uid") == 0) {
        cfg->ignore_uid = atoi(value);
        return;
    } else {
        log_warning("unknown option '%s'", key);
        return;
    }

    free(*strp);
    *strp = xstrdup(value);
}

void config_apply_offline_root(void)
{
    char *tmp;

    if (!cfg->offline_root)
        cfg->offline_root = getenv("OFFLINE_ROOT")
            ? xstrdup(getenv("OFFLINE_ROOT")) : NULL;

    if (!cfg->offline_root)
        return;

    xasprintf(&tmp, "%s%s", cfg->offline_root, cfg->lists_dir);
    free(cfg->lists_dir);
    cfg->lists_dir = tmp;

    xasprintf(&tmp, "%s%s", cfg->offline_root, cfg->cache_dir);
    free(cfg->cache_dir);
    cfg->cache_dir = tmp;

    xasprintf(&tmp, "%s%s", cfg->offline_root, cfg->info_dir);
    free(cfg->info_dir);
    cfg->info_dir = tmp;

    xasprintf(&tmp, "%s%s", cfg->offline_root, cfg->status_file);
    free(cfg->status_file);
    cfg->status_file = tmp;

    xasprintf(&tmp, "%s%s", cfg->offline_root, cfg->lock_file);
    free(cfg->lock_file);
    cfg->lock_file = tmp;

}

int config_load(const char *filename)
{
    FILE *fp;
    char buf[4096];

    config_set_defaults();

    fp = fopen(filename, "r");
    if (!fp) {
        log_error("cannot open config file '%s': %s",
                  filename, strerror(errno));
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp)) {
        char *line = buf;
        char *token;

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
            log_warning("unknown config directive '%s'", token);
        }
    }

    fclose(fp);

    return 0;
}

void config_free(void)
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
    free(cfg->status_file);
    free(cfg->cache_dir);
    free(cfg->tmp_dir);
    free(cfg->lock_file);
    free(cfg->usign_bin);
    free(cfg->usign_keydir);

    memset(cfg, 0, sizeof(*cfg));
}

char *config_root_path(const char *path)
{
    char *result;
    xasprintf(&result, "%s%s",
              cfg->offline_root ? cfg->offline_root : "", path);
    return result;
}
