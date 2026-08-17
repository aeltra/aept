#!/usr/bin/env python3
# coverage-report.py - aggregate gcov data into a per-tier coverage report.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Driven by "make coverage", "make coverage-check" and
# "make coverage-update"; runnable by hand for a subset of the tree.
#
# Why this exists rather than lcov or gcovr: gcov ships with gcc, which is
# already required, so a build that can compile aept can measure it with
# no further dependency.  python3 is already a test dependency (see
# condserver.py and friends).  The whole job is running gcov, summing what
# it prints and comparing the total against tests/coverage.tiers, which is
# a smaller thing than either of those tools.
#
# Three properties of this tree make the naive version of this script
# wrong, and all three cost a measurement before they were understood:
#
#   * libtool compiles every object twice -- non-PIC into src/, PIC into
#     src/.libs/ -- and the two carry DIFFERENT counters.  The PIC copy
#     accumulates from the aept binary by way of the shared library, the
#     non-PIC copy from the statically linked test harnesses.  Both are
#     real coverage and both must be summed.  Running gcov on every .gcda
#     into one directory instead silently overwrites, because the two
#     produce the same libaept_la-remove.gcov.json.gz -- which is how
#     remove.c once got reported at 0.0% when it was at 70.0%.  Hence one
#     output directory per object.
#
#   * gcov emits no branch data at all without -b, and its absence reads
#     as zero rather than as missing.
#
#   * anything downstream of a fork() that leaves by _exit() never flushes
#     its counters, so util.c's child paths (unshare_and_map_user,
#     write_map_file, child_err) read as unentered however hard they are
#     exercised.  That is a property of gcov, not of the tests.  Do not
#     add a __gcov_dump() to production code to make those lines appear.

import argparse
import glob
import gzip
import json
import os
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor


# ── collecting ───────────────────────────────────────────────────────────

class FileCov:
    """Merged coverage for one source file."""

    def __init__(self):
        self.lines = defaultdict(int)      # line number   -> summed count
        self.branches = defaultdict(int)   # (line, index)  -> summed count
        self.functions = defaultdict(int)  # function name  -> summed count

    def line_total(self):
        return len(self.lines)

    def line_hit(self):
        return sum(1 for c in self.lines.values() if c > 0)

    def branch_total(self):
        return len(self.branches)

    def branch_hit(self):
        return sum(1 for c in self.branches.values() if c > 0)

    def unentered(self):
        return sorted(n for n, c in self.functions.items() if c == 0)


def find_gcda(build_dir):
    out = []
    for root, _dirs, files in os.walk(build_dir):
        for f in files:
            if f.endswith(".gcda"):
                out.append(os.path.join(root, f))
    return sorted(out)


def run_gcov(index, gcda, workdir, gcov):
    """Run gcov for one object into its own directory; return the JSON paths.

    The output directory is per object and the index comes from the
    caller rather than a counter here, because these run in parallel:
    two objects sharing a directory is the collision this whole
    arrangement exists to avoid.

    Both paths handed to gcov are absolute.  gcov runs with its cwd set
    to that output directory, so a relative -o would resolve against the
    output directory instead of the build tree -- which fails as
    "./src.gcno:cannot open notes file", quietly, into a report of zeroes.
    """
    d = os.path.join(workdir, "%06d" % index)
    os.makedirs(d, exist_ok=True)
    gcda = os.path.abspath(gcda)
    r = subprocess.run([gcov, "-i", "-b", "-o", os.path.dirname(gcda), gcda],
                       cwd=d, capture_output=True, text=True)
    if r.returncode != 0:
        first = (r.stderr.strip().splitlines() or ["(no message)"])[0]
        print("warning: gcov failed for %s: %s" % (gcda, first),
              file=sys.stderr)
    return glob.glob(os.path.join(d, "*.gcov.json.gz"))


def normalise(path, cwd, srcdir):
    """Map a path as the compiler saw it to one relative to srcdir."""
    p = path if os.path.isabs(path) else os.path.join(cwd, path)
    p = os.path.realpath(p)
    rel = os.path.relpath(p, srcdir)
    if rel.startswith(os.pardir):
        return None
    return rel.replace(os.sep, "/")


