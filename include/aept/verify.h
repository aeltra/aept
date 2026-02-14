/* verify.h - usign signature verification */

#ifndef VERIFY_H_7BF97F
#define VERIFY_H_7BF97F

/* Verify file signature using usign. Returns 0 on success, -1 on failure. */
int aept_verify_signature(const char *file, const char *sigfile);

#endif
