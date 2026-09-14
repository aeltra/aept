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
 * Every write rewrites the whole file, dropping the name's line and
 * appending the new one -- so a name has one line by construction and
 * there is no second file for a first one to disagree with.
 *
 * A line that does not parse -- a name too long, a third word, a mark
 * word that is not one of the two -- is dropped on the next write and
 * ignored on read, like a damaged pin line: it costs that line and
 * nothing else.
 */
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

aept_mark_t aept_status_get_mark(struct aept_ctx *ctx, const char *name)
{
    FILE *fp;
    char buf[512];

    fp = fopen(ctx->config.marks_file, "r");
    if (!fp)
        return AEPT_MARK_MANUAL;

    while (fgets(buf, sizeof(buf), fp)) {
        char pkg_name[256];
        aept_mark_t mark;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        if (mark_parse(buf, pkg_name, &mark) < 0)
            continue;
        if (strcmp(pkg_name, name) == 0) {
            fclose(fp);
            return mark;
        }
    }

    fclose(fp);
    return AEPT_MARK_MANUAL;
}

/*
 * Rewrite the marks file dropping every line for drop_name and every
 * line carrying drop_mark (MANUAL: none, since no line carries it),
 * then appending "append_name append_word" if given.
 */
static int marks_rewrite(struct aept_ctx *ctx, const char *drop_name, aept_mark_t drop_mark,
                         const char *append_name, const char *append_word)
{
    FILE *fp, *tmp;
    char *tmp_path = NULL;
    char buf[512];

    fp = fopen(ctx->config.marks_file, "r");
    if (!fp) {
        if (errno != ENOENT) {
            aept_log_error("cannot read marks file '%s': %s", ctx->config.marks_file,
                           strerror(errno));
            return -1;
        }
        /* Nothing recorded: nothing to drop, and only something to
         * append makes the file worth creating. */
        if (!append_name)
            return 0;
    }

    aept_asprintf(&tmp_path, "%s.tmp", ctx->config.marks_file);
    tmp = fopen(tmp_path, "w");
    if (!tmp) {
        aept_log_error("cannot write marks file '%s': %s", tmp_path, strerror(errno));
        if (fp)
            fclose(fp);
        free(tmp_path);
        return -1;
    }

    while (fp && fgets(buf, sizeof(buf), fp)) {
        char pkg_name[256];
        aept_mark_t mark;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        if (mark_parse(buf, pkg_name, &mark) < 0)
            continue;
        if (drop_name && strcmp(pkg_name, drop_name) == 0)
            continue;
        if (drop_mark != AEPT_MARK_MANUAL && mark == drop_mark)
            continue;
        fputs(buf, tmp);
    }
    if (fp)
        fclose(fp);

    if (append_name)
        fprintf(tmp, "%s %s\n", append_name, append_word);

    if (ferror(tmp) || fclose(tmp) != 0) {
        aept_log_error("failed to write marks file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    if (rename(tmp_path, ctx->config.marks_file) < 0) {
        aept_log_error("cannot rename marks file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

int aept_status_set_mark(struct aept_ctx *ctx, const char *name, aept_mark_t mark)
{
    const char *word = mark_word(mark);

    return marks_rewrite(ctx, name, AEPT_MARK_MANUAL, word ? name : NULL, word);
}

int aept_status_load_marked(struct aept_ctx *ctx, aept_mark_t mark, aept_fileset_t *set)
{
    FILE *fp;
    char buf[512];

    fp = fopen(ctx->config.marks_file, "r");
    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        char pkg_name[256];
        aept_mark_t m;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        if (mark_parse(buf, pkg_name, &m) == 0 && m == mark)
            aept_fileset_add(set, pkg_name);
    }

    fclose(fp);
    aept_fileset_sort(set);
    return 0;
}

int aept_status_clear_auto(struct aept_ctx *ctx)
{
    return marks_rewrite(ctx, NULL, AEPT_MARK_AUTO, NULL, NULL);
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
