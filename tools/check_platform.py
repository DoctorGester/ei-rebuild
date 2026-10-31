"""The platform layer's rule (docs/notes/platform.md): the game's code and the renderer reach the operating system and
the graphics device only through src/platform (platform.h, graphics.h; their Windows backends in src/platform/win32 and
src/platform/d3d11).

Fails on a file under src/ outside src/platform that includes a Windows system header (<windows.h>, <dinput.h>,
<d3d11.h>, ...) or a header of a Windows backend (platform/win32/, platform/d3d11/), or calls game.exe's import slots
(`gameapi::X`) or sokol's Direct3D functions (`sg_d3d11_*`; comments left out).

usage: python tools/check_platform.py
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTSIDE = 'src/platform/'
# <windows.h> and the system's other headers (the SDK's, DirectX's and the C runtime's Windows parts): game code gets
# what it needs from src/platform.
SYSTEM_HEADERS = (r'windows|windowsx|winsock2?|ws2tcpip|wingdi|winuser|winbase|winnt|windef|winerror|mmsystem|mmreg|'
                  r'mmeapi|msacm|dinput|dsound|ddraw|d3d\w*|dxgi\w*|dwmapi|shellapi|shlobj|commctrl|commdlg|imm|'
                  r'objbase|ole2|oleauto|comdef|process|io|direct|tlhelp32|psapi|dbghelp|intrin|ddk\w*')
WINDOWS_H = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"](?:' + SYSTEM_HEADERS + r')\.h[">]', re.M | re.I)
# The Windows backends' own headers (platform_win32.h, gameapi.h, graphics_d3d11.h, ...).
WIN32_HEADER = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"platform/(?:win32|d3d11)/', re.M)
IMPORT_CALL = re.compile(r'\bgameapi::\w')
BACKEND_CALL = re.compile(r'\bsg_d3d11_\w+\s*\(')
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)


def counted(path):
    """Whether the rule looks at a file (its path relative to the repository, with forward slashes)."""
    return path.startswith('src/') and not path.startswith(OUTSIDE) and path.endswith(('.cpp', '.h', '.inc'))


def calls_windows(text):
    """Whether a file's text includes a Windows system header or one of the Windows implementation, or calls an import
    as gameapi::X or a sokol Direct3D function."""
    code = COMMENT.sub('', text)
    return bool(WINDOWS_H.search(code) or WIN32_HEADER.search(code) or IMPORT_CALL.search(code) or
                BACKEND_CALL.search(code))


def windows_files():
    out = []
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.*'), recursive=True):
        rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
        if counted(rel) and calls_windows(open(path, encoding='utf-8', errors='replace').read()):
            out.append(rel)
    return sorted(out)


def main(argv):
    found = windows_files()
    for p in found:
        print(f'{p}: includes a Windows header or calls gameapi:: or sg_d3d11_; go through src/platform '
              '(docs/notes/platform.md)')
    print(f'platform: {len(found)} files outside src/platform call Windows')
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
