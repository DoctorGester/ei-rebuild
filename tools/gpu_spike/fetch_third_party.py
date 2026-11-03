"""The SDL_GPU spike's third-party pieces (tools/gpu_spike/README.md), fetched from their official GitHub releases
or archives into build/third_party and checked against their SHA-256: SDL_shadercross (no tagged release yet: a pinned
commit), SPIRV-Cross (a Vulkan SDK tag) and DirectXShaderCompiler's release binaries (Windows and Linux).

python tools/gpu_spike/fetch_third_party.py
"""
import hashlib
import os
import sys
import tarfile
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'build', 'third_party')

# (file, url, sha256, extracted into)
PIECES = [
    ('shadercross.zip',
     'https://github.com/libsdl-org/SDL_shadercross/archive/1ff05bec573988a98ef9e0260b4da44f512b8367.zip',
     'f6baca7f53dc4e5cf5981d543ee17d93675242255d84ba34749e349e70945287', 'shadercross_src'),
    ('spirv-cross.tar.gz',
     'https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz',
     'f708982e88b763ef5b0394ed468fc2c3628f68cdab89f2e7a946cd628c04e721', '.'),
    ('dxc_2026_09_29.zip',
     'https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip',
     'ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1', 'dxc'),
    ('linux_dxc.tar.gz',
     'https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/'
     'linux_dxc_2026_09_28.x86_x64.tar.gz',
     '96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1', 'dxc-linux'),
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, url, digest, into in PIECES:
        path = os.path.join(OUT, name)
        if not os.path.exists(path) or sha256(path) != digest:
            print('fetching', url)
            urllib.request.urlretrieve(url, path)
        if sha256(path) != digest:
            sys.exit(f'{name}: SHA-256 differs from the pinned one')
        target = os.path.join(OUT, into)
        os.makedirs(target, exist_ok=True)
        if name.endswith('.zip'):
            with zipfile.ZipFile(path) as z:
                for member in z.infolist():   # the DXC archive's names use backslashes
                    member.filename = member.filename.replace('\\', '/')
                    z.extract(member, target)
        else:
            with tarfile.open(path) as t:
                t.extractall(target, filter='data')
        print('ok', name)


if __name__ == '__main__':
    main()
