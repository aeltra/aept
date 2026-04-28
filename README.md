# NAME

aept - Aeltra Package Tool

# SYNOPSIS

**aept** \[-c \<file\>\] \[-o \<dir\>\] \[-v\] \<command\> \[options\]
\[args...\]

# DESCRIPTION

**aept** is a minimal package manager for .aeltra packages with
dependency resolution. It uses libsolv for dependency solving,
libarchive for archive extraction, and libfetch for HTTP downloads.
Signature verification is performed by usign.

# GLOBAL OPTIONS

**-c**, **--conf** \<file\>

> Use *file* as the configuration file instead of the default
> */etc/aept/aept.conf*.

**-o**, **--offline-root** \<dir\>

> Use *dir* as the package root directory. See **OFFLINE ROOT**.

**-v**, **--verbose**

> Increase verbosity. Can be specified multiple times.

**-h**, **--help**

> Show usage summary and exit.

# COMMANDS

## update \[options\]

Fetch package lists from all configured repositories. If signature
checking is enabled, also downloads and verifies the *Packages.sig* file
for each source. Package lists are stored in the lists directory (see
**FILES**).

## install \[options\] \<packages...\>

Install one or more packages by name. Dependencies are resolved
automatically. Packages that are already installed at the requested
version are skipped. The solver may also schedule removals if a package
conflicts with or replaces an installed package.

**-f**, **--force-depends**

> Ignore dependency errors from the solver and proceed anyway.

**-d**, **--download-only**

> Download packages to the cache directory but do not install them.

**-n**, **--noaction**

> Dry run. Show which packages would be installed or removed, but do not
> actually perform any changes.

**--non-interactive**

> Do not prompt. Implies **--force-confold**, which can be overridden by
> also passing **--force-confnew**. Automatically enabled when standard
> input is not a terminal.

**--allow-downgrade**

> Allow the solver to downgrade installed packages.

**--no-cache**

> Download, install, and immediately delete each package instead of
> downloading all packages first. Useful on devices with limited
> storage. Ignored when **--download-only** is also specified.

**--reinstall**

> Reinstall packages that are already installed at the requested
> version.

**--force-confnew**

> During upgrades, always install the package maintainer's version of
> conffiles without prompting, even if the file has been locally
> modified.

**--force-confold**

> During upgrades, always keep the currently installed version of
> conffiles without prompting, even if the package ships a new version.

**--keep-going**

> Continue past per-package errors (such as failing maintainer scripts)
> instead of aborting the transaction. Useful for bringing a partially
> broken system back into a usable state. **aept** still exits with a
> non-zero status if any package failed.

## remove \[options\] \<packages...\>

Remove one or more installed packages. Reverse dependencies are resolved
automatically, and any packages that depend on the removed packages will
also be removed unless **--force-depends** is given.

**-f**, **--force-depends**

> Ignore dependency errors from the solver and proceed anyway.

**-n**, **--noaction**

> Dry run. Show which packages would be removed, but do not actually
> perform any changes.

**--non-interactive**

> Do not prompt. Automatically enabled when standard input is not a
> terminal.

**--purge**

> Also remove conffiles that have been modified since installation.
> Without this option, modified conffiles are preserved on disk.

**--keep-going**

> Continue past per-package errors (such as failing maintainer scripts)
> instead of aborting the transaction. **aept** still exits with a
> non-zero status if any package failed.

## autoremove \[options\]

Remove auto-installed packages that are no longer needed. A package is
considered unneeded if it was automatically installed as a dependency
and no manually installed package depends on it.

**-f**, **--force-depends**

> Ignore dependency errors and proceed anyway.

**-n**, **--noaction**

> Dry run. Show which packages would be removed, but do not actually
> perform any changes.

**--non-interactive**

> Do not prompt. Automatically enabled when standard input is not a
> terminal.

**--purge**

> Also remove conffiles that have been modified since installation.

**--keep-going**

> Continue past per-package errors (such as failing maintainer scripts)
> instead of aborting the transaction. **aept** still exits with a
> non-zero status if any package failed.

## upgrade \[options\]

Upgrade all installed packages to the latest available version. Packages
that have been pinned (see **pin**) are held back and not upgraded.

**-f**, **--force-depends**

> Ignore dependency errors from the solver and proceed anyway.

**-d**, **--download-only**

> Download packages to the cache directory but do not install them.

**-n**, **--noaction**

> Dry run. Show which packages would be installed or removed, but do not
> actually perform any changes.

**--non-interactive**

> Do not prompt. Implies **--force-confold**, which can be overridden by
> also passing **--force-confnew**. Automatically enabled when standard
> input is not a terminal.

**--allow-downgrade**

> Allow the solver to downgrade installed packages.

**--no-cache**

> Download, install, and immediately delete each package instead of
> downloading all packages first. Useful on devices with limited
> storage. Ignored when **--download-only** is also specified.