def collect(build_dir, srcdir, gcov, jobs):
    build_dir = os.path.realpath(build_dir)
    gcdas = find_gcda(build_dir)
    if not gcdas:
        sys.exit("no .gcda files under %s\n"
                 "  Nothing has been measured.  Build with coverage first:\n"
                 "    ./configure --enable-coverage && make && make check"
                 % build_dir)

    srcdir = os.path.realpath(srcdir)
    files = defaultdict(FileCov)
    version = None

    workdir = tempfile.mkdtemp(prefix="aept-coverage.")
    try:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            batches = list(pool.map(
                lambda ig: run_gcov(ig[0], ig[1], workdir, gcov),
                enumerate(gcdas)))

        for jsons in batches:
            for j in jsons:
                with gzip.open(j) as fh:
                    data = json.load(fh)
                version = version or data.get("gcc_version")
                cwd = data.get("current_working_directory", "")
                for entry in data.get("files", []):
                    rel = normalise(entry["file"], cwd, srcdir)
                    if rel is None or not rel.startswith("src/"):
                        continue
                    cov = files[rel]
                    for ln in entry.get("lines", []):
                        n = ln["line_number"]
                        cov.lines[n] += ln["count"]
                        for i, br in enumerate(ln.get("branches", [])):
                            cov.branches[(n, i)] += br.get("count", 0)
                    for fn in entry.get("functions", []):
                        cov.functions[fn["name"]] += fn["execution_count"]
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    return files, version, len(gcdas)


# ── tiers ────────────────────────────────────────────────────────────────

class Tier:
    def __init__(self, name, target, floor, cap, label):
        self.name = name
        self.target = target   # where the tier is meant to end up
        self.floor = floor     # per-file hard gate; 0 disables
        self.cap = cap         # target is a ceiling, not a floor
        self.label = label
        self.files = []


