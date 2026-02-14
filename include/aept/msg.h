/* msg.h - logging */

#ifndef MSG_H_7BF97F
#define MSG_H_7BF97F

enum aept_msg_level {
    AEPT_ERROR,
    AEPT_NOTICE,
    AEPT_INFO,
    AEPT_DEBUG
};

void aept_msg(int level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#endif
