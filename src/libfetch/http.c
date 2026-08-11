/*	$NetBSD: http.c,v 1.40 2016/10/21 11:51:18 jperkin Exp $	*/
/*-
 * Copyright (c) 2000-2004 Dag-Erling Coïdan Smørgrav
 * Copyright (c) 2003 Thomas Klausner <wiz@NetBSD.org>
 * Copyright (c) 2008, 2009 Joerg Sonnenberger <joerg@NetBSD.org>
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
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD: http.c,v 1.83 2008/02/06 11:39:55 des Exp $
 */

/*
 * The following copyright applies to the base64 code:
 *
 *-
 * Copyright 1997 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "fetch.h"
#include "common.h"

/* Maximum number of redirects to follow */
#define MAX_REDIRECT 5

/* Symbolic names for reply codes we care about */
#define HTTP_OK 200
#define HTTP_PARTIAL 206
#define HTTP_MOVED_PERM 301
#define HTTP_MOVED_TEMP 302
#define HTTP_SEE_OTHER 303
#define HTTP_NOT_MODIFIED 304
#define HTTP_TEMP_REDIRECT 307
#define HTTP_NEED_AUTH 401
#define HTTP_NEED_PROXY_AUTH 407
#define HTTP_BAD_RANGE 416
#define HTTP_PROTOCOL_ERROR 999

#define HTTP_REDIRECT(xyz)                                                     \
    ((xyz) == HTTP_MOVED_PERM || (xyz) == HTTP_MOVED_TEMP ||                   \
     (xyz) == HTTP_TEMP_REDIRECT || (xyz) == HTTP_SEE_OTHER)

#define HTTP_ERROR(xyz) ((xyz) >= 400 && (xyz) < 600)

static int http_cmd(libfetch_conn_t *, const char *, ...)
    LIBFETCH_PRINTFLIKE(2, 3);

/*****************************************************************************
 * I/O functions for decoding chunked streams
 */

struct httpio {
    struct libfetch_ctx *ctx; /* owns the connection cache */
    libfetch_conn_t *conn;    /* connection */
    int chunked;              /* chunked mode */
    int keep_alive;           /* keep-alive mode */
    char *buf;                /* chunk buffer */
    size_t bufsize;           /* size of chunk buffer */
    ssize_t buflen;           /* amount of data currently in buffer */
    int bufpos;               /* current read offset in buffer */
    int eof;                  /* end-of-file flag */
    int error;                /* error flag */
    size_t chunksize;         /* remaining size of current chunk */
    off_t contentlength;      /* remaining size of the content */
};

/*
 * Get next chunk header
 */
static int http_new_chunk(struct httpio *io)
{
    const char *p;

    if (libfetch_getln(io->conn) == -1)
        return -1;

    if (io->conn->buflen < 2)
        return -1;

    io->chunksize = libfetch_parseuint(io->conn->buf, &p, 16, SIZE_MAX);
    if (*p && *p != ';' && !isspace((unsigned char)*p))
        return -1;

    return 0;
}

/*
 * Grow the input buffer to at least len bytes
 */
static int http_growbuf(struct httpio *io, size_t len)
{
    char *tmp;

    if (io->bufsize >= len)
        return 0;

    if ((tmp = realloc(io->buf, len)) == NULL)
        return -1;
    io->buf = tmp;
    io->bufsize = len;
    return 0;
}

/*
 * Fill the input buffer, do chunk decoding on the fly
 */
