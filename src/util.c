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
#include <signal.h>
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

static volatile sig_atomic_t interrupted;

static void signal_handler(int sig)
{
    (void)sig;
    interrupted = 1;
}

void signal_setup(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int signal_was_interrupted(void)
{
    return interrupted;
}

int pkg_name_is_safe(const char *name)
{
    if (!name || name[0] == '\0')
        return 0;

    /* Debian policy: [a-z0-9][a-z0-9.+\-]+ */
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') continue;
        if (c >= '0' && c <= '9') continue;
        if (p != name && (c == '.' || c == '+' || c == '-'))
            continue;
        return 0;
    }

    return 1;
}

int symlink_target_is_safe(const char *target)
{
    if (!target)
        return 0;
    for (const char *p = target; *p; p++) {
        if (*p == '\n' || *p == '\t')
            return 0;
    }
    return 1;
}

/*
 * Check that an archive entry pathname is safe for recording in a
 * .list file and later consumption by remove/upgrade.  Rejects:
 *   - empty paths
 *   - consecutive dots  (directory traversal)
 *   - newlines          (line injection in .list)
 *   - tabs              (field injection in .list)
 */
int archive_path_is_safe(const char *path)
{
    int prev_dot = 0;

    if (!path || path[0] == '\0')
        return 0;

    for (const char *p = path; *p; p++) {
        if (*p == '\n' || *p == '\t')
            return 0;
        if (*p == '.' && prev_dot)
            return 0;
        prev_dot = (*p == '.');
    }

    return 1;
}

int fgets_is_truncated(const char *buf, size_t bufsize)
{
    size_t len = strlen(buf);
    return len > 0 && len == bufsize - 1 && buf[len - 1] != '\n';
}

void fgets_drain_line(FILE *fp)
{
    int c;
    while ((c = fgetc(fp)) != EOF && c != '\n')
        ;
}

int file_exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
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
    struct stat st;

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

    if (fstat(fileno(in), &st) == 0) {
        fchmod(fileno(out), st.st_mode);
        /* Best effort — requires CAP_CHOWN, harmless if it fails. */
        if (fchown(fileno(out), st.st_uid, st.st_gid) < 0)
            (void)0;
    }

    fclose(in);

    if (fclose(out) != 0) {
        unlink(dst);
        return -1;
    }

    return 0;
}

int file_mkdir_hier(const char *path, mode_t mode)
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

    pid = fork();

    switch (pid) {
    case -1:
        log_error("%s: fork: %s", argv[0], strerror(errno));
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

static const char *normalize_path(const char *path)
{
    while (path[0] == '.' && path[1] == '/')
        path += 2;
    while (path[0] == '/')
        path++;
    return path;
}

static int path_cmp(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

void fileset_init(aept_fileset_t *fs)
{
    fs->paths = NULL;
    fs->count = 0;
    fs->alloc = 0;
    fs->sorted = 0;
}

void fileset_add(aept_fileset_t *fs, const char *path)
{
    path = normalize_path(path);
    if (path[0] == '\0')
        return;

    if (fs->count >= fs->alloc) {
        fs->alloc = fs->alloc ? fs->alloc * 2 : 256;
        fs->paths = xrealloc(fs->paths, fs->alloc * sizeof(char *));
    }

    fs->paths[fs->count++] = xstrdup(path);
    fs->sorted = 0;
}

void fileset_sort(aept_fileset_t *fs)
{
    if (fs->sorted || fs->count <= 1)
        return;
    qsort(fs->paths, fs->count, sizeof(char *), path_cmp);
    fs->sorted = 1;
}

int fileset_contains(aept_fileset_t *fs, const char *path)
{
    path = normalize_path(path);
    if (fs->count == 0 || path[0] == '\0')
        return 0;
    fileset_sort(fs);
    return bsearch(&path, fs->paths, fs->count, sizeof(char *),
                   path_cmp) != NULL;
}

void fileset_free(aept_fileset_t *fs)
{
    int i;

    for (i = 0; i < fs->count; i++)
        free(fs->paths[i]);
    free(fs->paths);
    fileset_init(fs);
}

int xsystem_offline_root(const char *argv[])
{
    int status;
    pid_t pid;
    int r;

    pid = fork();

    switch (pid) {
    case -1:
        log_error("%s: fork: %s", argv[0], strerror(errno));
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
            if (chdir("/") != 0) {
                log_error("failed to chdir to '/': %s", strerror(errno));
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
