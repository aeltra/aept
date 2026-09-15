/* listfile.h - the per-package file list, {info_dir}/{name}.list
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef LISTFILE_H_7BF97F
#define LISTFILE_H_7BF97F

#include <stdio.h>

/*
 * One line per object the package put on disk, tab-separated:
 *
 *   <path> <mode> <uid> <gid> <size> <sha256> [<link-target>]
 *
 * path is as the archive spelled it ("./usr/bin/foo"); mode is octal
 * with the type bits; uid and gid are what the object has on disk;
 * size and sha256 are recorded for regular files and "-" for anything
 * else; the link target is present for a symlink only.
 *
 * Two shorter forms are read as well, since a list written before a
 * column existed is still the truth about what is on disk:
 * "<path> <mode>" and "<path> <mode> <link-target>".  A column that
 * is not there reads as unknown, not as zero.
 *
 * Every reader goes through here, so that a column can be added once.
 */
typedef struct {
    const char *path;     /* as written */
    const char *stripped; /* without leading "./" or "/"; may be "" */
    unsigned int mode;    /* 0 when not recorded */
    long uid, gid;        /* -1 when not recorded */
    long long size;       /* -1 when not recorded or not a regular file */
    const char *sha256;   /* NULL when not recorded or not a regular file */
    const char *link;     /* NULL unless a symlink with its target recorded */
    const char *raw;      /* the whole line, newline included, for a rewrite */
} aept_list_entry_t;

typedef struct {
    FILE *fp;
    char line[4096];
    char fields[4096];
    aept_list_entry_t entry;
} aept_list_t;

/* Open a list for reading; -1 with errno set if it cannot be opened. */
int aept_list_open(aept_list_t *l, const char *path);

/* The next entry, into l->entry: 1 when there is one, 0 at the end.
 * An over-long line is skipped whole, a blank one too. */
int aept_list_next(aept_list_t *l);

void aept_list_close(aept_list_t *l);

/* Write one line.  A negative uid, gid or size and a NULL sha256
 * write "-"; a NULL link writes no seventh column.  Returns 0, or -1
 * on a write error. */
int aept_list_write_line(FILE *fp, const char *path, unsigned int mode, long uid, long gid,
                         long long size, const char *sha256, const char *link);

#endif
