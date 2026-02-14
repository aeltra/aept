/* verify.c - usign signature verification
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "aept/aept.h"
#include "aept/msg.h"
#include "aept/util.h"
#include "aept/verify.h"

int aept_verify_signature(const char *file, const char *sigfile)
{
    int r;
    pid_t pid;
    int status;

    pid = fork();

    if (pid < 0) {
        aept_msg(AEPT_ERROR, "failed to fork usign process: %s\n",
                 strerror(errno));
        return -1;
    }

    if (pid == 0) {
        char *usign = NULL;
        struct stat st;

        /* Try configured path first, then search common locations */
        char *usign_progs[] = {
            cfg->usign_bin,
            "/usr/bin/usign",
            "/usr/local/bin/usign",
            NULL
        };

        for (int i = 0; (usign = usign_progs[i]) != NULL; i++) {
            if (lstat(usign, &st) == 0)
                break;
            usign = NULL;
        }

        if (usign) {
            execl(usign, "usign", "-q", "-V",
                  "-P", cfg->usign_keydir,
                  "-m", file,
                  "-x", sigfile,
                  NULL);
        }

        _exit(255);
    }

    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        aept_msg(AEPT_ERROR, "signature verification failed for '%s'\n", file);
        return -1;
    }

    return 0;
}
