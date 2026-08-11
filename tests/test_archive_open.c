/* test_archive_open.c - opening malformed .aep containers
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/archive.h"
#include "aept/msg.h"
#include "aept/util.h"

#include "test.h"

static char dir_template[] = "/tmp/aept-test-archive-XXXXXX";
static char *dir;
static struct aept_ctx ctx;

/* Malformed containers are expected here and each logs an error. */
static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

/* ── fixture construction ────────────────────────────────────────── */

struct ar_member {
    const char *name;
    const void *data;
    size_t len;
};

/* Write an ar container.  Built by hand so the suite does not depend on
 * binutils being installed. */
static void write_ar(const char *path, const struct ar_member *m, int n)
{
    FILE *fp = fopen(path, "wb");
    int i;

    if (!fp) {
        perror("fopen");
        exit(2);
    }

    fputs("!<arch>\n", fp);

    for (i = 0; i < n; i++) {
        char hdr[61];

        snprintf(hdr, sizeof(hdr), "%-16s%-12u%-6u%-6u%-8o%-10zu`\n", m[i].name, 0u, 0u, 0u, 0644u,
                 m[i].len);
        fwrite(hdr, 1, 60, fp);
        fwrite(m[i].data, 1, m[i].len, fp);
        if (m[i].len & 1)
            fputc('\n', fp);
    }

    fclose(fp);
}

/* Build a gzip-compressed tar holding one regular file. */
static void *make_targz(const char *name, const char *content, size_t *out_len)
{
    struct archive *a = archive_write_new();
    struct archive_entry *e;
    size_t cap = 65536;
    void *buf = aept_malloc(cap);
    size_t used = 0;

    archive_write_add_filter_gzip(a);
    archive_write_set_format_ustar(a);
    archive_write_open_memory(a, buf, cap, &used);

    e = archive_entry_new();
    archive_entry_set_pathname(e, name);
    archive_entry_set_size(e, (la_int64_t)strlen(content));
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_write_header(a, e);
    archive_write_data(a, content, strlen(content));
    archive_entry_free(e);

    archive_write_close(a);
    archive_write_free(a);

    *out_len = used;
    return buf;
}

static char *fixture_path(const char *name)
{
    char *p;
    aept_asprintf(&p, "%s/%s", dir, name);
    return p;
}

int main(void)
{
    static const char deb_bin[] = "2.0\n";
    static const char garbage[] = "this is not a tar, not gzip, not anything libarchive knows";

    size_t ctrl_len, data_len;
    void *ctrl = make_targz("control", "Package: t\n", &ctrl_len);
    void *data = make_targz("usr/bin/t", "payload\n", &data_len);
    struct aept_ar *ar;
    char *path;

    dir = mkdtemp(dir_template);
    if (!dir) {
        perror("mkdtemp");
        return 2;
    }

    silence_logging();

    /* ── positive control ────────────────────────────────────────
     *
     * Without this the whole file could pass simply because every
     * open returns NULL.
     */
    {
        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
            {"data.tar.gz",    data,    data_len           },
        };

        path = fixture_path("good.aep");
        write_ar(path, m, 3);

        ar = aept_ar_open_pkg_control_archive(path);
        test_ok(ar != NULL, "well-formed control archive opens");
        if (ar)
            aept_ar_close(ar);

        ar = aept_ar_open_pkg_data_archive(path, 1);
        test_ok(ar != NULL, "well-formed data archive opens");
        if (ar)
            aept_ar_close(ar);

        unlink(path);
        free(path);
    }

    /*
     * ── malformed members ────────────────────────────────────────
     *
     * archive_read_open() invokes the close callback itself when
     * format detection fails, and that callback frees both the pipe
     * context and the outer reader.  Freeing either of them again on
     * the error path corrupted the heap; aept aborted with "corrupted
     * size vs. prev_size" on any package carrying an unrecognisable
     * control.tar or data.tar.  Each open below must simply fail.
     */
    {
        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", garbage, sizeof(garbage) - 1},
            {"data.tar.gz",    data,    data_len           },
        };

        path = fixture_path("badctrl.aep");
        write_ar(path, m, 3);

        ar = aept_ar_open_pkg_control_archive(path);
        test_ok(ar == NULL, "unrecognisable control.tar fails to open");
        if (ar)
            aept_ar_close(ar);

        unlink(path);
        free(path);
    }

    {
        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
            {"data.tar.gz",    garbage, sizeof(garbage) - 1},
        };

        path = fixture_path("baddata.aep");
        write_ar(path, m, 3);

        ar = aept_ar_open_pkg_data_archive(path, 1);
        test_ok(ar == NULL, "unrecognisable data.tar fails to open");
        if (ar)
            aept_ar_close(ar);

        /* The same container is reached through the file-listing entry
         * point during clash detection. */
        {
            aept_ar_file_list_t fl;
            aept_ar_file_list_init(&fl);
            test_int_eq(aept_ar_list_data_paths(path, 1, &fl), -1,
                        "listing an unrecognisable data.tar fails");
            aept_ar_file_list_free(&fl);
        }

        unlink(path);
        free(path);
    }

    /* ── absent member: the other early-return in open_ipk_tar ───── */
    {
        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
        };

        path = fixture_path("nodata.aep");
        write_ar(path, m, 2);

        ar = aept_ar_open_pkg_data_archive(path, 1);
        test_ok(ar == NULL, "missing data.tar member fails to open");
        if (ar)
            aept_ar_close(ar);

        unlink(path);
        free(path);
    }

    /* ── not an archive at all ───────────────────────────────────── */
    {
        FILE *fp;

        path = fixture_path("notanar.aep");
        fp = fopen(path, "wb");
        fwrite(garbage, 1, sizeof(garbage) - 1, fp);
        fclose(fp);

        ar = aept_ar_open_pkg_control_archive(path);
        test_ok(ar == NULL, "non-archive file fails to open");
        if (ar)
            aept_ar_close(ar);

        unlink(path);
        free(path);
    }

    free(ctrl);
    free(data);
    rmdir(dir);

    return test_summary();
}
