/* status.c - installed package database
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/util.h"

static const char unpacked_status[] = "Status: install ok unpacked";
static const char installed_status[] = "Status: install ok installed";

/* Discard cached entries, leaving the cache empty and clean. */
static void status_cache_reset(aept_status_cache_t *c)
{
    int i;

    for (i = 0; i < c->count; i++) {
        free(c->entries[i].name);
        free(c->entries[i].text);
    }
    free(c->entries);
    c->entries = NULL;
    c->count = 0;
    c->alloc = 0;
    c->loaded = 0;
    c->dirty = 0;
}

void aept_status_cache_free(struct aept_ctx *ctx)
{
    status_cache_reset(&ctx->status_cache);
}

/* Append (name, text) to the cache, taking ownership of both. */
static void cache_append(aept_status_cache_t *c, char *name, char *text)
{
    if (c->count >= c->alloc) {
        c->alloc = c->alloc ? c->alloc * 2 : 256;
        c->entries = aept_realloc(c->entries,
                                  c->alloc * sizeof(*c->entries));
    }
    c->entries[c->count].name = name;
    c->entries[c->count].text = text;
    c->count++;
}

/* Find entry index by package name, or -1. */
static int cache_find(const aept_status_cache_t *c, const char *name)
{
    int i;
    for (i = 0; i < c->count; i++) {
        if (strcmp(c->entries[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* Extract the "Package: <name>" value from a stanza.  Returns a
 * newly-allocated string, or NULL if not found. */
static char *extract_package_name(const char *stanza)
{
    const char *p = stanza;

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);

        if (len >= 9 && strncmp(p, "Package: ", 9) == 0) {
            const char *start = p + 9;
            size_t nlen = len - 9;
            while (nlen > 0 &&
                   (start[nlen - 1] == ' ' || start[nlen - 1] == '\t' ||
                    start[nlen - 1] == '\r'))
                nlen--;
            char *name = aept_malloc(nlen + 1);
            memcpy(name, start, nlen);
            name[nlen] = '\0';
            return name;
        }

        if (!eol)
            break;
        p = eol + 1;
    }
    return NULL;
}

/* Parse a raw status file text into cache entries.  `content` is
 * modified in-place but not freed.  Each stanza is normalized to end
 * in exactly one '\n'. */
static void cache_parse_content(aept_status_cache_t *c, char *content)
{
    char *p = content;

    while (*p) {
        char *end = strstr(p, "\n\n");
        size_t len;

        if (end)
            len = (size_t)(end - p) + 1;   /* keep trailing '\n' */
        else
            len = strlen(p);

        if (len > 0) {
            /* Skip stanzas that are just whitespace */
            size_t i = 0;
            while (i < len && (p[i] == '\n' || p[i] == ' ' || p[i] == '\t'))
                i++;

            if (i < len) {
                int need_nl = (p[len - 1] != '\n');
                char *stanza = aept_malloc(len + (need_nl ? 1 : 0) + 1);
                memcpy(stanza, p, len);
                if (need_nl)
                    stanza[len++] = '\n';
                stanza[len] = '\0';

                char *name = extract_package_name(stanza);
                if (name) {
                    cache_append(c, name, stanza);
                } else {
                    free(stanza);
                }
            }
        }

        if (!end)
            break;
        p = end + 2;
    }
}

/* Read the entire status file into a malloc'd buffer.  Returns NULL
 * on error or if the file does not exist.  *out_len is set to the
 * length (excluding the NUL terminator) on success. */
static char *slurp_status_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;

    struct stat st;
    if (fstat(fileno(fp), &st) < 0) {
        fclose(fp);
        return NULL;
    }

    size_t len = (size_t)st.st_size;
    char *buf = aept_malloc(len + 1);
    size_t got = fread(buf, 1, len, fp);
    fclose(fp);

    if (got != len) {
        free(buf);
        return NULL;
    }

    buf[len] = '\0';
    if (out_len)
        *out_len = len;
    return buf;
}

/* Serialize the cache into a newly-allocated buffer in status-file
 * format.  If normalize_unpacked is non-zero, "Status: install ok
 * unpacked" lines are rewritten to "installed" so libsolv sees every
 * cached package as present.  Caller frees. */
static char *cache_serialize(const aept_status_cache_t *c,
                             int normalize_unpacked, size_t *out_len)
{
    char *buf = NULL;
    size_t buf_size = 0;
    FILE *mem = open_memstream(&buf, &buf_size);
    if (!mem)
        return NULL;

    for (int i = 0; i < c->count; i++) {
        const char *stanza = c->entries[i].text;

        if (normalize_unpacked) {
            const char *p = stanza;
            while (*p) {
                const char *eol = strchr(p, '\n');
                size_t len = eol ? (size_t)(eol - p) : strlen(p);

                if (len == sizeof(unpacked_status) - 1 &&
                        strncmp(p, unpacked_status,
                                sizeof(unpacked_status) - 1) == 0) {
                    fputs(installed_status, mem);
                } else {
                    fwrite(p, 1, len, mem);
                }
                if (!eol)
                    break;
                fputc('\n', mem);
                p = eol + 1;
            }
        } else {
            fputs(stanza, mem);
        }

        /* Blank line separator */
        fputc('\n', mem);
    }

    if (fflush(mem) != 0 || ferror(mem)) {
        fclose(mem);
        free(buf);
        return NULL;
    }
    fclose(mem);

    if (out_len)
        *out_len = buf_size;
    return buf;
}

int aept_status_load(struct aept_ctx *ctx)
{
    if (ctx->status_cache.dirty)
        aept_log_warning("reloading status file with pending cached "
                    "changes — changes will be lost");

    status_cache_reset(&ctx->status_cache);
    ctx->status_cache.loaded = 1;

    if (aept_file_exists(ctx->config.status_file)) {
        size_t content_len = 0;
        char *content = slurp_status_file(ctx->config.status_file,
                                          &content_len);
        if (!content) {
            aept_log_error("cannot read status file '%s': %s",
                      ctx->config.status_file, strerror(errno));
            return -1;
        }
        cache_parse_content(&ctx->status_cache, content);
        free(content);
    }

    /* Feed libsolv a normalized copy. */
    size_t feed_len = 0;
    char *feed = cache_serialize(&ctx->status_cache, 1, &feed_len);
    if (!feed)
        return -1;

    int r = 0;
    if (feed_len > 0) {
        FILE *fp = fmemopen(feed, feed_len, "r");
        if (!fp) {
            free(feed);
            return -1;
        }
        r = aept_solver_load_installed(ctx, fp);
        fclose(fp);
    }
    free(feed);
    return r;
}

int aept_status_add(struct aept_ctx *ctx, const char *control_path,
                    const char *state)
{
    size_t ctrl_len = 0;
    char *ctrl = slurp_status_file(control_path, &ctrl_len);
    if (!ctrl) {
        aept_log_error("cannot read control file '%s': %s",
                  control_path, strerror(errno));
        return -1;
    }

    /* Trim trailing whitespace/newlines so we can re-append exactly
     * one '\n' followed by the Status line. */
    while (ctrl_len > 0 &&
           (ctrl[ctrl_len - 1] == '\n' || ctrl[ctrl_len - 1] == '\r' ||
            ctrl[ctrl_len - 1] == ' '  || ctrl[ctrl_len - 1] == '\t'))
        ctrl_len--;
    ctrl[ctrl_len] = '\0';

    char *name = extract_package_name(ctrl);
    if (!name) {
        aept_log_error("control file '%s' has no Package field",
                  control_path);
        free(ctrl);
        return -1;
    }

    /* Make sure no stale entry for this name survives. */
    int idx = cache_find(&ctx->status_cache, name);
    if (idx >= 0) {
        free(ctx->status_cache.entries[idx].name);
        free(ctx->status_cache.entries[idx].text);
        memmove(&ctx->status_cache.entries[idx],
                &ctx->status_cache.entries[idx + 1],
                (ctx->status_cache.count - idx - 1) *
                    sizeof(*ctx->status_cache.entries));
        ctx->status_cache.count--;
    }

    /* Build the stanza: control fields, Status line, single trailing '\n'. */
    char *stanza = NULL;
    aept_asprintf(&stanza, "%s\nStatus: install ok %s\n", ctrl, state);
    free(ctrl);

    cache_append(&ctx->status_cache, name, stanza);
    ctx->status_cache.dirty = 1;
    return 0;
}

int aept_status_remove(struct aept_ctx *ctx, const char *name)
{
    int idx = cache_find(&ctx->status_cache, name);
    if (idx < 0)
        return 0;

    free(ctx->status_cache.entries[idx].name);
    free(ctx->status_cache.entries[idx].text);
    memmove(&ctx->status_cache.entries[idx],
            &ctx->status_cache.entries[idx + 1],
            (ctx->status_cache.count - idx - 1) *
                sizeof(*ctx->status_cache.entries));
    ctx->status_cache.count--;
    ctx->status_cache.dirty = 1;
    return 0;
}

int aept_status_flush(struct aept_ctx *ctx)
{
    if (!ctx->status_cache.dirty)
        return 0;

    size_t buf_len = 0;
    char *buf = cache_serialize(&ctx->status_cache, 0, &buf_len);
    if (!buf)
        return -1;

    char *tmp_path = NULL;
    aept_asprintf(&tmp_path, "%s.tmp", ctx->config.status_file);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        aept_log_error("cannot open status file '%s': %s",
                  tmp_path, strerror(errno));
        free(tmp_path);
        free(buf);
        return -1;
    }

    if (fwrite(buf, 1, buf_len, fp) != buf_len ||
            ferror(fp) || fclose(fp) != 0) {
        aept_log_error("failed to write status file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        free(buf);
        return -1;
    }

    free(buf);

    if (rename(tmp_path, ctx->config.status_file) < 0) {
        aept_log_error("cannot rename status file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    ctx->status_cache.dirty = 0;
    return 0;
}

int aept_status_mark_auto(struct aept_ctx *ctx, const char *name)
{
    if (aept_status_is_auto(ctx, name))
        return 0;

    FILE *fp = fopen(ctx->config.auto_file, "a");
    if (!fp) {
        aept_log_error("cannot open auto-installed file '%s': %s",
                  ctx->config.auto_file, strerror(errno));
        return -1;
    }

    fprintf(fp, "%s\n", name);

    if (ferror(fp) || fclose(fp) != 0) {
        aept_log_error("failed to write auto-installed file '%s'",
                  ctx->config.auto_file);
        return -1;
    }

    return 0;
}

int aept_status_unmark_auto(struct aept_ctx *ctx, const char *name)
{
    FILE *fp, *tmp;
    char *tmp_path = NULL;
    char buf[256];
    int found = 0;

    fp = fopen(ctx->config.auto_file, "r");
    if (!fp)
        return 0;

    aept_asprintf(&tmp_path, "%s.tmp", ctx->config.auto_file);
    tmp = fopen(tmp_path, "w");
    if (!tmp) {
        fclose(fp);
        free(tmp_path);
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp)) {
        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        char pkg_name[256];
        sscanf(buf, "%255s", pkg_name);
        if (strcmp(pkg_name, name) == 0) {
            found = 1;
            continue;
        }
        fputs(buf, tmp);
    }

    fclose(fp);

    if (!found) {
        fclose(tmp);
        unlink(tmp_path);
        free(tmp_path);
        return 0;
    }

    if (ferror(tmp) || fclose(tmp) != 0) {
        aept_log_error("failed to write auto-installed file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    if (rename(tmp_path, ctx->config.auto_file) < 0) {
        aept_log_error("cannot rename auto-installed file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

int aept_status_is_auto(struct aept_ctx *ctx, const char *name)
{
    FILE *fp;
    char buf[256];

    fp = fopen(ctx->config.auto_file, "r");
    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        char pkg_name[256];
        sscanf(buf, "%255s", pkg_name);
        if (strcmp(pkg_name, name) == 0) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

int aept_status_clear_auto(struct aept_ctx *ctx)
{
    FILE *fp = fopen(ctx->config.auto_file, "w");
    if (!fp) {
        aept_log_error("cannot open auto-installed file '%s': %s",
                  ctx->config.auto_file, strerror(errno));
        return -1;
    }
    fclose(fp);
    return 0;
}

int aept_status_load_auto_set(struct aept_ctx *ctx, aept_fileset_t *set)
{
    FILE *fp;
    char buf[256];

    fp = fopen(ctx->config.auto_file, "r");
    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        char pkg_name[256];
        if (sscanf(buf, "%255s", pkg_name) == 1)
            aept_fileset_add(set, pkg_name);
    }

    fclose(fp);
    aept_fileset_sort(set);
    return 0;
}
