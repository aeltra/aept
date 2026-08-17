/*	$NetBSD: fetch.c,v 1.19 2009/08/11 20:48:06 joerg Exp $	*/
/*-
 * Copyright (c) 1998-2004 Dag-Erling Coïdan Smørgrav
 * Copyright (c) 2008 Joerg Sonnenberger <joerg@NetBSD.org>
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
 * $FreeBSD: fetch.c,v 1.41 2007/12/19 00:26:36 des Exp $
 */

/*
 * Split out of fetch.c so that aept's config-time credential handling
 * (util.c) can share the one definition of what a percent-escape means
 * -- and so the unit tests that link util.c can pull in this file alone
 * rather than the whole of libfetch and its OpenSSL dependency.  This
 * file stays under libfetch's BSD licence, above; it deliberately
 * includes nothing of libfetch's.
 */

#include <stddef.h>
#include <string.h>

#include "fetch.h"

/*
 * Return value of the given hex digit.
 */
static int libfetch_hexval(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    else if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

/*
 * Decode percent-encoded URL component from src into dst, stopping at end
 * of string or one of the characters contained in brk.  Returns a pointer
 * to the unhandled part of the input string (null terminator, specified
 * character).  No terminator is written to dst (it is the caller's
 * responsibility).
 */
const char *libfetch_pctdecode(char *dst, const char *src, const char *brk, size_t dlen)
{
    int d1, d2;
    char c;
    const char *s;

    for (s = src; *s != '\0' && !strchr(brk, *s); s++) {
        if (s[0] == '%' && (d1 = libfetch_hexval(s[1])) >= 0 && (d2 = libfetch_hexval(s[2])) >= 0 &&
            (d1 > 0 || d2 > 0)) {
            c = d1 << 4 | d2;
            s += 2;
        } else if (s[0] == '%') {
            /* Invalid escape sequence. */
            return NULL;
        } else {
            c = *s;
        }
        if (!dlen)
            return NULL;
        dlen--;
        *dst++ = c;
    }
    return s;
}