static int http_fillbuf(struct httpio *io, size_t len)
{
    if (io->error)
        return -1;
    if (io->eof)
        return 0;

    if (io->contentlength >= 0 && (off_t)len > io->contentlength)
        len = io->contentlength;

    if (io->chunked == 0) {
        if (http_growbuf(io, len) == -1)
            return -1;
        if ((io->buflen = libfetch_read(io->conn, io->buf, len)) == -1) {
            io->error = 1;
            return -1;
        }
        if (io->buflen == 0) {
            /*
             * The peer hung up.  If it promised a length and
             * still owes us bytes, the body is short, and a
             * transfer cut in half must not be handed to the
             * caller as a complete one: an unsigned index would
             * be accepted truncated.  Without a promised length
             * the close *is* the end of the body.
             */
            if (io->contentlength > 0) {
                io->error = 1;
                errno = EIO;
                return -1;
            }
            io->eof = 1;
            return 0;
        }
        if (io->contentlength > 0)
            io->contentlength -= io->buflen;
        io->bufpos = 0;
        return io->buflen;
    }

    if (io->chunksize == 0) {
        if (http_new_chunk(io) == -1) {
            io->error = 1;
            return -1;
        }
        if (io->chunksize == 0) {
            io->eof = 1;
            if (libfetch_getln(io->conn) == -1)
                return -1;
            return 0;
        }
    }

    if (len > io->chunksize)
        len = io->chunksize;
    if (http_growbuf(io, len) == -1)
        return -1;
    if ((io->buflen = libfetch_read(io->conn, io->buf, len)) == -1) {
        io->error = 1;
        return -1;
    }
    if (io->buflen == 0) {
        /* End of file in the middle of a chunk: same story. */
        io->error = 1;
        errno = EIO;
        return -1;
    }
    io->chunksize -= io->buflen;
    if (io->contentlength >= 0)
        io->contentlength -= io->buflen;

    if (io->chunksize == 0) {
        char endl[2];
        ssize_t len2;

        /*
         * Every chunk is followed by CRLF.  Missing it means the
         * framing is not what the server claimed, so the stream
         * has to be abandoned -- and io->error is what says so:
         * a bare -1 from here leaves the flag clear, and
         * http_readfn then hands the caller the bytes it had
         * already collected as if the body had simply ended.
         */
        len2 = libfetch_read(io->conn, endl, 2);
        if (len2 == 1 && libfetch_read(io->conn, endl + 1, 1) != 1) {
            io->error = 1;
            return -1;
        }
        if (len2 == -1 || endl[0] != '\r' || endl[1] != '\n') {
            io->error = 1;
            errno = EIO;
            return -1;
        }
    }

    io->bufpos = 0;

    return io->buflen;
}

/*
 * Read function
 */
static ssize_t http_readfn(void *v, void *buf, size_t len)
{
    struct httpio *io = (struct httpio *)v;
    size_t l, pos;

    if (io->error) {
        /*
         * A read that failed part-way returns the bytes it did
         * get, so the failure is reported on the next call --
         * by which time errno belongs to whatever the caller
         * did in between.  Restate it, or a caller that retries
         * on EINTR can spin here forever.
         */
        errno = EIO;
        return -1;
    }
    if (io->eof)
        return 0;

    for (pos = 0; len > 0; pos += l, len -= l) {
        /* empty buffer */
        if (!io->buf || io->bufpos == io->buflen)
            if (http_fillbuf(io, len) < 1)
                break;
        l = io->buflen - io->bufpos;
        if (len < l)
            l = len;
        memcpy((char *)buf + pos, io->buf + io->bufpos, l);
        io->bufpos += l;
    }

    if (!pos && io->error)
        return -1;
    return pos;
}

/*
 * Write function
 */
static ssize_t http_writefn(void *v, const void *buf, size_t len)
{
    struct httpio *io = (struct httpio *)v;

    return libfetch_write(io->conn, buf, len);
}

/*
 * Close function
 */
static void http_closefn(void *v)
{
    struct httpio *io = (struct httpio *)v;
    libfetch_conn_t *conn = io->conn;

    /*
     * A stream that failed stopped at an unknown point in the
     * protocol, so the connection cannot be reused: the next request
     * on it would be answered by the tail of this one's body.
     */
    if (io->keep_alive && !io->error) {
        libfetch_cache_put(io->ctx, conn, libfetch_close);
    } else {
        libfetch_close(conn);
    }

    free(io->buf);
    free(io);
}

/*
 * Wrap a file descriptor up
 */
static libfetch_io_t *http_funopen(struct libfetch_ctx *fctx,
                                   libfetch_conn_t *conn, int chunked,
                                   int keep_alive, off_t clength)
{
    struct httpio *io;
    libfetch_io_t *f;

    if ((io = calloc(1, sizeof(*io))) == NULL) {
        libfetch_syserr();
        return NULL;
    }
    io->ctx = fctx;
    io->conn = conn;
    io->chunked = chunked;
    io->contentlength = clength;
    io->keep_alive = keep_alive;
    f = libfetch_io_unopen(io, http_readfn, http_writefn, http_closefn);
    if (f == NULL) {
        libfetch_syserr();
        free(io);
        return NULL;
    }
    return f;
}

