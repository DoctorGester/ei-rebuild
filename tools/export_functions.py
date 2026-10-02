"""Export the function list from the running GhidraMCP server to config/functions.csv.

Existing rows keep their status/source columns, so this can be re-run after renaming in Ghidra.
Size is estimated as the gap to the next function start (Ghidra MCP does not report body size).
"""
import csv, os, sys, urllib.request

GHIDRA = os.environ.get('GHIDRA_MCP', 'http://127.0.0.1:8080')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'config', 'functions.csv')
LIB_START = 0x6E6000   # first statically linked CRT/MFC/WinSock function
TEXT_END = 0x73B000

def kind_of(name, addr):
    if name.startswith(('Unwind@', 'Catch@')): return 'eh'
    if name.startswith('thunk_'): return 'thunk'
    return 'lib' if addr >= LIB_START else 'game'

def main():
    text = urllib.request.urlopen(GHIDRA + '/list_functions', timeout=120).read().decode('utf-8', 'replace')
    funcs = []
    for line in text.splitlines():
        if ' at ' not in line: continue
        name, addr = line.rsplit(' at ', 1)
        funcs.append((int(addr, 16), name.strip()))
    funcs.sort()
    old = {}
    if os.path.exists(OUT):
        with open(OUT, newline='') as f:
            for r in csv.DictReader(f): old[int(r['address'], 16)] = r
    with open(OUT, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['address', 'size_est', 'name', 'kind', 'status', 'source'])
        for i, (a, n) in enumerate(funcs):
            nxt = funcs[i + 1][0] if i + 1 < len(funcs) else TEXT_END
            o = old.get(a, {})
            w.writerow([f'{a:08X}', nxt - a, n, kind_of(n, a), o.get('status', 'original'), o.get('source', '')])
    print(f'{len(funcs)} functions -> {OUT}')

if __name__ == '__main__':
    sys.exit(main())
