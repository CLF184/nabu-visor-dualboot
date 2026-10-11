"""Compile the pinned, unchanged Renegade BootSlotDxe/BootSlotLib independently."""
import hashlib
import json
from pathlib import Path
import subprocess

SOURCE_UNITS = [
    'GPLDrivers/Drivers/BootSlotDxe/BootSlotDxe.c',
    'GPLDrivers/Library/BootSlotLib/BlockIoUtils.c',
    'GPLDrivers/Library/BootSlotLib/EFIUtils.c',
    'GPLDrivers/Library/BootSlotLib/PartitionTableUpdate.c',
    'GPLDrivers/Library/BootSlotLib/StorageUtils.c',
]

def verify_sources(root, port):
    record = json.loads((port/'upstream.json').read_text(encoding='utf-8'))
    files = {}
    for name, expected in record['sources'].items():
        data = (root/'third_party/renegade'/name).read_bytes().replace(b'\r\n', b'\n')
        blob = hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        if blob != expected:
            raise ValueError('Pinned BootSlotDxe source changed: '+name)
        files[name] = {'git_blob': blob, 'sha256': hashlib.sha256(data).hexdigest()}
    return record, files

def build(root, work, run, zig, port=None):
    port = port or root/'port/boot-slot'
    record, sources = verify_sources(root, port)
    work.mkdir(parents=True, exist_ok=True)
    target = work/'BootSlotDxe.efi'
    flags = ['-mstrict-align', '-mno-ms-bitfields', '-std=c11', '-fshort-wchar',
        '-ffreestanding', '-fno-stack-protector', '-fno-strict-aliasing',
        '-mno-stack-arg-probe', '-Os', '-Wno-incompatible-pointer-types', '-Wno-pointer-sign',
        '-I'+str(port/'compat'), '-I'+str(root/'third_party/renegade/GPLDrivers/Include'),
        '-I'+str(root/'third_party/gnu-efi/inc'), '-I'+str(root/'third_party/gnu-efi/inc/aarch64')]
    run(zig, 'build-exe', port/'entry.zig', '-target', 'aarch64-uefi-msvc',
        '-O', 'ReleaseSmall', '-fentry=EfiMain', '--subsystem', 'efi_boot_service_driver',
        '-femit-bin='+str(target), '-cflags', *flags, '--', port/'bridge.c',
        *[root/'third_party/renegade'/name for name in SOURCE_UNITS])
    return target, {
        'repo': record['repo'], 'commit': record['commit'], 'file_guid': record['file_guid'],
        'sources': sources,
        'bridge': {p.relative_to(port).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in sorted(port.rglob('*')) if p.is_file()},
        'driver_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
        'behavior': 'Unchanged upstream BootSlotMain: clear both unbootable flags, refresh current slot and mark successful',
        'secure_boot_policy_changed': False,
    }

if __name__ == '__main__':
    import argparse
    import os
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project', type=Path, default=Path(__file__).resolve().parent)
    ap.add_argument('--out-dir', type=Path)
    args = ap.parse_args()
    root = args.project.resolve()
    work = (args.out_dir or root/'build/boot-slot').resolve()
    work.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['ZIG_GLOBAL_CACHE_DIR'] = str(work/'zig-cache/global')
    env['ZIG_LOCAL_CACHE_DIR'] = str(work/'zig-cache/local')
    def run(*cmd):
        subprocess.run(list(map(str, cmd)), env=env, check=True)
    file, record = build(root, work, run, root/'tools/python/ziglang/zig.exe')
    print(file)
    (work/'boot-slot-manifest.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
