"""The compiler cache of the Windows builds (build.cmd; docs/notes/standalone.md, "The compiler cache"): sccache.

A fresh worktree would compile every object again; with the cache its first build takes the objects another checkout
compiled from the same source and flags.

- The program: sccache's release for x64 Windows, pinned and checked against its sha256, under the main checkout's
  build/third_party (shared by every worktree, like the shader toolchain); downloaded on the first use only.
- The cache: build/compiler_cache/<configuration> in the main checkout: one per configuration (release, verify,
  debug, asan), shared by every worktree.
- The server: one per checkout and configuration, on a port of its own. The checkout's folder is the server's base
  folder (SCCACHE_BASEDIRS): it is taken out of the paths before they are hashed, so another worktree's object is a
  hit. Its debug information names the folder that compiled it first (the cache's caveat for the debugger).
- The launcher (tools/compiler_cache_launcher.cpp, CMake's compiler launcher): runs each compile through sccache with
  the server's variables (or by itself in a build folder's later builds: EI_COMPILER_CACHE_DIRECT, set by build.cmd)
  and passes the compiler's output on with the other checkouts' folders in it changed to this one's.
- No cache (EI_COMPILER_CACHE=0, or the program unavailable): EI_COMPILER_LAUNCHER is empty and cl runs directly.

- The step wrapper (EI_STEP_WRAPPER, also without the cache): the same program in front of the linker and the archiver,
  running them directly. It prints the exit code of a step that failed with no output (H52a).

usage: python tools/compiler_cache.py CONFIGURATION
  (build.cmd, after vcvars64) prints EI_COMPILER_LAUNCHER=<CMake's launcher list> and EI_STEP_WRAPPER=<the step
  wrapper's list>, and starts this checkout's server
"""
import errno, os, re, socket, subprocess, sys, zlib

VERSION = '0.19.0'
ARCHIVE = f'sccache-v{VERSION}-x86_64-pc-windows-msvc'
URL = f'https://github.com/mozilla/sccache/releases/download/v{VERSION}/{ARCHIVE}.zip'
SHA256 = '28be8c61f6b2c89b9eb2869f6af7f78e6f992e78a8abae0ce855fcc129cbeac1'
CACHE_SIZE = '2G'        # per configuration (one verify build's objects take about 0.35 GB in it)
IDLE_TIMEOUT = 600       # seconds before an unused server ends
# The server ports lie below 49152, where Windows' dynamic range begins (Hyper-V, WSL and Docker reserve blocks of
# ports there that no program may bind). Ranges can still be excluded lower (5357, ...): see choose_port.
FIRST_PORT, PORTS = 42000, 7000

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main_checkout():
    """The main checkout's folder: the one that holds the repository (git's common folder), else this checkout."""
    try:
        common = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                capture_output=True, text=True, check=True).stdout.strip()
        if os.path.isdir(common):
            return os.path.dirname(os.path.normpath(common))
    except (OSError, subprocess.CalledProcessError):
        pass
    return ROOT


