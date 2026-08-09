/* test_config_root.c - offline root prefixing of configured paths
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include "aept/internal.h"
#include "aept/config.h"
#include "aept/util.h"

#include "test.h"

#define ROOT "/opt/target"

static void set_path(char **slot, const char *value)
{
    free(*slot);
    *slot = aept_strdup(value);
}

int main(void)
{
    struct aept_config cfg;

    /* ── defaults gain the prefix ────────────────────────────────── */

    aept_config_set_defaults(&cfg);
    cfg.offline_root = aept_strdup(ROOT);
    aept_config_apply_offline_root(&cfg);

    test_str_eq(cfg.info_dir,  ROOT "/var/lib/aept/info",  "info_dir prefixed");
    test_str_eq(cfg.lists_dir, ROOT "/var/lib/aept/lists", "lists_dir prefixed");
    test_str_eq(cfg.cache_dir, ROOT "/var/cache/aept",     "cache_dir prefixed");
    test_str_eq(cfg.lock_file, ROOT "/var/lib/aept/lock",  "lock_file prefixed");
    test_str_eq(cfg.auto_file, ROOT "/var/lib/aept/auto-installed",
                "auto_file prefixed");
    test_str_eq(cfg.pin_file,  ROOT "/var/lib/aept/pinned-packages",
                "pin_file prefixed");

    /*
     * tmp_dir must be prefixed as well.  Control archives are unpacked
     * into it and their maintainer scripts run after chroot()ing into
     * the offline root, so a temp directory on the host is unreachable
     * by the script interpreter and every preinst fails to exec.
     */
    test_str_eq(cfg.tmp_dir, ROOT "/tmp", "tmp_dir prefixed");

    /* The trust store deliberately stays on the host. */
    test_str_eq(cfg.usign_keydir, "/etc/aept/usign/trustdb",
                "usign_keydir not prefixed");

    aept_config_free(&cfg);

    /* ── an explicitly configured tmp_dir is prefixed too ────────── */

    aept_config_set_defaults(&cfg);
    cfg.offline_root = aept_strdup(ROOT);
    set_path(&cfg.tmp_dir, "/var/tmp/aept");
    aept_config_apply_offline_root(&cfg);

    test_str_eq(cfg.tmp_dir, ROOT "/var/tmp/aept",
                "configured tmp_dir prefixed");

    aept_config_free(&cfg);

    /* ── without an offline root nothing is rewritten ────────────── */

    aept_config_set_defaults(&cfg);
    aept_config_apply_offline_root(&cfg);

    test_str_eq(cfg.tmp_dir,   "/tmp",                 "tmp_dir untouched");
    test_str_eq(cfg.info_dir,  "/var/lib/aept/info",   "info_dir untouched");
    test_str_eq(cfg.cache_dir, "/var/cache/aept",      "cache_dir untouched");

    aept_config_free(&cfg);

    return test_summary();
}
