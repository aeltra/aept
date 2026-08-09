/* test_ssl_env.c - a configured TLS client certificate must not end up
 * in aept's own environment
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/util.h"

#include "test.h"

#define CERT_PATH "/nonexistent/aept-test/client.pem"
#define KEY_PATH  "/nonexistent/aept-test/client.key"

/* The download below is meant to fail and logs an error for it. */
static struct aept_ctx ctx;

static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

static void check_unset(const char *name)
{
    char label[128];

    snprintf(label, sizeof(label), "%s is absent from the environment", name);
    test_str_eq(getenv(name), NULL, label);
}

int main(void)
{
    char dest[] = "/tmp/aept-ssl-env-XXXXXX";
    int fd = mkstemp(dest);
    int r;

    if (fd < 0) {
        printf("Bail out! mkstemp failed\n");
        return 1;
    }
    close(fd);

    ctx.config.ssl_client_cert = CERT_PATH;
    ctx.config.ssl_client_key = KEY_PATH;
    silence_logging();

    /* Start from a clean slate, so a leak is unambiguous. */
    unsetenv("SSL_CLIENT_CERT_FILE");
    unsetenv("SSL_CLIENT_KEY_FILE");

    /*
     * Port 1 on loopback refuses immediately, so this needs no network
     * and no server.  The certificate was published to the environment
     * before the connection was attempted, so a refused connection is
     * enough to expose the leak.
     */
    r = aept_download(&ctx, "http://127.0.0.1:1/Packages", dest, "Packages");
    test_ok(r != 0, "the download failed, as intended");

    /*
     * The regression: aept_download() used to hand these to libfetch
     * with setenv() and never clear them, so the path to the private
     * key stayed in aept's environment for the rest of the run and was
     * inherited by every process forked afterwards — maintainer
     * scripts included.
     */
    check_unset("SSL_CLIENT_CERT_FILE");
    check_unset("SSL_CLIENT_KEY_FILE");

    /*
     * A caller's own environment is none of aept's business either:
     * whatever was there before must survive untouched.
     */
    setenv("SSL_CLIENT_CERT_FILE", "/set/by/the/caller", 1);

    r = aept_download(&ctx, "http://127.0.0.1:1/Packages", dest, "Packages");
    test_ok(r != 0, "the second download failed, as intended");

    test_str_eq(getenv("SSL_CLIENT_CERT_FILE"), "/set/by/the/caller",
                "an existing SSL_CLIENT_CERT_FILE is left alone");

    unsetenv("SSL_CLIENT_CERT_FILE");
    unlink(dest);

    return test_summary();
}
