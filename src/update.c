/* update.c - fetch package lists from repositories
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/archive.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/update.h"
#include "aept/util.h"
#include "aept/verify.h"

static int decompress_gz(const char *gz_path, const char *out_path)
{
    struct aept_ar *ar;
    FILE *fp;
    int r;

    ar = aept_ar_open_compressed_file(gz_path);
    if (!ar)
        return -1;

    fp = fopen(out_path, "w");
    if (!fp) {
        aept_ar_close(ar);
        return -1;
    }

    r = aept_ar_copy_to_stream(ar, fp);

    if (fclose(fp) != 0 && r == 0)
        r = -1;

    aept_ar_close(ar);

    return r;
}

static int is_active_source(const char *name)
{
    int i;

    for (i = 0; i < aept_cfg->nsources; i++) {
        if (strcmp(name, aept_cfg->sources[i].name) == 0)
            return 1;
    }

    return 0;
}

static void prune_stale_lists(void)
{
    DIR *d;
    struct dirent *ent;

    d = opendir(aept_cfg->lists_dir);
    if (!d)
        return;

    while ((ent = readdir(d)) != NULL) {
        char *path = NULL;
        const char *name = ent->d_name;
        char *base;
        char *copy;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        /* Strip .sig suffix to get the source name */
        copy = aept_strdup(name);
        base = copy;

        size_t len = strlen(base);
        if (len > 4 && strcmp(base + len - 4, ".sig") == 0)
            base[len - 4] = '\0';

        if (!is_active_source(base)) {
            aept_asprintf(&path, "%s/%s", aept_cfg->lists_dir, name);
            unlink(path);
            free(path);
        }

        free(copy);
    }

    closedir(d);
}

int aept_op_update(void)
{
    int i;
    int errors = 0;

    aept_file_mkdir_hier(aept_cfg->lists_dir, 0755);

    for (i = 0; i < aept_cfg->nsources; i++) {
        if (strncmp(aept_cfg->sources[i].url, "https://", 8) != 0)
            aept_log_warning("source '%s' uses insecure transport",
                        aept_cfg->sources[i].name);
    }

    for (i = 0; i < aept_cfg->nsources; i++) {
        aept_source_t *src = &aept_cfg->sources[i];
        char *url = NULL;
        char *dest = NULL;
        char *list_path = NULL;
        int r;

        aept_asprintf(&list_path, "%s/%s", aept_cfg->lists_dir, src->name);

        if (src->gzip) {
            char *gz_path = NULL;

            aept_asprintf(&url, "%s/Packages.gz", src->url);
            aept_asprintf(&gz_path, "%s.gz", list_path);

            r = aept_download(url, gz_path, url);
            if (r < 0) {
                errors++;
                goto next;
            }

            r = decompress_gz(gz_path, list_path);
            unlink(gz_path);
            free(gz_path);

            if (r < 0) {
                aept_log_error("failed to decompress Packages.gz for '%s'",
                          src->name);
                errors++;
                goto next;
            }
        } else {
            aept_asprintf(&url, "%s/Packages", src->url);

            r = aept_download(url, list_path, "Packages");
            if (r < 0) {
                errors++;
                goto next;
            }
        }

        if (aept_cfg->check_signature) {
            char *sig_url = NULL;
            char *sig_path = NULL;

            aept_asprintf(&sig_url, "%s/Packages.sig", src->url);
            aept_asprintf(&sig_path, "%s.sig", list_path);

            r = aept_download(sig_url, sig_path, sig_url);
            if (r < 0) {
                aept_log_error("failed to download signature for '%s'",
                          src->name);
                unlink(list_path);
                errors++;
                free(sig_url);
                free(sig_path);
                goto next;
            }

            r = aept_verify_signature(list_path, sig_path);
            if (r < 0) {
                unlink(list_path);
                unlink(sig_path);
                errors++;
                free(sig_url);
                free(sig_path);
                goto next;
            }

            free(sig_url);
            free(sig_path);
        }

        aept_log_info("updated source '%s'", src->name);

    next:
        free(url);
        free(dest);
        free(list_path);
    }

    prune_stale_lists();

    return errors ? -1 : 0;
}
