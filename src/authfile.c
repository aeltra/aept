/* authfile.c - source credentials from an auth.conf file
 *
 * The file is apt's auth.conf: netrc-style "machine", "login" and
 * "password" tokens, in any layout across lines.  The credentials it
 * yields go where the userinfo of a source url goes -- src->user and
 * src->password -- so src->url stays clean and download.c remains the
 * one place where credentials meet a request.
 *
 * Nothing read from the file is ever logged: a malformed entry is
 * reported by file and line, never by content, since the token at
 * fault may well be a password.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/authfile.h"
#include "aept/config.h"
#include "aept/msg.h"
#include "aept/util.h"
#include "libfetch/fetch.h"

#define AUTH_SYSTEM_FILE "/etc/aept/auth.conf"
#define AUTH_TARGET_FILE "/RealHome/.aeltra/auth.conf"
#define HTTPS_PREFIX "https://"
#define HTTPS_PREFIX_LEN (sizeof(HTTPS_PREFIX) - 1)

typedef struct {
    char *host; /* lower case, no default port */
    char *path; /* "" or a prefix starting with '/', no trailing '/' */
    char *login;
    char *password;
} auth_entry_t;

typedef struct {
    auth_entry_t *entries;
    int count;
} auth_list_t;

/* The entry being read: tokens may span lines, so it outlives one. */
typedef struct {
    char *machine;
    char *login;
    char *password;
    int line; /* where its "machine" was */
} auth_pending_t;

static void pending_clear(auth_pending_t *p)
{
    free(p->machine);
    free(p->login);
    free(p->password);
    memset(p, 0, sizeof(*p));
}

static void auth_list_free(auth_list_t *list)
{
    int i;

    for (i = 0; i < list->count; i++) {
        free(list->entries[i].host);
        free(list->entries[i].path);
        free(list->entries[i].login);
        free(list->entries[i].password);
    }
    free(list->entries);
    memset(list, 0, sizeof(*list));
}

/* Strip a default https port, which a url may or may not spell out. */
static size_t authority_len(const char *authority, size_t len)
{
    if (len > 4 && memcmp(authority + len - 4, ":443", 4) == 0)
        return len - 4;
    return len;
}

/*
 * Split a "machine" value into host and path prefix.  Returns -1 for
 * anything that names a scheme other than https: apt sends credentials
 * over plain http only when told to, and aept does not do it at all.
 */
static int parse_machine(const char *machine, char **host, char **path)
{
    const char *start = machine, *slash;
    size_t hlen, plen;
    char *h;

    if (strncasecmp(start, HTTPS_PREFIX, HTTPS_PREFIX_LEN) == 0)
        start += HTTPS_PREFIX_LEN;
    else if (strstr(start, "://"))
        return -1;

    slash = strchr(start, '/');
    hlen = slash ? (size_t)(slash - start) : strlen(start);
    hlen = authority_len(start, hlen);
    if (hlen == 0 || hlen > LIBFETCH_URL_HOSTLEN)
        return -1;

    h = aept_malloc(hlen + 1);
    for (size_t i = 0; i < hlen; i++)
        h[i] = (char)tolower((unsigned char)start[i]);
    h[hlen] = '\0';

    plen = slash ? strlen(slash) : 0;
    while (plen > 0 && slash[plen - 1] == '/')
        plen--;

    *host = h;
    *path = aept_malloc(plen + 1);
    if (plen)
        memcpy(*path, slash, plen);
    (*path)[plen] = '\0';
    return 0;
}

/* Turn a complete pending entry into a list entry, or explain why not. */
static void pending_finish(auth_list_t *list, auth_pending_t *p, const char *file)
{
    auth_entry_t *e;
    char *host, *path;

    if (!p->machine)
        goto done;

    if (!p->login || !p->password) {
        aept_log_warning("auth file '%s' line %d: entry lacks a login or a password, ignoring it",
                         file, p->line);
        goto done;
    }
    if (strlen(p->login) > LIBFETCH_URL_USERLEN || strlen(p->password) > LIBFETCH_URL_PWDLEN) {
        aept_log_warning("auth file '%s' line %d: login or password too long, ignoring entry", file,
                         p->line);
        goto done;
    }
    if (parse_machine(p->machine, &host, &path) < 0) {
        aept_log_warning("auth file '%s' line %d: machine is not an https host, ignoring entry",
                         file, p->line);
        goto done;
    }

    list->count++;
    list->entries = aept_realloc(list->entries, list->count * sizeof(auth_entry_t));
    e = &list->entries[list->count - 1];
    e->host = host;
    e->path = path;
    e->login = p->login;
    e->password = p->password;
    p->login = NULL;
    p->password = NULL;

done:
    pending_clear(p);
}

