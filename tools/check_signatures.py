"""Checks that every hooked or forwarded game function is declared with the stack arguments the
original pops (its RET n).

Four bugs so far came from declaring a function with too few stack arguments or misreading them:
the caller and callee then disagree on the stack and the game crashes later, far from the cause.
For every EI_REPLACE / EI_REPLACE_METHOD / EI_ORIGINAL / EI_VERIFY in src/ and tests/, this takes
the C++ declaration from build/verify/ei_hook.map (undecorated with undname), computes how many
bytes of stack arguments the calling convention leaves for the callee to pop, and compares that
with the RET n instructions of the original function in game.exe.

Argument ORDER is not checked (that needs reading the disassembly); counts are.

usage: python tools/check_signatures.py   (after build.cmd verify; needs capstone and undname on PATH,
                                           i.e. run from a VS developer prompt or via build.cmd's vcvars)
exit code 1 when a mismatch is found
"""
import bisect, glob, os, re, subprocess, sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = r'F:\games\Evil Islands (Rebirth)\starter\Engine\game.exe'
MAP = os.path.join(ROOT, 'build', 'verify', 'ei_hook.map')


def load_sizes():
    sizes = {}
    for line in open(os.path.join(ROOT, 'src', 'hook', 'function_sizes.inc')):
        m = re.match(r'\{0x([0-9A-F]+), (\d+)\}', line)
        if m:
            sizes[int(m.group(1), 16)] = int(m.group(2))
    return sizes


def original_pops(data, md, address, size):
    """The set of n in RET n inside the function (0 for a plain RET)."""
    pops = set()
    off = address - 0x400000
    prev = None
    for insn in md.disasm(data[off:off + size], address):
        if insn.mnemonic == 'ret':
            # A catch block (funclet) inside the function returns its continuation address with
            # MOV EAX,<address in the function>; RET. That RET is not the function's own.
            funclet = (not insn.op_str and prev is not None and prev.mnemonic == 'mov'
                       and prev.op_str.startswith('eax, 0x')
                       and address <= int(prev.op_str[5:], 16) < address + size)
            if not funclet:
                pops.add(int(insn.op_str, 0) if insn.op_str else 0)
        prev = insn
    return pops


def undecorate(names):
    """Decorated name -> undecorated, as undname prints it. dbghelp's UnDecorateSymbolName (in
    process, ~0.2 s for the whole map) does almost all; it writes "UNKNOWN" for a deduced `auto`
    (STL lambdas), so those few go to undname (one process per 200 names took ~5 s for all)."""
    out = {}
    try:
        import ctypes
        undecorate_one = ctypes.WinDLL('dbghelp').UnDecorateSymbolName
        undecorate_one.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32]
        buf = ctypes.create_string_buffer(8192)
        for name in names:
            if undecorate_one(name.encode('latin-1'), buf, len(buf), 0):
                und = buf.value.decode('latin-1')
                if 'UNKNOWN' not in und:
                    out[name] = und
    except (OSError, AttributeError):
        pass
    names = [n for n in names if n not in out]
    chunks, chunk, length = [], [], 0
    for n in names:     # Windows command lines stop at 32767 characters; release maps have long template names
        if chunk and (len(chunk) == 200 or length + len(n) > 24000):
            chunks.append(chunk)
            chunk, length = [], 0
        chunk.append(n)
        length += len(n) + 3
    if chunk:
        chunks.append(chunk)
    for chunk in chunks:
        res = subprocess.run(['undname'] + chunk, capture_output=True, text=True).stdout
        for dec, und in re.findall(r'Undecoration of :- "(.*?)"\s*is :- "(.*?)"', res):
            out[dec] = und
    return out


def split_params(text):
    depth, cur, out = 0, '', []
    for ch in text:
        if ch in '<(':
            depth += 1
        elif ch in '>)':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return [p for p in out if p and p != 'void']


BUILTIN = re.compile(r'^(const\s+|volatile\s+|unsigned\s+|signed\s+)*(int|char|short|long|float|double|bool|wchar_t|'
                     r'__int8|__int16|__int32|__int64|long long|enum\s+[\w:]+)(\s+const)?$')


def param_size(p):
    """Stack size of one parameter, or None for a struct passed by value (size unknown here)."""
    if '*' in p or '&' in p:
        return 4
    if not BUILTIN.match(p.strip()):
        return None
    if re.search(r'\bdouble\b|\b__int64\b|long long', p):
        return 8
    return 4


def stack_bytes(und):
    """(convention, bytes the callee pops) from an undecorated signature, or None."""
    m = re.search(r'__(thiscall|stdcall|fastcall|cdecl)\s+[^(]*\((.*)\)', und)
    if not m:
        return None
    conv, params = m.group(1), split_params(m.group(2))
    if conv == 'cdecl':
        return conv, 0
    sizes = [param_size(p) for p in params]
    # A class returned by value (e.g. mfc::CString, which has a destructor) goes through a hidden
    # pointer pushed after the arguments; the callee pops it with them (__thiscall, __stdcall).
    if conv in ('thiscall', 'stdcall') and re.search(r'(?:^|\s)class\s+[\w:<>, ]+?\s+__' + conv + r'\b', und):
        sizes.append(4)
    # MSVC member functions return every struct or union through that pointer too, even a small POD
    # such as SIZE that a free function would return in EDX:EAX.
    elif conv == 'thiscall' and re.search(r'(?:^|\s)(?:struct|union)\s+[\w:<>, ]+?\s+__thiscall\b', und):
        sizes.append(4)
    if None in sizes:
        return None
    if conv == 'fastcall':
        regs = 0
        rest = []
        for p, s in zip(params, sizes):
            integral = s == 4 and ('*' in p or '&' in p or not re.search(r'\bfloat\b', p))
            if integral and regs < 2:
                regs += 1
            else:
                rest.append(s)
        return conv, sum(rest)
    return conv, sum(sizes)


