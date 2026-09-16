/* cli.h - what the command files share with main.c
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef CLI_H_7BF97F
#define CLI_H_7BF97F

#include "aept/aept.h"

/*
 * Two kinds of option parser here, and which kind a function is decides
 * its optstring.
 *
 * A DISPATCHING parser hands the rest of the line to another parser, so
 * it must stop at the first non-option.  Otherwise it scans on and meets
 * options belonging to the inner parser: "aept mark manual --all" died
 * in cmd_mark() that way, on an --all that was never its to read.
 * main() and cmd_mark() are the two today; a sub-sub-command would add a
 * third, and it must say so here.
 *
 * A LEAF parser owns the whole line, so it permutes: options may come
 * before or after the operands, and "aept install curl --force-depends"
 * means what it looks like.  Without that, getopt stops at "curl" and
 * --force-depends reaches aept_install() as a package name.
 *
 * Both reset with optind = 0, never 1.  glibc re-reads the optstring
 * only on a full reinitialisation, which is what 0 asks for; at 1 it
 * silently reuses the ordering it derived for the previous parse, so the
 * "+" below -- or its absence -- is never looked at, and every parser
 * inherits main()'s.  musl re-reads on every call.  Each half needs the
 * other: "+" with optind = 1 does nothing on glibc, and optind = 0
 * without "+" would break the dispatchers there.  Together the two libcs
 * agree; tests/test_option_order.sh pins that, and
 * scripts/musl-build.sh is how the musl half gets run.
 */
#define OPTS_DISPATCH(s) "+" s
#define OPTS_LEAF(s) s

/*
 * A context with the global options applied -- the config file, the
 * offline root, the cache directory and the verbosity main() parsed
 * before the command name.  Returns NULL having reported why.
 */
aept_ctx_t *init_aept(void);

/* Release ctx, and the signal handler's view of it. */
void cli_cleanup(aept_ctx_t *ctx);

/*
 * Turn a transaction result into an exit status.  A trigger left owing
 * is not a failure but is not silence either, so it has one of its own.
 */
int transaction_exit(aept_ctx_t *ctx, int r);

/*
 * One per command word, each parsing its own tail of the line.  main()
 * dispatches on the word and hands over from there.
 */
int cmd_update(int argc, char *argv[]);
int cmd_install(int argc, char *argv[]);
int cmd_remove(int argc, char *argv[]);
int cmd_autoremove(int argc, char *argv[]);
int cmd_upgrade(int argc, char *argv[]);
int cmd_clean(int argc, char *argv[]);
int cmd_triggers(int argc, char *argv[]);
int cmd_list(int argc, char *argv[]);
int cmd_show(int argc, char *argv[]);
int cmd_files(int argc, char *argv[]);
int cmd_owns(int argc, char *argv[]);
int cmd_verify(int argc, char *argv[]);
int cmd_mark(int argc, char *argv[]);
int cmd_pin(int argc, char *argv[]);
int cmd_unpin(int argc, char *argv[]);
int cmd_print_architecture(int argc, char *argv[]);

#endif