def excluded_ranges():
    """The (first, last) TCP ports Windows has excluded (netsh int ipv4 show excludedportrange); none elsewhere."""
    if os.name != 'nt':
        return []
    try:
        shown = subprocess.run(['netsh', 'int', 'ipv4', 'show', 'excludedportrange', 'protocol=tcp'],
                               capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return []
    return [(int(a), int(b)) for a, b in re.findall(r'^\s*(\d+)\s+(\d+)', shown, re.MULTILINE)]


def bind_denied(port):
    """True when binding port on the loopback is refused for lack of permission (WSAEACCES, 10013: a range that
    netsh does not show). A port in use is not denied: it may be this checkout's own server."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(('127.0.0.1', port))
    except OSError as e:
        return e.errno in (errno.EACCES, 10013)
    return False


def choose_port(key, excluded, denied=bind_denied):
    """The server port for key (the checkout and configuration): the same one every time, from a crc32 of key, stepping
    up (wrapping inside FIRST_PORT..FIRST_PORT+PORTS) past ports in excluded ranges or that denied() refuses."""
    start = zlib.crc32(key.encode()) % PORTS
    for step in range(PORTS):
        port = FIRST_PORT + (start + step) % PORTS
        if not any(a <= port <= b for a, b in excluded) and not denied(port):
            return port
    return FIRST_PORT + start   # every port refused: the server fails to start and cl runs directly


def server(main, config):
    """The variables of this checkout's server for config."""
    return {
        'SCCACHE_DIR': os.path.join(main, 'build', 'compiler_cache', config),
        'SCCACHE_BASEDIRS': ROOT,
        'SCCACHE_SERVER_PORT': str(choose_port(f'{os.path.normcase(ROOT)}|{config}', excluded_ranges())),
        'SCCACHE_CACHE_SIZE': CACHE_SIZE,
        'SCCACHE_IDLE_TIMEOUT': str(IDLE_TIMEOUT),
        'SCCACHE_IGNORE_SERVER_IO_ERROR': '1',   # a server that cannot start: cl runs directly
    }


def launcher(main):
    """tools/compiler_cache_launcher.exe, built once per version of its source into the main checkout's
    build/third_party with the cl of build.cmd's environment; None when that fails."""
    source = os.path.join(ROOT, 'tools', 'compiler_cache_launcher.cpp')
    with open(source, 'rb') as f:
        version = zlib.crc32(f.read())
    folder = os.path.join(main, 'build', 'third_party', f'compiler-cache-launcher-{version:08x}')
    exe = os.path.join(folder, 'compiler_cache_launcher.exe')
    if os.path.isfile(exe):
        return exe
    os.makedirs(folder, exist_ok=True)
    part = os.path.join(folder, f'launcher-{os.getpid()}')   # another build may be making it at the same time
    try:
        made = subprocess.run(['cl', '/nologo', '/std:c++20', '/O2', '/W4', '/WX', '/EHsc', '/MT', source,
                               f'/Fe{part}.exe', f'/Fo{part}.obj', 'shell32.lib'], cwd=folder, capture_output=True,
                              text=True)
    except OSError as e:
        print(f'compiler_cache: no cl for the launcher ({e})', file=sys.stderr)
        return None
    if made.returncode != 0:
        print('compiler_cache: the launcher did not build:', made.stdout, made.stderr, file=sys.stderr)
        return None
    try:
        os.replace(part + '.exe', exe)
    except OSError:
        pass   # made by the other build meanwhile
    for leftover in (part + '.exe', part + '.obj'):
        if os.path.exists(leftover):
            os.remove(leftover)
    return exe if os.path.isfile(exe) else None


def sccache(main):
    return os.path.join(main, 'build', 'third_party', f'sccache-{VERSION}', ARCHIVE, 'sccache.exe')


def download(main):
    """sccache.exe, downloaded and checked on the first use; None when that fails."""
    import hashlib, urllib.request, zipfile
    exe = sccache(main)
    if os.path.isfile(exe):
        return exe
    folder = os.path.join(main, 'build', 'third_party', f'sccache-{VERSION}')
    try:
        os.makedirs(os.path.join(folder, 'download'), exist_ok=True)
        archive = os.path.join(folder, 'download', ARCHIVE + '.zip')
        urllib.request.urlretrieve(URL, archive)
        with open(archive, 'rb') as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        if digest != SHA256:
            print(f'compiler_cache: {URL} has sha256 {digest}, not {SHA256}', file=sys.stderr)
            return None
        with zipfile.ZipFile(archive) as z:
            z.extractall(folder)
    except OSError as e:
        print(f'compiler_cache: no sccache ({e})', file=sys.stderr)
        return None
    return exe if os.path.isfile(exe) else None


def main():
    config = sys.argv[1].lower() if len(sys.argv) > 1 else 'release'
    main = main_checkout()
    run = launcher(main) if os.name == 'nt' else None
    steps = run and ';'.join([run, 'EI_COMPILER_CACHE_DIRECT=1', '--'])
    if not (run and os.environ.get('EI_COMPILER_CACHE', '1') != '0' and download(main)):
        print('EI_COMPILER_LAUNCHER=')
        print('EI_STEP_WRAPPER=' + (steps or ''))
        return 0
    variables = server(main, config)
    # Started here, its output to nowhere: a server started by the first compile would hold the build's output pipe.
    # None for the build folder's later builds, which compile directly.
    if os.environ.get('EI_COMPILER_CACHE_DIRECT') != '1':
        subprocess.run([sccache(main), '--start-server'], env={**os.environ, **variables},
                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    variables.update(EI_CACHE_PROGRAM=sccache(main), EI_CACHE_MAIN=main, EI_CACHE_HERE=ROOT)
    print('EI_COMPILER_LAUNCHER=' + ';'.join([run] + [f'{k}={v}' for k, v in variables.items()] + ['--']))
    print('EI_STEP_WRAPPER=' + steps)
    return 0


if __name__ == '__main__':
    sys.exit(main())
