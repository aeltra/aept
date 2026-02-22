/* conffile.h - conffile tracking and conflict resolution
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CONFFILE_H_7BF97F
#define CONFFILE_H_7BF97F

typedef struct {
    char *path;
    char *md5;
} aept_conffile_t;

typedef struct {
    aept_conffile_t *entries;
    int count;
    int alloc;
} aept_conffile_set_t;

void aept_conffile_set_init(aept_conffile_set_t *cs);
void aept_conffile_set_add(aept_conffile_set_t *cs, const char *path,
                      const char *md5);
const char *aept_conffile_set_lookup(const aept_conffile_set_t *cs,
                                const char *path);
void aept_conffile_set_free(aept_conffile_set_t *cs);

/* Compute MD5 hex digest of a file. Returns malloc'd string or NULL. */
char *aept_conffile_md5(const char *path);

/* Parse conffiles list from an extracted control directory.
 * Sets md5 fields to NULL. Returns 0 on success (empty set if no file). */
int aept_conffile_parse_list(const char *control_dir, aept_conffile_set_t *cs);

/* Load saved conffile metadata from {info_dir}/{name}.conffiles.
 * Format: "md5sum  path\n". Returns 0 on success (empty set if no file). */
int aept_conffile_load(const char *name, aept_conffile_set_t *cs);

/* Save conffile metadata to {info_dir}/{name}.conffiles.
 * Returns 0 on success, -1 on error. */
int aept_conffile_save(const char *name, const aept_conffile_set_t *cs);

/* Handle conffile conflicts during upgrade. For each new conffile,
 * compares the on-disk version against the ".aept-new" version placed
 * next to it during extraction and the saved old metadata.
 * Applies decisions (rename new / keep old / prompt) and saves metadata.
 * Returns 0 on success, -1 on error. */
int aept_conffile_resolve_upgrade(const char *name,
                             const aept_conffile_set_t *old_conffiles,
                             const aept_conffile_set_t *new_conffiles);

#endif
