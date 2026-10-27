"""Forward-declaration finder (C33): `class X;` / `struct X;` lines under src that nothing needs.

usage: python tools/fwd_decls.py [--list] [--remove CATEGORY[,CATEGORY] [--files-from N:M]] [--keep-file PATH]

Each declaration in a header is sorted into a category:
  defined-here   the same file defines the type (before or after the declaration)
  defined-incl   a header the file includes (transitively) defines the type
  unused         the name appears nowhere else in the file
  needed         the file names the type, and no included header defines it (keep)
--remove deletes the lines of the given categories (a blank line left behind is dropped too). The build decides
whether a removal was safe: a type an includer had only through this declaration, or a declaration that breaks an
include cycle, fails to compile and is put back by hand (config list below, KEEP).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
INCLUDE_DIRS = [SRC, ROOT / "include", ROOT / "third_party"]
DECL = re.compile(r"^[ \t]*(class|struct)[ \t]+(\w+)[ \t]*;[ \t]*(//.*)?$")
INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)

# Declarations that break an include cycle or keep a heavy header out; found by a failing build. "path:Name".
KEEP = {"src/game/figure_mesh.h:CMaterial"}  # unused here, but link_figure.h and others get it through this header


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)


def resolve(frm, name):
    for d in [frm.parent] + INCLUDE_DIRS:
        p = d / name
        if p.is_file():
            return p.resolve()
    return None


_inc_cache = {}


def includes(path):
    if path not in _inc_cache:
        out = []
        for m in INC.finditer(path.read_text(encoding="utf-8", errors="replace")):
            r = resolve(path, m.group(1))
            if r:
                out.append(r)
        _inc_cache[path] = out
    return _inc_cache[path]


def transitive(path):
    seen, todo = set(), list(includes(path))
    while todo:
        p = todo.pop()
        if p in seen or p == path:
            continue
        seen.add(p)
        todo.extend(includes(p))
    return seen


_def_cache = {}


def defines(path, name):
    key = path
    if key not in _def_cache:
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        _def_cache[key] = set(
            re.findall(r"\b(?:class|struct|union|enum(?:\s+class)?)\s+(\w+)\s*(?::[^;{]*)?\{", text)
        ) | set(re.findall(r"\busing\s+(\w+)\s*=", text)) | set(re.findall(r"\btypedef\b[^;{]*\b(\w+)\s*;", text)) | set(
            re.findall(r"\}\s*(\w+)\s*;", text))
    return name in _def_cache[key]


def classify(path):
    raw = path.read_text(encoding="utf-8", errors="replace")
    lines = raw.split("\n")
    body = strip_comments(raw)
    result = []
    incl = None
    for i, line in enumerate(lines):
        m = DECL.match(line)
        if not m:
            continue
        name = m.group(2)
        if i and lines[i - 1].lstrip().startswith("template"):
            result.append((i, name, "needed"))  # a template declaration: the line above belongs to it
            continue
        if f"{path.relative_to(ROOT).as_posix()}:{name}" in KEEP:
            result.append((i, name, "needed"))
            continue
        # uses outside declarations of this name
        uses = [
            x for x in re.finditer(r"\b%s\b" % re.escape(name), body)
            if not re.match(r"[ \t]*(class|struct)[ \t]+%s[ \t]*;" % re.escape(name), body[body.rfind("\n", 0, x.start()) + 1:])
        ]
        off = sum(len(x) + 1 for x in lines[:i])  # offset of this line in body (comment stripping keeps lines)
        d = re.search(r"(?:class|struct|union|enum(?:\s+class)?)\s+%s\s*(?::[^;{]*)?\{" % re.escape(name), body)
        earlier = re.search(r"^[ 	]*(?:class|struct)[ 	]+%s[ 	]*;" % re.escape(name), body[:off], re.M)
        if earlier:
            cat = "duplicate"
        elif d and d.start() < off:
            cat = "defined-here"
        elif d:
            # defined further down: the declaration is needed if anything names the type before the definition
            first = min((x.start() for x in uses), default=None)
            cat = "needed" if first is not None and first < d.start() else "defined-here"
        else:
            if incl is None:
                incl = transitive(path)
            providers = [h for h in incl if defines(h, name)]
            if providers and body[:off].count("{") != body[:off].count("}"):
                cat = "needed"  # inside a namespace or class: the name may be another type
            elif providers and any(path in transitive(h) for h in incl):
                cat = "needed"  # the file sits in an include cycle: which header comes first decides, keep
            elif providers:
                cat = "defined-incl"
            elif not uses:
                cat = "unused"
            else:
                cat = "needed"
        result.append((i, name, cat))
    return lines, result


def main():
    args = sys.argv[1:]
    remove = []
    only = None
    if "--remove" in args:
        remove = args[args.index("--remove") + 1].split(",")
    if "--files" in args:
        only = set(args[args.index("--files") + 1].split(","))
    totals = {}
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".h", ".cpp", ".inc", ".hpp"):
            continue
        rel = path.relative_to(ROOT).as_posix()
        if only is not None and rel not in only:
            continue
        lines, found = classify(path)
        if not found:
            continue
        drop = set()
        for i, name, cat in found:
            totals[cat] = totals.get(cat, 0) + 1
            if "--list" in args:
                print(f"{rel}:{i + 1}: {cat}: {name}")
            if cat in remove:
                drop.add(i)
        if drop:
            out = []
            for i, line in enumerate(lines):
                if i in drop:
                    # drop a blank line that would now double up
                    if out and out[-1].strip() == "" and i + 1 < len(lines) and lines[i + 1].strip() == "":
                        out.pop()
                    continue
                out.append(line)
            path.write_text("\n".join(out), encoding="utf-8", newline="")
    print("total", sum(totals.values()), totals)


if __name__ == "__main__":
    main()
