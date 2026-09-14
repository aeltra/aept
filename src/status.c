/* status.c - installed package database
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <dirent.h>
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
static const char triggers_pending_status[] = "Status: install ok triggers-pending";

/* Read a file into a malloc'd NUL-terminated buffer.  Returns NULL on
 * error.  Sets *out_len to the content length on success. */
static char *slurp_file(const char *path, size_t *out_len)
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

int aept_status_load(struct aept_ctx *ctx)
{
    DIR *dir;
    struct dirent *ent;
    char *buf = NULL;
    size_t buf_size = 0;
    FILE *mem;
    int r = 0;

    dir = opendir(ctx->config.info_dir);
    if (!dir)
        return 0;

    mem = open_memstream(&buf, &buf_size);
    if (!mem) {
        closedir(dir);
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".control") != 0)
            continue;

        char *path = NULL;
        aept_asprintf(&path, "%s/%s", ctx->config.info_dir, ent->d_name);

        size_t len = 0;
        char *content = slurp_file(path, &len);
        free(path);

        if (!content)
            continue;

        /* Write the control stanza to the feed, normalizing
         * "unpacked" to "installed" for libsolv and adding a
         * default Status line if the file lacks one (pre-migration
         * .control files written by an older aept). */
        const char *p = content;
        int found_status = 0;

        while (*p) {
            const char *eol = strchr(p, '\n');
            size_t llen = eol ? (size_t)(eol - p) : strlen(p);

            if ((llen == sizeof(unpacked_status) - 1 &&
                 strncmp(p, unpacked_status, sizeof(unpacked_status) - 1) == 0) ||
                (llen == sizeof(triggers_pending_status) - 1 &&
                 strncmp(p, triggers_pending_status, sizeof(triggers_pending_status) - 1) == 0)) {
                /* Both states describe a package whose files are on
                 * disk, and the solver (clash detection above all)
                 * must see it as present. */
                fputs(installed_status, mem);
                found_status = 1;
            } else {
                fwrite(p, 1, llen, mem);
                if (llen >= 7 && strncmp(p, "Status:", 7) == 0)
                    found_status = 1;
            }

            if (!eol) {
                fputc('\n', mem);
                break;
            }
            fputc('\n', mem);
            p = eol + 1;
        }

        if (!found_status)
            fprintf(mem, "%s\n", installed_status);

        /* Blank-line stanza separator */
        fputc('\n', mem);

        free(content);
    }

    closedir(dir);

    if (fflush(mem) != 0 || ferror(mem)) {
        fclose(mem);
        free(buf);
        return -1;
    }
    fclose(mem);

    if (buf_size > 0) {
        FILE *fp = fmemopen(buf, buf_size, "r");
        if (!fp) {
            free(buf);
            return -1;
        }
        r = aept_solver_load_installed(ctx, fp);
        fclose(fp);
    }
    free(buf);
    return r;
}

int aept_status_add(struct aept_ctx *ctx, const char *control_src, const char *dest_path,
                    const char *state)
{
    (void)ctx;

    size_t ctrl_len = 0;
    char *ctrl = slurp_file(control_src, &ctrl_len);
    if (!ctrl) {
        aept_log_error("cannot read control file '%s': %s", control_src, strerror(errno));
        return -1;
    }

    /* Trim trailing whitespace so the Status line lands cleanly. */
    while (ctrl_len > 0 && (ctrl[ctrl_len - 1] == '\n' || ctrl[ctrl_len - 1] == '\r' ||
                            ctrl[ctrl_len - 1] == ' ' || ctrl[ctrl_len - 1] == '\t'))
        ctrl_len--;
    ctrl[ctrl_len] = '\0';

    char *stanza = NULL;
    aept_asprintf(&stanza, "%s\nStatus: install ok %s\n", ctrl, state);
    free(ctrl);

    char *tmp_path = NULL;
    aept_asprintf(&tmp_path, "%s.tmp", dest_path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        aept_log_error("cannot write '%s': %s", tmp_path, strerror(errno));
        free(tmp_path);
        free(stanza);
        return -1;
    }

    int r = -1;
    if (fputs(stanza, fp) == EOF || ferror(fp) || fclose(fp) != 0) {
        aept_log_error("failed to write '%s'", tmp_path);
        unlink(tmp_path);
    } else if (rename(tmp_path, dest_path) < 0) {
        aept_log_error("cannot rename '%s': %s", tmp_path, strerror(errno));
        unlink(tmp_path);
    } else {
        r = 0;
    }

    free(tmp_path);
    free(stanza);
    return r;
}

/*
 * The marks file: one "name mark" line per package that is auto or
 * protected.  Manual is the absence of a line, so a fresh root has no
 * file and an installed package nobody has marked reads as manual.
 *
 * It is read once per API call into ctx->marks and every lookup is
 * answered from there; every write keeps the copy in step, so a
 * transaction that marks each of its N packages as it goes reads the
 * file once rather than N times.  The copy is safe because the
 * transaction lock keeps anything else from editing the file under
 * it, and it is dropped on entry to the next API call (AEPT_OOM_ENTER)
 * so no call trusts what an earlier one saw.
 *
 * A line that does not parse -- a name too long, a third word, a mark
 * word that is not one of the two -- is ignored on read and dropped
 * by the next rewrite, like a damaged pin line: it costs that line and
 * nothing else.  A duplicated name reads by its first line.
 */
