/* util.c - utility functions
 *
 * Copyright (C) 2026 Tobias Koch
 * Based in part on xsystem by Carl D. Worth,
 *   Copyright (C) 2001 University of Southern California
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "aept/aept.h"
#include "aept/msg.h"
#include "aept/util.h"

void *xmalloc(size_t size)
{
    void *p = malloc(size);
    if (!p) {
        fprintf(stderr, "aept: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

void *xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (!p) {
        fprintf(stderr, "aept: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

char *xstrdup(const char *s)
{
    char *p = strdup(s);
    if (!p) {
        fprintf(stderr, "aept: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

int xasprintf(char **strp, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vasprintf(strp, fmt, ap);
    va_end(ap);

    if (r < 0) {
        fprintf(stderr, "aept: out of memory\n");
        exit(EXIT_FAILURE);
    }

    return r;
}

int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int file_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int file_copy(const char *src, const char *dst)
{
    FILE *in, *out;
    char buf[4096];
    size_t n;

    in = fopen(src, "r");
    if (!in)
        return -1;

    out = fopen(dst, "w");
    if (!out) {
        fclose(in);
        return -1;
    }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            unlink(dst);
            return -1;
        }
    }

    fclose(in);

    if (fclose(out) != 0) {
        unlink(dst);
        return -1;
    }

    return 0;
}

int file_mkdir_hier(const char *path, int mode)
{
    char *p, *dir;
    int r;

    dir = xstrdup(path);

    for (p = dir + 1; *p; p++) {
        if (*p != '/')
            continue;

        *p = '\0';
        r = mkdir(dir, mode);
        if (r < 0 && errno != EEXIST) {
            free(dir);
            return -1;
        }
        *p = '/';
    }

    r = mkdir(dir, mode);
    free(dir);

    if (r < 0 && errno != EEXIST)
        return -1;

    return 0;
}

int xsystem(const char *argv[])
{
    int status;
    pid_t pid;
    int r;

    pid = vfork();

    switch (pid) {
    case -1:
        log_error("%s: vfork: %s", argv[0], strerror(errno));
        return -1;
    case 0:
        execvp(argv[0], (char *const *)argv);
        _exit(AEPT_EXIT_EXEC_FAILED);
    default:
        break;
    }

    r = waitpid(pid, &status, 0);
    if (r == -1) {
        log_error("%s: waitpid: %s", argv[0], strerror(errno));
        return -1;
    }

    if (WIFSIGNALED(status)) {
        log_error("%s: killed by signal %d", argv[0], WTERMSIG(status));
        return -1;
    }

    if (!WIFEXITED(status)) {
        log_error("%s: unexpected status %d from waitpid", argv[0], status);
        return -1;
    }

    return WEXITSTATUS(status);
}

static int unshare_and_map_user(void)
{
    int fd, ret = -1;
    char *mapfile = NULL;
    char *content = NULL;

    pid_t pid = getpid();
    uid_t uid = geteuid();
    gid_t gid = getegid();

    if (unshare(CLONE_NEWUSER) != 0) {
        log_error("failed to unshare user namespace: %s",
                  strerror(errno));
        return -1;
    }

    /* Write uid_map: map real uid to 0 inside the namespace */
    xasprintf(&mapfile, "/proc/%ld/uid_map", (long)pid);
    ret = fd = open(mapfile, O_RDWR);
    if (ret == -1) {
        log_error("failed to open '%s': %s", mapfile, strerror(errno));
        goto error;
    }

    xasprintf(&content, "0 %lu 1", (unsigned long)uid);
    ret = write(fd, content, strlen(content));
    close(fd);
    free(content);
    content = NULL;

    if (ret == -1) {
        log_error("failed to write uid_map");
        goto error;
    }

    free(mapfile);
    mapfile = NULL;

    /* Write "deny" to setgroups (required before writing gid_map) */
    xasprintf(&mapfile, "/proc/%ld/setgroups", (long)pid);
    ret = fd = open(mapfile, O_RDWR);
    if (ret == -1) {
        log_error("failed to open '%s': %s", mapfile, strerror(errno));
        goto error;
    }

    ret = write(fd, "deny", 4);
    close(fd);

    if (ret == -1) {
        log_error("failed to disable setgroups");
        goto error;
    }

    free(mapfile);
    mapfile = NULL;

    /* Write gid_map: map real gid to 0 inside the namespace */
    xasprintf(&mapfile, "/proc/%ld/gid_map", (long)pid);
    ret = fd = open(mapfile, O_RDWR);
    if (ret == -1) {
        log_error("failed to open '%s': %s", mapfile, strerror(errno));
        goto error;
    }

    xasprintf(&content, "0 %lu 1\n", (unsigned long)gid);
    ret = write(fd, content, strlen(content));
    close(fd);
    free(content);
    content = NULL;

    if (ret == -1) {
        log_error("failed to write gid_map");
        goto error;
    }

    free(mapfile);
    mapfile = NULL;

    ret = 0;

error:
    free(mapfile);
    free(content);
    return ret;
}

int xsystem_offline_root(const char *argv[])
{
    int status;
    pid_t pid;
    int r;

    pid = vfork();

    switch (pid) {
    case -1:
        log_error("%s: vfork: %s", argv[0], strerror(errno));
        return -1;
    case 0:
        if (cfg->offline_root) {
            if (geteuid() != 0) {
                if (unshare_and_map_user() != 0)
                    _exit(AEPT_EXIT_SETUP_FAILED);
            }

            if (chroot(cfg->offline_root) != 0) {
                log_error("failed to chroot to '%s': %s",
                          cfg->offline_root, strerror(errno));
                _exit(AEPT_EXIT_SETUP_FAILED);
            }
        }
        execvp(argv[0], (char *const *)argv);
        _exit(AEPT_EXIT_EXEC_FAILED);
    default:
        break;
    }

    r = waitpid(pid, &status, 0);
    if (r == -1) {
        log_error("%s: waitpid: %s", argv[0], strerror(errno));
        return -1;
    }

    if (WIFSIGNALED(status)) {
        log_error("%s: killed by signal %d", argv[0], WTERMSIG(status));
        return -1;
    }

    if (!WIFEXITED(status)) {
        log_error("%s: unexpected status %d from waitpid", argv[0], status);
        return -1;
    }

    return WEXITSTATUS(status);
}
