"""Reports rewrite progress: functions replaced in src/ and how many of them are verified.

Counts EI_REPLACE / EI_REPLACE_METHOD registrations against the game functions in
config/functions.csv (sizes from src/hook/function_sizes.inc).
"""
import csv, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main():
    sizes = {}
    for line in open(os.path.join(ROOT, 'src', 'hook', 'function_sizes.inc')):
        m = re.match(r'\{0x([0-9A-F]+), (\d+)\}', line)
        if m:
            sizes[int(m.group(1), 16)] = int(m.group(2))
    game = {int(r['address'], 16) for r in csv.DictReader(open(os.path.join(ROOT, 'config', 'functions.csv')))
            if r['kind'] == 'game'}
    # Library code identified later (STLport/MFC/CRT instantiations named in config/library.csv) is not
    # game code to port: the standalone build links its own runtime.
    library = {int(r['address'], 16) for r in csv.DictReader(open(os.path.join(ROOT, 'config', 'library.csv')))}
    game -= library
    game_bytes = sum(sizes.get(a, 0) for a in game)

    replaced = set()
    for dirpath, _, files in os.walk(os.path.join(ROOT, 'src')):
        for f in files:
            if f.endswith('.cpp'):
                text = open(os.path.join(dirpath, f), encoding='utf-8').read()
                replaced |= {int(a, 16) for a in re.findall(r'EI_REPLACE(?:_METHOD)?\(0x([0-9A-Fa-f]+)', text)}
    verified = set()
    for line in open(os.path.join(ROOT, 'tests', 'verify', 'verified.txt')):
        m = re.match(r'([0-9A-Fa-f]{8})\b', line)
        if m:
            verified.add(int(m.group(1), 16))

    rb = sum(sizes.get(a, 0) for a in replaced)
    print(f'game functions: {len(game)} ({game_bytes:,} bytes)')
    print(f'rewritten:      {len(replaced)} ({rb:,} bytes, {100 * rb / game_bytes:.2f}% of game code)')
    print(f'verified:       {len(verified & replaced)} of them listed in tests/verify/verified.txt')

if __name__ == '__main__':
    sys.exit(main())