struct aept_mark_entry {
    char *name;
    aept_mark_t mark;
};

struct aept_marks {
    struct aept_mark_entry *e;
    int count, alloc;
};

static const char *mark_word(aept_mark_t mark)
{
    switch (mark) {
    case AEPT_MARK_AUTO:
        return "auto";
    case AEPT_MARK_PROTECTED:
        return "protected";
    case AEPT_MARK_MANUAL:
        break;
    }
    return NULL;
}

/* Parse one line into name and mark.  Returns 0 on a well-formed
 * line, -1 on one to ignore. */
static int mark_parse(const char *line, char name[256], aept_mark_t *mark)
{
    char word[16], extra[2];
    int n = sscanf(line, "%255s %15s %1s", name, word, extra);

    if (n != 2)
        return -1;
    if (strcmp(word, "auto") == 0)
        *mark = AEPT_MARK_AUTO;
    else if (strcmp(word, "protected") == 0)
        *mark = AEPT_MARK_PROTECTED;
    else
        return -1;
    return 0;
}

void aept_marks_reset(struct aept_ctx *ctx)
{
    struct aept_marks *m = ctx->marks;
    int i;

    if (!m)
        return;
    for (i = 0; i < m->count; i++)
        free(m->e[i].name);
    free(m->e);
    free(m);
    ctx->marks = NULL;
}

static int marks_find(struct aept_marks *m, const char *name)
{
    int i;

    for (i = 0; i < m->count; i++)
        if (strcmp(m->e[i].name, name) == 0)
            return i;
    return -1;
}

static void marks_add(struct aept_marks *m, const char *name, aept_mark_t mark)
{
    if (m->count >= m->alloc) {
        m->alloc = m->alloc ? m->alloc * 2 : 64;
        m->e = aept_realloc(m->e, m->alloc * sizeof(*m->e));
    }
    m->e[m->count].name = aept_strdup(name);
    m->e[m->count].mark = mark;
    m->count++;
}

static struct aept_marks *marks_load(struct aept_ctx *ctx)
{
    struct aept_marks *m;
    FILE *fp;
    char buf[512];

    if (ctx->marks)
        return ctx->marks;

    m = aept_malloc(sizeof(*m));
    memset(m, 0, sizeof(*m));
    ctx->marks = m;

    fp = fopen(ctx->config.marks_file, "r");
    if (!fp)
        return m;

    while (fgets(buf, sizeof(buf), fp)) {
        char name[256];
        aept_mark_t mark;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        if (mark_parse(buf, name, &mark) < 0 || marks_find(m, name) >= 0)
            continue;
        marks_add(m, name, mark);
    }

    fclose(fp);
    return m;
}

/* Write the whole file from the copy, aside and renamed.  On failure
 * the copy is dropped, so the next use re-reads what is really there. */
static int marks_save(struct aept_ctx *ctx)
{
    struct aept_marks *m = ctx->marks;
    char *tmp_path = NULL;
    FILE *tmp;
    int i, failed = 0;

    aept_asprintf(&tmp_path, "%s.tmp", ctx->config.marks_file);
    tmp = fopen(tmp_path, "w");
    if (!tmp) {
        aept_log_error("cannot write marks file '%s': %s", tmp_path, strerror(errno));
        free(tmp_path);
        aept_marks_reset(ctx);
        return -1;
    }

    for (i = 0; i < m->count; i++)
        fprintf(tmp, "%s %s\n", m->e[i].name, mark_word(m->e[i].mark));

    if (ferror(tmp) || fclose(tmp) != 0) {
        aept_log_error("failed to write marks file '%s'", tmp_path);
        failed = 1;
    } else if (rename(tmp_path, ctx->config.marks_file) < 0) {
        aept_log_error("cannot rename marks file: %s", strerror(errno));
        failed = 1;
    }

    if (failed) {
        unlink(tmp_path);
        aept_marks_reset(ctx);
    }
    free(tmp_path);
    return failed ? -1 : 0;
}

/* Add one line for a name the file has no line for: O(1), against the
 * O(n) of a rewrite.  A hand-edited file may lack its final newline;
 * appending to that would fuse the new line onto the last one and cost
 * both, so one is supplied first. */
static int marks_append(struct aept_ctx *ctx, const char *name, aept_mark_t mark)
{
    FILE *fp = fopen(ctx->config.marks_file, "a+");

    if (!fp) {
        aept_log_error("cannot open marks file '%s': %s", ctx->config.marks_file, strerror(errno));
        return -1;
    }
    if (fseek(fp, -1, SEEK_END) == 0 && fgetc(fp) != '\n')
        fputc('\n', fp);
    fprintf(fp, "%s %s\n", name, mark_word(mark));
    if (ferror(fp) || fclose(fp) != 0) {
        aept_log_error("failed to write marks file '%s'", ctx->config.marks_file);
        aept_marks_reset(ctx);
        return -1;
    }
    marks_add(ctx->marks, name, mark);
    return 0;
}

