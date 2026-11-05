r"""What the CI jobs print for each run that did not pass or differs from its golden (H62c; the macOS and Windows
workflows, docs/notes/harness.md): the jobs keep no run folders, so this is all there is to read afterwards.

    python tools/ci_details.py <runs folder> [--install vanilla|rebirth] <scenario> ...

For each scenario's run folder (<runs>/<name>-standalone, or -standalone-rebirth): the report's FAILED lines; when it
differs, golden.py compare's first differing records (with their world update, frame and tick) and the fields; the log
lines on the game window (client area made, shown, set; windowsize; the resolution), the present mode, the device,
focus, the cursor confinement, a lost device and a fatal renderer error; the frame-stamped log lines (steps, replayed
input events, pauses) from 15 frames before the first differing record to 2 after it (a failed run: the last 20); a
crash's stack by function names; and the TIMING lines on the median frame, the waits on the GPU and the memory (H62d).
Runs that passed and are the same as their golden print nothing.
"""
import argparse
import os
import re
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ABOUT = re.compile(r"client area|windowsize|Resolution: |render: presenting|render: fatal|gpu: .*device|"
                   r"cursor confinement|focus|activat|minimi|occlu", re.I)
STAMP = re.compile(r"harness: frame (\d+):")
FIRST = re.compile(r"first difference at record \d+ \(world update \d+, (?:tick \d+, )?frame (\d+)")


def read_lines(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()


def details(runs, name, install):
    """The lines printed for one scenario (none when its run passed and is the same as its golden)."""
    run = os.path.join(runs, name + ("-standalone-rebirth" if install == "rebirth" else "-standalone"))
    if not os.path.isdir(run):
        return ["== " + name + ": no run folder " + run]
    report = read_lines(os.path.join(run, "harness_report.txt"))
    failed = [line for line in report if line.startswith("FAILED")]
    done = subprocess.run([sys.executable, os.path.join(TOOLS, "golden.py"), "compare", name, "--install", install,
                           "--run", run, "--records", "3", "--fields", "30"], capture_output=True, text=True)
    compared = (done.stdout + done.stderr).splitlines()
    differs = any("DIFFERS" in line for line in compared)
    if not failed and not differs and any(line == "RESULT: PASS" for line in report):
        return []
    # A scenario that says how it ends ("# expect: TIMEOUT", the watchdog ones) is judged by run_all.
    scenario = read_lines(os.path.join(os.path.dirname(TOOLS), "tests", "scenarios", name + ".txt"))
    if not differs and any(line.startswith("# expect:") for line in scenario):
        return []
    out = ["== " + name + " (" + install + ")" + (": not passed" if failed or not report else "") +
           (": differs from its golden" if differs else "")]
    out += failed[:20]
    if differs:
        out += compared[:80]
    log = read_lines(os.path.join(run, "ei-rebuild.log"))
    out.append("-- the window, the present mode, the device, focus")
    out += [line for line in log if ABOUT.search(line)][:30]
    first = next((m for line in compared if (m := FIRST.search(line))), None)
    if first:
        frame = int(first.group(1))
        out.append("-- the log from frame %d to %d (steps, input events, pauses; the first differing record is at "
                   "frame %d)" % (frame - 15, frame + 2, frame))
        inside = [i for i, line in enumerate(log) if (m := STAMP.search(line)) and frame - 15 <= int(m.group(1)) <= frame + 2]
        out += log[inside[0]:inside[-1] + 1][:80] if inside else ["(nothing stamped then)"]
    elif failed:
        out.append("-- the last 20 frame-stamped log lines")
        out += [line for line in log if STAMP.search(line)][-20:]
    if log:
        sys.path.insert(0, TOOLS)
        import resolve_crash
        try:
            stack = resolve_crash.resolve(run)
        except Exception as e:   # a stack that cannot be named still leaves the rest
            stack = ["the crash's stack not named: %s" % e]
        if stack != ["no crash logged"]:
            out.append("-- the crash (tools/resolve_crash.py)")
            out += stack[:40]
    out += [line for line in report if line.startswith(("TIMING: median frame", "TIMING: gpu"))]
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("runs")
    parser.add_argument("--install", choices=("vanilla", "rebirth"), default="vanilla")
    parser.add_argument("scenarios", nargs="*")
    args = parser.parse_args(argv)
    for name in args.scenarios:
        for line in details(args.runs, name, args.install):
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
