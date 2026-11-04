"""H47a: no run writes into an install. The install trees' file names, sizes and modification times are listed before a
suite and again after; a run whose game ran while a file changed fails.

The game's own writes all pass the harness's path seam, which refuses them under an install root. What the seam does not
see (a driver's cache, SDL's own file calls) shows here. Read only: the installs are only listed, never written.

A Watch lists the installs when it is made and check() lists them again. A change belongs to the runs whose game window
(windows: {run folder: [(start, end)]}, seconds since the epoch, filled by run_all.run_once) holds the file's time, give
or take a few seconds. A change with no time of its own (a removed file, a size change that kept an old time) belongs to
every run. A change that falls in no run's window is only reported (the user playing from the install, say).

usage: python tools/install_watch.py <install folder>...    (lists them and prints the scan time, twice)
"""
import concurrent.futures
import os
import sys
import time

BEFORE_S = 2.0         # a file's time may lead the game window by this much (clocks, file system granularity)
AFTER_S = 3.0          # and trail it by this much
SHOWN = 4              # changes named in a run's failure; the rest counted


def is_link(entry):
    return entry.is_symlink() or entry.is_junction()


def list_tree(root):
    """{path relative to root, '/'-separated: (size, mtime_ns)} of every file under root (a folder: None; a link is not
    followed, it is listed as a leaf). {} for a root that does not exist."""
    found = {}
    stack = [root]
    while stack:
        folder = stack.pop()
        try:
            with os.scandir(folder) as entries:
                for entry in entries:
                    rel = os.path.relpath(entry.path, root).replace(os.sep, '/')
                    try:
                        if not is_link(entry) and entry.is_dir(follow_symlinks=False):
                            found[rel + '/'] = None
                            stack.append(entry.path)
                            continue
                        info = entry.stat(follow_symlinks=False)
                        found[rel] = (info.st_size, info.st_mtime_ns)
                    except OSError:
                        found[rel] = (-1, 0)          # listed but not readable: still a name
        except OSError:
            pass
    return found


def list_installs(roots, workers=8):
    """{root: list_tree(root)} for the distinct roots; the top-level folders of a tree are listed in parallel."""
    roots = list(dict.fromkeys(os.path.normpath(r) for r in roots))
    result = {r: {} for r in roots}
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        jobs = []
        for root in roots:
            try:
                with os.scandir(root) as entries:
                    tops = list(entries)
            except OSError:
                continue
            for entry in tops:
                jobs.append((root, entry, pool.submit(list_tree_entry, root, entry)))
        for root, entry, future in jobs:
            result[root].update(future.result())
    return result


def list_tree_entry(root, entry):
    """list_tree()'s part for one entry of the root (a file or a folder with everything under it), names relative to root."""
    rel = entry.name
    try:
        if is_link(entry) or not entry.is_dir(follow_symlinks=False):
            info = entry.stat(follow_symlinks=False)
            return {rel: (info.st_size, info.st_mtime_ns)}
    except OSError:
        return {rel: (-1, 0)}
    found = {rel + '/': None}
    for sub, value in list_tree(entry.path).items():
        found[rel + '/' + sub] = value
    return found


def changes(before, after):
    """[(kind, root, path, seconds or None)] between two list_installs() results: kind 'added', 'removed' or 'changed';
    seconds: the new file's modification time (None for a removed file)."""
    found = []
    for root in sorted(set(before) | set(after)):
        old, new = before.get(root, {}), after.get(root, {})
        for path in sorted(set(old) | set(new)):
            if path not in new:
                found.append(('removed', root, path, None))
            elif path not in old:
                found.append(('added', root, path, new[path][1] / 1e9 if new[path] else None))
            elif old[path] != new[path]:
                found.append(('changed', root, path, new[path][1] / 1e9 if new[path] else None))
    return found


def owners(found, windows, since=None):
    """({run folder: [change]}, [change in no run's window]) for changes() `found` and windows {folder: [(start, end)]}.
    since: when the first list was made. A change with no time, or one older than that (a file copied in with its old
    time: it cannot be placed), belongs to every run."""
    hit = {}
    loose = []
    for change in found:
        seconds = change[3]
        if seconds is None or (since is not None and seconds < since - BEFORE_S):
            for folder in windows:
                hit.setdefault(folder, []).append(change)
            if not windows:
                loose.append(change)
            continue
        folders = [f for f, ws in windows.items() if any(s - BEFORE_S <= seconds <= e + AFTER_S for s, e in ws)]
        for folder in folders:
            hit.setdefault(folder, []).append(change)
        if not folders:
            loose.append(change)
    return hit, loose


def describe(change):
    kind, root, path, _ = change
    return f'{kind} {os.path.join(root, path.rstrip("/"))}'


class Watch:
    """The installs' lists from when it was made (taking `scan_seconds`), and the check after the suite."""

    def __init__(self, roots, lister=list_installs):
        self.lister = lister
        started = self.since = time.time()
        self.before = lister(roots)
        self.scan_seconds = time.time() - started
        self.roots = list(self.before)
        self.files = sum(len(t) for t in self.before.values())

    def check(self, windows):
        """Lists the installs again. (the changes, {run folder: [change]}, [loose change], seconds the listing took)."""
        started = time.time()
        after = self.lister(self.roots)
        seconds = time.time() - started
        found = changes(self.before, after)
        hit, loose = owners(found, windows, self.since)
        return found, hit, loose, seconds

    def apply(self, results, folders, windows, out=None):
        """results {job: (result, seconds, failures, notes)}, folders {job: run folder}: every run whose window holds a
        change fails (its failure names the first changes). Prints what the check found; returns results."""
        out = out or sys.stdout
        found, hit, loose, seconds = self.check(windows)
        print(f'\ninstall trees: {self.files} files in {len(self.roots)} install(s) listed read-only before '
              f'({self.scan_seconds:.1f}s) and after ({seconds:.1f}s) the suite; '
              + ('no change' if not found else f'{len(found)} changed'), file=out)
        for change in loose:
            print(f'  {describe(change)} (changed in no run\'s time: not a run\'s)', file=out)
        failed = []
        for job, folder in folders.items():
            mine = hit.get(folder)
            if not mine or job not in results:
                continue
            result, run_seconds, failures, notes = results[job]
            names = '; '.join(describe(c) for c in mine[:SHOWN]) + (f'; and {len(mine) - SHOWN} more' if len(mine) > SHOWN else '')
            results[job] = ('FAIL', run_seconds, list(failures) + [f'an install changed while this run played: {names}'], notes)
            failed.append(job)
        if failed:
            print(f'  {len(failed)} run(s) overlap a change and fail: ' + ', '.join(f'{n}-{i}' for n, i in failed[:12])
                  + (' ...' if len(failed) > 12 else ''), file=out)
        return results


def main(argv):
    roots = argv or [os.getcwd()]
    for i in range(2):
        started = time.time()
        tree = list_installs(roots)
        print(f'run {i + 1}: {sum(len(t) for t in tree.values())} entries in {time.time() - started:.2f}s')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
