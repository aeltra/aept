/* util.c - utility functions
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
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

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/util.h"

void *aept_malloc(size_t size)
{
    /* malloc(0) may return NULL on some libc implementations */
    void *p = malloc(size ? size : 1);
    if (!p) {
        fprintf(stderr, "aept: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

void *aept_realloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (!p) {
        fprintf(stderr, "aept: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

char *aept_strdup(const char *s)
{
    char *p = strdup(s);
    if (!p) {
        fprintf(stderr, "aept: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

int aept_asprintf(char **strp, const char *fmt, ...)
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

int aept_pkg_name_is_safe(const char *name)
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

int aept_symlink_target_is_safe(const char *target)
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
int aept_archive_path_is_safe(const char *path)
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

int aept_fgets_is_truncated(const char *buf, size_t bufsize)
{
    size_t len = strlen(buf);
    return len > 0 && len == bufsize - 1 && buf[len - 1] != '\n';
}

void aept_fgets_drain_line(FILE *fp)
{
    int c;
    while ((c = fgetc(fp)) != EOF && c != '\n')
        ;
}

int aept_file_exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

int aept_file_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int aept_file_copy(const char *src, const char *dst)
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

int aept_file_mkdir_hier(const char *path, mode_t mode)
{
    char *p, *dir;
    int r;

    dir = aept_strdup(path);

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

int aept_system(const char *argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        aept_log_error("fork failed for '%s': %s", argv[0], strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(AEPT_EXIT_EXEC_FAILED);
    }

    int wstatus;
    while (waitpid(pid, &wstatus, 0) < 0) {
        if (errno != EINTR) {
            aept_log_error("waitpid failed for '%s': %s",
                      argv[0], strerror(errno));
            return -1;
        }
    }

    if (WIFEXITED(wstatus))
        return WEXITSTATUS(wstatus);

    if (WIFSIGNALED(wstatus)) {
        aept_log_error("'%s' terminated by signal %d",
                  argv[0], WTERMSIG(wstatus));
    }

    return -1;
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
        aept_log_error("failed to unshare user namespace: %s",
                  strerror(errno));
        return -1;
    }

    /* Write uid_map: map real uid to 0 inside the namespace */
    aept_asprintf(&mapfile, "/proc/%ld/uid_map", (long)pid);
    ret = fd = open(mapfile, O_RDWR);
    if (ret == -1) {
        aept_log_error("failed to open '%s': %s", mapfile, strerror(errno));
        goto error;
    }

    aept_asprintf(&content, "0 %lu 1", (unsigned long)uid);
    ret = write(fd, content, strlen(content));
    close(fd);
    free(content);
    content = NULL;

    if (ret < 0) {
        aept_log_error("failed to write uid_map: %s", strerror(errno));
        goto error;
    }

    free(mapfile);
    mapfile = NULL;

    /* Write "deny" to setgroups (required before writing gid_map) */
    aept_asprintf(&mapfile, "/proc/%ld/setgroups", (long)pid);
    ret = fd = open(mapfile, O_RDWR);
    if (ret == -1) {
        aept_log_error("failed to open '%s': %s", mapfile, strerror(errno));
        goto error;
    }

    ret = write(fd, "deny", 4);
    close(fd);

    if (ret != 4) {
        aept_log_error("failed to disable setgroups");
        goto error;
    }

    free(mapfile);
    mapfile = NULL;

    /* Write gid_map: map real gid to 0 inside the namespace */
    aept_asprintf(&mapfile, "/proc/%ld/gid_map", (long)pid);
    ret = fd = open(mapfile, O_RDWR);
    if (ret == -1) {
        aept_log_error("failed to open '%s': %s", mapfile, strerror(errno));
        goto error;
    }

    aept_asprintf(&content, "0 %lu 1\n", (unsigned long)gid);
    ret = write(fd, content, strlen(content));
    close(fd);
    free(content);
    content = NULL;

    if (ret < 0) {
        aept_log_error("failed to write gid_map: %s", strerror(errno));
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

void aept_fileset_init(aept_fileset_t *fs)
{
    fs->paths = NULL;
    fs->count = 0;
    fs->alloc = 0;
    fs->sorted = 0;
}

void aept_fileset_add(aept_fileset_t *fs, const char *path)
{
    path = normalize_path(path);
    if (path[0] == '\0')
        return;

    if (fs->count >= fs->alloc) {
        fs->alloc = fs->alloc ? fs->alloc * 2 : 256;
        fs->paths = aept_realloc(fs->paths, fs->alloc * sizeof(char *));
    }

    fs->paths[fs->count++] = aept_strdup(path);
    fs->sorted = 0;
}

void aept_fileset_sort(aept_fileset_t *fs)
{
    if (fs->sorted || fs->count <= 1)
        return;
    qsort(fs->paths, fs->count, sizeof(char *), path_cmp);
    fs->sorted = 1;
}

int aept_fileset_contains(aept_fileset_t *fs, const char *path)
{
    path = normalize_path(path);
    if (fs->count == 0 || path[0] == '\0')
        return 0;
    aept_fileset_sort(fs);
    return bsearch(&path, fs->paths, fs->count, sizeof(char *),
                   path_cmp) != NULL;
}

void aept_fileset_free(aept_fileset_t *fs)
{
    int i;

    for (i = 0; i < fs->count; i++)
        free(fs->paths[i]);
    free(fs->paths);
    aept_fileset_init(fs);
}

int aept_system_offline_root(const char *argv[])
{
    int status;
    pid_t pid;
    int r;

    pid = fork();

    switch (pid) {
    case -1:
        aept_log_error("%s: fork: %s", argv[0], strerror(errno));
        return -1;
    case 0:
        if (aept_cfg->offline_root) {
            if (geteuid() != 0) {
                if (unshare_and_map_user() != 0)
                    _exit(AEPT_EXIT_SETUP_FAILED);
            }

            if (chroot(aept_cfg->offline_root) != 0) {
                aept_log_error("failed to chroot to '%s': %s",
                          aept_cfg->offline_root, strerror(errno));
                _exit(AEPT_EXIT_SETUP_FAILED);
            }
            if (chdir("/") != 0) {
                aept_log_error("failed to chdir to '/': %s", strerror(errno));
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
        aept_log_error("%s: waitpid: %s", argv[0], strerror(errno));
        return -1;
    }

    if (WIFSIGNALED(status)) {
        aept_log_error("%s: killed by signal %d", argv[0], WTERMSIG(status));
        return -1;
    }

    if (!WIFEXITED(status)) {
        aept_log_error("%s: unexpected status %d from waitpid", argv[0], status);
        return -1;
    }

    return WEXITSTATUS(status);
}
