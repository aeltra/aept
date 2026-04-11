/* owner_index.h - file path → owning package index
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef OWNER_INDEX_H_7BF97F
#define OWNER_INDEX_H_7BF97F

struct aept_ctx;

/*
 * In-memory index mapping normalized file paths to the package that
 * owns them.  Replaces per-file directory scans of {info_dir}/*.list
 * during file-clash checks.
 *
 * aept_op_install builds one index at the start of the transaction
 * and threads it through do_install_package, do_upgrade_package, and
 * aept_do_remove so that later clash checks see the effects of
 * earlier steps in the same transaction.
 */

typedef struct {
    char *path;            /* normalized: no leading "./" or "/" */
    const char *owner;     /* shared pointer into owners[] */
} aept_owner_entry_t;

typedef struct aept_owner_index {
    /* Main index, sorted by path.  Populated once at build time. */
    aept_owner_entry_t *entries;
    int count;
    int alloc;

    /* Entries added during the transaction, sorted by path. */
    aept_owner_entry_t *recent;
    int n_recent;
    int recent_alloc;

    /* Live interned owner-name strings referenced by entries[]/recent[]. */
    char **owners;
    int n_owners;
    int owners_alloc;

    /* Tombstones: owner pointers whose entries must be ignored.  When
     * a package is dropped its live pointer is moved from owners[] to
     * dead[], so a subsequent re-install under the same name gets a
     * fresh pointer that cannot be mistaken for the dead one. */
    const char **dead;
    int n_dead;
    int dead_alloc;
} aept_owner_index_t;

void aept_owner_index_init(aept_owner_index_t *idx);
void aept_owner_index_free(aept_owner_index_t *idx);

/* Walk {info_dir} and populate the index from every *.list file. */
int aept_owner_index_build(struct aept_ctx *ctx, aept_owner_index_t *idx);

/* Return the package that owns path, or NULL if unknown.  The returned
 * pointer is owned by the index. */
const char *aept_owner_index_find(aept_owner_index_t *idx, const char *path);

/* Notify the index that owner_name has been (re)installed.  Reads
 * list_path (a freshly written .list file) and appends its entries. */
int aept_owner_index_add_owner_files(aept_owner_index_t *idx,
                                     const char *owner_name,
                                     const char *list_path);

/* Notify the index that owner_name has been removed.  All entries
 * referencing the owner's current live pointer are invalidated. */
void aept_owner_index_drop_owner(aept_owner_index_t *idx,
                                 const char *owner_name);

#endif
