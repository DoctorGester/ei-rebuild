"""Checks the stolen-byte counts of every EI_VERIFY registration against the original code.

A verify wrapper calls the original through a trampoline holding the function's first N bytes.
N must end on an instruction boundary, be at least 5, and the copied instructions must not be
IP-relative (CALL/JMP/Jcc rel). A wrong N jumps into the middle of an instruction and crashes
only when that function first runs, which can be deep into a harness run.

usage: python tools/check_stolen.py     (needs the capstone module; exits 1 on any problem)
"""
import glob, os, re, sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = r'F:\games\Evil Islands (Rebirth)\starter\Engine\game.exe'
RELATIVE = re.compile(r'^(call|j\w+|loop\w*)$')


def main():
    data = open(EXE, 'rb').read()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    bad = 0
    total = 0
    for path in sorted(glob.glob(os.path.join(ROOT, 'tests', 'verify', '*.cpp'))):
        text = open(path, encoding='utf-8').read()
        for m in re.finditer(r'EI_VERIFY\(\s*0x([0-9A-Fa-f]+)\s*,\s*\w+\s*,\s*(\d+)\s*,', text):
            address, stolen = int(m.group(1), 16), int(m.group(2))
            if stolen == 0:
                continue
            total += 1
            line = text.count('\n', 0, m.start()) + 1
            where = f'{os.path.relpath(path, ROOT)}:{line} {address:08X}'
            offset = address - 0x400000
            end = 0
            problem = None
            for insn in md.disasm(data[offset:offset + stolen + 16], address):
                if end >= stolen:
                    break
                if RELATIVE.match(insn.mnemonic):
                    problem = f'copies a relative {insn.mnemonic} at +{end}'
                end += insn.size
            if problem is None and stolen < 5:
                problem = 'fewer than 5 bytes'
            elif problem is None and end != stolen:
                problem = f'{stolen} bytes is not an instruction boundary (instructions end at {end})'
            if problem:
                bad += 1
                print(f'{where}: {problem}')
    print(f'{total} trampolines checked, {bad} problem(s)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