**--force-confnew**

> Always install the package maintainer's version of conffiles without
> prompting, even if the file has been locally modified.

**--force-confold**

> Always keep the currently installed version of conffiles without
> prompting, even if the package ships a new version.

**--keep-going**

> Continue past per-package errors (such as failing maintainer scripts)
> instead of aborting the transaction. **aept** still exits with a
> non-zero status if any package failed.

## mark manual \[--all\] \<packages...\>

Mark one or more installed packages as manually installed. Manually
installed packages are never candidates for removal by **autoremove**.

**--all**

> Mark all installed packages as manually installed.

## mark auto \<packages...\>

Mark one or more installed packages as automatically installed.
Auto-installed packages become candidates for removal by **autoremove**
once no manually installed package depends on them.

## pin \<packages...\>

Pin one or more packages to their currently installed version. Pinned
packages are held back during **upgrade**. To pin to a specific version,
use the *name*=*version* syntax:

> aept pin mypackage=1.2.3

## unpin \<packages...\>

Remove version pins for one or more packages.

## show \[options\] \<package\>

Show information about a package. Displays fields such as name, version,
architecture, installed size, dependencies, homepage, filename, and
description. If a package is available in a repository and also
installed, the repository version is shown and the install status is
indicated. Returns exit code 1 if the package is not found.

## list \[options\] \[pattern\]

List packages. With no arguments, all available packages are shown. An
optional glob pattern filters by package name.

**--installed**

> Only show installed packages.

**--upgradable**

> Only show packages for which a newer version is available.

## clean \[options\]

Remove all cached package files from the cache directory.

## owns \[options\] \<path\>

Find which installed package owns a file. Searches the *.list* files in
the info directory for a matching path. Returns exit code 1 if no
package owns the file.

## files \[options\] \<package\>

List files belonging to an installed package. Prints file paths to
stdout. Returns exit code 1 if the package is not installed.

## print-architecture

Print the configured architectures, one per line. The first architecture
is the native (most preferred) one.

# CONFIGURATION

The configuration file is a simple line-oriented format. Blank lines and
lines starting with **\#** are ignored. Each directive occupies a single
line.

## Source directives

**src/gz** \<name\> \<url\>

> Add a repository that provides a gzip-compressed package list. During
> **aept update**, *Packages.gz* is fetched from *url*, decompressed,
> and stored as *name* in the lists directory.

**src** \<name\> \<url\>

> Add a repository that provides an uncompressed package list. During
> **aept update**, *Packages* is fetched from *url* directly.

## Architecture directive

**arch** \<architecture\>

> Declare a supported architecture. Can be specified multiple times. The
> first **arch** directive sets the native (most preferred)
> architecture. Subsequent entries are accepted at lower priority. If no
> **arch** is configured, only architecture-independent packages are
> considered.

## Options

Options are set with the **option** directive:

> option \<key\> \<value\>

The following keys are recognized:

|  |  |  |
|:---|:---|:---|
| **Key** | **Default** | **Description** |
| offline_root | (none) | Offline root directory (see **OFFLINE ROOT**) |
| info_dir | /var/lib/aept/info | Directory for installed package metadata |
| lists_dir | /var/lib/aept/lists | Directory for downloaded package lists |
| status_file | /var/lib/aept/status | Path to the installed-packages database |
| cache_dir | /var/cache/aept | Directory for downloaded .aeltra files |
| tmp_dir | /tmp | Temporary directory |
| lock_file | /var/lib/aept/lock | Path to the lock file |
| usign_keydir | /etc/aept/usign/trustdb | Directory containing trusted public keys |
| auto_file | /var/lib/aept/auto-installed | Path to the auto-installed packages tracking file |
| pin_file | /var/lib/aept/pinned-packages | Path to the version pins file |
| check_signature | 1 | Set to 0 to disable signature verification |
| ignore_uid | 0 | Set to 1 to not preserve file ownership during extraction. Files will be owned by the calling user instead of the uid/gid recorded in the package. |
| ssl_client_cert | (none) | Path to a PEM client certificate for HTTPS |
| ssl_client_key | (none) | Path to the corresponding PEM private key |
| allow_downgrade | 0 | Set to 1 to allow package downgrades |

## Example configuration

    # Repositories
    src/gz base https://repo.example.com/packages/base
    src/gz extra https://repo.example.com/packages/extra

    # Architectures
    arch aarch64
    arch all

    # Options
    option cache_dir /var/cache/aept
    option check_signature 1

# OFFLINE ROOT

When an offline root is set (via **--offline-root** or the
**offline_root** config option), the configuration file is read from
*\<dir\>/etc/aept/aept.conf* (unless **--conf** is given explicitly) and
all state directories (lists, cache, info, status, lock, auto-installed,
pinned-packages) are automatically prefixed with the offline root path.
The signature trust directory (*usign_keydir*) is **not** prefixed —
signature verification always uses the host's trusted keys.