def load_tiers(path):
    """Parse tests/coverage.tiers into tier definitions and assignments."""
    tiers, assign = {}, {}
    order = []
    with open(path) as fh:
        for lineno, raw in enumerate(fh, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            word = line.split()
            if word[0] == "tier":
                if len(word) < 2:
                    sys.exit("%s:%d: tier needs a name" % (path, lineno))
                name = word[1]
                target = floor = 0
                cap = False
                label = name
                for opt in word[2:]:
                    if opt.startswith("target="):
                        target = int(opt.split("=", 1)[1])
                    elif opt.startswith("floor="):
                        floor = int(opt.split("=", 1)[1])
                    elif opt.startswith("label="):
                        label = opt.split("=", 1)[1].replace("_", " ")
                    elif opt == "cap":
                        cap = True
                    else:
                        sys.exit("%s:%d: unknown option %r"
                                 % (path, lineno, opt))
                tiers[name] = Tier(name, target, floor, cap, label)
                order.append(name)
            elif word[0] == "file":
                if len(word) != 3:
                    sys.exit("%s:%d: file needs a path and a tier"
                             % (path, lineno))
                src, tier = word[1], word[2]
                if tier not in tiers:
                    sys.exit("%s:%d: %s is in unknown tier %r"
                             % (path, lineno, src, tier))
                assign[src] = tier
                tiers[tier].files.append(src)
            else:
                sys.exit("%s:%d: expected 'tier' or 'file', got %r"
                         % (path, lineno, word[0]))
    return [tiers[n] for n in order], tiers, assign


def reconcile(files, assign, srcdir, tierpath):
    """Every measured file must be in a tier, and vice versa."""
    problems = []
    for src in sorted(files):
        if src not in assign:
            problems.append("  %s has coverage data but no tier" % src)
    for src in sorted(assign):
        if not os.path.exists(os.path.join(srcdir, src)):
            problems.append("  %s is in a tier but not in the tree" % src)
    if problems:
        sys.exit("%s does not match the tree:\n%s\n\n"
                 "Add the file to a tier, or drop the stale entry.  A new\n"
                 "source file is meant to force this decision -- which tier\n"
                 "it is in determines what coverage it owes."
                 % (tierpath, "\n".join(problems)))


# ── reporting ────────────────────────────────────────────────────────────

def pct(hit, total):
    return 100.0 * hit / total if total else 0.0


def bar(value, width=22):
    """A coarse visual, for scanning a long table."""
    filled = int(round(value / 100.0 * width))
    return "█" * filled + "·" * (width - filled)


def fmt_pct(hit, total):
    if total == 0:
        return "     -"
    return "%5.1f%%" % pct(hit, total)


# One format for the header and the data rows, so the words sit over the
# numbers they label whatever the counts widen to.
ROW = "    %-22s %11s %6s  %11s %6s"


def row(name, lh, lt, bh, bt):
    return ROW % (name, "%d/%d" % (lh, lt), fmt_pct(lh, lt),
                  "%d/%d" % (bh, bt), fmt_pct(bh, bt))


def report(files, tiers, version, nprofiles, show_uncovered):
    print()
    print("aept coverage  --  gcc %s, %d profiles merged"
          % (version or "?", nprofiles))
    print()

    grand = [0, 0, 0, 0]  # line hit, line total, branch hit, branch total
    tier_totals = {}

    for tier in tiers:
        lh = lt = bh = bt = 0
        rows = []
        for src in sorted(tier.files):
            cov = files.get(src)
            if cov is None:      # in a tier, never compiled into a profile
                rows.append((src, 0, 0, 0, 0))
                continue
            rows.append((src, cov.line_hit(), cov.line_total(),
                         cov.branch_hit(), cov.branch_total()))
            lh += cov.line_hit()
            lt += cov.line_total()
            bh += cov.branch_hit()
            bt += cov.branch_total()

        tier_totals[tier.name] = (lh, lt, bh, bt)
        grand[0] += lh
        grand[1] += lt
        grand[2] += bh
        grand[3] += bt

        goal = ("cap %d%%" % tier.target) if tier.cap \
            else ("target %d%%" % tier.target)
        if tier.floor:
            goal += ", floor %d%% per file" % tier.floor
        print("  %-24s %44s" % (tier.label.upper(), goal))
        print(ROW % ("", "lines", "", "branches", ""))

        for src, rlh, rlt, rbh, rbt in rows:
            flag = ""
            if tier.floor and rlt and pct(rlh, rlt) < tier.floor:
                flag = "   <-- below %d%% floor" % tier.floor
            print(row(src[4:], rlh, rlt, rbh, rbt) + flag)

        mark = "" if tier.cap or pct(lh, lt) >= tier.target else \
            "   (%.0f points short)" % (tier.target - pct(lh, lt))
        print(row("--- tier", lh, lt, bh, bt) + mark)
        print("    %s %s" % (" " * 22, bar(pct(lh, lt))))
        print()

    print(row("TOTAL src/", grand[0], grand[1], grand[2], grand[3]))
    print("    %s %s" % (" " * 22, bar(pct(grand[0], grand[1]))))
    print()
    print("  Line coverage is the headline; branch coverage is the honest")
    print("  number.  A tier whose lines climb while its branches do not is")
    print("  a tier whose tests assert success and nothing else.")
    print()

    if show_uncovered:
        print("  never entered (a fork() child reads as unentered however")
        print("  hard it is exercised -- see the note in this script):")
        print()
        for src in sorted(files):
            un = files[src].unentered()
            if un:
                print("    %s (%d/%d)" % (src, len(un),
                                          len(files[src].functions)))
                for name in un:
                    print("      %s" % name)
        print()

    return tier_totals, grand


# ── baseline ─────────────────────────────────────────────────────────────

BASELINE_HEADER = """\
# coverage.baseline - the committed coverage figures.
#
# SPDX-FileCopyrightText: NONE
# SPDX-License-Identifier: MIT
#
# NONE rather than a copyright line because there is nothing here to own.
# The figures are measurements of the tree: facts about it rather than
# expression, and the effort of collecting them confers nothing either.
# The licence is still named, so anything scanning the tarball has an
# answer for this file.  The prose you are reading lives in
# coverage-report.py, which is authored and says so.
#
# Re-bless with "make coverage-update" after a deliberate change, the way
# "make abi-update" re-blesses the ABI.
#
# This file is what makes "make coverage-check" a ratchet rather than an
# aspiration.  A tier target is where the tier is meant to end up, and
# most are not there yet -- gating on those would leave CI red from the
# first day, which teaches everybody to ignore it.  Gating on THIS
# instead means the rule is "do not go backwards", which is enforceable
# immediately and tightens by itself as the figures rise:
#
#   * a tier that drops more than its slack below the recorded figure
#     fails, so a refactor cannot quietly shed tests;
#   * a per-file floor is enforced for every file that ALREADY meets it,
#     and merely reported for one that does not.  So a file cannot slip
#     back under a floor it has reached, and each file starts being
#     guarded the moment it first clears it.
#
# Counts rather than percentages: the arithmetic stays exact, and the
# diff shows which way a change moved.
#
# tier <name>       lines <hit>/<total> branches <hit>/<total>
# file <path>       lines <hit>/<total> branches <hit>/<total>
"""


def write_baseline(path, tiers, tier_totals, files):
    with open(path, "w") as fh:
        fh.write(BASELINE_HEADER)
        for tier in tiers:
            lh, lt, bh, bt = tier_totals[tier.name]
            fh.write("tier %-22s lines %9s branches %9s\n"
                     % (tier.name, "%d/%d" % (lh, lt), "%d/%d" % (bh, bt)))
        fh.write("\n")
        for tier in tiers:
            for src in sorted(tier.files):
                cov = files.get(src)
                if cov is None:
                    continue
                fh.write("file %-22s lines %9s branches %9s\n"
                         % (src, "%d/%d" % (cov.line_hit(), cov.line_total()),
                            "%d/%d" % (cov.branch_hit(), cov.branch_total())))
    print("blessed %d tiers and %d files in %s"
          % (len(tiers), len(files), path))


def read_baseline(path):
    """-> ({tier: counts}, {file: counts}) or None if there is no baseline."""
    if not os.path.exists(path):
        return None
    tiers, files = {}, {}
    with open(path) as fh:
        for raw in fh:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            w = line.split()
            # <kind> <name> lines h/t branches h/t
            if len(w) == 6 and w[0] in ("tier", "file"):
                lh, lt = (int(x) for x in w[3].split("/"))
                bh, bt = (int(x) for x in w[5].split("/"))
                (tiers if w[0] == "tier" else files)[w[1]] = (lh, lt, bh, bt)
    return tiers, files


# ── the gate ─────────────────────────────────────────────────────────────

def check(files, tiers, tier_totals, baseline, slack):
    failures, notes, owed = [], [], []
    base_tiers, base_files = baseline if baseline else ({}, {})

    for tier in tiers:
        # Per-file floor, checked per file and never as a tier average --
        # an average lets one file rot while another carries it, and in
        # the security tier that must not happen.
        #
        # A file that has never met the floor is owed, not failed: see the
        # ratchet note in coverage.baseline.  A file that met it once and
        # has now fallen under is a regression, which is the case worth
        # stopping a build for.
        if tier.floor:
            for src in sorted(tier.files):
                cov = files.get(src)
                if cov is None or not cov.line_total():
                    continue
                p = pct(cov.line_hit(), cov.line_total())
                if p >= tier.floor:
                    continue
                was = None
                if src in base_files:
                    blh, blt, _, _ = base_files[src]
                    was = pct(blh, blt)
                if was is not None and was >= tier.floor:
                    failures.append(
                        "%s fell to %.1f%%, under the %d%% floor for the %s "
                        "tier it had met (%.1f%%)"
                        % (src, p, tier.floor, tier.name, was))
                else:
                    owed.append("%s is at %.1f%%, owes %d%% (%s tier)"
                                % (src, p, tier.floor, tier.name))

        # Any file may regress against its own recorded figure, floor or
        # no floor; the slack is the same.
        for src in sorted(tier.files):
            cov = files.get(src)
            if cov is None or not cov.line_total() or src not in base_files:
                continue
            blh, blt, _, _ = base_files[src]
            was, now = pct(blh, blt), pct(cov.line_hit(), cov.line_total())
            if now < was - slack:
                failures.append("%s fell from %.1f%% to %.1f%%, more than the "
                                "%g point slack" % (src, was, now, slack))

        lh, lt, _bh, _bt = tier_totals[tier.name]
        p = pct(lh, lt)

        if tier.name in base_tiers:
            blh, blt, _, _ = base_tiers[tier.name]
            was = pct(blh, blt)
            if p < was - slack:
                failures.append(
                    "tier %s fell from %.1f%% to %.1f%%, more than the "
                    "%g point slack" % (tier.name, was, p, slack))
            elif p > was + slack:
                notes.append(
                    "tier %s rose from %.1f%% to %.1f%% -- run "
                    "\"make coverage-update\" to ratchet it in"
                    % (tier.name, was, p))

        if not tier.cap and p < tier.target:
            notes.append("tier %s is at %.1f%%, short of its %d%% target"
                         % (tier.name, p, tier.target))

    if baseline is None:
        notes.append("no baseline recorded yet -- run "
                     "\"make coverage-update\" to commit one")

    for n in notes:
        print("  note: %s" % n)
    if notes:
        print()

    if owed:
        print("  not yet at their floor (owed, not failed -- these become")
        print("  hard gates the moment they first clear it):")
        for o in owed:
            print("    %s" % o)
        print()

    if failures:
        print("  coverage-check FAILED:")
        for f in failures:
            print("    %s" % f)
        print()
        return 1

    print("  coverage-check passed: nothing regressed, and no file slipped")
    print("  back under a floor it had reached.")
    print()
    return 0


# ── main ─────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(
        description="Aggregate gcov data into a per-tier coverage report.")
    ap.add_argument("--build-dir", default=".",
                    help="top of the build tree to search for .gcda")
    ap.add_argument("--srcdir", default=".",
                    help="top of the source tree")
    ap.add_argument("--tiers", default=None,
                    help="tier table (default <srcdir>/tests/coverage.tiers)")
    ap.add_argument("--baseline", default=None,
                    help="baseline (default <srcdir>/tests/coverage.baseline)")
    ap.add_argument("--gcov", default=os.environ.get("GCOV", "gcov"),
                    help="the gcov to run; must match the compiler")
    ap.add_argument("--jobs", type=int, default=min(8, (os.cpu_count() or 2)),
                    help="parallel gcov invocations")
    ap.add_argument("--slack", type=float, default=2.0,
                    help="points a tier may drop before --check fails")
    ap.add_argument("--check", action="store_true",
                    help="enforce the floors and the baseline; exit non-zero")
    ap.add_argument("--update", action="store_true",
                    help="rewrite the baseline from this run")
    ap.add_argument("--uncovered", action="store_true",
                    help="also list functions never entered")
    args = ap.parse_args()

    tierpath = args.tiers or os.path.join(args.srcdir, "tests",
                                          "coverage.tiers")
    basepath = args.baseline or os.path.join(args.srcdir, "tests",
                                             "coverage.baseline")
    if not os.path.exists(tierpath):
        sys.exit("no tier table at %s" % tierpath)

    if not shutil.which(args.gcov):
        sys.exit("%s not found; it comes with gcc, so a coverage build "
                 "implies it" % args.gcov)

    tiers, _by_name, assign = load_tiers(tierpath)
    files, version, nprofiles = collect(args.build_dir, args.srcdir,
                                        args.gcov, args.jobs)
    reconcile(files, assign, args.srcdir, tierpath)

    tier_totals, _grand = report(files, tiers, version, nprofiles,
                                 args.uncovered)

    if args.update:
        write_baseline(basepath, tiers, tier_totals, files)
        return 0
    if args.check:
        return check(files, tiers, tier_totals, read_baseline(basepath),
                     args.slack)
    return 0


if __name__ == "__main__":
    sys.exit(main())
