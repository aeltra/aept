/* update.c - fetch package lists from repositories
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/aept.h"
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

    ar = ar_open_compressed_file(gz_path);
    if (!ar)
        return -1;

    fp = fopen(out_path, "w");
    if (!fp) {
        ar_close(ar);
        return -1;
    }

    r = ar_copy_to_stream(ar, fp);

    if (fclose(fp) != 0 && r == 0)
        r = -1;

    ar_close(ar);

    return r;
}

int aept_update(void)
{
    int i;
    int errors = 0;

    file_mkdir_hier(cfg->lists_dir, 0755);

    for (i = 0; i < cfg->nsources; i++) {
        if (strncmp(cfg->sources[i].url, "https://", 8) != 0)
            log_warning("source '%s' uses insecure transport",
                        cfg->sources[i].name);
    }

    for (i = 0; i < cfg->nsources; i++) {
        aept_source_t *src = &cfg->sources[i];
        char *url = NULL;
        char *dest = NULL;
        char *list_path = NULL;
        int r;

        xasprintf(&list_path, "%s/%s", cfg->lists_dir, src->name);

        if (src->gzip) {
            char *gz_path = NULL;

            xasprintf(&url, "%s/Packages.gz", src->url);
            xasprintf(&gz_path, "%s.gz", list_path);

            r = aept_download(url, gz_path, src->name);
            if (r < 0) {
                errors++;
                goto next;
            }

            r = decompress_gz(gz_path, list_path);
            unlink(gz_path);
            free(gz_path);

            if (r < 0) {
                log_error("failed to decompress Packages.gz for '%s'",
                          src->name);
                errors++;
                goto next;
            }
        } else {
            xasprintf(&url, "%s/Packages", src->url);

            r = aept_download(url, list_path, src->name);
            if (r < 0) {
                errors++;
                goto next;
            }
        }

        if (cfg->check_signature) {
            char *sig_url = NULL;
            char *sig_path = NULL;

            xasprintf(&sig_url, "%s/Packages.sig", src->url);
            xasprintf(&sig_path, "%s.sig", list_path);

            r = aept_download(sig_url, sig_path, src->name);
            if (r < 0) {
                log_error("failed to download signature for '%s'",
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

        log_info("updated source '%s'", src->name);

    next:
        free(url);
        free(dest);
        free(list_path);
    }

    return errors ? -1 : 0;
}
