"""C20l's mechanical step: deletes every EI_REWRITTEN / EI_REWRITTEN_SPECIAL line (the function-to-original map) from
src/ and tests/. Run from the repository root:

    python drop_rewritten.py [--report]

Per file with such lines:
  1. the lines `EI_REWRITTEN(0x..., Name);` (a trailing // comment included) go, and the comment blocks between two
     of them;
  2. a comment block right before a run of them goes when nothing but a blank line, a closing brace, a preprocessor
     line or the end of the file follows the run (it described the map lines);
  then, until nothing changes, and only next to a deletion:
  3. a namespace block left empty goes;
  4. a paragraph of comments only goes when what follows it is the end of the file, a closing brace or a section
     comment (`// --`): the map lines it introduced are gone;
  5. (.cpp, at namespace scope) an alias or using-declaration the file no longer uses goes, and a `using` line with
     nothing but closing braces after it;
  6. `#include "ei/hook.h"` goes when the file names nothing of hook.h any more;
  7. blank lines: a run is cut to one, and none stay at the file's ends, after a block's opening brace or before its
     closing one (not a namespace's);
  8. a .cpp left with nothing but comments, includes and namespace braces is deleted.
--report prints the comment lines steps 1, 2 and 4 delete.
"""
import os, re, sys

MAP = re.compile(r'^\s*EI_REWRITTEN(?:_SPECIAL)?\(0x[0-9A-Fa-f]{8}, [^;]*\);\s*(?://.*)?$')
COMMENT = re.compile(r'^\s*//')
HOOK_NAMES = re.compile(r'\b(?:kImageBase|HookEntry|HookRegistrar|InstallHooks|kGameTextBegin|kGameTextEnd|InGameText|'
                        r'NoteHarnessPatch|HarnessPatchAt|UnexpectedSkips|SetMissingCodeHandler|ReportNoCode|'
                        r'StartupAction|StartupActionRegistrar|RunStartupActions|InstallHooksAtWinMain|'
                        r'SetOriginalWinMainAction|EI_RECORD|EI_STARTUP_ACTION|EI_CAT|EI_CAT_)\b|'
                        r'\bStandalone\s*\(')
INCLUDE = re.compile(r'^#include "ei/hook\.h"\s*$')
NAMESPACE_OPEN = re.compile(r'^namespace(?:\s+[\w:]+)?\s*\{\s*$')
NAMESPACE_CLOSE = re.compile(r'^\}\s*(?://.*)?$')
ALIAS = re.compile(r'^using\s+(\w+)\s*=[^;]*;\s*(?://.*)?$')
DECLARATION = re.compile(r'^using\s+(?!namespace\b)[\w:]*::(\w+)\s*;\s*(?://.*)?$')
USING_NAMESPACE = re.compile(r'^using\s+namespace\s+[\w:]+\s*;\s*$')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)
    return re.sub(r'//[^\n]*', '', text)


class File:
    """The file's surviving lines with their original indices: a deletion lies between two survivors whose indices
    are not consecutive."""

    def __init__(self, path, lines, report):
        self.path, self.report, self.total = path, report, len(lines)
        self.lines = list(enumerate(lines))   # (original index, text)

    def text(self, i):
        return self.lines[i][1]

    def gap(self, i, j):
        """A deletion between survivors i < j (-1: the file's start, len: its end)."""
        a = self.lines[i][0] if i >= 0 else -1
        b = self.lines[j][0] if j < len(self.lines) else self.total
        return b - a != j - i

    def delete(self, i, j, why=None):
        if why and self.report:
            for k in range(i, j):
                if not MAP.match(self.text(k)):
                    print(f'{self.path}:{self.lines[k][0] + 1}: [{why}] {self.text(k).strip()}')
        del self.lines[i:j]

    def next_nonblank(self, i):
        while i < len(self.lines) and not self.text(i).strip():
            i += 1
        return i


def drop_map(f):
    """Steps 1 and 2."""
    n = len(f.lines)
    gone = [False] * n
    text = [t for _, t in f.lines]
    i = 0
    while i < n:
        if not MAP.match(text[i]):
            i += 1
            continue
        j = i   # the run: map lines and the comment blocks between two of them
        while j < n:
            if MAP.match(text[j]):
                j += 1
                continue
            k = j
            while k < n and COMMENT.match(text[k]):
                k += 1
            if k > j and k < n and MAP.match(text[k]):
                j = k
                continue
            break
        for k in range(i, j):
            gone[k] = True
        after = text[j].strip() if j < n else ''
        if not after or after.startswith(('}', '#')):
            k = i - 1
            while k >= 0 and COMMENT.match(text[k]) and not gone[k]:
                k -= 1
            for c in range(k + 1, i):
                gone[c] = True
        i = j
    if f.report:
        for k in range(n):
            if gone[k] and not MAP.match(text[k]):
                print(f'{f.path}:{k + 1}: [map comment] {text[k].strip()}')
    f.lines = [line for line, g in zip(f.lines, gone) if not g]


