/* validator.h - cache validators kept beside a fetched index
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef VALIDATOR_H_7BF97F
#define VALIDATOR_H_7BF97F

struct libfetch_validators;

/*
 * Read the validators recorded at path, which must have been recorded
 * for this very url.
 *
 * The url is part of the record and is checked because the validator
 * says nothing about which document it describes: an index fetched as
 * InPackages.gz and one fetched as Packages.gz live under the same
 * name in the lists directory, and sending one's validator with the
 * other's request invites a 304 that means nothing.
 *
 * Returns 0 when out holds at least one validator, -1 otherwise --
 * absent, unreadable, malformed, recorded for another url, or empty.
 * A -1 is not an error to report: it means the next request is simply
 * unconditional.
 */
int aept_validator_load(const char *path, const char *url, struct libfetch_validators *out);

/* Record the validators for url at path, replacing whatever is there.
 * Returns 0 on success, -1 on error. */
int aept_validator_save(const char *path, const char *url, const struct libfetch_validators *v);

#endif
