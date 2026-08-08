/* test_script.c - maintainer script execution contract
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

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/script.h"
#include "aept/util.h"

#include "test.h"

static char dir_template[] = "/tmp/aept-test-script-XXXXXX";
static char *dir;
static struct aept_ctx ctx;

/* Failing scripts are expected here and each one logs an error; keep the
 * TAP stream clean by dropping anything at or below AEPT_LOG_ERROR. */
static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

static char *path_in_dir(const char *name)
{
    char *p;
    aept_asprintf(&p, "%s/%s", dir, name);
    return p;
}

static void write_script(const char *name, const char *body)
{
    char *p = path_in_dir(name);
    FILE *fp = fopen(p, "w");

    if (!fp) {
        perror("fopen");
        exit(2);
    }

    fputs(body, fp);
    fclose(fp);
    chmod(p, 0755);
    free(p);
}

static void unlink_in_dir(const char *name)
{
    char *p = path_in_dir(name);
    unlink(p);
    free(p);
}

/* Read the first line of a file written by a script under test. */
static char *read_line(const char *name)
{
    char *p = path_in_dir(name);
    static char buf[256];
    FILE *fp = fopen(p, "r");

    free(p);
    buf[0] = '\0';

    if (!fp)
        return NULL;

    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    buf[strcspn(buf, "\n")] = '\0';
    return buf;
}

int main(void)
{
    dir = mkdtemp(dir_template);
    if (!dir) {
        perror("mkdtemp");
        return 2;
    }

    silence_logging();

    /* A script that is not present is not an error. */
    test_int_eq(aept_run_script(&ctx, dir, NULL, "nosuch", "install", NULL), 0,
                "absent script succeeds");

    /* Success stays 0. */
    write_script("ok", "#!/bin/sh\nexit 0\n");
    test_int_eq(aept_run_script(&ctx, dir, NULL, "ok", "install", NULL), 0,
                "script exiting 0 succeeds");

    /*
     * Failure must be reported as -1.  Returning the script's exit code
     * here is what let aept_op_install() — which classifies results with
     * "r < 0" — treat a failed preinst as neither success nor error, so
     * the package was skipped while aept still exited 0.
     */
    write_script("fail1", "#!/bin/sh\nexit 1\n");
    test_int_eq(aept_run_script(&ctx, dir, NULL, "fail1", "install", NULL), -1,
                "script exiting 1 reports -1, not 1");

    write_script("fail42", "#!/bin/sh\nexit 42\n");
    test_int_eq(aept_run_script(&ctx, dir, NULL, "fail42", "install", NULL), -1,
                "script exiting 42 reports -1, not 42");

    write_script("fail127", "#!/bin/sh\nexec /nonexistent/binary\n");
    test_int_eq(aept_run_script(&ctx, dir, NULL, "fail127", "install", NULL), -1,
                "script failing to exec reports -1");

    /* A script killed by a signal is a failure too. */
    write_script("killed", "#!/bin/sh\nkill -TERM $$\n");
    test_int_eq(aept_run_script(&ctx, dir, NULL, "killed", "install", NULL), -1,
                "script killed by signal reports -1");

    /* Action and version reach the script as $1 and $2. */
    {
        char *body;
        aept_asprintf(&body,
                      "#!/bin/sh\nprintf '%%s %%s\\n' \"$1\" \"$2\" > %s/args\n",
                      dir);
        write_script("args", body);
        free(body);

        test_int_eq(aept_run_script(&ctx, dir, NULL, "args",
                                    "configure", "1.2-3"), 0,
                    "script with action and version succeeds");
        test_str_eq(read_line("args"), "configure 1.2-3",
                    "action and version passed as $1 and $2");
        unlink_in_dir("args");
    }

    /* With a package name the script resolves as <dir>/<pkg>.<script>. */
    write_script("mypkg.postinst", "#!/bin/sh\nexit 3\n");
    test_int_eq(aept_run_script(&ctx, dir, "mypkg", "postinst",
                                "configure", NULL), -1,
                "package-prefixed script resolves and reports -1");

    unlink_in_dir("ok");
    unlink_in_dir("fail1");
    unlink_in_dir("fail42");
    unlink_in_dir("fail127");
    unlink_in_dir("killed");
    unlink_in_dir("args");
    unlink_in_dir("mypkg.postinst");
    rmdir(dir);

    return test_summary();
}
