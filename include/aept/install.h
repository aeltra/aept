/* install.h - install/upgrade orchestration */

#ifndef INSTALL_H_7BF97F
#define INSTALL_H_7BF97F

/* Install packages by name. If names is NULL, upgrade all.
 * Resolves dependencies via solver. */
int aept_install(const char **names, int count);

#endif