def declarations(map_path=MAP):
    """Qualified function name -> [(undecorated signature, object file)] from the linker map."""
    entries = re.findall(r'^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\?\S+)\s+[0-9a-f]{8}\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$',
                         open(map_path, errors='replace').read(), re.M)
    und = undecorate({d for d, _ in entries})
    by_name = {}
    for dec, obj in entries:
        u = und.get(dec)
        m = re.search(r'([\w:~<>]+)\(', u) if u else None
        if m:
            by_name.setdefault(m.group(1), []).append((u, obj.lower()))
    return by_name


def registrations():
    """(address, name, kind, file, line) of every EI_REPLACE(_METHOD) ('replace'), EI_VERIFY ('verify')
    and EI_ORIGINAL ('forwarder') in src/ and tests/verify/."""
    regs = []
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True) + \
                glob.glob(os.path.join(ROOT, 'tests', 'verify', '*.cpp')):
        text = open(path, encoding='utf-8', errors='replace').read()
        # Blank out line comments and __declspec(...) (same length, so line numbers hold): otherwise
        # "__declspec" or a word in a comment before a forwarder is taken as the function's name.
        blank = lambda m: re.sub(r'[^\n]', ' ', m.group(0))
        text = re.sub(r'//[^\n]*', blank, text)
        text = re.sub(r'__declspec\(\s*\w+\s*\)', blank, text)
        rel = os.path.relpath(path, ROOT)
        for m in re.finditer(r'EI_REPLACE(?:_METHOD)?\(\s*0x([0-9A-Fa-f]+)\s*,\s*([\w:]+)', text):
            regs.append((int(m.group(1), 16), m.group(2), 'replace', rel, text.count('\n', 0, m.start()) + 1))
        for m in re.finditer(r'EI_VERIFY\(\s*0x([0-9A-Fa-f]+)\s*,\s*(\w+)', text):
            regs.append((int(m.group(1), 16), m.group(2), 'verify', rel, text.count('\n', 0, m.start()) + 1))
        for m in re.finditer(r'([\w:~]+)\s*\((?:(?!EI_ORIGINAL)[^;{}])*\)\s*(?:const\s*)?EI_ORIGINAL\(\s*0x([0-9A-Fa-f]+)', text):
            regs.append((int(m.group(2), 16), m.group(1), 'forwarder', rel, text.count('\n', 0, m.start()) + 1))
    return regs


_suffixes = {}


def _by_suffix(by_name):
    """For one declarations() result: each "B::C" and "C" of a qualified "A::B::C" -> the full names
    ending in "::" + it (what a search of every name with endswith found, once per registration)."""
    index = _suffixes.get(id(by_name))
    if index is None:
        index = {}
        for n in by_name:
            parts = n.split('::')
            for i in range(1, len(parts)):
                index.setdefault('::'.join(parts[i:]), []).append(n)
        _suffixes[id(by_name)] = index
    return index


def candidates_for(by_name, name, kind, rel, key):
    """The declarations a registration of `name` in file `rel` refers to; `key` maps a signature to
    the property being checked, and same-file declarations break ties when candidates disagree."""
    candidates = by_name.get(name) or [c for n in _by_suffix(by_name).get(name, ()) for c in by_name[n]]
    obj = os.path.splitext(os.path.basename(rel))[0].lower() + '.cpp.obj'
    same_file = [c for c in candidates if c[1].endswith(obj)]
    if kind == 'verify':
        return same_file                       # file-local wrappers share names across files
    if len({key(u) for u, _ in candidates} - {None}) > 1:
        return same_file or candidates
    return candidates


def main():
    data = open(EXE, 'rb').read()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    sizes = load_sizes()
    by_name = declarations()
    regs = registrations()

    problems = checked = unresolved = 0
    for address, name, kind, rel, line in regs:
        size = sizes.get(address)
        if not size:
            continue
        pops = original_pops(data, md, address, size)
        if not pops:
            continue
        candidates = candidates_for(by_name, name, kind, rel, stack_bytes)
        sigs = {stack_bytes(u) for u, _ in candidates} - {None}
        if len(sigs) != 1:
            unresolved += 1
            continue
        conv, nbytes = sigs.pop()
        checked += 1
        if conv == 'cdecl':
            if pops != {0}:
                problems += 1
                print(f'{rel}:{line} {name} ({kind} {address:08X}): declared __cdecl but the original pops {sorted(pops)}')
            continue
        if pops != {nbytes}:
            problems += 1
            print(f'{rel}:{line} {name} ({kind} {address:08X}): declared __{conv} with {nbytes} bytes of stack '
                  f'arguments, the original pops {sorted(pops)}')
    print(f'{checked} declarations checked against RET n, {unresolved} not resolved (overloads, templates, '
          f'lambdas), {problems} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