/*****************************************************************************
 * Helper functions for talking to the server and parsing its replies
 */

/* Header types */
typedef enum {
    hdr_syserror = -2,
    hdr_error = -1,
    hdr_end = 0,
    hdr_unknown = 1,
    hdr_connection,
    hdr_content_length,
    hdr_location,
    hdr_transfer_encoding,
    hdr_www_authenticate
} hdr_t;

/* Names of interesting headers */
static struct {
    hdr_t num;
    const char *name;
} hdr_names[] = {
    {hdr_connection, "Connection"},
    {hdr_content_length, "Content-Length"},
    {hdr_location, "Location"},
    {hdr_transfer_encoding, "Transfer-Encoding"},
    {hdr_www_authenticate, "WWW-Authenticate"},
    {hdr_unknown, NULL},
};

/*
 * Send a formatted line; optionally echo to terminal
 */
LIBFETCH_PRINTFLIKE(2, 3)
static int http_cmd(libfetch_conn_t *conn, const char *fmt, ...)
{
    va_list ap;
    size_t len;
    char *msg;
    int r;

    va_start(ap, fmt);
    len = vasprintf(&msg, fmt, ap);
    va_end(ap);

    if (msg == NULL) {
        errno = ENOMEM;
        libfetch_syserr();
        return -1;
    }

    r = libfetch_write(conn, msg, len);
    free(msg);

    if (r == -1) {
        libfetch_syserr();
        return -1;
    }

    return 0;
}

/*
 * Get and parse status line
 */