aept_mark_t aept_status_get_mark(struct aept_ctx *ctx, const char *name)
{
    struct aept_marks *m = marks_load(ctx);
    int i = marks_find(m, name);

    return i < 0 ? AEPT_MARK_MANUAL : m->e[i].mark;
}

int aept_status_set_mark(struct aept_ctx *ctx, const char *name, aept_mark_t mark)
{
    struct aept_marks *m = marks_load(ctx);
    int i = marks_find(m, name);

    /* Only a change of mark rewrites the file: a mark already held and
     * manual on a name with no line (every removal asks for that) cost
     * nothing, and a new mark for a name with no line is an append. */
    if (i < 0)
        return mark == AEPT_MARK_MANUAL ? 0 : marks_append(ctx, name, mark);
    if (m->e[i].mark == mark)
        return 0;

    if (mark == AEPT_MARK_MANUAL) {
        free(m->e[i].name);
        memmove(&m->e[i], &m->e[i + 1], (m->count - i - 1) * sizeof(*m->e));
        m->count--;
    } else {
        m->e[i].mark = mark;
    }
    return marks_save(ctx);
}

int aept_status_load_marked(struct aept_ctx *ctx, aept_mark_t mark, aept_fileset_t *set)
{
    struct aept_marks *m = marks_load(ctx);
    int i;

    for (i = 0; i < m->count; i++)
        if (m->e[i].mark == mark)
            aept_fileset_add(set, m->e[i].name);
    aept_fileset_sort(set);
    return 0;
}

int aept_status_clear_auto(struct aept_ctx *ctx)
{
    struct aept_marks *m = marks_load(ctx);
    int i, kept = 0;

    for (i = 0; i < m->count; i++) {
        if (m->e[i].mark == AEPT_MARK_AUTO)
            free(m->e[i].name);
        else
            m->e[kept++] = m->e[i];
    }
    if (kept == m->count)
        return 0;
    m->count = kept;
    return marks_save(ctx);
}

/*
 * The current state word of a package's Status line -- "installed",
 * "unpacked", "triggers-pending" -- copied into buf.  Returns 0 when a
 * Status line was found, -1 otherwise (no .control, no Status line).
 */
int aept_status_get_state(struct aept_ctx *ctx, const char *name, char *buf, size_t buflen)
{
    char *path = NULL;
    FILE *fp;
    char line[1024];
    int r = -1;

    aept_asprintf(&path, "%s/%s.control", ctx->config.info_dir, name);
    fp = fopen(path, "r");
    free(path);

    if (!fp)
        return -1;

    while (fgets(line, sizeof(line), fp)) {
        if (aept_fgets_is_truncated(line, sizeof(line))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        if (strncmp(line, "Status:", 7) != 0)
            continue;

        /* "Status: install ok <state>" -- the state is the third word.
         * The newline goes first, or the scan below never ends: it is
         * neither a separator nor part of a word. */
        line[strcspn(line, "\n")] = '\0';
        const char *p = line + 7;
        int word = 0;
        while (*p) {
            while (*p == ' ' || *p == '\t')
                p++;
            if (!*p)
                break;
            const char *start = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (++word == 3) {
                size_t len = (size_t)(p - start);
                if (len >= buflen)
                    len = buflen - 1;
                memcpy(buf, start, len);
                buf[len] = '\0';
                r = 0;
            }
        }
        break;
    }

    fclose(fp);
    return r;
}

/*
 * Rewrite a package's Status line to the given state, leaving the rest
 * of the .control untouched.  Written aside and renamed, like every
 * other file whose half-written form would be read back as the truth.
 */
int aept_status_set_state(struct aept_ctx *ctx, const char *name, const char *state)
{
    char *path = NULL;
    char *tmp_path = NULL;
    FILE *in, *out;
    char line[4096];
    int found = 0;
    int r = -1;

    aept_asprintf(&path, "%s/%s.control", ctx->config.info_dir, name);
    in = fopen(path, "r");
    if (!in) {
        free(path);
        return -1;
    }

    aept_asprintf(&tmp_path, "%s.tmp", path);
    out = fopen(tmp_path, "w");
    if (!out) {
        fclose(in);
        free(path);
        free(tmp_path);
        return -1;
    }

    while (fgets(line, sizeof(line), in)) {
        if (!found && strncmp(line, "Status:", 7) == 0) {
            fprintf(out, "Status: install ok %s\n", state);
            found = 1;
        } else {
            fputs(line, out);
        }
    }
    if (!found)
        fprintf(out, "Status: install ok %s\n", state);

    fclose(in);

    if (ferror(out) || fclose(out) != 0) {
        aept_log_error("failed to write '%s'", tmp_path);
        unlink(tmp_path);
    } else if (rename(tmp_path, path) < 0) {
        aept_log_error("cannot rename '%s': %s", tmp_path, strerror(errno));
        unlink(tmp_path);
    } else {
        r = 0;
    }

    free(path);
    free(tmp_path);
    return r;
}
