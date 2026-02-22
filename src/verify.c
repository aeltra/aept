/* verify.c - usign signature verification
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "aept/internal.h"
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
        aept_log_error("failed to fork usign process: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execl(AEPT_USIGN_BIN, "usign", "-q", "-V",
              "-P", aept_cfg->usign_keydir,
              "-m", file,
              "-x", sigfile,
              NULL);
        _exit(AEPT_EXIT_EXEC_FAILED);
    }

    r = waitpid(pid, &status, 0);
    if (r == -1) {
        aept_log_error("usign: waitpid: %s", strerror(errno));
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        aept_log_error("signature verification failed for '%s'", file);
        return -1;
    }

    return 0;
}
