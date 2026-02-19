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

#define log_error(fmt, ...) \
    aept_log(AEPT_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_warning(fmt, ...) \
    aept_log(AEPT_WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) \
    aept_log(AEPT_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) \
    aept_log(AEPT_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

int confirm_continue(void);
void print_heading(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
void print_names(const char **list, int count);

#endif
