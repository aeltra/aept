/* msg.h - logging
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef MSG_H_7BF97F
#define MSG_H_7BF97F

struct aept_ctx;
struct aept_transaction;

enum aept_log_level {
    AEPT_ERROR,
    AEPT_WARNING,
    AEPT_INFO,
    AEPT_DEBUG
};

/* Set the global logging context.  Called once by aept_init(),
 * cleared by aept_cleanup().  Immutable after aept_init() returns. */
void aept_log_set_ctx(struct aept_ctx *ctx);

void aept_log(int level, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#define aept_log_error(fmt, ...) \
    aept_log(AEPT_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define aept_log_warning(fmt, ...) \
    aept_log(AEPT_WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define aept_log_info(fmt, ...) \
    aept_log(AEPT_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define aept_log_debug(fmt, ...) \
    aept_log(AEPT_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

int aept_confirm_continue(void);
void aept_display_transaction(const struct aept_transaction *txn);
void aept_print_heading(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
void aept_print_names(const char **list, int count);

/* Check whether the current operation has been cancelled. */
int aept_cancelled(void);

#endif
