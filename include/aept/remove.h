/* remove.h - remove orchestration */

#ifndef REMOVE_H_7BF97F
#define REMOVE_H_7BF97F

/* Remove packages by name. Resolves reverse dependencies via solver. */
int aept_remove(const char **names, int count);

/* Remove a single package (used internally by install for upgrades). */
int aept_do_remove(const char *name);

#endif
