/* integrity.c - what is on disk against what the packages recorded
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>

#include "aept/internal.h"
#include "aept/config.h"
#include "aept/conffile.h"
#include "aept/integrity.h"
#include "aept/listfile.h"
#include "aept/msg.h"
#include "aept/util.h"

/* The hex SHA-256 of a file's content, or -1 if it cannot be read. */
static int sha256_file(const char *path, char hex[65])
{
    unsigned char d[EVP_MAX_MD_SIZE];
    unsigned int dlen = 0, i;
    char buf[65536];
    size_t n;
    FILE *fp;
    EVP_MD_CTX *md;

    fp = fopen(path, "rb");
    if (!fp)
        return -1;
    md = EVP_MD_CTX_new();
    if (!md || EVP_DigestInit_ex(md, EVP_sha256(), NULL) != 1) {
        fclose(fp);
        if (md)
            EVP_MD_CTX_free(md);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        EVP_DigestUpdate(md, buf, n);
    if (ferror(fp)) {
        fclose(fp);
        EVP_MD_CTX_free(md);
        return -1;
    }
    fclose(fp);
    EVP_DigestFinal_ex(md, d, &dlen);
    EVP_MD_CTX_free(md);
    for (i = 0; i < dlen && i < 32; i++)
        sprintf(hex + 2 * i, "%02x", d[i]);
    hex[64] = '\0';
    return 0;
}

static void add(aept_verify_list_t *out, const char *package, const char *path, int kind,
                const char *expected, const char *found)
{
    aept_verify_entry_t *e;

    if (out->count >= out->alloc) {
        out->alloc = out->alloc ? out->alloc * 2 : 32;
        out->entries = aept_realloc(out->entries, out->alloc * sizeof(*out->entries));
    }
    e = &out->entries[out->count++];
    e->package = aept_strdup(package);
    e->path = aept_strdup(path);
    e->kind = kind;
    e->expected = expected ? aept_strdup(expected) : NULL;
    e->found = found ? aept_strdup(found) : NULL;
}

static const char *type_word(mode_t mode)
{
    switch (mode & S_IFMT) {
    case S_IFREG:
        return "regular file";
    case S_IFDIR:
        return "directory";
    case S_IFLNK:
        return "symlink";
    default:
        return "special file";
    }
}

/*
 * One package.  Every line of its .list is held against the object at
 * that path.  What is recorded decides what is checked: a column that
 * was not recorded -- an older list -- is not held against anything,
 * and a regular file with no digest at all is reported as unverifiable
 * rather than passed.  A conffile is the admin's to change, so its
 * content is compared with the hash the conffile record keeps and a
 * difference is reported as such, not as damage.
 */
static int verify_package(struct aept_ctx *ctx, const char *name, aept_verify_list_t *out)
{
    char *list_path = NULL;
    aept_list_t l;
    aept_conffile_set_t cf;
    int r;

    aept_asprintf(&list_path, "%s/%s.list", ctx->config.info_dir, name);
    r = aept_list_open(&l, list_path);
    free(list_path);
    if (r < 0)
        return 1;

    aept_conffile_set_init(&cf);
    aept_conffile_load(ctx, name, &cf);

    while (aept_list_next(&l)) {
        const aept_list_entry_t *e = &l.entry;
        char *abs_path, *disk_path;
        struct stat st;
        char expected[128], found[128];

        if (e->stripped[0] == '\0')
            continue;

        aept_asprintf(&abs_path, "/%s", e->stripped);
        disk_path = aept_config_root_path(&ctx->config, abs_path);

        if (lstat(disk_path, &st) != 0) {
            add(out, name, abs_path, AEPT_VERIFY_MISSING, e->mode ? type_word(e->mode) : NULL,
                NULL);
            goto next;
        }

        if (e->mode && (st.st_mode & S_IFMT) != (e->mode & S_IFMT)) {
            add(out, name, abs_path, AEPT_VERIFY_TYPE, type_word(e->mode), type_word(st.st_mode));
            goto next;
        }

        /* A directory with something mounted on it is another
         * filesystem's root, not the package's directory: on a live
         * root /proc and /sys are kernel-owned 0555 whatever base-files
         * shipped.  Nothing of it is the package's to check. */
        if (S_ISDIR(st.st_mode)) {
            char *parent = aept_strdup(disk_path);
            size_t plen = strlen(parent);
            char *slash;
            struct stat pst;
            int mounted = 0;

            /* Directory entries are recorded with a trailing slash. */
            while (plen > 1 && parent[plen - 1] == '/')
                parent[--plen] = '\0';
            slash = strrchr(parent, '/');
            if (slash && slash != parent) {
                *slash = '\0';
                mounted = lstat(parent, &pst) == 0 && pst.st_dev != st.st_dev;
            }
            free(parent);
            if (mounted)
                goto next;
        }

        if (e->mode && !S_ISLNK(st.st_mode) && (st.st_mode & 07777) != (e->mode & 07777)) {
            snprintf(expected, sizeof(expected), "%04o", e->mode & 07777);
            snprintf(found, sizeof(found), "%04o", st.st_mode & 07777);
            add(out, name, abs_path, AEPT_VERIFY_MODE, expected, found);
        }

        if (e->uid >= 0 && !ctx->config.ignore_ownership &&
            ((long)st.st_uid != e->uid || (long)st.st_gid != e->gid)) {
            snprintf(expected, sizeof(expected), "%ld:%ld", e->uid, e->gid);
            snprintf(found, sizeof(found), "%ld:%ld", (long)st.st_uid, (long)st.st_gid);
            add(out, name, abs_path, AEPT_VERIFY_OWNER, expected, found);
        }

        if (S_ISLNK(st.st_mode)) {
            if (e->link) {
                char target[4096];
                ssize_t n = readlink(disk_path, target, sizeof(target) - 1);

                if (n < 0)
                    n = 0;
                target[n] = '\0';
                if (strcmp(target, e->link) != 0)
                    add(out, name, abs_path, AEPT_VERIFY_LINK, e->link, target);
            }
            goto next;
        }

        if (!S_ISREG(st.st_mode))
            goto next;

        {
            const char *cf_md5 = aept_conffile_set_lookup(&cf, abs_path);

            if (cf_md5) {
                char *cur = aept_conffile_md5(disk_path);

                if (cur && strcmp(cur, cf_md5) != 0)
                    add(out, name, abs_path, AEPT_VERIFY_CONFFILE, NULL, NULL);
                free(cur);
                goto next;
            }
        }

        if (e->size >= 0 && (long long)st.st_size != e->size) {
            snprintf(expected, sizeof(expected), "%lld", e->size);
            snprintf(found, sizeof(found), "%lld", (long long)st.st_size);
            add(out, name, abs_path, AEPT_VERIFY_SIZE, expected, found);
            goto next;
        }

        if (e->sha256) {
            char hex[65];

            if (sha256_file(disk_path, hex) < 0)
                add(out, name, abs_path, AEPT_VERIFY_DIGEST, e->sha256, "unreadable");
            else if (strcmp(hex, e->sha256) != 0)
                add(out, name, abs_path, AEPT_VERIFY_DIGEST, e->sha256, hex);
        } else if (e->size < 0) {
            add(out, name, abs_path, AEPT_VERIFY_UNVERIFIABLE, NULL, NULL);
        }

    next:
        free(disk_path);
        free(abs_path);
    }

    aept_list_close(&l);
    aept_conffile_set_free(&cf);
    return 0;
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int aept_op_verify(struct aept_ctx *ctx, const char **names, int count, aept_verify_list_t *out)
{
    char **all = NULL;
    int nall = 0, alloc = 0, i, r = 0;

    memset(out, 0, sizeof(*out));

    if (count == 0) {
        DIR *dir = opendir(ctx->config.info_dir);
        struct dirent *ent;

        if (!dir)
            return 0; /* nothing installed */
        while ((ent = readdir(dir)) != NULL) {
            const char *dot = strrchr(ent->d_name, '.');
            size_t len;

            if (!dot || strcmp(dot, ".list") != 0)
                continue;
            len = (size_t)(dot - ent->d_name);
            if (nall >= alloc) {
                alloc = alloc ? alloc * 2 : 64;
                all = aept_realloc(all, alloc * sizeof(*all));
            }
            all[nall] = aept_malloc(len + 1);
            memcpy(all[nall], ent->d_name, len);
            all[nall][len] = '\0';
            nall++;
        }
        closedir(dir);
        qsort(all, nall, sizeof(*all), name_cmp);
        names = (const char **)all;
        count = nall;
    }

    for (i = 0; i < count; i++) {
        if (!aept_pkg_name_is_safe(names[i])) {
            aept_log_error("refusing to verify package with unsafe name '%s'", names[i]);
            r = -1;
            break;
        }
        if (verify_package(ctx, names[i], out) != 0) {
            aept_log_error("package '%s' is not installed", names[i]);
            r = 1;
            break;
        }
    }

    for (i = 0; i < nall; i++)
        free(all[i]);
    free(all);
    return r;
}
