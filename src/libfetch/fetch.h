/*	$NetBSD: fetch.h,v 1.16 2010/01/22 13:21:09 joerg Exp $	*/
/*-
 * Copyright (c) 1998-2004 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: fetch.h,v 1.26 2004/09/21 18:35:20 des Exp $
 */

#ifndef LIBFETCH_H_7BF97F
#define LIBFETCH_H_7BF97F

#include <sys/types.h>
#include <limits.h>
#include <stdio.h>

#define LIBFETCH_VER "libfetch/2.0"

#define LIBFETCH_URL_HOSTLEN 255
#define LIBFETCH_URL_SCHEMELEN 16
#define LIBFETCH_URL_USERLEN 256
#define LIBFETCH_URL_PWDLEN 4096

typedef struct libfetch_io_t libfetch_io_t;

struct libfetch_url {
    char scheme[LIBFETCH_URL_SCHEMELEN + 1];
    char user[LIBFETCH_URL_USERLEN + 1];
    char pwd[LIBFETCH_URL_PWDLEN + 1];
    char host[LIBFETCH_URL_HOSTLEN + 1];
    int port;
    char *doc;
};

/* Recognized schemes */
#define LIBFETCH_SCHEME_HTTP "http"
#define LIBFETCH_SCHEME_HTTPS "https"

enum {
    /* Error categories */
    LIBFETCH_ERRCAT_FETCH = 0,
    LIBFETCH_ERRCAT_ERRNO,
    LIBFETCH_ERRCAT_NETDB,
    LIBFETCH_ERRCAT_HTTP,
    LIBFETCH_ERRCAT_URL,
    LIBFETCH_ERRCAT_TLS,

    /* Error FETCH category codes */
    LIBFETCH_OK = 0,
    LIBFETCH_ERR_UNKNOWN,
    LIBFETCH_ERR_UNCHANGED,

    /* Error URL category codes */
    LIBFETCH_ERR_URL_MALFORMED = 1,
    LIBFETCH_ERR_URL_BAD_SCHEME,
    LIBFETCH_ERR_URL_BAD_PORT,
    LIBFETCH_ERR_URL_BAD_HOST,
    LIBFETCH_ERR_URL_BAD_AUTH,

    /* Error TLS category codes */
    LIBFETCH_ERR_TLS = 1,
    LIBFETCH_ERR_TLS_SERVER_CERT_ABSENT,
    LIBFETCH_ERR_TLS_SERVER_CERT_HOSTNAME,
    LIBFETCH_ERR_TLS_SERVER_CERT_UNTRUSTED,
    LIBFETCH_ERR_TLS_CLIENT_CERT_UNTRUSTED,
    LIBFETCH_ERR_TLS_HANDSHAKE,
};

struct libfetch_error {
    unsigned int category;
    int code;
};

#if defined(__cplusplus)
extern "C" {
#endif

/* Context: connection cache and per-caller TLS configuration.  A
 * stream returned by libfetch_get_url() holds a reference to the context it
 * was created from, so the context must outlive the stream. */
struct libfetch_ctx;
struct libfetch_ctx *libfetch_ctx_new(int global_limit, int per_host_limit);
void libfetch_ctx_free(struct libfetch_ctx *);

/*
 * Select the client certificate, taking precedence over the
 * SSL_CLIENT_{CERT,KEY}_FILE environment variables.  Either argument
 * may be NULL: a NULL cert_file falls back to the environment, and a
 * NULL key_file means the certificate file also contains the key.
 * The strings are not copied and must outlive the fetch calls made
 * with them.
 *
 * The selection belongs to the context, and so does the connection
 * cache: a connection opened while presenting one certificate is never
 * reused by a context configured with another.
 */
void libfetch_set_client_certificate(struct libfetch_ctx *ctx,
                                     const char *cert_file,
                                     const char *key_file);

void libfetch_io_close(libfetch_io_t *);
ssize_t libfetch_io_read(libfetch_io_t *, void *, size_t);

/* HTTP */
libfetch_io_t *libfetch_get_http(struct libfetch_ctx *, struct libfetch_url *,
                                 const char *);

/* Generic */
libfetch_io_t *libfetch_get_url(struct libfetch_ctx *, const char *,
                                const char *);

/* URL parsing.  Internal to the library: nothing outside it needs to
 * build or inspect a struct libfetch_url, but redirects and the connection
 * cache do. */
struct libfetch_url *libfetch_make_url(const char *, const char *, int,
                                       const char *, const char *,
                                       const char *);
struct libfetch_url *libfetch_parse_url(const char *);
struct libfetch_url *libfetch_copy_url(const struct libfetch_url *);
void libfetch_free_url(struct libfetch_url *);

/* Last error code, per-thread */
extern _Thread_local struct libfetch_error libfetch_last_error;

/* I/O timeout */
extern int libfetch_timeout;

/* Restart interrupted syscalls */
extern volatile int libfetch_restart_calls;

#if defined(__cplusplus)
}
#endif

#endif
