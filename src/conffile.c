/* conffile.c - conffile tracking and conflict resolution
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <solv/chksum.h>
#include <solv/knownid.h>

#include "aept/aept.h"
#include "aept/conffile.h"
#include "aept/config.h"
#include "aept/msg.h"
#include "aept/util.h"

void conffile_set_init(aept_conffile_set_t *cs)
{
    cs->entries = NULL;
    cs->count = 0;
    cs->alloc = 0;
}

void conffile_set_add(aept_conffile_set_t *cs, const char *path,
                      const char *md5)
{
    if (cs->count >= cs->alloc) {
        cs->alloc = cs->alloc ? cs->alloc * 2 : 8;
        cs->entries = xrealloc(cs->entries,
                               cs->alloc * sizeof(aept_conffile_t));
    }

    cs->entries[cs->count].path = xstrdup(path);
    cs->entries[cs->count].md5 = md5 ? xstrdup(md5) : NULL;
    cs->count++;
}

const char *conffile_set_lookup(const aept_conffile_set_t *cs,
                                const char *path)
{
    for (int i = 0; i < cs->count; i++) {
        if (strcmp(cs->entries[i].path, path) == 0)
            return cs->entries[i].md5;
    }

    return NULL;
}

void conffile_set_free(aept_conffile_set_t *cs)
{
    for (int i = 0; i < cs->count; i++) {
        free(cs->entries[i].path);
        free(cs->entries[i].md5);
    }
    free(cs->entries);
    conffile_set_init(cs);
}

char *conffile_md5(const char *path)
{
    Chksum *chk;
    FILE *fp;
    char buf[4096];
    size_t n;
    const unsigned char *raw;
    int len;
    char *hex;

    fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    chk = solv_chksum_create(REPOKEY_TYPE_MD5);
    if (!chk) {
        fclose(fp);
        return NULL;
    }

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        solv_chksum_add(chk, buf, (int)n);
    fclose(fp);

    raw = solv_chksum_get(chk, &len);

    hex = xmalloc(len * 2 + 1);
    for (int i = 0; i < len; i++)
        sprintf(hex + i * 2, "%02x", raw[i]);
    hex[len * 2] = '\0';

    solv_chksum_free(chk, NULL);
    return hex;
}

int conffile_parse_list(const char *control_dir, aept_conffile_set_t *cs)
{
    char *path = NULL;
    FILE *fp;
    char buf[4096];

    xasprintf(&path, "%s/conffiles", control_dir);
    fp = fopen(path, "r");
    free(path);

    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\n")] = '\0';
        if (buf[0] == '\0')
            continue;
        if (!archive_path_is_safe(buf)) {
            log_warning("ignoring unsafe conffile path '%s'", buf);
            continue;
        }
        conffile_set_add(cs, buf, NULL);
    }

    fclose(fp);
    return 0;
}

int conffile_load(const char *name, aept_conffile_set_t *cs)
{
    char *path = NULL;
    FILE *fp;
    char buf[4096];

    xasprintf(&path, "%s/%s.conffiles", cfg->info_dir, name);
    fp = fopen(path, "r");
    free(path);

    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        char *sep;

        buf[strcspn(buf, "\n")] = '\0';
        if (buf[0] == '\0')
            continue;

        sep = strchr(buf, ' ');
        if (!sep)
            continue;
        *sep = '\0';

        char *file_path = sep + 1;
        while (*file_path == ' ')
            file_path++;

        conffile_set_add(cs, file_path, buf);
    }

    fclose(fp);
    return 0;
}

int conffile_save(const char *name, const aept_conffile_set_t *cs)
{
    char *path = NULL;
    FILE *fp;

    xasprintf(&path, "%s/%s.conffiles", cfg->info_dir, name);
    fp = fopen(path, "w");
    if (!fp) {
        log_error("cannot write '%s': %s", path, strerror(errno));
        free(path);
        return -1;
    }

    for (int i = 0; i < cs->count; i++) {
        if (cs->entries[i].md5)
            fprintf(fp, "%s  %s\n", cs->entries[i].md5,
                    cs->entries[i].path);
    }

    if (ferror(fp) || fclose(fp) != 0) {
        log_error("failed to write '%s'", path);
        free(path);
        return -1;
    }

    free(path);
    return 0;
}

/* Prompt the user to decide what to do with a modified conffile.
 * Returns 1 to install the new version, 0 to keep the old one. */
