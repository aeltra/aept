/* test_archive_open.c - opening malformed .aeltra containers
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

/* Entries for every directory above `name`, as a package ships them:
 * aept makes no directory it is not told about. */
static void write_parent_dirs(struct archive *a, const char *name)
{
    char *path = aept_strdup(name);
    char *p;

    for (p = strchr(path, '/'); p; p = strchr(p + 1, '/')) {
        struct archive_entry *e = archive_entry_new();

        *p = '\0';
        archive_entry_set_pathname(e, path);
        archive_entry_set_filetype(e, AE_IFDIR);
        archive_entry_set_perm(e, 0755);
        archive_write_header(a, e);
        archive_entry_free(e);
        *p = '/';
    }
    free(path);
}

/* Build a gzip-compressed tar holding one regular file, with its
 * directories. */
static void *make_targz(const char *name, const char *content, size_t *out_len)
{
    struct archive *a = archive_write_new();
    struct archive_entry *e;
    size_t cap = 640 * 1024;
    void *buf = aept_malloc(cap);
    size_t used = 0;

    archive_write_add_filter_gzip(a);
    archive_write_set_format_ustar(a);
    archive_write_open_memory(a, buf, cap, &used);

    write_parent_dirs(a, name);
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

/* Like make_targz, for an entry of any type: a directory, a device
 * node, a FIFO.  Content is written only for a regular file. */
static void *make_targz_typed(const char *name, unsigned int filetype, const char *content,
                              size_t *out_len)
{
    struct archive *a = archive_write_new();
    struct archive_entry *e;
    size_t cap = 64 * 1024;
    void *buf = aept_malloc(cap);
    size_t used = 0;

    archive_write_add_filter_gzip(a);
    archive_write_set_format_ustar(a);
    archive_write_open_memory(a, buf, cap, &used);

    write_parent_dirs(a, name);
    e = archive_entry_new();
    archive_entry_set_pathname(e, name);
    archive_entry_set_filetype(e, filetype);
    archive_entry_set_perm(e, filetype == AE_IFDIR ? 0755 : 0644);
    if (filetype == AE_IFREG)
        archive_entry_set_size(e, (la_int64_t)strlen(content));
    if (filetype == AE_IFCHR || filetype == AE_IFBLK)
        archive_entry_set_rdev(e, 0);
    archive_write_header(a, e);
    if (filetype == AE_IFREG)
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

        path = fixture_path("good.aeltra");
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

        path = fixture_path("badctrl.aeltra");
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

        path = fixture_path("baddata.aeltra");
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

        path = fixture_path("nodata.aeltra");
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

        path = fixture_path("notanar.aeltra");
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

    /* ── a truncated inner tar fails extraction, loudly ──────────── *
     *
     * The stream opens -- gzip needs only its header to be recognised
     * -- so the failure has to be caught during extraction, and it
     * must be reported as one: a partial extraction reported as
     * success would enter the status database as an installed package.
     */
    {
        static char big[256 * 1024];
        size_t big_len;
        void *bigdata;
        char *exdir;
        unsigned int x = 12345;
        size_t i;

        /* Incompressible bytes, so the gzip stream is about as long as
         * the payload and a cut at half lands *inside* the entry data:
         * the header parses, the extraction dies.  Compressible
         * content shrinks to a few dozen bytes and the cut falls
         * before the first header, which only exercises the open. */
        for (i = 0; i < sizeof(big) - 1; i++) {
            x = x * 1103515245 + 12345;
            big[i] = (char)(' ' + (x >> 16) % 90);
        }
        big[sizeof(big) - 1] = '\0';
        bigdata = make_targz("usr/bin/big", big, &big_len);

        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
            {"data.tar.gz",    bigdata, big_len / 2        },
        };

        path = fixture_path("truncdata.aeltra");
        write_ar(path, m, 3);
        exdir = fixture_path("truncdata.d");
        mkdir(exdir, 0755);

        ar = aept_ar_open_pkg_data_archive(path, 1);
        if (ar) {
            test_int_eq(aept_ar_extract_all(ar, exdir, NULL, NULL, NULL, NULL), -1,
                        "extracting a truncated data.tar fails");
            aept_ar_close(ar);
        } else {
            test_ok(1, "a truncated data.tar already fails to open");
        }

        unlink(path);
        free(path);
        path = fixture_path("truncdata.d/usr/bin/big");
        unlink(path);
        free(path);
        path = fixture_path("truncdata.d/usr/bin");
        rmdir(path);
        free(path);
        path = fixture_path("truncdata.d/usr");
        rmdir(path);
        free(path);
        rmdir(exdir);
        free(exdir);
        free(bigdata);
    }

    /* ── a zero-length member is an *empty* archive, by design ───── *
     *
     * archive_read_support_format_empty() is registered deliberately,
     * so a package whose data member holds nothing installs as a
     * package with no files rather than failing.  Pinned here so the
     * choice cannot drift silently.
     */
    {
        char *exdir;
        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
            {"data.tar.gz",    "",      0                  },
        };

        path = fixture_path("emptydata.aeltra");
        write_ar(path, m, 3);
        exdir = fixture_path("emptydata.d");
        mkdir(exdir, 0755);

        ar = aept_ar_open_pkg_data_archive(path, 1);
        test_ok(ar != NULL, "a zero-length data.tar member opens as empty");
        if (ar) {
            test_int_eq(aept_ar_extract_all(ar, exdir, NULL, NULL, NULL, NULL), 0,
                        "and extracts nothing, successfully");
            aept_ar_close(ar);
        }

        rmdir(exdir);
        free(exdir);
        unlink(path);
        free(path);
    }

    /* ── a duplicated member: the first one is the archive ───────── *
     *
     * The ar container has no rule against two members sharing a
     * name.  Pinned down here: the reader takes the first, so bytes
     * appended after it cannot override what the front of the file
     * says -- the same direction every parser in aept leans
     * (clearsign.c splits at the *last* marker for its own reasons,
     * and documents them).
     */
    {
        size_t first_len, second_len;
        void *first = make_targz("usr/bin/t", "first\n", &first_len);
        void *second = make_targz("usr/bin/t", "second\n", &second_len);
        char *exdir, *payload;
        FILE *fp;
        char got[16] = "";

        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
            {"data.tar.gz",    first,   first_len          },
            {"data.tar.gz",    second,  second_len         },
        };

        path = fixture_path("dupdata.aeltra");
        write_ar(path, m, 4);
        exdir = fixture_path("dupdata.d");
        mkdir(exdir, 0755);

        ar = aept_ar_open_pkg_data_archive(path, 1);
        test_ok(ar != NULL, "a container with a duplicated member opens");
        if (ar) {
            test_int_eq(aept_ar_extract_all(ar, exdir, NULL, NULL, NULL, NULL), 0, "and extracts");
            aept_ar_close(ar);
        }
        unlink(path);
        free(path);

        payload = fixture_path("dupdata.d/usr/bin/t");
        fp = fopen(payload, "r");
        if (fp) {
            if (!fgets(got, sizeof(got), fp))
                got[0] = '\0';
            fclose(fp);
        }
        test_str_eq(got, "first\n", "the first member wins, not the last");

        unlink(payload);
        free(payload);
        payload = fixture_path("dupdata.d/usr/bin/t.aept-new");
        test_int_eq(access(payload, F_OK), -1, "no .aept-new is left beside an extracted file");
        free(payload);
        path = fixture_path("dupdata.d/usr/bin");
        rmdir(path);
        free(path);
        path = fixture_path("dupdata.d/usr");
        rmdir(path);
        free(path);
        rmdir(exdir);
        free(exdir);
        free(first);
        free(second);
    }

    /* ── an entry that could not be written is a failure ─────────── *
     *
     * libarchive reports an entry it could not create below a path
     * that is a regular file as ARCHIVE_WARN, the same code it uses
     * for "could not restore the mtime".  Taken as success, the
     * extraction "completes" and the file list records a path with
     * nothing at it.  A warning is only tolerable when the object is
     * actually there.
     */
    {
        size_t len;
        void *nested = make_targz("usr/lib/blocker/inner", "inner\n", &len);
        char *exdir, *sub, *blocker;
        FILE *fp;

        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
            {"data.tar.gz",    nested,  len                },
        };

        path = fixture_path("blocked.aeltra");
        write_ar(path, m, 3);
        exdir = fixture_path("blocked.d");
        mkdir(exdir, 0755);
        sub = fixture_path("blocked.d/usr");
        mkdir(sub, 0755);
        free(sub);
        sub = fixture_path("blocked.d/usr/lib");
        mkdir(sub, 0755);
        blocker = fixture_path("blocked.d/usr/lib/blocker");
        fp = fopen(blocker, "w");
        if (fp) {
            fputs("in the way\n", fp);
            fclose(fp);
        }

        ar = aept_ar_open_pkg_data_archive(path, 1);
        test_ok(ar != NULL, "a data.tar with an entry below a file opens");
        if (ar) {
            test_int_eq(aept_ar_extract_all(ar, exdir, NULL, NULL, NULL, NULL), -1,
                        "extracting an entry below a regular file fails");
            aept_ar_close(ar);
        }

        unlink(path);
        free(path);
        unlink(blocker);
        free(blocker);
        rmdir(sub);
        free(sub);
        sub = fixture_path("blocked.d/usr");
        rmdir(sub);
        free(sub);
        rmdir(exdir);
        free(exdir);
        free(nested);
    }

    /* ── a file where a directory is: refused, directory kept ────── *
     *
     * The mirror image of the case above.  A file is written aside and
     * renamed over its path, and rename() will not replace a directory
     * with a file, so this is an explicit failure now rather than
     * NO_OVERWRITE silently keeping the directory.
     */
    {
        size_t len;
        void *over = make_targz("usr/lib/keep", "file\n", &len);
        char *exdir, *sub, *keep;
        struct stat st;

        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
            {"data.tar.gz",    over,    len                },
        };

        path = fixture_path("overdir.aeltra");
        write_ar(path, m, 3);
        exdir = fixture_path("overdir.d");
        mkdir(exdir, 0755);
        sub = fixture_path("overdir.d/usr");
        mkdir(sub, 0755);
        free(sub);
        sub = fixture_path("overdir.d/usr/lib");
        mkdir(sub, 0755);
        keep = fixture_path("overdir.d/usr/lib/keep");
        mkdir(keep, 0755);

        ar = aept_ar_open_pkg_data_archive(path, 1);
        if (ar) {
            test_int_eq(aept_ar_extract_all(ar, exdir, NULL, NULL, NULL, NULL), -1,
                        "a file over an existing directory is refused");
            aept_ar_close(ar);
        }
        test_ok(lstat(keep, &st) == 0 && S_ISDIR(st.st_mode), "and the directory survives");
        {
            char *aside = fixture_path("overdir.d/usr/lib/keep.aept-new");
            test_int_eq(access(aside, F_OK), -1, "with no .aept-new left beside it");
            free(aside);
        }

        unlink(path);
        free(path);
        rmdir(keep);
        free(keep);
        rmdir(sub);
        free(sub);
        sub = fixture_path("overdir.d/usr");
        rmdir(sub);
        free(sub);
        rmdir(exdir);
        free(exdir);
        free(over);
    }

    /* ── a directory where a file is: refused ────────────────────── *
     *
     * NO_OVERWRITE keeps the file and reports success; the entry after
     * it is what the earlier case caught.  A package shipping only the
     * directory has no such entry, so the directory itself has to be
     * checked once it is "extracted".
     */
    {
        size_t len;
        void *dirent = make_targz_typed("usr/lib/blocker", AE_IFDIR, NULL, &len);
        char *exdir, *sub, *blocker;
        FILE *fp;

        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
            {"data.tar.gz",    dirent,  len                },
        };

        path = fixture_path("dirover.aeltra");
        write_ar(path, m, 3);
        exdir = fixture_path("dirover.d");
        mkdir(exdir, 0755);
        sub = fixture_path("dirover.d/usr");
        mkdir(sub, 0755);
        free(sub);
        sub = fixture_path("dirover.d/usr/lib");
        mkdir(sub, 0755);
        blocker = fixture_path("dirover.d/usr/lib/blocker");
        fp = fopen(blocker, "w");
        if (fp) {
            fputs("in the way\n", fp);
            fclose(fp);
        }

        ar = aept_ar_open_pkg_data_archive(path, 1);
        if (ar) {
            test_int_eq(aept_ar_extract_all(ar, exdir, NULL, NULL, NULL, NULL), -1,
                        "a directory over an existing regular file is refused");
            aept_ar_close(ar);
        }

        unlink(path);
        free(path);
        unlink(blocker);
        free(blocker);
        rmdir(sub);
        free(sub);
        sub = fixture_path("dirover.d/usr");
        rmdir(sub);
        free(sub);
        rmdir(exdir);
        free(exdir);
        free(dirent);
    }

    /* ── a file whose directory the package does not ship ─────────── *
     *
     * A package carries an entry for every directory it writes into;
     * one that does not is not refused, and the directory is made on
     * the way, as libarchive made it.  (It is then in nobody's file
     * list, which is the package's fault and a small one.)
     */
    {
        struct archive *a = archive_write_new();
        struct archive_entry *e;
        size_t cap = 64 * 1024, len = 0;
        void *bare = aept_malloc(cap);
        char *exdir;

        archive_write_add_filter_gzip(a);
        archive_write_set_format_ustar(a);
        archive_write_open_memory(a, bare, cap, &len);
        e = archive_entry_new();
        archive_entry_set_pathname(e, "usr/bin/orphan");
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_entry_set_size(e, 3);
        archive_write_header(a, e);
        archive_write_data(a, "hi\n", 3);
        archive_entry_free(e);
        archive_write_close(a);
        archive_write_free(a);

        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
            {"data.tar.gz",    bare,    len                },
        };

        path = fixture_path("nodirs.aeltra");
        write_ar(path, m, 3);
        exdir = fixture_path("nodirs.d");
        mkdir(exdir, 0755);

        ar = aept_ar_open_pkg_data_archive(path, 1);
        if (ar) {
            test_int_eq(aept_ar_extract_all(ar, exdir, NULL, NULL, NULL, NULL), 0,
                        "a file whose directory the package does not ship still extracts");
            aept_ar_close(ar);
        }
        {
            char *sub = fixture_path("nodirs.d/usr/bin/orphan");
            test_int_eq(access(sub, F_OK), 0, "with the directory made on the way");
            unlink(sub);
            free(sub);
            sub = fixture_path("nodirs.d/usr/bin");
            rmdir(sub);
            free(sub);
            sub = fixture_path("nodirs.d/usr");
            rmdir(sub);
            free(sub);
        }

        unlink(path);
        free(path);
        rmdir(exdir);
        free(exdir);
        free(bare);
    }

    /* ── a device node: refused at listing and at extraction ──────── *
     *
     * Nothing a package ships should be a device, a FIFO or a socket;
     * udev makes the nodes and the other two are runtime objects.  A
     * package carrying one is refused up front -- the listing is what
     * the clash check reads, before any script has run -- and, should
     * one reach extraction anyway, there too.
     */
    {
        size_t len;
        void *dev = make_targz_typed("dev/thing", AE_IFCHR, NULL, &len);
        char *exdir;
        aept_ar_file_list_t fl;

        struct ar_member m[] = {
            {"debian-binary",  deb_bin, sizeof(deb_bin) - 1},
            {"control.tar.gz", ctrl,    ctrl_len           },
            {"data.tar.gz",    dev,     len                },
        };

        path = fixture_path("device.aeltra");
        write_ar(path, m, 3);
        exdir = fixture_path("device.d");
        mkdir(exdir, 0755);

        aept_ar_file_list_init(&fl);
        test_int_eq(aept_ar_list_data_paths(path, 1, &fl), -1,
                    "listing a data.tar with a device node is refused");
        aept_ar_file_list_free(&fl);

        ar = aept_ar_open_pkg_data_archive(path, 1);
        if (ar) {
            test_int_eq(aept_ar_extract_all(ar, exdir, NULL, NULL, NULL, NULL), -1,
                        "and so is extracting it");
            aept_ar_close(ar);
        }

        unlink(path);
        free(path);
        {
            char *sub = fixture_path("device.d/dev/thing");
            unlink(sub);
            free(sub);
            sub = fixture_path("device.d/dev");
            rmdir(sub);
            free(sub);
        }
        rmdir(exdir);
        free(exdir);
        free(dev);
    }

    free(ctrl);
    free(data);
    rmdir(dir);

    return test_summary();
}
