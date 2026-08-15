/*	$NetBSD: common.h,v 1.24 2016/10/20 21:25:57 joerg Exp $	*/
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
 * $FreeBSD: common.h,v 1.30 2007/12/18 11:03:07 des Exp $
 */

#ifndef LIBFETCH_COMMON_H_7BF97F
#define LIBFETCH_COMMON_H_7BF97F

#define HTTP_DEFAULT_PORT 80
#define HTTPS_DEFAULT_PORT 443
#define HTTP_DEFAULT_PROXY_PORT 3128

#include <sys/types.h>
#include <limits.h>

/*
 * OpenSSL 1.1.1 or newer is required (see configure.ac), so these are
 * included directly.  They used to arrive via an openssl-compat.h that
 * also carried a private X509_check_host() for OpenSSL older than
 * 1.0.2; nothing in the tree needs that any more.
 *
 * x509v3.h is what declares X509_check_host() and its flags.
 */
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

/*
 * Everything a fetch reads from its caller's configuration, plus the
 * connections it keeps alive between calls.  Held by the caller and
 * passed down, so two of them never interfere: in particular a cached
 * connection is never handed to a request that would have presented a
 * different client certificate.
 */
struct libfetch_ctx {
    struct libfetch_conn *connection_cache;
    int cache_global_limit;
    int cache_per_host_limit;
    int timeout; /* seconds per wait; 0 waits indefinitely */
    const char *ssl_client_cert_file;
    const char *ssl_client_key_file;
};

#if defined(__GNUC__) && __GNUC__ >= 3
#define LIBFETCH_PRINTFLIKE(fmtarg, firstvararg)                                                   \
    __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#else
#define LIBFETCH_PRINTFLIKE(fmtarg, firstvararg)
#endif

#if !defined(__sun) && !defined(__hpux) && !defined(__INTERIX) && !defined(__digital__) &&         \
    !defined(__linux) && !defined(__MINT__) && !defined(__sgi) && !defined(__minix) &&             \
    !defined(__CYGWIN__)
#define HAVE_SA_LEN
#endif

#ifndef IPPORT_MAX
#define IPPORT_MAX 65535
#endif

#ifndef OFF_MAX
#define OFF_MAX (((((off_t)1 << (sizeof(off_t) * CHAR_BIT - 2)) - 1) << 1) + 1)
#endif

/* Connection */
typedef struct libfetch_conn libfetch_conn_t;

struct libfetch_conn {
    int sd;                     /* socket descriptor */
    char *buf;                  /* buffer */
    size_t bufsize;             /* buffer size */
    size_t buflen;              /* length of buffer contents */
    int buf_events;             /* poll flags for the next cycle */
    int timeout;                /* copied from the context that opened it */
    char *next_buf;             /* pending buffer, e.g. after getln */
    size_t next_len;            /* size of pending buffer */
    int line_partial;           /* a line was interrupted half-read */
    int err;                    /* last protocol reply code */
    SSL *ssl;                   /* SSL handle */
    SSL_CTX *ssl_ctx;           /* SSL context */
    X509 *ssl_cert;             /* server certificate */
    const SSL_METHOD *ssl_meth; /* SSL method */
    struct libfetch_url *cache_url;
    int cache_af;
    int (*cache_close)(libfetch_conn_t *);
    libfetch_conn_t *next_cached;
};

void libfetch_info(const char *, ...) LIBFETCH_PRINTFLIKE(1, 2);
uintmax_t libfetch_parseuint(const char *p, const char **endptr, unsigned int radix, uintmax_t max);
int libfetch_default_port(const char *);
int libfetch_default_proxy_port(const char *);
libfetch_conn_t *libfetch_cache_get(struct libfetch_ctx *, const struct libfetch_url *, int);
void libfetch_cache_put(struct libfetch_ctx *, libfetch_conn_t *, int (*)(libfetch_conn_t *));
libfetch_conn_t *libfetch_connect(struct libfetch_url *, struct libfetch_url *, int, int, int);
int libfetch_wait(int sd, short events, int timeout);
libfetch_conn_t *libfetch_reopen(int);
int libfetch_ssl(struct libfetch_ctx *, libfetch_conn_t *, const struct libfetch_url *, int);
ssize_t libfetch_read(libfetch_conn_t *, char *, size_t);
int libfetch_getln(libfetch_conn_t *);
ssize_t libfetch_write(libfetch_conn_t *, const void *, size_t);
int libfetch_close(libfetch_conn_t *);
int libfetch_no_proxy_match(const char *);
int libfetch_urlpath_safe(char);

static inline void libfetch_set_error(unsigned int category, int code)
{
    libfetch_last_error = (struct libfetch_error){.category = category, .code = code};
}
static inline void libfetch_syserr(void)
{
    libfetch_set_error(LIBFETCH_ERRCAT_ERRNO, errno);
}

#define libfetch_seterr(n) libfetch_set_error(LIBFETCH_ERRCAT_FETCH, n)
#define url_seterr(n) libfetch_set_error(LIBFETCH_ERRCAT_URL, LIBFETCH_ERR_##n)
#define http_seterr(n) libfetch_set_error(LIBFETCH_ERRCAT_HTTP, n)
#define netdb_seterr(n) libfetch_set_error(LIBFETCH_ERRCAT_NETDB, n)
#define tls_seterr(n) libfetch_set_error(LIBFETCH_ERRCAT_TLS, n)

libfetch_io_t *libfetch_io_unopen(void *, ssize_t (*)(void *, void *, size_t),
                                  ssize_t (*)(void *, const void *, size_t), void (*)(void *));

/*
 * Check whether a particular flag is set
 */
#define CHECK_FLAG(x) (flags && strchr(flags, (x)))

#endif
