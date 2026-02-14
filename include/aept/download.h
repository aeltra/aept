/* download.h - wget download wrapper */

#ifndef DOWNLOAD_H_7BF97F
#define DOWNLOAD_H_7BF97F

/* Download url to dest file. Returns 0 on success, -1 on error. */
int aept_download(const char *url, const char *dest);

#endif
