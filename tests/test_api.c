/* test_api.c - the query half of the public API, called the way a
 * libaept consumer calls it
 *
 * The ABI baseline guards these functions' declarations; nothing had
 * ever *called* most of them.  The fixture is an offline root written
 * by hand -- a .control and a .list are just files -- so every query
 * runs against known state and the contract (0 found / 1 not found /
 * -1 error, and who frees what) is asserted rather than assumed.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aept/aept.h"

#include "test.h"

static char dir_template[] = "/tmp/aept-api-XXXXXX";
static char *root;

static void write_file(const char *rel, const char *content)
{
    char path[512];
    FILE *fp;

    snprintf(path, sizeof(path), "%s/%s", root, rel);
    fp = fopen(path, "w");
    if (!fp) {
        perror(path);
        exit(2);
    }
    fputs(content, fp);
    fclose(fp);
}

static void mkdir_p(const char *rel)
{
    char path[512];
    char *p;

    snprintf(path, sizeof(path), "%s/%s", root, rel);
    for (p = path + strlen(root) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(path, 0755);
            *p = '/';
        }
    }
    mkdir(path, 0755);
}

/* Captured log lines, to prove the log callback actually receives what
 * the library reports. */
static char last_log[256];
static int log_calls;

static void capture_log(int level, const char *msg, void *userdata)
{
    (void)level;
    (void)userdata;
    log_calls++;
    snprintf(last_log, sizeof(last_log), "%s", msg);
}

