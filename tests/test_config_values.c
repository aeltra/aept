/* test_config_values.c - option parsing on hostile values
 *
 * config.c parses user input; a bad value must land on the documented
 * default with a warning, never on garbage.  parse_seconds() had never
 * been entered by a test.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/config.h"
#include "aept/msg.h"
#include "aept/util.h"

#include "test.h"

static struct aept_ctx ctx;
static char path[] = "/tmp/aept-conf-XXXXXX";

/* Bad values are expected and each logs a warning. */
static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

static int load_timeout(const char *value)
{
    struct aept_config cfg;
    FILE *fp = fopen(path, "w");
    int r;

    if (!fp) {
        perror(path);
        exit(2);
    }
    fprintf(fp, "option network_timeout %s\n", value);
    fclose(fp);

    aept_config_set_defaults(&cfg);
    if (aept_config_load(&cfg, path) < 0) {
        aept_config_free(&cfg);
        return -1;
    }
    r = cfg.network_timeout;
    aept_config_free(&cfg);
    return r;
}

int main(void)
{
    int fd = mkstemp(path);

    if (fd < 0) {
        perror("mkstemp");
        return 2;
    }
    close(fd);

    silence_logging();

    /* ── network_timeout via parse_seconds ────────────────────────── */

    test_int_eq(load_timeout("60"), 60, "a plain number is taken");
    test_int_eq(load_timeout("0"), 0, "0 (wait forever) is a valid value");
    test_int_eq(load_timeout("86400"), 86400, "the upper bound is inclusive");

    test_int_eq(load_timeout("86401"), 120, "beyond a day falls back to the default");
    test_int_eq(load_timeout("-5"), 120, "a negative falls back");
    test_int_eq(load_timeout("120s"), 120, "a suffix is rejected, not truncated");
    test_int_eq(load_timeout("abc"), 120, "garbage falls back");
    test_int_eq(load_timeout("999999999999999999999"), 120, "an overflow falls back");
    test_int_eq(load_timeout(""), 120, "an empty value falls back");

    unlink(path);
    return test_summary();
}