def empty_namespace(f):
    for a in range(len(f.lines)):
        if NAMESPACE_OPEN.match(f.text(a)):
            b = f.next_nonblank(a + 1)
            if b < len(f.lines) and NAMESPACE_CLOSE.match(f.text(b)) and f.gap(a, b):
                f.delete(a, b + 1)
                return True
    return False


def orphan_comments(f):
    i = 0
    while i < len(f.lines):
        if not COMMENT.match(f.text(i)) or i and f.text(i - 1).strip():
            i += 1
            continue
        q = i
        while q < len(f.lines) and COMMENT.match(f.text(q)):
            q += 1
        if q < len(f.lines) and f.text(q).strip():   # the paragraph goes on with code
            i = q
            continue
        r = f.next_nonblank(q)
        follows = f.text(r).strip() if r < len(f.lines) else ''
        if i and f.gap(q - 1, r) and (not follows or follows.startswith('}') or follows.startswith('// --')):
            f.delete(i, q, 'orphan comment')
            return True
        i = q
    return False


def unused_usings(f):
    code = strip_comments('\n'.join(t for _, t in f.lines))
    for i in range(len(f.lines)):
        line = f.text(i)
        m = ALIAS.match(line) or DECLARATION.match(line)
        if m and len(re.findall(r'\b' + re.escape(m.group(1)) + r'\b', code)) == 1:
            f.delete(i, i + 1)
            return True
        if (m or USING_NAMESPACE.match(line)) and all(
                not l.strip() or l.strip().startswith('}')
                for l in strip_comments('\n'.join(t for _, t in f.lines[i + 1:])).split('\n')):
            f.delete(i, i + 1)
            return True
    return False


def hook_include(f):
    if HOOK_NAMES.search(strip_comments('\n'.join(t for _, t in f.lines))):
        return False
    for i in range(len(f.lines)):
        if INCLUDE.match(f.text(i)):
            f.delete(i, i + 1)
            return True
    return False


def blanks(f):
    i = 0
    while i < len(f.lines):
        if f.text(i).strip():
            i += 1
            continue
        j = f.next_nonblank(i)   # the run [i, j)
        if i == 0 or j == len(f.lines):
            f.delete(i, j)
            return True
        if not f.gap(i - 1, j):
            i = j
            continue
        if j - i > 1:
            f.delete(i + 1, j)
            return True
        prev, nxt = f.text(i - 1), f.text(j)
        if (prev.rstrip().endswith('{') and not NAMESPACE_OPEN.match(prev)
                or nxt.lstrip().startswith('}') and not NAMESPACE_CLOSE.match(nxt) and not COMMENT.match(prev)):
            f.delete(i, j)
            return True
        i = j
    return False


def has_code(text):
    for line in strip_comments(text).split('\n'):
        s = line.strip()
        if s and not re.match(r'(#|namespace\b[^;]*\{$|\}(\s*//.*)?$)', s):
            return True
    return False


def main():
    report = '--report' in sys.argv[1:]
    files = removed = 0
    for top in ('src', 'tests'):
        for d, _, names in os.walk(top):
            for n in sorted(names):
                if not n.endswith(('.cpp', '.h', '.inc')):
                    continue
                p = os.path.join(d, n)
                raw = open(p, encoding='utf-8', newline='').read()
                nl = '\r\n' if '\r\n' in raw else '\n'
                lines = raw.split(nl)
                if lines and lines[-1] == '':
                    lines.pop()
                count = sum(1 for l in lines if MAP.match(l))
                if not count:
                    continue
                f = File(p, lines, report)
                drop_map(f)
                steps = [empty_namespace, orphan_comments]
                if p.endswith('.cpp'):
                    steps.append(unused_usings)
                steps += [hook_include, blanks]
                while any(step(f) for step in steps):
                    pass
                text = nl.join(t for _, t in f.lines) + nl
                files += 1
                removed += count
                if p.endswith('.cpp') and not has_code(text):
                    os.remove(p)
                    print(f'{p}: nothing left, deleted')
                    continue
                with open(p, 'w', encoding='utf-8', newline='') as out:
                    out.write(text)
    print(f'{removed} map line(s) removed from {files} file(s)')


if __name__ == '__main__':
    main()
