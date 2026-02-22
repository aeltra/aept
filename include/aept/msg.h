/* msg.h - logging
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MSG_H_7BF97F
#define MSG_H_7BF97F

enum aept_log_level {
    AEPT_ERROR,
    AEPT_WARNING,
    AEPT_INFO,
    AEPT_DEBUG
};

void aept_log_init(void);
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

/* Callback types (matching public API in aept/aept.h) */
typedef void (*aept_log_fn)(int level, const char *msg, void *userdata);

struct aept_transaction;
typedef void (*aept_display_fn)(const struct aept_transaction *txn,
                                void *userdata);
typedef int (*aept_confirm_fn)(void *userdata);

/* Global callback pointers (set/restored by API entry points) */
extern aept_log_fn     aept_log_cb;
extern void           *aept_log_cb_data;
extern aept_display_fn aept_display_cb;
extern void           *aept_display_cb_data;
extern aept_confirm_fn aept_confirm_cb;
extern void           *aept_confirm_cb_data;

#endif
