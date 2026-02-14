/* config.h - configuration file parsing */

#ifndef AEPT_CONFIG_H_7BF97F
#define AEPT_CONFIG_H_7BF97F

int config_load(const char *filename);
void config_free(void);

/* Return path prefixed with offline_root if set. Caller must free. */
char *config_root_path(const char *path);

#endif
