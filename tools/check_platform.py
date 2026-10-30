"""Ratchet on the platform layer's rule (docs/notes/platform.md): the game's code reaches the operating system only
through src/platform/platform.h (its Windows implementation in src/platform/win32); the renderer's Direct3D backend
(src/render) keeps its own Windows calls.

This lists the files under src/ outside src/platform, src/render and src/hook (the harness's hook framework) that still
include a Windows system header (<windows.h>, <dinput.h>, <wingdi.h>, <mmsystem.h>, ...) or a header of the Windows
implementation (platform/win32/: its own declarations, game.exe's imports) or call game.exe's imports (`gameapi::X`,
comments left out), and fails when one is not in
config/platform_files.txt: a file may leave the list, not join it. Run with --update after a move onto the platform
layer to drop the files that no longer need to be listed (it never adds one).
Compiler/OS conditionals and compiler annotations are also forbidden in src/game and src/lib, apart from the
generated PE import prototypes' Windows calling conventions.

usage: python tools/check_platform.py [--update]
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LISTED = os.path.join(ROOT, 'config', 'platform_files.txt')
OUTSIDE = ('src/platform/', 'src/render/', 'src/hook/')
# <windows.h> and the system's other headers (the SDK's, DirectX's and the C runtime's Windows parts): game code gets
# what it needs from src/platform.
SYSTEM_HEADERS = (r'windows|windowsx|winsock2?|ws2tcpip|wingdi|winuser|winbase|winnt|windef|winerror|mmsystem|mmreg|'
                  r'mmeapi|msacm|dinput|dsound|ddraw|d3d\w*|dxgi\w*|dwmapi|shellapi|shlobj|commctrl|commdlg|imm|'
                  r'objbase|ole2|oleauto|comdef|process|io|direct|tlhelp32|psapi|dbghelp|intrin|ddk\w*')
WINDOWS_H = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"](?:' + SYSTEM_HEADERS + r')\.h[">]', re.M | re.I)
# The Windows implementation's own headers (platform_win32.h, gameapi.h, win_api.h).
WIN32_HEADER = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"platform/win32/', re.M)
IMPORT_CALL = re.compile(r'\bgameapi::\w')
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
# Compiler and OS selection belongs to the platform implementation, not portable game/library code.
PLATFORM_CONDITION = re.compile(
    r'^\s*#\s*(?:if|elif|ifdef|ifndef|define|undef)\b[^\n]*\b(?:_WIN32|_WIN64|_MSC_VER|__APPLE__|__linux__|__unix__|'
    r'__GNUC__|__clang__|__aarch64__|__x86_64__)\b', re.M)
COMPILER_ANNOTATION = re.compile(r'\b(?:EI_FORCE_INLINE|EI_NOINLINE|EI_THREAD_CALL|EI_RETURN_ADDRESS|'
                                 r'__forceinline|__declspec|__attribute__|__stdcall|_ReturnAddress|_AddressOfReturnAddress)\b')


def counted(path):
    """Whether the rule looks at a file (its path relative to the repository, with forward slashes)."""
    return path.startswith('src/') and not path.startswith(OUTSIDE) and path.endswith(('.cpp', '.h', '.inc'))


def calls_windows(text):
    """Whether a file's text includes a Windows system header or one of the Windows implementation, or calls an import
    as gameapi::X."""
    code = COMMENT.sub('', text)
    return bool(WINDOWS_H.search(code) or WIN32_HEADER.search(code) or IMPORT_CALL.search(code))


def windows_files():
    out = []
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.*'), recursive=True):
        rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
        if counted(rel) and calls_windows(open(path, encoding='utf-8', errors='replace').read()):
            out.append(rel)
    return sorted(out)


def portability_problems():
    problems = []
    for folder in ('src/game', 'src/lib'):
        for path in glob.glob(os.path.join(ROOT, folder, '**', '*.*'), recursive=True):
            rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
            if not rel.endswith(('.cpp', '.h', '.inc')): continue
            code = COMMENT.sub('', open(path, encoding='utf-8', errors='replace').read())
            # The old PE import declarations describe Windows ABI types, not portable game code.
            annotations = rel != 'src/lib/import_prototypes.h'
            if PLATFORM_CONDITION.search(code) or (annotations and COMPILER_ANNOTATION.search(code)):
                problems.append(rel)
    return sorted(problems)


def main(argv):
    now = windows_files()
    listed = []
    if os.path.exists(LISTED):
        listed = [l.strip() for l in open(LISTED, encoding='utf-8') if l.strip() and not l.startswith('#')]
    if '--update' in argv:
        keep = [p for p in listed if p in now]
        with open(LISTED, 'w', encoding='utf-8', newline='\n') as f:
            f.write('# The files that still call Windows outside the platform layer (tools/check_platform.py).\n')
            f.writelines(p + '\n' for p in keep)
        print(f'wrote {os.path.relpath(LISTED, ROOT)}: {len(keep)} files ({len(listed) - len(keep)} dropped)')
        return 0
    problems = [p for p in now if p not in listed]
    for p in problems:
        print(f'{p}: includes a Windows header or calls gameapi::; go through platform/platform.h (docs/notes/platform.md)')
    portability = portability_problems()
    for p in portability:
        print(f'{p}: compiler/OS conditions or annotations belong in src/platform')
    total = len(problems) + len(portability)
    print(f'platform: {len(now)} files outside src/platform and src/render call Windows; {total} problem(s)')
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
