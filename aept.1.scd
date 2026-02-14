aept(1)

# NAME

aept - Aeltra Package Tool

# SYNOPSIS

*aept* [-c <file>] [-v] <command> [options] [args...]

# DESCRIPTION

*aept* is a minimal package manager for .aeltra packages with dependency
resolution. It uses libsolv for dependency solving and libarchive for archive
extraction. Package lists and signatures are fetched with wget, and signature
verification is performed by usign.

# GLOBAL OPTIONS

*-c*, *--conf* <file>
	Use _file_ as the configuration file instead of the default
	_/etc/aept/aept.conf_.

*-v*, *--verbose*
	Increase verbosity. Can be specified multiple times.

*-h*, *--help*
	Show usage summary and exit.

# COMMANDS

## update [options]

Fetch package lists from all configured repositories. If signature checking
is enabled, also downloads and verifies the _Packages.sig_ file for each
source. Package lists are stored in the lists directory (see *FILES*).

*-o*, *--offline-root* <dir>
	Install packages into _dir_ instead of the filesystem root. See
	*OFFLINE ROOT*.

## install [options] <packages...>

Install one or more packages by name. Dependencies are resolved automatically.
Packages that are already installed at the requested version are skipped. The
solver may also schedule removals if a package conflicts with or replaces an
installed package.

*-o*, *--offline-root* <dir>
	Install packages into _dir_ instead of the filesystem root. See
	*OFFLINE ROOT*.

*-f*, *--force-depends*
	Ignore dependency errors from the solver and proceed anyway.

*-d*, *--download-only*
	Download packages to the cache directory but do not install them.

*-n*, *--noaction*
	Dry run. Show which packages would be installed or removed, but do not
	actually perform any changes.

*--allow-downgrade*
	Allow the solver to downgrade installed packages.

## remove [options] <packages...>

Remove one or more installed packages. Reverse dependencies are resolved
automatically, and any packages that depend on the removed packages will also
be removed unless *--force-depends* is given.

*-o*, *--offline-root* <dir>
	Install packages into _dir_ instead of the filesystem root. See
	*OFFLINE ROOT*.

*-f*, *--force-depends*
	Ignore dependency errors from the solver and proceed anyway.

*-n*, *--noaction*
	Dry run. Show which packages would be removed, but do not actually
	perform any changes.

## upgrade [options]

Upgrade all installed packages to the latest available version.

*-o*, *--offline-root* <dir>
	Install packages into _dir_ instead of the filesystem root. See
	*OFFLINE ROOT*.

*-f*, *--force-depends*
	Ignore dependency errors from the solver and proceed anyway.

*-d*, *--download-only*
	Download packages to the cache directory but do not install them.

*-n*, *--noaction*
	Dry run. Show which packages would be installed or removed, but do not
	actually perform any changes.

*--allow-downgrade*
	Allow the solver to downgrade installed packages.

## show [options] <package>

Show information about a package. Displays fields such as name, version,
architecture, installed size, dependencies, homepage, filename, and
description. If a package is available in a repository and also installed,
the repository version is shown and the install status is indicated. Returns
exit code 1 if the package is not found.

*-o*, *--offline-root* <dir>
	Use _dir_ as the offline root directory. See *OFFLINE ROOT*.

## clean [options]

Remove all cached package files from the cache directory.

*-o*, *--offline-root* <dir>
	Install packages into _dir_ instead of the filesystem root. See
	*OFFLINE ROOT*.

## owns [options] <path>

Find which installed package owns a file. Searches the _.list_ files in the
info directory for a matching path. Returns exit code 1 if no package owns
the file.

*-o*, *--offline-root* <dir>
	Use _dir_ as the offline root directory. See *OFFLINE ROOT*.

## print-architecture

Print the configured architectures, one per line. The first architecture is
the native (most preferred) one.

# CONFIGURATION

The configuration file is a simple line-oriented format. Blank lines and lines
starting with *#* are ignored. Each directive occupies a single line.

## Source directives

