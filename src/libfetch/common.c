/*	$NetBSD: common.c,v 1.31 2016/10/20 21:25:57 joerg Exp $	*/
/*-
 * Copyright (c) 1998-2004 Dag-Erling Coïdan Smørgrav
 * Copyright (c) 2008, 2010 Joerg Sonnenberger <joerg@NetBSD.org>
 * Copyright (c) 2020 Noel Kuntze <noel.kuntze@thermi.consulting>
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
 * $FreeBSD: common.c,v 1.53 2007/12/19 00:26:36 des Exp $
 */

#include <poll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fetch.h"
#include "common.h"

/*** Error-reporting functions ***********************************************/

void libfetch_set_client_certificate(struct libfetch_ctx *ctx, const char *cert_file,
                                     const char *key_file)
{
    ctx->ssl_client_cert_file = cert_file;
    ctx->ssl_client_key_file = key_file;
}

void libfetch_set_timeout(struct libfetch_ctx *ctx, int seconds)
{
    ctx->timeout = seconds > 0 ? seconds : 0;
}

/*
 * Emit status message
 */
void libfetch_info(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/*** Network-related utility functions ***************************************/

/*
 * The radix is unsigned so that the digit check below compares like
 * with like: `d` is a uintmax_t, and against a signed radix the
 * comparison was -Wsign-compare noise.  Every caller passes a literal
 * 10 or 16.
 */
uintmax_t libfetch_parseuint(const char *str, const char **endptr, unsigned int radix,
                             uintmax_t max)
{
    uintmax_t val = 0, maxx = max / radix, d;
    const char *p;

    for (p = str; isxdigit((unsigned char)*p); p++) {
        unsigned char ch = (unsigned char)*p;
        if (isdigit(ch))
            d = ch - '0';
        else
            d = tolower(ch) - 'a' + 10;
        if (d >= radix || val > maxx)
            goto err;
        val *= radix;
        if (val > max - d)
            goto err;
        val += d;
    }
    if (p == str || val > max)
        goto err;
    *endptr = p;
    return val;
err:
    *endptr = "\xff";
    return 0;
}

/*
 * Return the default port for a scheme
 */
int libfetch_default_port(const char *scheme)
{
    struct servent *se;

    if ((se = getservbyname(scheme, "tcp")) != NULL)
        return ntohs(se->s_port);
    if (strcasecmp(scheme, LIBFETCH_SCHEME_HTTP) == 0)
        return HTTP_DEFAULT_PORT;
    if (strcasecmp(scheme, LIBFETCH_SCHEME_HTTPS) == 0)
        return HTTPS_DEFAULT_PORT;
    return 0;
}

/*
 * Return the default proxy port for a scheme
 */
int libfetch_default_proxy_port(const char *scheme)
{
    return HTTP_DEFAULT_PROXY_PORT;
}

/*
 * Create a connection for an existing descriptor.
 */
libfetch_conn_t *libfetch_reopen(int sd)
{
    libfetch_conn_t *conn;

    /* allocate and fill connection structure */
    if ((conn = calloc(1, sizeof(*conn))) == NULL)
        return NULL;
    conn->cache_url = NULL;
    conn->next_buf = NULL;
    conn->next_len = 0;
    conn->sd = sd;
    /* POLLIN from the start, so a plain read waits in libfetch_wait()
     * rather than in read(2).  libfetch_ssl() clears it: on a TLS
     * connection it is SSL_read() that names the readiness it wants. */
    conn->buf_events = POLLIN;
    return conn;
}

/*
 * Every wait in this library funnels through here, and every wait is a
 * poll: connect, the TLS handshake, reads and writes alike.  Two
 * properties follow from that, and neither survives if any of them is
 * allowed to block in a bare read(2) instead.
 *
 * It can be bounded.  `timeout` is the seconds this one wait may take,
 * afresh each time -- an idle timeout, so a slow transfer is never cut
 * off, only a silent one.  Zero waits indefinitely.
 *
 * It can be interrupted.  poll(2) is never restarted after a signal,
 * whatever flags the handler carries, whereas read(2) is restarted when
 * the handler was installed with SA_RESTART -- which is what glibc's
 * signal(3) gives a C embedder by default, and what Python's
 * signal.siginterrupt(sig, False) turns on.  Waiting here rather than in
 * read(2) is therefore what lets any embedder abandon a transfer, and
 * EINTR is reported rather than retried so the caller can act on it.
 *
 * One wait escapes both properties: getaddrinfo(3) retries internally on
 * EINTR and is not ours to bound, so name resolution is capped only by
 * the resolver's own configuration -- around ten seconds by default.
 */
int libfetch_wait(int sd, short events, int timeout)
{
    struct pollfd pfd = {.fd = sd, .events = events};
    int r;

    errno = 0;
    r = poll(&pfd, 1, timeout > 0 ? timeout * 1000 : -1);
    if (r == 0) {
        libfetch_seterr(LIBFETCH_ERR_TIMEOUT);
        return -1;
    }
    if (r == -1) {
        libfetch_syserr();
        return -1;
    }
    return 0;
}

/*
 * Establish a TCP connection to the specified port on the specified host.
 */
libfetch_conn_t *libfetch_connect(struct libfetch_url *cache_url, struct libfetch_url *url, int af,
                                  int verbose, int timeout)
{
    libfetch_conn_t *conn;
    char pbuf[10];
    struct addrinfo hints, *res, *res0;
    int sd, error, reported = 0;

    if (verbose)
        libfetch_info("looking up %s", url->host);

    /* look up host name and set up socket address structure */
    snprintf(pbuf, sizeof(pbuf), "%d", url->port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = af;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    if ((error = getaddrinfo(url->host, pbuf, &hints, &res0)) != 0) {
        netdb_seterr(error);
        return NULL;
    }
    if (verbose)
        libfetch_info("connecting to %s:%d", url->host, url->port);

    /*
     * The socket is non-blocking for the connect whether or not a
     * timeout is set, so that the wait always happens in libfetch_wait()
     * -- bounded when a timeout is configured, interruptible either way.
     * Blocking is restored below; libfetch_ssl() turns it off again for
     * the life of a TLS connection.
     */
    for (sd = -1, res = res0; res; sd = -1, res = res->ai_next) {
        /* Reset per attempt, so the last address tried is the one whose
         * failure gets reported. */
        reported = 0;

        if ((sd = socket(res->ai_family, res->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK,
                         res->ai_protocol)) == -1)
            continue;

        if (connect(sd, res->ai_addr, res->ai_addrlen) == 0)
            break;

        if (errno == EINPROGRESS) {
            if (libfetch_wait(sd, POLLOUT, timeout) == 0) {
                socklen_t len = sizeof(error);

                if (getsockopt(sd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0)
                    break;
                errno = error;
            } else {
                /* libfetch_wait() has recorded the timeout or the
                 * signal; close() below would clobber errno. */
                reported = 1;
            }
        }
        close(sd);
    }
    freeaddrinfo(res0);
    if (sd == -1) {
        if (!reported)
            libfetch_syserr();
        return NULL;
    }

    if (fcntl(sd, F_SETFL, fcntl(sd, F_GETFL) & ~O_NONBLOCK) == -1) {
        libfetch_syserr();
        close(sd);
        return NULL;
    }

    if ((conn = libfetch_reopen(sd)) == NULL) {
        libfetch_syserr();
        close(sd);
        return NULL;
    }
    conn->cache_url = libfetch_copy_url(cache_url);
    conn->cache_af = af;
    conn->timeout = timeout;
    return conn;
}

/*
 * Create a fetch context holding a connection cache with the given
 * limits.  Everything a fetch reads from its caller's configuration
 * lives here, so two contexts never see each other's connections or
 * each other's client certificate.
 */
struct libfetch_ctx *libfetch_ctx_new(int global_limit, int per_host_limit)
{
    struct libfetch_ctx *ctx = calloc(1, sizeof(*ctx));

    if (ctx == NULL)
        return NULL;

    if (global_limit < 0)
        ctx->cache_global_limit = INT_MAX;
    else if (per_host_limit > global_limit)
        ctx->cache_global_limit = per_host_limit;
    else
        ctx->cache_global_limit = global_limit;
    if (per_host_limit < 0)
        ctx->cache_per_host_limit = INT_MAX;
    else
        ctx->cache_per_host_limit = per_host_limit;

    return ctx;
}

/*
 * Flush the cache, free all associated resources and the context.
 */
void libfetch_ctx_free(struct libfetch_ctx *ctx)
{
    libfetch_conn_t *conn;

    if (ctx == NULL)
        return;

    while ((conn = ctx->connection_cache) != NULL) {
        ctx->connection_cache = conn->next_cached;
        (*conn->cache_close)(conn);
    }

    free(ctx);
}

/*
 * Check connection cache for an existing entry matching
 * protocol/host/port/user/password/family.
 */
libfetch_conn_t *libfetch_cache_get(struct libfetch_ctx *ctx, const struct libfetch_url *url,
                                    int af)
{
    libfetch_conn_t *conn, *last_conn = NULL;

    for (conn = ctx->connection_cache; conn; conn = conn->next_cached) {
        if (conn->cache_url->port == url->port &&
            strcmp(conn->cache_url->scheme, url->scheme) == 0 &&
            strcmp(conn->cache_url->host, url->host) == 0 &&
            strcmp(conn->cache_url->user, url->user) == 0 &&
            strcmp(conn->cache_url->pwd, url->pwd) == 0 &&
            (conn->cache_af == AF_UNSPEC || af == AF_UNSPEC || conn->cache_af == af)) {
            if (last_conn != NULL)
                last_conn->next_cached = conn->next_cached;
            else
                ctx->connection_cache = conn->next_cached;
            /* The connection carries the timeout it was opened with;
             * refresh it in case the context has been reconfigured
             * since. */
            conn->timeout = ctx->timeout;
            return conn;
        }
        last_conn = conn;
    }

    return NULL;
}

/*
 * Put the connection back into the cache for reuse.
 * If the connection is freed due to LRU or if the cache
 * is explicitly closed, the given callback is called.
 */
void libfetch_cache_put(struct libfetch_ctx *ctx, libfetch_conn_t *conn,
                        int (*closecb)(libfetch_conn_t *))
{
    libfetch_conn_t *iter, *last, *next_cached;
    int global_count, host_count;

    if (conn->cache_url == NULL || ctx->cache_global_limit == 0) {
        (*closecb)(conn);
        return;
    }

    global_count = host_count = 0;
    last = NULL;
    for (iter = ctx->connection_cache; iter; iter = next_cached) {
        int host_match;

        next_cached = iter->next_cached;
        ++global_count;
        host_match = strcmp(conn->cache_url->host, iter->cache_url->host) == 0;
        if (host_match)
            ++host_count;
        if (global_count < ctx->cache_global_limit && host_count < ctx->cache_per_host_limit) {
            last = iter;
            continue;
        }
        --global_count;
        if (host_match)
            --host_count;
        if (last != NULL)
            last->next_cached = iter->next_cached;
        else
            ctx->connection_cache = iter->next_cached;
        (*iter->cache_close)(iter);
    }

    conn->cache_close = closecb;
    conn->next_cached = ctx->connection_cache;
    ctx->connection_cache = conn;
}

/*
 * Configure peer verification:
 *   1. If compile time #define CA_CERT_FILE is set, and it exists, use it.
 *   2. Otherwise use the CA store OpenSSL was built to look in.
 *
 * Deliberately not SSL_CTX_set_default_verify_paths(): that resolves
 * the default store through $SSL_CERT_FILE and $SSL_CERT_DIR, so one
 * environment variable replaces every CA aept trusts -- with the
 * attacker's own, if they have one, and it reports success either way.
 * X509_get_default_cert_{file,dir}() return the compiled-in paths, so
 * naming them explicitly gets the same store without the override.
 */
/* Returns 0 on success, -1 on error, per the project convention. */
static int libfetch_ssl_setup_peer_verification(SSL_CTX *ctx, int verbose)
{
    const char *ca_file = NULL;

#ifdef CA_CERT_FILE
    if (access(CA_CERT_FILE, R_OK) == 0) {
        ca_file = CA_CERT_FILE;
#ifdef CA_CRL_FILE
        if (access(CA_CRL_FILE, R_OK) == 0) {
            X509_STORE *crl_store = SSL_CTX_get_cert_store(ctx);
            X509_LOOKUP *crl_lookup = X509_STORE_add_lookup(crl_store, X509_LOOKUP_file());
            if (!crl_lookup || !X509_load_crl_file(crl_lookup, CA_CRL_FILE, X509_FILETYPE_PEM)) {
                fprintf(stderr, "Could not load CRL file %s\n", CA_CRL_FILE);
                return -1;
            }
            X509_STORE_set_flags(crl_store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
        }
#endif
    }
#endif
    if (ca_file) {
        if (SSL_CTX_load_verify_locations(ctx, ca_file, NULL) != 1) {
            fprintf(stderr, "Could not load CA file %s\n", ca_file);
            return -1;
        }
    } else {
        /*
         * Two calls, because a system may ship one or the other:
         * on Debian the file is a symlink to the bundle and the
         * directory is the hashed store, elsewhere only the
         * directory exists.  Failing both means an empty trust
         * store, which is worth saying out loud -- every fetch
         * would otherwise fail with a certificate error that
         * looks like the server's fault.
         */
        int have_file, have_dir;

        have_file = SSL_CTX_load_verify_locations(ctx, X509_get_default_cert_file(), NULL) == 1;
        have_dir = SSL_CTX_load_verify_locations(ctx, NULL, X509_get_default_cert_dir()) == 1;

        if (!have_file && !have_dir) {
            fprintf(stderr,
                    "Could not load any trusted CA "
                    "certificates from %s or %s\n",
                    X509_get_default_cert_file(), X509_get_default_cert_dir());
            return -1;
        }

        /*
         * Whichever of the two failed left its complaint on the
         * error queue, and map_tls_error() reads that queue to
         * describe a later handshake failure.  Drop it.
         */
        ERR_clear_error();
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, 0);
    return 0;
}

/*
 * Configure client certificate:
 *  1. Use the files set with libfetch_set_client_certificate() if any
 *  2. Use compile time set CLIENT_{CERT,KEY}_FILE #define's if set
 *  3. No client certificate used
 *
 * If the key file is not specified, it is assumed that the certificate
 * file is a .pem file containing both the cert and the key.
 *
 * Upstream also read SSL_CLIENT_{CERT,KEY}_FILE from the environment.
 * That is gone: the client identity aept presents to a repository is
 * configuration, and a process that inherits a stray variable should
 * not silently start authenticating as somebody else.
 */
/* Returns 0 on success, -1 on error, per the project convention. */
static int libfetch_ssl_setup_client_certificate(struct libfetch_ctx *fctx, SSL_CTX *ctx,
                                                 int verbose)
{
    const char *cert_file = NULL, *key_file = NULL;

    cert_file = fctx->ssl_client_cert_file;
    if (cert_file)
        key_file = fctx->ssl_client_key_file;

#ifdef CLIENT_CERT_FILE
    if (!cert_file && access(CLIENT_CERT_FILE, R_OK) == 0) {
        cert_file = CLIENT_CERT_FILE;
#ifdef CLIENT_KEY_FILE
        if (access(CLIENT_KEY_FILE, R_OK) == 0)
            key_file = CLIENT_KEY_FILE;
#endif
    }
#endif
    if (!cert_file)
        return 0;
    if (!key_file)
        key_file = cert_file;

    if (verbose) {
        libfetch_info("Using client cert file: %s", cert_file);
        libfetch_info("Using client key file: %s", key_file);
    }

    if (SSL_CTX_use_certificate_chain_file(ctx, cert_file) != 1) {
        fprintf(stderr, "Could not load client certificate %s\n", cert_file);
        return -1;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1) {
        fprintf(stderr, "Could not load client key %s\n", key_file);
        return -1;
    }

    return 0;
}

static int map_tls_error(void)
{
    unsigned long err = ERR_peek_error();
    if (ERR_GET_LIB(err) != ERR_LIB_SSL)
        err = ERR_peek_last_error();
    if (ERR_GET_LIB(err) != ERR_LIB_SSL)
        return LIBFETCH_ERR_TLS;
    switch (ERR_GET_REASON(err)) {
    case SSL_R_CERTIFICATE_VERIFY_FAILED:
        return LIBFETCH_ERR_TLS_SERVER_CERT_UNTRUSTED;
    case SSL_AD_REASON_OFFSET + TLS1_AD_UNKNOWN_CA:
        return LIBFETCH_ERR_TLS_CLIENT_CERT_UNTRUSTED;
    case SSL_AD_REASON_OFFSET + SSL3_AD_HANDSHAKE_FAILURE:
        return LIBFETCH_ERR_TLS_HANDSHAKE;
    default:
        return LIBFETCH_ERR_TLS;
    }
}

static int set_nonblocking(int sd)
{
    int flags = fcntl(sd, F_GETFL);

    if (flags == -1)
        return -1;
    return fcntl(sd, F_SETFL, flags | O_NONBLOCK);
}

/*
 * Enable SSL on a connection.
 */
int libfetch_ssl(struct libfetch_ctx *fctx, libfetch_conn_t *conn, const struct libfetch_url *URL,
                 int verbose)
{
    conn->ssl_meth = TLS_client_method();
    conn->ssl_ctx = SSL_CTX_new(conn->ssl_meth);
    if (conn->ssl_ctx == NULL)
        goto err;
    SSL_CTX_set_mode(conn->ssl_ctx, SSL_MODE_AUTO_RETRY);

    if (libfetch_ssl_setup_peer_verification(conn->ssl_ctx, verbose) < 0)
        goto err;
    if (libfetch_ssl_setup_client_certificate(fctx, conn->ssl_ctx, verbose) < 0)
        goto err;

    conn->ssl = SSL_new(conn->ssl_ctx);
    if (conn->ssl == NULL)
        goto err;

    conn->buf_events = 0;

    /*
     * The socket goes non-blocking here and stays that way for the life
     * of the connection, so OpenSSL never waits inside its own read(2):
     * every wait is a libfetch_wait() we can bound and interrupt.  The
     * handshake in particular was unreachable by any timeout before,
     * which mattered because it is where a peer that accepts and then
     * says nothing leaves us -- before a single byte of HTTP.
     */
    if (set_nonblocking(conn->sd) == -1) {
        libfetch_syserr();
        return -1;
    }

    SSL_set_fd(conn->ssl, conn->sd);
    if (!SSL_set_tlsext_host_name(conn->ssl, (char *)(uintptr_t)URL->host)) {
        fprintf(stderr, "TLS server name indication extension failed for host %s\n", URL->host);
        goto err;
    }

    for (;;) {
        int r = SSL_connect(conn->ssl);

        if (r == 1)
            break;
        switch (SSL_get_error(conn->ssl, r)) {
        case SSL_ERROR_WANT_READ:
            if (libfetch_wait(conn->sd, POLLIN, conn->timeout) == -1)
                return -1;
            break;
        case SSL_ERROR_WANT_WRITE:
            if (libfetch_wait(conn->sd, POLLOUT, conn->timeout) == -1)
                return -1;
            break;
        default:
            tls_seterr(map_tls_error());
            return -1;
        }
    }

    conn->ssl_cert = SSL_get_peer_certificate(conn->ssl);
    if (!conn->ssl_cert)
        goto err;

    /*
     * Unconditional: upstream let SSL_NO_VERIFY_HOSTNAME switch this
     * off, which turned any certificate the CA store accepts into a
     * certificate for every host.  aept runs as root and installs
     * what it downloads, so there is no caller for whom that is a
     * reasonable trade.
     */
    if (verbose)
        libfetch_info("Verify hostname");
    if (X509_check_host(conn->ssl_cert, URL->host, strlen(URL->host),
                        X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS, NULL) != 1) {
        tls_seterr(LIBFETCH_ERR_TLS_SERVER_CERT_HOSTNAME);
        return -1;
    }

    if (verbose) {
        X509_NAME *name;
        char *str;

        libfetch_info("SSL connection established using %s\n", SSL_get_cipher(conn->ssl));
        name = X509_get_subject_name(conn->ssl_cert);
        str = X509_NAME_oneline(name, 0, 0);
        libfetch_info("Certificate subject: %s", str);
        free(str);
        name = X509_get_issuer_name(conn->ssl_cert);
        str = X509_NAME_oneline(name, 0, 0);
        libfetch_info("Certificate issuer: %s", str);
        free(str);
    }

    return 0;
err:
    tls_seterr(LIBFETCH_ERR_TLS);
    return -1;
}

/*
 * Read from a connection.  Waits until data arrives, the peer hangs up,
 * the idle timeout expires or a signal interrupts it.
 */
ssize_t libfetch_read(libfetch_conn_t *conn, char *buf, size_t len)
{
    ssize_t rlen;

    if (len == 0)
        return 0;

    if (conn->next_len != 0) {
        if (conn->next_len < len)
            len = conn->next_len;
        memmove(buf, conn->next_buf, len);
        conn->next_len -= len;
        conn->next_buf += len;
        return len;
    }

    for (;;) {
        if (conn->buf_events &&
            libfetch_wait(conn->sd, (short)conn->buf_events, conn->timeout) == -1)
            return -1;

        if (conn->ssl != NULL) {
            rlen = SSL_read(conn->ssl, buf, len);
            if (rlen == -1) {
                switch (SSL_get_error(conn->ssl, rlen)) {
                case SSL_ERROR_WANT_READ:
                    conn->buf_events = POLLIN;
                    continue;
                case SSL_ERROR_WANT_WRITE:
                    conn->buf_events = POLLOUT;
                    continue;
                default:
                    errno = EIO;
                    libfetch_syserr();
                    return -1;
                }
            } else {
                /* Assume buffering on the SSL layer. */
                conn->buf_events = 0;
            }
        } else {
            rlen = read(conn->sd, buf, len);
        }
        if (rlen >= 0)
            break;

        /* Only reachable when read(2) itself failed -- every other
         * failure above has returned already, having recorded its own
         * reason.  Report this one here too, so that callers never need
         * to guess at errno and overwrite a timeout with it. */
        libfetch_syserr();
        return -1;
    }
    return rlen;
}

/*
 * Read a line of text from a connection
 */
#define MIN_BUF_SIZE 1024

int libfetch_getln(libfetch_conn_t *conn)
{
    char *tmp, *next;
    size_t tmpsize;
    ssize_t len;

    if (conn->buf == NULL) {
        if ((conn->buf = malloc(MIN_BUF_SIZE)) == NULL) {
            errno = ENOMEM;
            return -1;
        }
        conn->bufsize = MIN_BUF_SIZE;
    }

    conn->buflen = 0;
    next = NULL;

    do {
        /*
         * conn->bufsize != conn->buflen at this point,
         * so the buffer can be NUL-terminated below for
         * the case of len == 0.
         */
        len = libfetch_read(conn, conn->buf + conn->buflen, conn->bufsize - conn->buflen);
        if (len == -1)
            return -1;
        if (len == 0)
            break;
        next = memchr(conn->buf + conn->buflen, '\n', len);
        conn->buflen += len;
        if (conn->buflen == conn->bufsize && next == NULL) {
            tmp = conn->buf;
            tmpsize = conn->bufsize * 2;
            if (tmpsize < conn->bufsize) {
                errno = ENOMEM;
                return -1;
            }
            if ((tmp = realloc(tmp, tmpsize)) == NULL) {
                errno = ENOMEM;
                return -1;
            }
            conn->buf = tmp;
            conn->bufsize = tmpsize;
        }
    } while (next == NULL);

    if (next != NULL) {
        *next = '\0';
        conn->next_buf = next + 1;
        conn->next_len = conn->buflen - (conn->next_buf - conn->buf);
        conn->buflen = next - conn->buf;
    } else {
        conn->buf[conn->buflen] = '\0';
        conn->next_len = 0;
    }
    return 0;
}

/*
 * Write to a connection.  Waits for the socket to accept the data, the
 * idle timeout to expire or a signal to interrupt it.
 */
ssize_t libfetch_write(libfetch_conn_t *conn, const void *buf, size_t len)
{
    ssize_t wlen, total;

    total = 0;
    while (len) {
        errno = 0;
        if (conn->ssl != NULL) {
            /* Non-blocking on a TLS connection, so a write that cannot
             * proceed asks to be retried rather than blocking inside
             * OpenSSL.  It may want readability, mid-renegotiation. */
            wlen = SSL_write(conn->ssl, buf, len);
            if (wlen <= 0) {
                switch (SSL_get_error(conn->ssl, wlen)) {
                case SSL_ERROR_WANT_READ:
                    if (libfetch_wait(conn->sd, POLLIN, conn->timeout) == -1)
                        return -1;
                    continue;
                case SSL_ERROR_WANT_WRITE:
                    if (libfetch_wait(conn->sd, POLLOUT, conn->timeout) == -1)
                        return -1;
                    continue;
                default:
                    errno = EIO;
                    libfetch_syserr();
                    return -1;
                }
            }
        } else {
            /* Wait here rather than in a blocking send(), so a peer that
             * stops reading is bounded like every other stall. */
            if (libfetch_wait(conn->sd, POLLOUT, conn->timeout) == -1)
                return -1;
            wlen = send(conn->sd, buf, len, MSG_NOSIGNAL);
            if (wlen == 0) {
                /* we consider a short write a failure */
                errno = EPIPE;
                libfetch_syserr();
                return -1;
            }
            if (wlen < 0)
                return -1;
        }
        total += wlen;
        buf = (const char *)buf + wlen;
        len -= wlen;
    }
    return total;
}

/*
 * Close connection
 */
int libfetch_close(libfetch_conn_t *conn)
{
    int ret;

    if (conn->ssl) {
        SSL_shutdown(conn->ssl);
        SSL_set_connect_state(conn->ssl);
        SSL_free(conn->ssl);
    }
    if (conn->ssl_ctx) {
        SSL_CTX_free(conn->ssl_ctx);
    }
    if (conn->ssl_cert) {
        X509_free(conn->ssl_cert);
    }

    ret = close(conn->sd);
    if (conn->cache_url)
        libfetch_free_url(conn->cache_url);
    free(conn->buf);
    free(conn);
    return ret;
}

#define MAX_ADDRESS_BYTES sizeof(struct in6_addr)
#define MAX_ADDRESS_STRING INET6_ADDRSTRLEN
#define MAX_CIDR_STRING (MAX_ADDRESS_STRING + 4)

static size_t host_to_address(uint8_t *buf, size_t buf_len, const char *host, size_t len)
{
    char tmp[MAX_ADDRESS_STRING];

    if (len >= sizeof tmp)
        return 0;
    if (buf_len < sizeof(struct in6_addr))
        return 0;

    /* Make zero terminated copy of the hostname */
    memcpy(tmp, host, len);
    tmp[len] = 0;

    if (inet_pton(AF_INET, tmp, (struct in_addr *)buf))
        return sizeof(struct in_addr);
    if (inet_pton(AF_INET6, tmp, (struct in6_addr *)buf))
        return sizeof(struct in6_addr);
    return 0;
}

static int bitcmp(const uint8_t *a, const uint8_t *b, int len)
{
    int bytes, bits, mask, r;

    bytes = len / 8;
    bits = len % 8;
    if (bytes != 0) {
        r = memcmp(a, b, bytes);
        if (r != 0)
            return r;
    }
    if (bits != 0) {
        mask = (0xff << (8 - bits)) & 0xff;
        return ((int)(a[bytes] & mask)) - ((int)(b[bytes] & mask));
    }
    return 0;
}

static int cidr_match(const uint8_t *addr, size_t addr_len, const char *cidr, size_t cidr_len)
{
    const char *slash;
    uint8_t cidr_addr[MAX_ADDRESS_BYTES];
    size_t cidr_addrlen;
    long bits;

    if (!addr_len || cidr_len > MAX_CIDR_STRING)
        return 0;
    slash = memchr(cidr, '/', cidr_len);
    if (!slash)
        return 0;
    /*
     * Reject a non-positive prefix length here rather than leaving it
     * to the width check below.  That check compared a long against a
     * size_t, so a negative `bits` was converted to a huge unsigned
     * value and rejected by accident -- correct, but only as a side
     * effect of the conversion the compiler was warning about.
     */
    bits = strtol(slash + 1, NULL, 10);
    if (bits < 1 || bits > 128)
        return 0;

    cidr_addrlen = host_to_address(cidr_addr, sizeof cidr_addr, cidr, slash - cidr);
    if (cidr_addrlen != addr_len || (size_t)bits > addr_len * 8)
        return 0;
    return bitcmp(cidr_addr, addr, bits) == 0;
}

/*
 * The no_proxy environment variable specifies a set of domains for
 * which the proxy should not be consulted; the contents is a comma-,
 * or space-separated list of domain names.  A single asterisk will
 * override all proxy variables and no transactions will be proxied
 * (for compatability with lynx and curl, see the discussion at
 * <http://curl.haxx.se/mail/archive_pre_oct_99/0009.html>).
 */
int libfetch_no_proxy_match(const char *host)
{
    const char *no_proxy, *p, *q;
    uint8_t addr[MAX_ADDRESS_BYTES];
    size_t h_len, d_len, addr_len;

    if ((no_proxy = getenv("NO_PROXY")) == NULL && (no_proxy = getenv("no_proxy")) == NULL)
        return 0;

    /* asterisk matches any hostname */
    if (strcmp(no_proxy, "*") == 0)
        return 1;

    h_len = strlen(host);
    addr_len = host_to_address(addr, sizeof addr, host, h_len);
    p = no_proxy;
    do {
        /* position p at the beginning of a domain suffix */
        while (*p == ',' || isspace((unsigned char)*p))
            p++;

        /* position q at the first separator character */
        for (q = p; *q; ++q)
            if (*q == ',' || isspace((unsigned char)*q))
                break;

        d_len = q - p;

        /*
         * A leading dot is accepted and ignored, so ".example.com" and
         * "example.com" mean the same thing.  curl and apt both read it
         * that way, and a no_proxy string is usually copied from one of
         * them.
         */
        while (d_len > 0 && *p == '.') {
            p++;
            d_len--;
        }

        /*
         * The host must *be* the domain or sit under it -- the entry has
         * to line up with a label boundary.  This was a bare suffix
         * compare, so "example.com" also matched "notexample.com", and
         * the request then went direct instead of through the proxy.
         * Silently: the fetch succeeds either way, and only a packet
         * capture or a puzzled look at the proxy's logs would show it.
         */
        if (d_len > 0 && h_len >= d_len && strncasecmp(host + h_len - d_len, p, d_len) == 0 &&
            (h_len == d_len || host[h_len - d_len - 1] == '.')) {
            /* domain name matches */
            return 1;
        }

        if (cidr_match(addr, addr_len, p, d_len)) {
            return 1;
        }

        p = q + 1;
    } while (*q);

    return 0;
}

struct libfetch_io_t {
    void *io_cookie;
    ssize_t (*io_read)(void *, void *, size_t);
    ssize_t (*io_write)(void *, const void *, size_t);
    void (*io_close)(void *);
};

void libfetch_io_close(libfetch_io_t *f)
{
    if (f->io_close != NULL)
        (*f->io_close)(f->io_cookie);

    free(f);
}

libfetch_io_t *libfetch_io_unopen(void *io_cookie, ssize_t (*io_read)(void *, void *, size_t),
                                  ssize_t (*io_write)(void *, const void *, size_t),
                                  void (*io_close)(void *))
{
    libfetch_io_t *f;

    f = malloc(sizeof(*f));
    if (f == NULL)
        return f;

    f->io_cookie = io_cookie;
    f->io_read = io_read;
    f->io_write = io_write;
    f->io_close = io_close;

    return f;
}

ssize_t libfetch_io_read(libfetch_io_t *f, void *buf, size_t len)
{
    if (f->io_read == NULL) {
        errno = EBADF;
        return -1;
    }
    return (*f->io_read)(f->io_cookie, buf, len);
}
