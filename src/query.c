/* query.c - read-only query commands
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/aept.h"
#include "aept/msg.h"
#include "aept/query.h"
#include "aept/util.h"

static const char *strip_leading(const char *p)
{
    while (p[0] == '.' && p[1] == '/')
        p += 2;
    while (p[0] == '/')
        p++;
    return p;
}

int aept_search(const char *path)
{
    DIR *dir;
    struct dirent *ent;
    const char *needle;
    int found = 0;

    needle = strip_leading(path);
    if (*needle == '\0') {
        log_error("empty search path");
        return 1;
    }

    dir = opendir(cfg->info_dir);
    if (!dir)
        return 1;

    while ((ent = readdir(dir)) != NULL) {
        const char *dot;
        char *list_path = NULL;
        FILE *fp;
        char buf[4096];

        dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".list") != 0)
            continue;

        xasprintf(&list_path, "%s/%s", cfg->info_dir, ent->d_name);
        fp = fopen(list_path, "r");
        free(list_path);

        if (!fp)
            continue;

        while (fgets(buf, sizeof(buf), fp)) {
            const char *entry;
            char *tab;

            buf[strcspn(buf, "\n")] = '\0';

            tab = strchr(buf, '\t');
            if (tab)
                *tab = '\0';

            entry = strip_leading(buf);

            if (strcmp(entry, needle) == 0) {
                /* Print package name (filename minus .list suffix) */
                size_t name_len = (size_t)(dot - ent->d_name);
                printf("%.*s\n", (int)name_len, ent->d_name);
                found = 1;
                break;
            }
        }

        fclose(fp);

        if (found)
            break;
    }

    closedir(dir);

    return found ? 0 : 1;
}

int aept_print_architecture(void)
{
    int i;

    for (i = 0; i < cfg->narchs; i++)
        printf("%s\n", cfg->archs[i]);

    return 0;
}