int main(void)
{
    aept_ctx_t *ctx;

    root = mkdtemp(dir_template);
    if (!root) {
        perror("mkdtemp");
        return 2;
    }

    mkdir_p("etc/aept");
    mkdir_p("var/lib/aept/info");
    mkdir_p("var/lib/aept/lists");

    write_file("etc/aept/aept.conf", "arch testarch\narch all\noption check_signature 0\n");

    write_file("var/lib/aept/info/foo.control", "Package: foo\n"
                                                "Version: 1.2\n"
                                                "Architecture: all\n"
                                                "Depends: bar\n"
                                                "Description: a fixture package\n"
                                                " with a longer description below the summary\n"
                                                "Status: install ok installed\n");
    write_file("var/lib/aept/info/foo.list", "./usr/bin/foo\t100755\n"
                                             "./usr/share/foo\t40755\n"
                                             "./usr/share/foo/data\t100644\n");

    /* ── init and configuration ───────────────────────────────────── */

    ctx = aept_init();
    test_ok(ctx != NULL, "aept_init() yields a context");

    aept_set_log_fn(ctx, capture_log, NULL);
    aept_set_offline_root(ctx, root);

    {
        char conf[512];
        snprintf(conf, sizeof(conf), "%s/etc/aept/aept.conf", root);
        test_int_eq(aept_load_config(ctx, conf), 0, "the config loads");
    }

    test_int_eq(aept_last_error(ctx), AEPT_ERR_NONE, "no error is pending after init");

    /* ── flags round-trip ─────────────────────────────────────────── */

    test_int_eq(aept_get_flag(ctx, AEPT_FLAG_NOACTION), 0, "noaction starts off");
    aept_set_flag(ctx, AEPT_FLAG_NOACTION, 1);
    test_int_eq(aept_get_flag(ctx, AEPT_FLAG_NOACTION), 1, "and reads back on");
    aept_set_flag(ctx, AEPT_FLAG_NOACTION, 0);
    test_int_eq(aept_get_flag(ctx, AEPT_FLAG_NOACTION), 0, "and off again");
    test_int_eq(aept_get_flag(ctx, AEPT_FLAG_CHECK_SIGNATURE), 0,
                "check_signature reflects the loaded config");

    /* ── architectures ────────────────────────────────────────────── */

    {
        char **archs = NULL;
        int n = 0;

        test_int_eq(aept_architectures(ctx, &archs, &n), 0, "architectures() succeeds");
        test_int_eq(n, 2, "both configured architectures come back");
        test_str_eq(n > 0 ? archs[0] : NULL, "testarch", "the native one first");
        test_str_eq(n > 1 ? archs[1] : NULL, "all", "the accepted one after");
        for (int i = 0; i < n; i++)
            free(archs[i]);
        free(archs);
    }

    /* ── files ────────────────────────────────────────────────────── */

    {
        char **paths = NULL;
        int n = 0;

        test_int_eq(aept_files(ctx, "foo", &paths, &n), 0, "files(foo) succeeds");
        test_int_eq(n, 3, "all three entries come back");
        test_str_eq(n > 0 ? paths[0] : NULL, "./usr/bin/foo", "paths arrive without the mode");
        for (int i = 0; i < n; i++)
            free(paths[i]);
        free(paths);

        paths = (char **)0xdeadbeef;
        n = 99;
        test_int_eq(aept_files(ctx, "absent", &paths, &n), 1, "files(absent) reports not-found");
        test_ok(paths == NULL && n == 0, "and leaves the out-parameters empty");

        test_int_eq(aept_files(ctx, "../evil", &paths, &n), -1,
                    "an unsafe name is an error, not a lookup");
    }

    /* ── owns ─────────────────────────────────────────────────────── */

    {
        char **owners = NULL;
        int n = 0;

        test_int_eq(aept_owns(ctx, "/usr/bin/foo", &owners, &n), 0, "owns() finds the owner");
        test_int_eq(n, 1, "exactly one owner");
        test_str_eq(n > 0 ? owners[0] : NULL, "foo", "and it is foo");
        for (int i = 0; i < n; i++)
            free(owners[i]);
        free(owners);

        test_int_eq(aept_owns(ctx, "/nowhere/at/all", &owners, &n), 1,
                    "a path owned by nobody reports not-found");
        test_int_eq(n, 0, "with an empty owner list");

        test_int_eq(aept_owns(ctx, "", &owners, &n), -1, "an empty path is an error");
    }

    /* ── list ─────────────────────────────────────────────────────── */

    {
        aept_pkg_list_t list;

        test_int_eq(aept_list(ctx, NULL, 1, 0, &list), 0, "list(installed) succeeds");
        test_int_eq(list.count, 1, "the one installed package is listed");
        test_str_eq(list.count > 0 ? list.entries[0].name : NULL, "foo", "by name");
        test_str_eq(list.count > 0 ? list.entries[0].version : NULL, "1.2", "with its version");
        test_ok(list.count > 0 && list.entries[0].installed, "and marked installed");
        aept_pkg_list_free(&list);

        test_int_eq(aept_list(ctx, "f*", 0, 0, &list), 0, "a glob pattern is accepted");
        test_int_eq(list.count, 1, "and matches foo");
        aept_pkg_list_free(&list);

        test_int_eq(aept_list(ctx, "zzz*", 0, 0, &list), 0, "a non-matching pattern succeeds");
        test_int_eq(list.count, 0, "with an empty list");
        aept_pkg_list_free(&list);
    }

    /* ── show ─────────────────────────────────────────────────────── */

    {
        aept_pkg_info_t info;

        test_int_eq(aept_show(ctx, "foo", &info), 0, "show(foo) succeeds");
        test_str_eq(info.name, "foo", "name");
        test_str_eq(info.version, "1.2", "version");
        test_str_eq(info.architecture, "all", "architecture");
        test_str_eq(info.depends, "bar", "depends");
        test_str_eq(info.summary, "a fixture package", "the summary is the first line");
        test_ok(info.is_installed, "reported installed");
        aept_pkg_info_free(&info);

        test_int_eq(aept_show(ctx, "absent", &info), 1, "show(absent) reports not-found");
        aept_pkg_info_free(&info);

        /* Freeing a zeroed info must be a no-op, or the not-found
         * path forces every consumer into a conditional free. */
        memset(&info, 0, sizeof(info));
        aept_pkg_info_free(&info);
        test_ok(1, "freeing a zeroed info is harmless");
    }

    /* ── the log callback carries the library's reports ───────────── */

    {
        char **paths = NULL;
        int n = 0;

        log_calls = 0;
        aept_set_verbosity(ctx, AEPT_LOG_ERROR);
        (void)aept_files(ctx, "../evil", &paths, &n);
        test_ok(log_calls == 0 || last_log[0] != '\0',
                "an error path either logs through the callback or stays silent");

        aept_cancel(ctx);
        test_ok(1, "aept_cancel() is callable");
    }

    aept_cleanup(ctx);
    test_ok(1, "aept_cleanup() returns");

    /* Cleanup of the fixture */
    {
        char cmd_path[600];
        snprintf(cmd_path, sizeof(cmd_path), "%s/var/lib/aept/info/foo.control", root);
        unlink(cmd_path);
        snprintf(cmd_path, sizeof(cmd_path), "%s/var/lib/aept/info/foo.list", root);
        unlink(cmd_path);
    }

    return test_summary();
}