static int conffile_prompt(const char *cf_path,
                           const char *disk_path,
                           const char *new_path)
{
    if (cfg->force_confnew)
        return 1;
    if (cfg->force_confold)
        return 0;

    if (!isatty(STDIN_FILENO)) {
        log_warning("'%s' has been modified; "
                    "keeping old version (non-interactive)", cf_path);
        return -1;
    }

    struct termios old_tio, new_tio;

    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    new_tio.c_cc[VMIN] = 1;
    new_tio.c_cc[VTIME] = 0;

    for (;;) {
        int ch;

        printf("\nConfiguration file '%s'\n", cf_path);
        printf(" ==> Modified (by you or by a script) since installation.\n");
        printf(" ==> Package distributor has shipped an updated version.\n");
        printf("   What would you like to do about it?\n");
        printf("    Y or I  : install the package maintainer's version\n");
        printf("    N or O  : keep your currently-installed version\n");
        printf("      D     : show the differences between the versions\n");
        printf("      Z     : start a shell to examine the situation\n");
        printf(" The default action is to keep your current version.\n");
        printf("*** %s (Y/I/N/O/D/Z) [default=N] ? ", cf_path);
        fflush(stdout);

        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
        ch = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

        if (ch == EOF || ch == '\n' || ch == 'n' || ch == 'N' ||
            ch == 'o' || ch == 'O') {
            putchar('\n');
            return 0;
        }

        if (ch == 'y' || ch == 'Y' || ch == 'i' || ch == 'I') {
            putchar('\n');
            return 1;
        }

        if (ch == 'd' || ch == 'D') {
            putchar('\n');
            const char *argv[] = {"diff", "-u", disk_path, new_path, NULL};
            xsystem(argv);
            continue;
        }

        if (ch == 'z' || ch == 'Z') {
            putchar('\n');
            const char *shell = getenv("SHELL");
            if (!shell)
                shell = "/bin/sh";
            printf("Type 'exit' to return to the conffile prompt.\n");
            const char *argv[] = {shell, NULL};
            xsystem(argv);
            continue;
        }
    }
}

int conffile_resolve_upgrade(const char *name,
                             const aept_conffile_set_t *old_conffiles,
                             const aept_conffile_set_t *new_conffiles)
{
    aept_conffile_set_t result;

    conffile_set_init(&result);

    for (int i = 0; i < new_conffiles->count; i++) {
        const char *cf_path = new_conffiles->entries[i].path;
        char *disk_path = config_root_path(cf_path);
        char *new_path = NULL;
        const char *old_md5;
        char *current_md5;
        char *new_md5;
        int install_new = 0;

        xasprintf(&new_path, "%s.aept-new", disk_path);

        old_md5 = old_conffiles ?
            conffile_set_lookup(old_conffiles, cf_path) : NULL;
        current_md5 = conffile_md5(disk_path);
        new_md5 = conffile_md5(new_path);

        if (!current_md5) {
            /* File does not exist on disk: install new */
            install_new = 1;
        } else if (!new_md5) {
            /* New package version not extracted: keep old */
            install_new = 0;
        } else if (strcmp(current_md5, new_md5) == 0) {
            /* On-disk matches new version: nothing to do */
            install_new = 0;
        } else if (old_md5 && strcmp(old_md5, current_md5) == 0) {
            /* User did not modify: silently install new */
            install_new = 1;
        } else if (old_md5 && strcmp(old_md5, new_md5) == 0) {
            /* Package did not change: keep user's version */
            install_new = 0;
        } else {
            /* Both changed: prompt */
            install_new = conffile_prompt(cf_path, disk_path, new_path);
        }

        if (install_new > 0 && new_md5) {
            if (rename(new_path, disk_path) < 0)
                log_warning("failed to install new conffile '%s'", cf_path);
        } else if (install_new == 0) {
            unlink(new_path);
        }
        /* install_new < 0: leave .aept-new for admin review */

        /* Record the resulting MD5 for saving */
        {
            const char *save_md5 = install_new ? new_md5 : current_md5;
            if (!save_md5)
                save_md5 = new_md5;
            conffile_set_add(&result, cf_path, save_md5);
        }

        free(disk_path);
        free(new_path);
        free(current_md5);
        free(new_md5);
    }

    conffile_save(name, &result);
    conffile_set_free(&result);
    return 0;
}
