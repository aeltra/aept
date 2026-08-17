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

#define LIBFETCH_URL_HOSTLEN 255
#define LIBFETCH_URL_SCHEMELEN 16
#define LIBFETCH_URL_USERLEN 256
#define LIBFETCH_URL_PWDLEN 4096

/* Longest cache validator that will be stored.  An ETag is bounded by
 * nothing in the specification, but the ones servers actually emit are
 * a hash or an inode-mtime-size triple; an HTTP-date is 29 characters. */
#define LIBFETCH_VALIDATOR_MAX 255

/* The one status code a caller has to be able to tell from a failure. */
#define LIBFETCH_HTTP_NOT_MODIFIED 304

typedef struct libfetch_io_t libfetch_io_t;

/*
 * Cache validators, for asking a server whether a copy already held is
 * still current.
 *
 * Both are opaque tokens: whatever arrived is stored and sent back byte
 * for byte, so neither an ETag nor an HTTP-date is ever parsed, here or
 * above.  That is a deliberate consequence of keeping them in a file of
 * our own rather than in the cached file's mtime -- the mtime route
 * forces a date through time_t, and so needs a parser and a generator
 * on top of a store that any copy without -a silently resets.
 *
 * An empty string means "none".
 */
struct libfetch_validators {
    char etag[LIBFETCH_VALIDATOR_MAX + 1];          /* ETag, quotes and all */
    char last_modified[LIBFETCH_VALIDATOR_MAX + 1]; /* HTTP-date, verbatim */
};

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
    LIBFETCH_ERR_TIMEOUT,

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
void libfetch_set_client_certificate(struct libfetch_ctx *ctx, const char *cert_file,
                                     const char *key_file);

/*
 * Seconds a single wait may take before the transfer is abandoned with
 * LIBFETCH_ERR_TIMEOUT; 0 waits indefinitely.
 *
 * This is an idle timeout, not a budget for the transfer: the clock
 * starts afresh on every wait, so it fires only when the peer stops
 * saying anything at all.  A transfer that is merely slow is never cut
 * off, which a deadline over the whole request could not promise.
 *
 * It belongs to the context rather than to the process, so two contexts
 * downloading concurrently can hold different values.  Connections
 * carry the value they were opened with, refreshed from the context
 * when one is taken back out of the cache.
 */
void libfetch_set_timeout(struct libfetch_ctx *ctx, int seconds);

void libfetch_io_close(libfetch_io_t *);
ssize_t libfetch_io_read(libfetch_io_t *, void *, size_t);

/*
 * Retrieve a document.
 *
 * "have" holds the validators recorded for this URL when it was last
 * retrieved, or is NULL to fetch unconditionally.  When it is given,
 * the request carries them and the server may answer 304 Not Modified,
 * which is reported as a NULL return with libfetch_last_error set to
 * LIBFETCH_ERRCAT_HTTP / LIBFETCH_HTTP_NOT_MODIFIED.
 *
 * "got" is filled in with the validators the server offered for what it
 * sent, or emptied when it offered none, and may be NULL.  It is only
 * ever written for a body that was actually returned.
 */

/* HTTP */
libfetch_io_t *libfetch_get_http(struct libfetch_ctx *, struct libfetch_url *, const char *,
                                 const struct libfetch_validators *have,
                                 struct libfetch_validators *got);

/* Generic */
libfetch_io_t *libfetch_get_url(struct libfetch_ctx *, const char *, const char *,
                                const struct libfetch_validators *have,
                                struct libfetch_validators *got);

/* URL parsing.  Used by redirects and the connection cache inside the
 * library, and by aept's download.c, which parses the credential-free
 * url and injects the source's user and password before the request --
 * the one place the two halves of a credentialed source meet. */
struct libfetch_url *libfetch_make_url(const char *, const char *, int, const char *, const char *,
                                       const char *);
struct libfetch_url *libfetch_parse_url(const char *);
struct libfetch_url *libfetch_copy_url(const struct libfetch_url *);
void libfetch_free_url(struct libfetch_url *);

/* The percent-escape decoder the URL parser uses (pctdecode.c),
 * exported so aept's config-time credential split (util.c) shares the
 * one definition of what an escape means. */
const char *libfetch_pctdecode(char *dst, const char *src, const char *brk, size_t dlen);

/* Last error code, per-thread */
extern _Thread_local struct libfetch_error libfetch_last_error;

#if defined(__cplusplus)
}
#endif

#endif