static int http_get_reply(libfetch_conn_t *conn)
{
    char *p;

    if (libfetch_getln(conn) == -1)
        return -1;
    /*
     * A valid status line looks like "HTTP/m.n xyz reason" where m
     * and n are the major and minor protocol version numbers and xyz
     * is the reply code.
     * Unfortunately, there are servers out there (NCSA 1.5.1, to name
     * just one) that do not send a version number, so we can't rely
     * on finding one, but if we do, insist on it being 1.0 or 1.1.
     * We don't care about the reason phrase.
     */
    if (strncmp(conn->buf, "HTTP", 4) != 0)
        return HTTP_PROTOCOL_ERROR;
    p = conn->buf + 4;
    if (*p == '/') {
        if (p[1] != '1' || p[2] != '.' || (p[3] != '0' && p[3] != '1'))
            return HTTP_PROTOCOL_ERROR;
        p += 4;
    }
    if (*p != ' ' || !isdigit((unsigned char)p[1]) ||
        !isdigit((unsigned char)p[2]) || !isdigit((unsigned char)p[3]))
        return HTTP_PROTOCOL_ERROR;

    conn->err = (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
    return conn->err;
}

/*
 * Check a header; if the type matches the given string, return a pointer
 * to the beginning of the value.
 */
static const char *http_match(const char *str, const char *hdr)
{
    while (*str && *hdr &&
           tolower((unsigned char)*str++) == tolower((unsigned char)*hdr++))
        /* nothing */;
    if (*str || *hdr != ':')
        return NULL;
    while (*hdr && isspace((unsigned char)*++hdr))
        /* nothing */;
    return hdr;
}

/*
 * Get the next header and return the appropriate symbolic code.
 */
static hdr_t http_next_header(libfetch_conn_t *conn, const char **p)
{
    int i;

    if (libfetch_getln(conn) == -1)
        return hdr_syserror;
    while (conn->buflen && isspace((unsigned char)conn->buf[conn->buflen - 1]))
        conn->buflen--;
    conn->buf[conn->buflen] = '\0';
    if (conn->buflen == 0)
        return hdr_end;
    /*
     * We could check for malformed headers but we don't really care.
     * A valid header starts with a token immediately followed by a
     * colon; a token is any sequence of non-control, non-whitespace
     * characters except "()<>@,;:\\\"{}".
     */
    for (i = 0; hdr_names[i].num != hdr_unknown; i++)
        if ((*p = http_match(hdr_names[i].name, conn->buf)) != NULL)
            return hdr_names[i].num;
    return hdr_unknown;
}

/*****************************************************************************
 * Helper functions for authorization
 */

/*
 * Base64 encoding
 */
static char *http_base64(const char *src)
{
    static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                 "abcdefghijklmnopqrstuvwxyz"
                                 "0123456789+/";
    char *str, *dst;
    size_t l;
    int t;

    l = strlen(src);
    if ((str = malloc(((l + 2) / 3) * 4 + 1)) == NULL)
        return NULL;
    dst = str;

    while (l >= 3) {
        t = ((unsigned char)src[0] << 16) | ((unsigned char)src[1] << 8) |
            (unsigned char)src[2];
        dst[0] = base64[(t >> 18) & 0x3f];
        dst[1] = base64[(t >> 12) & 0x3f];
        dst[2] = base64[(t >> 6) & 0x3f];
        dst[3] = base64[(t >> 0) & 0x3f];
        src += 3;
        l -= 3;
        dst += 4;
    }

    switch (l) {
    case 2:
        t = ((unsigned char)src[0] << 16) | ((unsigned char)src[1] << 8);
        dst[0] = base64[(t >> 18) & 0x3f];
        dst[1] = base64[(t >> 12) & 0x3f];
        dst[2] = base64[(t >> 6) & 0x3f];
        dst[3] = '=';
        dst += 4;
        break;
    case 1:
        t = (unsigned char)src[0] << 16;
        dst[0] = base64[(t >> 18) & 0x3f];
        dst[1] = base64[(t >> 12) & 0x3f];
        dst[2] = dst[3] = '=';
        dst += 4;
        break;
    case 0:
        break;
    }

    *dst = 0;
    return str;
}

/*
 * Encode username and password
 */
static int http_basic_auth(libfetch_conn_t *conn, const char *hdr,
                           const char *usr, const char *pwd)
{
    char *upw, *auth;
    int r;

    if (asprintf(&upw, "%s:%s", usr, pwd) == -1)
        return -1;
    auth = http_base64(upw);
    free(upw);
    if (auth == NULL)
        return -1;
    r = http_cmd(conn, "%s: Basic %s\r\n", hdr, auth);
    free(auth);
    return r;
}

/*
 * Send a Proxy authorization header
 *
 * Credentials come from the proxy URL and nowhere else.  Upstream also
 * accepted them in $HTTP_PROXY_AUTH, as "basic:<realm>:<user>:<pass>";
 * that is gone, along with the http_authorize() parser it was the last
 * caller of.  $HTTP_PROXY carries a URL, and a URL already has a place
 * to put a user and a password.
 */
static void http_proxy_authorize(libfetch_conn_t *conn,
                                 struct libfetch_url *purl)
{
    if (!purl)
        return;
    if (*purl->user || *purl->pwd)
        http_basic_auth(conn, "Proxy-Authorization", purl->user, purl->pwd);
}

/*****************************************************************************
 * Helper functions for connecting to a server or proxy
 */

/*
 * Helper for setting socket options regarding packetization
 */
static void http_cork(libfetch_conn_t *conn, int val)
{
#if defined(TCP_CORK)
    setsockopt(conn->sd, IPPROTO_TCP, TCP_CORK, &val, sizeof val);
#else
#if defined(TCP_NOPUSH) && !defined(__APPLE__)
    setsockopt(conn->sd, IPPROTO_TCP, TCP_NOPUSH, &val, sizeof val);
#endif
    val = !val;
    setsockopt(conn->sd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof val);
#endif
}

/*
 * Connect to the correct HTTP server or proxy.
 */
static libfetch_conn_t *http_connect(struct libfetch_ctx *fctx,
                                     struct libfetch_url *URL,
                                     struct libfetch_url *purl,
                                     const char *flags, int *cached)
{
    struct libfetch_url *cache_url;
    libfetch_conn_t *conn;
    hdr_t h;
    const char *p;
    int af, verbose, is_https;

    *cached = 0;
    af = AF_UNSPEC;
    verbose = CHECK_FLAG('v');
    if (CHECK_FLAG('4'))
        af = AF_INET;
    else if (CHECK_FLAG('6'))
        af = AF_INET6;

    is_https = strcasecmp(URL->scheme, LIBFETCH_SCHEME_HTTPS) == 0;
    cache_url = (is_https || !purl) ? URL : purl;

    if ((conn = libfetch_cache_get(fctx, cache_url, af)) != NULL) {
        *cached = 1;
        return conn;
    }

    if ((conn = libfetch_connect(cache_url, purl ?: URL, af, verbose)) == NULL)
        /* libfetch_connect() has already set an error code */
        return NULL;

    if (is_https && purl) {
        http_cork(conn, 1);
        http_cmd(conn, "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n", URL->host,
                 URL->port, URL->host, URL->port);
        http_proxy_authorize(conn, purl);
        http_cmd(conn, "\r\n");
        http_cork(conn, 0);
        if (http_get_reply(conn) != HTTP_OK) {
            http_seterr(conn->err);
            goto ouch;
        }
        do {
            switch ((h = http_next_header(conn, &p))) {
            case hdr_syserror:
                libfetch_syserr();
                goto ouch;
            case hdr_error:
                http_seterr(HTTP_PROTOCOL_ERROR);
                goto ouch;
            default:
                /* ignore */;
            }
        } while (h > hdr_end);
    }
    if (is_https && libfetch_ssl(fctx, conn, URL, verbose) == -1) {
        goto ouch;
    }
    return conn;
ouch:
    libfetch_close(conn);
    return NULL;
}

static struct libfetch_url *http_make_proxy_url(const char *env1,
                                                const char *env2)
{
    struct libfetch_url *purl;
    char *p;

    p = getenv(env1);
    if (!p)
        p = getenv(env2);
    if (!p || !*p)
        return NULL;

    purl = libfetch_parse_url(p);
    if (!purl)
        return NULL;

    if (!*purl->scheme)
        strcpy(purl->scheme, LIBFETCH_SCHEME_HTTP);
    if (!purl->port)
        purl->port = libfetch_default_proxy_port(purl->scheme);

    if (strcasecmp(purl->scheme, LIBFETCH_SCHEME_HTTP) == 0)
        return purl;

    libfetch_free_url(purl);
    return NULL;
}

static struct libfetch_url *http_get_proxy(struct libfetch_url *url,
                                           const char *flags)
{
    if (flags != NULL && strchr(flags, 'd') != NULL)
        return NULL;
    if (libfetch_no_proxy_match(url->host))
        return NULL;
    if (strcasecmp(url->scheme, LIBFETCH_SCHEME_HTTPS) == 0)
        return http_make_proxy_url("HTTPS_PROXY", "https_proxy");
    if (strcasecmp(url->scheme, LIBFETCH_SCHEME_HTTP) == 0)
        return http_make_proxy_url("HTTP_PROXY", "http_proxy");
    return NULL;
}

/*****************************************************************************
 * Core
 */

/*
 * Send a request and process the reply
 *
 * XXX This function is way too long, the do..while loop should be split
 * XXX off into a separate function.
 */
static libfetch_io_t *http_request(struct libfetch_ctx *fctx,
                                   struct libfetch_url *URL, const char *op,
                                   struct libfetch_url *purl, const char *flags)
{
    libfetch_conn_t *conn;
    struct libfetch_url *url, *new;
    int chunked, direct, need_auth, noredirect, nocache;
    int keep_alive, verbose, cached;
    int e, i, n;
    off_t clength;
    const char *p, *q;
    libfetch_io_t *f;
    hdr_t h;
    char hbuf[LIBFETCH_URL_HOSTLEN + 7], *host;

    direct = CHECK_FLAG('d');
    noredirect = CHECK_FLAG('A');
    nocache = CHECK_FLAG('C');
    verbose = CHECK_FLAG('v');
    keep_alive = 0;

    if (direct && purl) {
        libfetch_free_url(purl);
        purl = NULL;
    }

    /* try the provided URL first */
    url = URL;

    /* if the A flag is set, we only get one try */
    n = noredirect ? 1 : MAX_REDIRECT;
    i = 0;

    e = HTTP_PROTOCOL_ERROR;
    need_auth = 0;
    do {
        new = NULL;
        chunked = 0;
        clength = -1;

        /* check port */
        if (!url->port)
            url->port = libfetch_default_port(url->scheme);

        /* connect to server or proxy */
        if ((conn = http_connect(fctx, url, purl, flags, &cached)) == NULL)
            goto ouch;

        host = url->host;
        if (strchr(url->host, ':')) {
            snprintf(hbuf, sizeof(hbuf), "[%s]", url->host);
            host = hbuf;
        }
        if (url->port != libfetch_default_port(url->scheme)) {
            if (host != hbuf) {
                strcpy(hbuf, host);
                host = hbuf;
            }
            snprintf(hbuf + strlen(hbuf), sizeof(hbuf) - strlen(hbuf), ":%d",
                     url->port);
        }

        /* send request */
        if (verbose)
            libfetch_info("requesting %s://%s%s", url->scheme, host, url->doc);

        http_cork(conn, 1);
        if (purl && strcasecmp(URL->scheme, LIBFETCH_SCHEME_HTTPS) != 0) {
            http_cmd(conn, "%s %s://%s%s HTTP/1.1\r\n", op, url->scheme, host,
                     url->doc);
        } else {
            http_cmd(conn, "%s %s HTTP/1.1\r\n", op, url->doc);
        }

        if (nocache)
            http_cmd(conn, "Cache-Control: no-cache\r\n");

        /* virtual host */
        http_cmd(conn, "Host: %s\r\n", host);

        /* proxy authorization */
        http_proxy_authorize(conn, purl);

        /* server authorization */
        if (need_auth || *url->user || *url->pwd) {
            if (*url->user || *url->pwd) {
                http_basic_auth(conn, "Authorization", url->user, url->pwd);
            } else {
                http_seterr(HTTP_NEED_AUTH);
                goto ouch;
            }
        }

        /* other headers */
        if ((p = getenv("HTTP_REFERER")) != NULL && *p != '\0') {
            if (strcasecmp(p, "auto") == 0)
                http_cmd(conn, "Referer: %s://%s%s\r\n", url->scheme, host,
                         url->doc);
            else
                http_cmd(conn, "Referer: %s\r\n", p);
        }
        if ((p = getenv("HTTP_USER_AGENT")) != NULL && *p != '\0')
            http_cmd(conn, "User-Agent: %s\r\n", p);
        else
            http_cmd(conn, "User-Agent: %s\r\n", LIBFETCH_VER);
        http_cmd(conn, "\r\n");

        /*
         * Force the queued request to be dispatched.  Normally, one
         * would do this with shutdown(2) but squid proxies can be
         * configured to disallow such half-closed connections.  To
         * be compatible with such configurations, fiddle with socket
         * options to force the pending data to be written.
         */
        http_cork(conn, 0);

        /* get reply */
        switch (http_get_reply(conn)) {
        case HTTP_OK:
        case HTTP_NOT_MODIFIED:
            /* fine */
            break;
        case HTTP_PARTIAL:
        case HTTP_BAD_RANGE:
            /*
             * A range reply, or a complaint about a range,
             * to a request that carried no Range header --
             * aept never sends one.  Accepting a 206 would
             * mean writing part of a file and calling it
             * whole: a server could hand back the second
             * half of an index and nothing downstream could
             * tell the difference.
             */
            goto protocol_error;
        case HTTP_MOVED_PERM:
        case HTTP_MOVED_TEMP:
        case HTTP_SEE_OTHER:
        case HTTP_TEMP_REDIRECT:
            /*
             * Not so fine, but we still have to read the
             * headers to get the new location.
             */
            break;
        case HTTP_NEED_AUTH:
            if (need_auth) {
                /*
                 * We already sent out authorization code,
                 * so there's nothing more we can do.
                 */
                http_seterr(conn->err);
                goto ouch;
            }
            /* try again, but send the password this time */
            if (verbose)
                libfetch_info("server requires authorization");
            break;
        case HTTP_NEED_PROXY_AUTH:
            /*
             * If we're talking to a proxy, we already sent
             * our proxy authorization code, so there's
             * nothing more we can do.
             */
            http_seterr(conn->err);
            goto ouch;
        case HTTP_PROTOCOL_ERROR:
            /* fall through */
        case -1:
            --i;
            if (cached)
                continue;
            libfetch_syserr();
            goto ouch;
        default:
            http_seterr(conn->err);
            if (!verbose)
                goto ouch;
            /* fall through so we can get the full error message */
        }

        /* get headers */
        do {
            switch ((h = http_next_header(conn, &p))) {
            case hdr_syserror:
                libfetch_syserr();
                goto ouch;
            case hdr_error:
                goto protocol_error;
            case hdr_connection:
                /* XXX too weak? */
                keep_alive = (strcasecmp(p, "keep-alive") == 0);
                break;
            case hdr_content_length:
                clength = libfetch_parseuint(p, &q, 10, OFF_MAX);
                if (*q)
                    goto protocol_error;
                break;
            case hdr_location:
                if (!HTTP_REDIRECT(conn->err))
                    break;
                if (new)
                    libfetch_free_url(new);
                if (verbose)
                    libfetch_info("%d redirect to %s", conn->err, p);
                if (*p == '/')
                    /* absolute path */
                    new = libfetch_make_url(url->scheme, url->host, url->port,
                                            p, url->user, url->pwd);
                else
                    new = libfetch_parse_url(p);
                if (new == NULL) {
                    /* XXX should set an error code */
                    goto ouch;
                }
                if (!new->port)
                    new->port = libfetch_default_port(new->scheme);
                if (!new->user[0] && !new->pwd[0] && new->port == url->port &&
                    strcmp(new->scheme, url->scheme) == 0 &&
                    strcmp(new->host, url->host) == 0) {
                    /* keep auth if staying on same host */
                    strcpy(new->user, url->user);
                    strcpy(new->pwd, url->pwd);
                }
                break;
            case hdr_transfer_encoding:
                /* XXX weak test*/
                chunked = (strcasecmp(p, "chunked") == 0);
                break;
            case hdr_www_authenticate:
                if (conn->err != HTTP_NEED_AUTH)
                    break;
                /* if we were smarter, we'd check the method and realm */
                break;
            case hdr_end:
                /* fall through */
            case hdr_unknown:
                /* ignore */
                break;
            }
        } while (h > hdr_end);

        /* we need to provide authentication */
        if (conn->err == HTTP_NEED_AUTH) {
            e = conn->err;
            need_auth = 1;
            libfetch_close(conn);
            conn = NULL;
            continue;
        }

        /* we have a hit or an error */
        if (conn->err == HTTP_OK || conn->err == HTTP_NOT_MODIFIED ||
            HTTP_ERROR(conn->err))
            break;

        /* all other cases: we got a redirect */
        e = conn->err;
        need_auth = 0;
        libfetch_close(conn);
        conn = NULL;
        if (!new)
            break;
        if (url != URL)
            libfetch_free_url(url);
        url = new;
    } while (++i < n);

    /* we failed, or ran out of retries */
    if (conn == NULL) {
        http_seterr(e);
        goto ouch;
    }

    if (clength == -1 && !chunked)
        keep_alive = 0;

    if (conn->err == HTTP_NOT_MODIFIED) {
        http_seterr(HTTP_NOT_MODIFIED);
        if (keep_alive) {
            libfetch_cache_put(fctx, conn, libfetch_close);
            conn = NULL;
        }
        goto ouch;
    }

    /* wrap it up in a libfetch_io_t */
    if ((f = http_funopen(fctx, conn, chunked, keep_alive, clength)) == NULL) {
        libfetch_syserr();
        goto ouch;
    }

    if (url != URL)
        libfetch_free_url(url);
    if (purl)
        libfetch_free_url(purl);

    if (HTTP_ERROR(conn->err)) {

        if (keep_alive) {
            char buf[512];
            do {
            } while (libfetch_io_read(f, buf, sizeof(buf)) > 0);
        }

        libfetch_io_close(f);
        f = NULL;
    }

    return f;

protocol_error:
    http_seterr(HTTP_PROTOCOL_ERROR);
ouch:
    /* new aliases url once the retry loop has consumed a redirect */
    if (new != NULL &&new != url)
        libfetch_free_url(new);
    if (url != URL)
        libfetch_free_url(url);
    if (purl)
        libfetch_free_url(purl);
    if (conn != NULL)
        libfetch_close(conn);
    return NULL;
}

/*****************************************************************************
 * Entry points
 */

/*
 * Retrieve a file by HTTP
 */
libfetch_io_t *libfetch_get_http(struct libfetch_ctx *fctx,
                                 struct libfetch_url *URL, const char *flags)
{
    return http_request(fctx, URL, "GET", http_get_proxy(URL, flags), flags);
}
