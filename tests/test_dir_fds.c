/* test_dir_fds.c - close_dir_fds() and parse_fd() in util.c
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * close_dir_fds() runs in the child of aept_system_offline_root(),
 * which leaves by exec or _exit(), so neither test_script_confinement.sh
 * nor the coverage counters see more of it than its effect on one
 * descriptor.  Here it runs in the test process itself, against many
 * descriptors: more than one getdents64() buffer holds, of every kind it
 * has to tell apart.
 */

#include <config.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * parse_fd() and close_dir_fds() are file-scope helpers in util.c.  Pull
 * the translation unit in directly rather than widening the internal API.
 */

#include "util.c"

#include "test.h"

/* Directories and files interleaved, so every getdents64() batch holds both. */
#define N_FDS 900

static int is_open(int fd)
{
    return fcntl(fd, F_GETFD) != -1;
}

static void test_parse_fd(void)
{
    test_int_eq(parse_fd("0"), 0, "parse_fd: 0");
    test_int_eq(parse_fd("17"), 17, "parse_fd: 17");
    test_int_eq(parse_fd("."), -1, "parse_fd: . is not a descriptor");
    test_int_eq(parse_fd(".."), -1, "parse_fd: .. is not a descriptor");
    test_int_eq(parse_fd(""), -1, "parse_fd: empty name");
    test_int_eq(parse_fd("3a"), -1, "parse_fd: trailing garbage");
    test_int_eq(parse_fd("99999999999"), -1, "parse_fd: too large for an int");
}

static void test_close_dir_fds(void)
{
    struct rlimit rl;
    int kind[N_FDS];
    int fds[N_FDS];
    int dirs_open = 0;
    int files_open = 0;
    int files = 0;
    int i;

    /* Room for N_FDS on top of what is open already. */
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < rl.rlim_max) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
    }

    for (i = 0; i < N_FDS; i++) {
        kind[i] = i % 3;
        if (kind[i] == 0)
            fds[i] = open("/", O_RDONLY | O_DIRECTORY);
        else if (kind[i] == 1)
            fds[i] = open("/", O_PATH | O_DIRECTORY);
        else
            fds[i] = open("/dev/null", O_RDONLY);

        if (fds[i] == -1) {
            test_ok(0, "open the descriptors to close");
            return;
        }
        if (kind[i] == 2)
            files++;
    }

    test_int_eq(close_dir_fds(), 0, "close_dir_fds succeeds");

    for (i = 0; i < N_FDS; i++) {
        if (!is_open(fds[i]))
            continue;
        if (kind[i] == 2)
            files_open++;
        else
            dirs_open++;
    }

    test_int_eq(dirs_open, 0, "every directory descriptor is closed, O_PATH ones included");
    test_int_eq(files_open, files, "every other descriptor stays open");

    for (i = 0; i < N_FDS; i++) {
        if (kind[i] == 2)
            close(fds[i]);
    }
}

static void test_directory_on_stdin(void)
{
    struct stat st, null_st;
    int saved = dup(STDIN_FILENO);
    int dir = open("/", O_RDONLY | O_DIRECTORY);

    if (saved == -1 || dir == -1 || dup2(dir, STDIN_FILENO) == -1) {
        test_ok(0, "put a directory on stdin");
        return;
    }
    close(dir);

    test_int_eq(close_dir_fds(), 0, "close_dir_fds succeeds with a directory on stdin");

    test_ok(fstat(STDIN_FILENO, &st) == 0 && stat("/dev/null", &null_st) == 0 &&
                S_ISCHR(st.st_mode) && st.st_rdev == null_st.st_rdev,
            "a directory on stdin is replaced by /dev/null, not closed");

    dup2(saved, STDIN_FILENO);
    close(saved);
}

int main(void)
{
    test_parse_fd();
    test_close_dir_fds();
    test_directory_on_stdin();

    return test_summary();
}