*src/gz* <name> <url>
	Add a repository that provides a gzip-compressed package list. During
	*aept update*, _Packages.gz_ is fetched from _url_, decompressed, and
	stored as _name_ in the lists directory.

*src* <name> <url>
	Add a repository that provides an uncompressed package list. During
	*aept update*, _Packages_ is fetched from _url_ directly.

## Architecture directive

*arch* <architecture>
	Declare a supported architecture. Can be specified multiple times. The
	first *arch* directive sets the native (most preferred) architecture.
	Subsequent entries are accepted at lower priority. If no *arch* is
	configured, only architecture-independent packages are considered.

## Options

Options are set with the *option* directive:

	option <key> <value>

The following keys are recognized:

[[ *Key*
:[ *Default*
:[ *Description*
|  offline_root
:  (none)
:  Offline root directory (see *OFFLINE ROOT*)
|  info_dir
:  /var/lib/aept/info
:  Directory for installed package metadata
|  lists_dir
:  /var/lib/aept/lists
:  Directory for downloaded package lists
|  status_file
:  /var/lib/aept/status
:  Path to the installed-packages database
|  cache_dir
:  /var/cache/aept
:  Directory for downloaded .aeltra files
|  tmp_dir
:  /tmp
:  Temporary directory
|  lock_file
:  /var/lib/aept/lock
:  Path to the lock file
|  usign_bin
:  usign
:  Path to the usign binary
|  usign_keydir
:  /etc/aept/usign/trustdb
:  Directory containing trusted public keys
|  check_signature
:  1
:  Set to 0 to disable signature verification
|  ignore_uid
:  0
:  Set to 1 to skip the root/UID check
|  allow_downgrade
:  0
:  Set to 1 to allow package downgrades

## Example configuration

```
# Repositories
src/gz base https://repo.example.com/packages/base
src/gz extra https://repo.example.com/packages/extra

# Architectures
arch aarch64
arch all

# Options
option cache_dir /var/cache/aept
option check_signature 1
```

# OFFLINE ROOT

When an offline root is set (via *--offline-root* or the *offline_root* config
option), all state directories (lists, cache, info, status, lock) are
automatically prefixed with the offline root path.

Maintainer scripts are executed inside the offline root using
*unshare*(2) with *CLONE_NEWUSER* to set up a user namespace. UID and GID
mappings are written to _/proc/self/uid_map_ and _/proc/self/gid_map_ so that
the current user appears as root inside the namespace. The script then
chroots into the offline root directory.

This allows non-root users to build root filesystems or install packages into
an alternate directory tree.

# MAINTAINER SCRIPTS

Maintainer scripts receive Debian-style arguments indicating the action being
performed:

*preinst* install++
*postinst* configure
	Fresh installation.

*preinst* upgrade <old-version>++
*postinst* configure <old-version>
	Upgrade from _old-version_.

*prerm* remove++
*postrm* remove
	Pure removal.

*prerm* upgrade <new-version>++
*postrm* upgrade <new-version>
	Removal of old version during upgrade to _new-version_.

# FILES

_/etc/aept/aept.conf_
	Default configuration file.

_/etc/aept/usign/trustdb/_
	Default directory for trusted usign public keys.

_/var/lib/aept/status_
	Installed-packages database (Debian control file format).

_/var/lib/aept/info/_
	Per-package metadata directory. For each installed package _pkg_, the
	following files may exist:

	- _pkg.control_ — control metadata
	- _pkg.list_ — list of installed files
	- _pkg.preinst_, _pkg.postinst_, _pkg.prerm_, _pkg.postrm_ — maintainer scripts

_/var/lib/aept/lists/_
	Downloaded package lists, one file per source.

_/var/cache/aept/_
	Downloaded .aeltra package files.

_/var/lib/aept/lock_
	Lock file to prevent concurrent operations.

# EXIT STATUS

*0* on success, *1* on error.

# AUTHORS

Tobias Koch

# SEE ALSO

*usign*(1), *libsolv*(3), *libarchive*(3)