/* Read the entries of an open auth file. */
static void auth_parse(auth_list_t *list, FILE *fp, const char *file)
{
    auth_pending_t p;
    char buf[4096];
    char **expect = NULL; /* the field the next token fills */
    int lineno = 0;

    memset(&p, 0, sizeof(p));

    while (fgets(buf, sizeof(buf), fp)) {
        char *line = buf, *token;

        lineno++;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_log_warning("auth file '%s' line %d: line too long, skipping", file, lineno);
            aept_fgets_drain_line(fp);
            continue;
        }

        while ((token = strsep(&line, " \t\r\n")) != NULL) {
            if (*token == '\0')
                continue;

            /* A comment starts at a token, not anywhere: a password may
             * contain '#'. */
            if (*token == '#' && !expect)
                break;

            if (expect) {
                free(*expect);
                *expect = aept_strdup(token);
                expect = NULL;
            } else if (strcmp(token, "machine") == 0) {
                pending_finish(list, &p, file);
                p.line = lineno;
                expect = &p.machine;
            } else if (strcmp(token, "login") == 0 && p.machine) {
                expect = &p.login;
            } else if (strcmp(token, "password") == 0 && p.machine) {
                expect = &p.password;
            } else {
                aept_log_warning("auth file '%s' line %d: unexpected token, ignoring it", file,
                                 lineno);
            }
        }
    }

    if (expect)
        aept_log_warning("auth file '%s' line %d: missing value at end of file", file, lineno);

    pending_finish(list, &p, file);
}

/*
 * Load an auth file.  A file that will not open is an error when it was
 * asked for explicitly and only a warning when it was found by lookup;
 * either way the caller gets -1 and no entries.
 */
static int auth_load(auth_list_t *list, const char *file, int explicit_file)
{
    struct stat st;
    FILE *fp;

    fp = fopen(file, "r");
    if (!fp) {
        if (explicit_file)
            aept_log_error("cannot open auth file '%s': %s", file, strerror(errno));
        else
            aept_log_warning("cannot open auth file '%s': %s", file, strerror(errno));
        return -1;
    }

    if (fstat(fileno(fp), &st) == 0 && (st.st_mode & 077))
        aept_log_warning("auth file '%s' is accessible by other users", file);

    auth_parse(list, fp, file);
    fclose(fp);
    return 0;
}

/* Whether an entry applies to a url.  Only https urls ever match. */
static int auth_entry_matches(const auth_entry_t *e, const char *url)
{
    const char *authority, *path;
    size_t alen, plen;

    if (strncasecmp(url, HTTPS_PREFIX, HTTPS_PREFIX_LEN) != 0)
        return 0;

    authority = url + HTTPS_PREFIX_LEN;
    path = authority + strcspn(authority, "/?#");
    alen = authority_len(authority, (size_t)(path - authority));

    if (alen != strlen(e->host) || strncasecmp(authority, e->host, alen) != 0)
        return 0;

    plen = strlen(e->path);
    if (plen == 0)
        return 1;
    if (strncmp(path, e->path, plen) != 0)
        return 0;
    return path[plen] == '\0' || path[plen] == '/' || path[plen] == '?' || path[plen] == '#';
}

static const auth_entry_t *auth_find(const auth_list_t *list, const char *url)
{
    int i;

    for (i = 0; i < list->count; i++) {
        if (auth_entry_matches(&list->entries[i], url))
            return &list->entries[i];
    }
    return NULL;
}

/* The file to read: the first that exists of the lookup order in
 * authfile.h, or NULL.  The explicit one is returned as is, so a
 * missing explicit file reaches auth_load() and is reported there. */
static char *auth_file_path(const struct aept_config *cfg, const char *explicit_path)
{
    char *path;

    if (explicit_path)
        return aept_strdup(explicit_path);

    if (geteuid() != 0 && aept_config_is_build_box_target(cfg)) {
        struct passwd *pw = getpwuid(geteuid());

        if (pw && pw->pw_dir && pw->pw_dir[0] == '/') {
            char *rel;

            aept_asprintf(&rel, "%s%s", pw->pw_dir, AUTH_TARGET_FILE);
            path = aept_config_root_path(cfg, rel);
            free(rel);
            if (aept_file_exists(path))
                return path;
            free(path);
        }
    }

    path = aept_config_root_path(cfg, AUTH_SYSTEM_FILE);
    if (aept_file_exists(path))
        return path;
    free(path);
    return NULL;
}

int aept_auth_apply(struct aept_config *cfg, const char *explicit_path)
{
    auth_list_t list;
    char *path;
    int i, r = 0;

    memset(&list, 0, sizeof(list));

    for (i = 0; i < cfg->nsources; i++) {
        aept_source_t *src = &cfg->sources[i];

        if (!src->file_credentials)
            continue;
        free(src->user);
        free(src->password);
        src->user = NULL;
        src->password = NULL;
        src->file_credentials = 0;
    }
    free(cfg->auth_file_used);
    cfg->auth_file_used = NULL;

    path = auth_file_path(cfg, explicit_path);
    if (!path)
        return 0;

    if (auth_load(&list, path, explicit_path != NULL) < 0) {
        r = explicit_path ? -1 : 0;
        goto cleanup;
    }

    for (i = 0; i < cfg->nsources; i++) {
        aept_source_t *src = &cfg->sources[i];
        const auth_entry_t *e;

        if (src->user || src->password)
            continue;

        e = auth_find(&list, src->url);
        if (!e)
            continue;

        src->user = aept_strdup(e->login);
        src->password = aept_strdup(e->password);
        src->file_credentials = 1;
    }

    /* Kept for the log: verbosity is set only after the config loads,
     * so saying it here would reach nobody.  update.c names it. */
    cfg->auth_file_used = path;
    path = NULL;

cleanup:
    auth_list_free(&list);
    free(path);
    return r;
}
