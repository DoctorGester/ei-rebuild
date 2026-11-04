"""Runs the build (cmake --build) and runs it once more when its failed steps failed without a cause (H52a;
docs/notes/standalone.md, "Build steps that fail without a cause").

On a loaded machine a step now and then exits non-zero and prints nothing (a compile, lib.exe) or the linker's
"LNK1000: Internal error". A plain rerun passes. After such a failure this prints a line saying so and builds again;
the second failure is final. A failure with a message (a compile error, a missing symbol) is never retried.

A step counts as causeless when ninja's block for it (its FAILED line, its command line, then the step's own output)
holds no output, or only the exit-code line of the step wrapper (tools/compiler_cache_launcher.cpp), or LNK1000.
Every failed step must be causeless to retry.

usage: python tools/build_retry.py BUILD_FOLDER [ninja/cmake build arguments]
"""
import os, re, subprocess, sys

# What the launcher prints for every step that exits non-zero (ninja filters the compiler's own file name line, so a
# crashed compile shows this line only).
LAUNCHER_SILENT = re.compile(rb'^compiler_cache_launcher: .* exited with 0x[0-9A-Fa-f]+$')
LNK1000 = re.compile(rb'\bLNK1000\b')


def failed_blocks(lines):
    """The output lines (without the FAILED line and the command line) of each failed step in a ninja log."""
    blocks = []
    current = None
    for line in lines:
        if line.startswith(b'FAILED:'):
            current = []
            blocks.append(current)
        elif current is not None and (line.startswith(b'ninja: build stopped') or re.match(rb'^\[\d+/\d+\] ', line)):
            current = None
        elif current is not None:
            current.append(line)
    # The first line of a block is the step's command line.
    return [block[1:] for block in blocks]


def causeless(lines):
    """True when the failed steps in a ninja log (its lines) failed without a cause: there is at least one and none
    has output of its own beyond the launcher's silent-exit line (a block with LNK1000 may have more)."""
    blocks = failed_blocks([line.rstrip(b'\r\n') for line in lines])
    if not blocks:
        return False
    for block in blocks:
        if any(LNK1000.search(line) for line in block):   # the linker's crash report has detail lines of its own
            continue
        if any(line.strip() and not LAUNCHER_SILENT.match(line) for line in block):
            return False
    return True


def build(arguments):
    """Runs cmake --build, passing its output through as it comes; returns (exit code, output lines)."""
    process = subprocess.Popen(['cmake', '--build'] + arguments, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    lines = []
    for line in process.stdout:
        lines.append(line)
        sys.stdout.buffer.write(line)
        sys.stdout.buffer.flush()
    return process.wait(), lines


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    code, lines = build(sys.argv[1:])
    if code != 0 and causeless(lines):
        print(f'build_retry: the build failed in steps that printed no error (exit code {code}; docs/notes/'
              'standalone.md, "Build steps that fail without a cause"); building again once.', flush=True)
        code, lines = build(sys.argv[1:])
    return code


if __name__ == '__main__':
    sys.exit(main())
