/* scriptprobe.c - try from inside an offline root what a confined
 * maintainer script must not be able to do
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * Usage: scriptprobe chroot
 *        scriptprobe unshare
 *        scriptprobe read <fd> <name>
 *
 *   chroot   calls chroot("."), which needs CAP_SYS_CHROOT and is the
 *            first step of the classic way out of a chroot;
 *   unshare  creates a mount namespace, which needs CAP_SYS_ADMIN;
 *   read     reads the first line of <name> relative to the inherited
 *            descriptor <fd>, which leads outside the root if it is a
 *            directory there.
 *
 * Prints "<what>: allowed" or the line read on success, and
 * "<what>: <error>" otherwise.  Always exits 0, so the script running
 * it does not fail the install.
 *
 * This is not a test: it is the harness test_script_confinement.sh
 * copies into an offline root and runs from a maintainer script.  It is
 * linked statically, so it runs there without any library.
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "chroot") == 0) {
        if (chroot(".") == 0)
            printf("chroot: allowed\n");
        else
            printf("chroot: %s\n", strerror(errno));
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "unshare") == 0) {
        if (unshare(CLONE_NEWNS) == 0)
            printf("unshare: allowed\n");
        else
            printf("unshare: %s\n", strerror(errno));
        return 0;
    }

    if (argc == 4 && strcmp(argv[1], "read") == 0) {
        char line[256];
        FILE *fp = NULL;
        int fd = openat(atoi(argv[2]), argv[3], O_RDONLY);

        if (fd == -1 || (fp = fdopen(fd, "r")) == NULL) {
            printf("read: %s\n", strerror(errno));
            return 0;
        }

        if (fgets(line, sizeof(line), fp) != NULL)
            printf("%s", line);
        fclose(fp);
        return 0;
    }

    fprintf(stderr, "usage: scriptprobe chroot | unshare | read <fd> <name>\n");
    return 2;
}
