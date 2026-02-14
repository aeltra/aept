/* util.c - utility functions */

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
        aept_msg(AEPT_ERROR, "%s: vfork: %s\n", argv[0], strerror(errno));
        return -1;
    case 0:
        execvp(argv[0], (char *const *)argv);
        _exit(255);
    default:
        break;
    }

    r = waitpid(pid, &status, 0);
    if (r == -1) {
        aept_msg(AEPT_ERROR, "%s: waitpid: %s\n", argv[0], strerror(errno));
        return -1;
    }

    if (WIFSIGNALED(status)) {
        aept_msg(AEPT_ERROR, "%s: killed by signal %d\n",
                 argv[0], WTERMSIG(status));
        return -1;
    }

    if (!WIFEXITED(status)) {
        aept_msg(AEPT_ERROR, "%s: unexpected status %d from waitpid\n",
                 argv[0], status);
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
        aept_msg(AEPT_ERROR, "failed to unshare user namespace: %s\n",
                 strerror(errno));
        return -1;
    }

    /* Write uid_map: map real uid to 0 inside the namespace */
    xasprintf(&mapfile, "/proc/%ld/uid_map", (long)pid);
    ret = fd = open(mapfile, O_RDWR);
    if (ret == -1) {
        aept_msg(AEPT_ERROR, "failed to open '%s': %s\n",
                 mapfile, strerror(errno));
        goto error;
    }

    xasprintf(&content, "0 %lu 1", (unsigned long)uid);
    ret = write(fd, content, strlen(content));
    close(fd);
    free(content);
    content = NULL;

    if (ret == -1) {
        aept_msg(AEPT_ERROR, "failed to write uid_map\n");
        goto error;
    }

    free(mapfile);
    mapfile = NULL;

    /* Write "deny" to setgroups (required before writing gid_map) */
    xasprintf(&mapfile, "/proc/%ld/setgroups", (long)pid);
    ret = fd = open(mapfile, O_RDWR);
    if (ret == -1) {
        aept_msg(AEPT_ERROR, "failed to open '%s': %s\n",
                 mapfile, strerror(errno));
        goto error;
    }

    ret = write(fd, "deny", 4);
    close(fd);

    if (ret == -1) {
        aept_msg(AEPT_ERROR, "failed to disable setgroups\n");
        goto error;
    }

    free(mapfile);
    mapfile = NULL;

    /* Write gid_map: map real gid to 0 inside the namespace */
    xasprintf(&mapfile, "/proc/%ld/gid_map", (long)pid);
    ret = fd = open(mapfile, O_RDWR);
    if (ret == -1) {
        aept_msg(AEPT_ERROR, "failed to open '%s': %s\n",
                 mapfile, strerror(errno));
        goto error;
    }

    xasprintf(&content, "0 %lu 1\n", (unsigned long)gid);
    ret = write(fd, content, strlen(content));
    close(fd);
    free(content);
    content = NULL;

    if (ret == -1) {
        aept_msg(AEPT_ERROR, "failed to write gid_map\n");
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
        aept_msg(AEPT_ERROR, "%s: vfork: %s\n", argv[0], strerror(errno));
        return -1;
    case 0:
        if (cfg->offline_root) {
            if (geteuid() != 0) {
                if (unshare_and_map_user() != 0)
                    _exit(255);
            }

            if (chroot(cfg->offline_root) != 0) {
                aept_msg(AEPT_ERROR, "failed to chroot to '%s': %s\n",
                         cfg->offline_root, strerror(errno));
                _exit(255);
            }
        }
        execvp(argv[0], (char *const *)argv);
        _exit(255);
    default:
        break;
    }

    r = waitpid(pid, &status, 0);
    if (r == -1) {
        aept_msg(AEPT_ERROR, "%s: waitpid: %s\n", argv[0], strerror(errno));
        return -1;
    }

    if (WIFSIGNALED(status)) {
        aept_msg(AEPT_ERROR, "%s: killed by signal %d\n",
                 argv[0], WTERMSIG(status));
        return -1;
    }

    if (!WIFEXITED(status)) {
        aept_msg(AEPT_ERROR, "%s: unexpected status %d from waitpid\n",
                 argv[0], status);
        return -1;
    }

    return WEXITSTATUS(status);
}
