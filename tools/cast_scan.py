"""Finds the redundant pointer static_casts with the compiler (C21).

A static_cast to a pointer type is redundant when its operand already has that type (SAME), when it
only adds const (CONST), or when it is an implicit upcast (UP). The script copies the tree to
build/cast_scan/tree, rewrites every `static_cast<T*>(` there to `C21_SC<T*, __LINE__>(`, force-includes
a header (/FI through the CL variable) whose overloads for the three kinds are deleted, builds the release
configuration with `ninja -k 0`, and lists each call site the compiler refused (C2280), with the kind and
the operand's type. The worktree's own sources are not touched.

    python tools/cast_scan.py [--jobs 4] [--exe PATH]   # build/cast_scan/sites.txt: kind file:line to <- from

Before dropping a cast (rewrite guide, "Clean code"):
- SAME: drop it (`(&x)->f()` becomes `x.f()`).
- CONST: keep the ones that add volatile (the x87 reads), and the ones that choose a const overload.
- UP: drop it only when no class between the two declares a member with the called name (the cast picks
  the base's hidden member, as `static_cast<CUiContainer*>(w)->Construct(ui)` does; write
  `w->CUiContainer::Construct(ui)` there), and give an `auto*` local the base type it had.
A cast inside a template body is reported for one instantiation only; leave those.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CAST = re.compile(r'\bstatic_cast\s*<([^<>();]*\*\s*(?:const\s*)?)>\s*\(')
SITE = re.compile(r'[\\/]tree[\\/](src[\\/][^()]*?)\((\d+)\): error C2280: .To \*?C21_SC<(.*),(\d+),(.*),c21_(same|const|up)_tag>')

HEADER = r'''#pragma once
#include <type_traits>
struct c21_same_tag {};
struct c21_const_tag {};
struct c21_up_tag {};
template <class To, class From> constexpr int c21_kind() {
    using F = std::remove_cvref_t<From>;
    if constexpr (std::is_pointer_v<F> && std::is_pointer_v<To>) {
        using FP = std::remove_pointer_t<F>;
        using TP = std::remove_pointer_t<To>;
        if constexpr (std::is_same_v<FP, TP>) return 1;
        else if constexpr (std::is_same_v<std::remove_cv_t<FP>, std::remove_cv_t<TP>>) return 2;
        else if constexpr (!std::is_void_v<std::remove_cv_t<TP>> && std::is_convertible_v<F, To>) return 3;
    }
    return 0;
}
template <class To, int L, class From, class K = c21_same_tag> requires(c21_kind<To, From>() == 1) To C21_SC(From&& f) = delete;
template <class To, int L, class From, class K = c21_const_tag> requires(c21_kind<To, From>() == 2) To C21_SC(From&& f) = delete;
template <class To, int L, class From, class K = c21_up_tag> requires(c21_kind<To, From>() == 3) To C21_SC(From&& f) = delete;
template <class To, int L, class From> requires(c21_kind<To, From>() == 0)
constexpr To C21_SC(From&& f) { return static_cast<To>(static_cast<From&&>(f)); }
'''


def copy_tree(out):
    tree = os.path.join(out, 'tree')
    os.makedirs(tree, exist_ok=True)
    for part in ('CMakeLists.txt', 'src', 'include', 'third_party', 'tools', 'config', 'tests'):
        src = os.path.join(ROOT, part)
        dst = os.path.join(tree, part)
        if os.path.isdir(src):
            if os.path.exists(dst):
                shutil.rmtree(dst)
            shutil.copytree(src, dst, ignore=shutil.ignore_patterns('data', '*.bin'))
        else:
            shutil.copy2(src, dst)
    count = 0
    for base, _, files in os.walk(os.path.join(tree, 'src')):
        for name in files:
            if not name.endswith(('.cpp', '.h', '.hpp', '.inl')):
                continue
            path = os.path.join(base, name)
            text = open(path, encoding='utf-8', errors='surrogateescape').read()
            new, n = CAST.subn(lambda m: 'C21_SC<' + m.group(1) + ', __LINE__>(', text)
            if n:
                count += n
                open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(new)
    header = os.path.join(out, 'cast_scan.h')
    open(header, 'w', encoding='utf-8').write(HEADER)
    return tree, header, count


def build(out, tree, header, jobs, exe):
    vs = os.environ.get('EI_VSROOT', r'E:\bin\vs\buildtools')
    tools = vs + r'\Common7\IDE\CommonExtensions\Microsoft\CMake'
    log = os.path.join(out, 'build.log')
    script = os.path.join(out, 'scan.cmd')
    exe_arg = ' -DEI_GAME_EXE="%s"' % exe if exe else ''
    open(script, 'w').write('\r\n'.join([
        '@echo off',
        'call "%s\\VC\\Auxiliary\\Build\\vcvars32.bat" >nul || exit /b 1' % vs,
        'set PATH=%s\\CMake\\bin;%s\\Ninja;%%PATH%%' % (tools, tools),
        'set CL=/FI"%s"' % header,
        'cmake -S "%s" -B "%s\\b" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEI_HARNESS=OFF%s >nul || exit /b 1' % (tree, out, exe_arg),
        'cmake --build "%s\\b" -j %d -- -k 0 > "%s" 2>&1' % (out, jobs, log),
        'exit /b 0', '']))
    subprocess.run(['cmd', '/c', script], check=True)
    return log


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--exe', help="game.exe for the build's generated data (default: tools/exe_path.py's)")
    parser.add_argument('--out', default=os.path.join(ROOT, 'build', 'cast_scan'))
    args = parser.parse_args()
    if not args.exe:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import exe_path
        args.exe = exe_path.find_exe()
    tree, header, count = copy_tree(args.out)
    print('%d pointer static_casts rewritten in %s' % (count, tree))
    log = build(args.out, tree, header, args.jobs, args.exe)
    sites = set()
    other = []
    for line in open(log, encoding='utf-8', errors='replace'):
        m = SITE.search(line)
        if m:
            sites.add((m.group(6).upper(), m.group(1).replace('\\', '/'), int(m.group(2)), m.group(3), m.group(5)))
        elif ': error ' in line:
            other.append(line.rstrip())
    out = os.path.join(args.out, 'sites.txt')
    with open(out, 'w', encoding='utf-8') as f:
        for kind, path, line, to, frm in sorted(sites, key=lambda s: (s[1], s[2])):
            f.write('%s %s:%d %s <- %s\n' % (kind, path, line, to, frm))
    kinds = {}
    for s in sites:
        kinds[s[0]] = kinds.get(s[0], 0) + 1
    print('redundant: %s -> %s' % (', '.join('%s %d' % kv for kv in sorted(kinds.items())), out))
    if other:
        print('%d other build errors (the tree does not build; see %s):' % (len(other), log))
        for line in other[:10]:
            print('  ' + line)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
