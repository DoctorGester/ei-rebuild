"""Shared helpers for the tools that scan the C++ sources under src/ (progress.py, inline_shims.py)."""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')

# Comments, string literals and character literals, in the order the compiler meets them.
_TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"'
                    r"|'(?:\\(?:x[0-9A-Fa-f]+|[0-7]{1,3}|.)|[^'\\\n])'", re.S)

class Source:
    """One file of src/: `path` relative to the repo root with '/' separators, `raw` the text as
    written, `code` the same text with every comment blanked (same offsets and line numbers)."""

    def __init__(self, path, raw):
        self.path = path
        self.raw = raw
        self.code = blank_comments(raw)

    def line_of(self, offset):
        return self.raw.count('\n', 0, offset) + 1


def blank_comments(text):
    """Replaces comments by spaces (newlines kept); strings and character literals stay."""
    def blank(match):
        token = match.group(0)
        return re.sub(r'[^\n]', ' ', token) if token.startswith('/') else token
    return _TOKEN.sub(blank, text)


def load_sources():
    """Every .cpp and .h under src/, sorted by path."""
    sources = []
    for dirpath, _, names in os.walk(SRC):
        for name in names:
            if name.endswith(('.cpp', '.h')):
                full = os.path.join(dirpath, name)
                with open(full, encoding='utf-8', errors='replace') as f:
                    sources.append(Source(os.path.relpath(full, ROOT).replace(os.sep, '/'), f.read()))
    return sorted(sources, key=lambda s: s.path)



def blank_literals(code):
    """`code` (comments already blanked) with string and character literals blanked too (same offsets)."""
    def blank(match):
        token = match.group(0)
        return token if token.startswith('/') else re.sub(r'[^\n]', ' ', token)
    return _TOKEN.sub(blank, code)


_OPENER = re.compile(r'(?:^|[;{}])([^;{}]*)\Z')
_NAMESPACE = re.compile(r'\bnamespace\s*([\w:]*)\s*\Z')
_CLASS = re.compile(r'(?<!enum\s)\b(?:class|struct|union)\s+(?:__declspec\(\w+\)\s+)?(\w+)\b[^()]*\Z')


class Scopes:
    """The brace blocks of one file (comments and literals blanked): each a namespace (its name, '' for an anonymous
    one), a class (struct, union: its name), `extern "C"` (transparent) or another block (a function body, an
    initialiser, an enum). `at(offset)` is the list of the blocks around an offset, outermost first."""

    def __init__(self, code):
        code = blank_literals(code)
        self.blocks = []   # (begin, end, kind, name)
        stack = []
        last = 0
        for m in re.finditer(r'[{}]', code):
            if m.group(0) == '{':
                head = code[last:m.start()]
                tail = head[max(head.rfind(';'), head.rfind('}'), head.rfind('{')) + 1:].strip()
                ns, cls = _NAMESPACE.search(tail), _CLASS.search(tail)
                if ns:
                    kind, name = 'namespace', ns.group(1)
                elif re.search(r'\bextern\s*\Z', tail) or tail.endswith('extern'):
                    kind, name = 'extern', ''
                elif cls and '=' not in tail:
                    kind, name = 'class', cls.group(1)
                else:
                    kind, name = 'block', ''
                stack.append((m.start(), kind, name))
            elif stack:
                begin, kind, name = stack.pop()
                self.blocks.append((begin, m.start(), kind, name))
            last = m.start() + 1
        self.blocks.sort()

    def at(self, offset):
        return [b for b in self.blocks if b[0] < offset < b[1]]

    def namespace_at(self, offset):
        """The namespaces around `offset` ('' for an anonymous one), or None inside a class or another block."""
        out = []
        for _, _, kind, name in self.at(offset):
            if kind == 'namespace':
                out += name.split('::') if name else ['']
            elif kind != 'extern':
                return None
        return out
