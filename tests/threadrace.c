/* threadrace.c - drive several aept contexts from several threads
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * Usage: threadrace <iterations> <root-a> <root-b> <url>
 *
 * Runs three threads concurrently, each with contexts of its own:
 *
 *   - two listing threads, one per offline root, so two different
 *     libsolv pools are being sorted at the same time;
 *   - one downloading thread, so two contexts fetch at once.
 *
 * Every iteration is a full lifecycle (init, load config, work,
 * cleanup), which is what a caller managing several roots would do.
 *
 * This is not a test: it is the harness test_threads.sh drives, and the
 * one to build under ThreadSanitizer when checking for data races.
 * Exits 0 only if every thread completed its work.
 */

#include <config.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/aept.h"
#include "aept/download.h"

struct job {
    const char *root;       /* offline root */
    const char *url;        /* NULL for a listing job */
    int iterations;
    int failures;           /* written by the thread, read after join */
    int listed;             /* packages seen, to prove work happened */
};

static aept_ctx_t *open_ctx(const char *root)
{
    aept_ctx_t *ctx = aept_init();
    char conf[4096];

    if (!ctx)
        return NULL;

    aept_set_offline_root(ctx, root);

    snprintf(conf, sizeof(conf), "%s/etc/aept/aept.conf", root);
    if (aept_load_config(ctx, conf) < 0) {
        aept_cleanup(ctx);
        return NULL;
    }

    /* After load_config: it resets verbosity to the configured value. */
    aept_set_verbosity(ctx, AEPT_LOG_ERROR - 1);

    return ctx;
}

static void *list_thread(void *arg)
{
    struct job *job = arg;
    int i;

    for (i = 0; i < job->iterations; i++) {
        aept_pkg_list_t list;
        aept_ctx_t *ctx = open_ctx(job->root);

        if (!ctx) {
            job->failures++;
            continue;
        }

        if (aept_list(ctx, NULL, 0, 0, &list) == 0) {
            /*
             * The listing must come back sorted.  The comparator used
             * to resolve names through a pool pointer held in a
             * file-scope variable, so a listing racing another context
             * would compare its own name ids against the other
             * context's pool -- yielding an order that is not this
             * root's alphabetical one.  The emitted names stay correct
             * either way, because they are resolved afterwards through
             * the right pool, so the order is the observable symptom.
             */
            for (int n = 1; n < list.count; n++) {
                if (strcmp(list.entries[n - 1].name,
                           list.entries[n].name) > 0)
                    job->failures++;
            }
            job->listed += list.count;
            aept_pkg_list_free(&list);
        } else {
            job->failures++;
        }

        aept_cleanup(ctx);
    }

    return NULL;
}

static void *download_thread(void *arg)
{
    struct job *job = arg;
    char dest[] = "/tmp/aept-threadrace-XXXXXX";
    int fd = mkstemp(dest);
    int i;

    if (fd < 0) {
        job->failures++;
        return NULL;
    }
    close(fd);

    for (i = 0; i < job->iterations; i++) {
        aept_ctx_t *ctx = open_ctx(job->root);

        if (!ctx) {
            job->failures++;
            continue;
        }

        if (aept_download(ctx, job->url, dest, "test") != 0)
            job->failures++;

        aept_cleanup(ctx);
    }

    unlink(dest);
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t ta, tb, tc;
    struct job ja, jb, jc;
    int iterations;

    if (argc != 5) {
        fprintf(stderr,
                "usage: %s <iterations> <root-a> <root-b> <url>\n", argv[0]);
        return 2;
    }

    iterations = atoi(argv[1]);

    memset(&ja, 0, sizeof(ja));
    memset(&jb, 0, sizeof(jb));
    memset(&jc, 0, sizeof(jc));

    ja.root = argv[2];  ja.iterations = iterations;
    jb.root = argv[3];  jb.iterations = iterations;
    jc.root = argv[2];  jc.iterations = iterations;  jc.url = argv[4];

    if (pthread_create(&ta, NULL, list_thread, &ja) != 0 ||
        pthread_create(&tb, NULL, list_thread, &jb) != 0 ||
        pthread_create(&tc, NULL, download_thread, &jc) != 0) {
        fprintf(stderr, "pthread_create failed\n");
        return 2;
    }

    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    pthread_join(tc, NULL);

    printf("list-a: %d listed, %d failures\n", ja.listed, ja.failures);
    printf("list-b: %d listed, %d failures\n", jb.listed, jb.failures);
    printf("download: %d failures\n", jc.failures);

    if (ja.listed == 0 || jb.listed == 0) {
        fprintf(stderr, "a listing thread saw no packages at all\n");
        return 1;
    }

    return (ja.failures || jb.failures || jc.failures) ? 1 : 0;
}
