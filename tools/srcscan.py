"""Shared helpers for the tools that scan the C++ sources under src/ (progress.py, gen_originals.py)."""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')

# Comments, string literals and character literals, in the order the compiler meets them.
_TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"'
                    r"|'(?:\\(?:x[0-9A-Fa-f]+|[0-7]{1,3}|.)|[^'\\\n])'", re.S)

ORIGINAL = re.compile(r'\bEI_ORIGINAL\s*\(\s*0[xX]([0-9A-Fa-f]+)\s*\)')


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


def forwarder_sites(sources):
    """Every `EI_ORIGINAL(0x...)` forwarder as (address, source, offset of the macro), in file order."""
    return [(int(m.group(1), 16), s, m.start()) for s in sources for m in ORIGINAL.finditer(s.code)]