Maintainer scripts are executed inside the offline root using
**unshare**(2) with **CLONE_NEWUSER** to set up a user namespace. UID
and GID mappings are written to */proc/self/uid_map* and
*/proc/self/gid_map* so that the current user appears as root inside the
namespace. The script then chroots into the offline root directory.

This allows non-root users to build root filesystems or install packages
into an alternate directory tree. This functionality is required by the
build-box tool.

# MAINTAINER SCRIPTS

Maintainer scripts receive Debian-style arguments indicating the action
being performed:

**preinst** install  
**postinst** configure

> Fresh installation.

**preinst** upgrade \<old-version\>  
**postinst** configure \<old-version\>

> Upgrade from *old-version*.

**prerm** remove  
**postrm** remove

> Pure removal.

**prerm** upgrade \<new-version\>  
**postrm** upgrade \<new-version\>

> Removal of old version during upgrade to *new-version*.

# TRIGGERS

Triggers allow a package to run a script when other packages install or
remove files in directories of interest. A common use case is rebuilding
a cache or index after any package places files into a particular
directory.

## Declaring trigger interest

A package declares trigger interest by shipping a *triggers* file in its
control archive. Each line names a directory pattern:

    /usr/lib/modules
    /usr/share/icons/*

Patterns are matched with **fnmatch**(3) using **FNM_PATHNAME**. The
wildcards **\***, **?**, and **\[...\]** are supported.

A pattern may be prefixed with **+** to mark it as modify-only:

    +/usr/share/mime/packages

A modify-only pattern fires the trigger only when a file in the matching
directory was actually installed or removed during the transaction.
Without the prefix, the trigger also fires for freshly installed
packages whose concrete (non-glob) patterns match an existing directory
on disk, even if no file in that directory changed.

## Trigger script

The package must also ship a *trigger* file in its control archive. This
is an executable script (invoked with **/bin/sh**) that receives the
matched directory paths as positional arguments:

    #!/bin/sh
    for dir in "$@"; do
        update-icon-caches "$dir"
    done

## Execution order

Triggers are deferred: they do not run during individual package
operations. Instead, all directories touched by installs, upgrades, and
removals are collected throughout the transaction. After every package
operation has completed (including maintainer scripts), **aept** matches
the collected directories against the aggregated trigger index and runs
each matching trigger script once.

## Trigger index

When a package with a *triggers* file is installed or removed, **aept**
automatically rebuilds *triggers-index* in the info directory. This file
aggregates all trigger interests and has the format:

    <pattern>	<package>

The index is an implementation detail and should not be edited by hand.

# PACKAGE FORMAT

Aeltra packages (.aeltra) use the same structure as Debian binary
packages: an AR archive containing a *control.tar* and a *data.tar*
member, each with a compression suffix (.gz, .xz, .bz2, .lz4, or .zst).
The control archive holds package metadata and maintainer scripts; the
data archive holds the installable file tree.

Because the format is compatible, standard Debian tools can be used to
inspect packages:

    dpkg -c package.aeltra   # list data archive contents
    dpkg -I package.aeltra   # show control information

# FILES

*/etc/aept/aept.conf*

> Default configuration file.

*/etc/aept/usign/trustdb/*

> Default directory for trusted usign public keys.

*/var/lib/aept/status*

> Installed-packages database (Debian control file format).

*/var/lib/aept/info/*

> Per-package metadata directory. For each installed package *pkg*, the
> following files may exist:
>
> - *pkg.control* — control metadata
>
> - *pkg.list* — list of installed files
>
> - *pkg.conffiles* — conffile paths and MD5 checksums
>
> - *pkg.preinst*, *pkg.postinst*, *pkg.prerm*, *pkg.postrm* —
>   maintainer scripts
>
> - *pkg.triggers* — trigger interest declarations (directory patterns)
>
> - *pkg.trigger* — trigger script
>
> - *triggers-index* — aggregated trigger interest index (rebuilt
>   automatically)

*/var/lib/aept/lists/*

> Downloaded package lists, one file per source.

*/var/lib/aept/auto-installed*

> Tracks which packages were pulled in automatically as dependencies.
> Used by **autoremove** to decide removal candidates.

*/var/lib/aept/pinned-packages*

> Version pins, one *name version* pair per line. Pinned packages are
> held back during **upgrade**. Managed by the **pin** and **unpin**
> commands.

*/var/cache/aept/*

> Downloaded .aeltra package files.

*/var/lib/aept/lock*

> Lock file to prevent concurrent operations.

# EXIT STATUS

**0** on success, **1** on error. When **aept** is terminated by a
signal (such as **SIGINT** from Ctrl-C), it re-raises the signal under
the default disposition so the parent process sees the conventional
**128+N** exit status.

# AUTHORS

Tobias Koch
