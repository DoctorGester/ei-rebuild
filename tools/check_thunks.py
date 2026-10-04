"""Finds EI_REPLACE_METHOD registrations of methods that are (or override) virtual functions.

A pointer to a virtual member function is MSVC's dispatch thunk (MOV EAX,[ECX]; JMP [EAX+n]):
hooking the game's function with it jumps through the object's vtable back into the hook, and
the game overflows its stack (rewrite-guide rule 4). A method silently becomes an override when a
base class declares a virtual with the same name and signature (CEngineObject::Create, Destroy,
Release, Load, Save, ...). The installer refuses such hooks at run time and logs
"virtual-call thunk"; this check catches them at build time.

The headers' class declarations are read with regular expressions: each class's bases and the
names it declares `virtual` or `override`. A registration Class::Method is a problem when Method
is virtual in Class or in any of its bases (by name; overloads are not told apart).

usage: python tools/check_thunks.py     (exits 1 on any problem)
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLASS = re.compile(r'\b(?:class|struct)\s+(\w+)\s*(?:final\s*)?(?::\s*([^{;]+))?\{')
VIRTUAL = re.compile(r'\bvirtual\b[^;{(]*?\b(~?\w+)\s*\(')
OVERRIDE = re.compile(r'\b(~?\w+)\s*\([^;{]*\)\s*(?:const\s*)?override\b')
REGISTER = re.compile(r'EI_REPLACE_METHOD\(\s*0x([0-9A-Fa-f]+)\s*,\s*([\w:]+)\s*\)')


def strip_comments(text):
    """The text without // and /* */ comments (string literals are not special-cased)."""
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def class_bodies(text):
    """(name, bases, body) of every class/struct definition, with nested braces matched."""
    for m in CLASS.finditer(text):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
            i += 1
        bases = [b.split()[-1].split('::')[-1] for b in (m.group(2) or '').split(',') if b.strip()]
        yield m.group(1), bases, text[m.end():i]


def main():
    bases, virtuals = {}, {}
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.h'), recursive=True) + \
            glob.glob(os.path.join(ROOT, 'include', '**', '*.h'), recursive=True):
        text = strip_comments(open(path, encoding='utf-8', errors='replace').read())
        for name, base_list, body in class_bodies(text):
            bases.setdefault(name, set()).update(base_list)
            names = virtuals.setdefault(name, set())
            names.update(VIRTUAL.findall(body))
            names.update(OVERRIDE.findall(body))

    def virtual_in(cls, method, seen=()):
        if cls in seen:
            return None
        if method in virtuals.get(cls, ()):
            return cls
        for b in bases.get(cls, ()):
            found = virtual_in(b, method, seen + (cls,))
            if found:
                return found
        return None

    bad = total = 0
    for path in sorted(glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True)):
        text = open(path, encoding='utf-8', errors='replace').read()
        for m in REGISTER.finditer(text):
            parts = m.group(2).split('::')
            if len(parts) < 2:
                continue
            total += 1
            cls, method = parts[-2], parts[-1]
            owner = virtual_in(cls, method)
            if owner:
                line = text.count('\n', 0, m.start()) + 1
                print(f'{os.path.relpath(path, ROOT)}:{line} {m.group(1).upper()} {cls}::{method}: '
                      f'virtual in {owner} (register a non-virtual shim; rewrite-guide rule 4)')
                bad += 1
    print(f'{total} method registrations checked, {bad} problem(s)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
