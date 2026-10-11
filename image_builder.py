#!/usr/bin/env python3
"""Build the current Nabu Visor image; never writes a device.

Uses the unchanged Renegade Reboot2PayloadApp.c and its BootShim marker loops.
Keeps Nabu's original FD copy/entry convention and original Android payload.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

sys.dont_write_bytecode = True
COMMIT = 'e1952621f419f8db60ed28271264e1b5184c571d'
SOURCE_HASHES = {
    'Reboot2PayloadApp.c': '363f69fdcddd7534646126bc401aefded0efc9db',
    'Reboot2PayloadApp.inf': '03920f5dee0eacc63bf094bb835473706bcbd5cc',
    'BootShim.Dualboot.S': 'd3d9cb065d7f88cea91ec86b61778434d254a0cd',
    'LICENSE': '300ea7017ec605ffd84a7f0fb8daf26cc9240361',
}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def gitblob(data):
    return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()

def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

def branch(target, pc):
    delta = target - pc
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('AArch64 branch out of range')
    return struct.pack('<I', 0x14000000 | ((delta // 4) & 0x3ffffff))

def inventory(fp, fd):
    modules = {}
    def sections(data):
        for _, kind, hs, payload in fp.iter_sections(data):
            if kind == 0x17:
                volume(payload)
            elif kind == 2 and payload[:16] == fp.LZMA_GUID:
                offset = struct.unpack_from('<H', payload, 16)[0] - hs
                sections(fp.lzma_unpack(payload[offset:]))
    def volume(data):
        for _, f in fp.parse_fv(data)[3]:
            if f[18] in (0xf0, 1, 2):
                continue
            modules[f[:16]] = f
            sections(f[24:])
    volume(fd)
    return modules

def helper_text(obj, origin):
    """Resolve only the two external B instructions in the upstream loops."""
    data = Path(obj).read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('Expected little-endian AArch64 ELF64 object')
    off = struct.unpack_from('<Q', data, 40)[0]
    size, count, names_idx = struct.unpack_from('<HHH', data, 58)
    headers = [struct.unpack_from('<IIQQQQIIQQ', data, off+i*size) for i in range(count)]
    names = data[headers[names_idx][4]:headers[names_idx][4]+headers[names_idx][5]]
    name = lambda h: names[h[0]:].split(b'\0', 1)[0].decode()
    sections = {name(h): h for h in headers}
    text = sections['.text']
    blob = bytearray(data[text[4]:text[4]+text[5]])
    rela = sections['.rela.text']
    symbols = headers[rela[6]]
    strings_h = headers[symbols[6]]
    strings = data[strings_h[4]:strings_h[4]+strings_h[5]]
    targets = {'_CopyUEFI': 0x70, '_KernelStart': 4}
    resolved = []
    for pos in range(rela[4], rela[4]+rela[5], rela[9]):
        place, info, addend = struct.unpack_from('<QQq', data, pos)
        if info & 0xffffffff != 282:  # R_AARCH64_JUMP26
            raise ValueError('Unexpected BootShim relocation')
        sym = struct.unpack_from('<IBBHQQ', data, symbols[4]+(info >> 32)*symbols[9])
        symbol = strings[sym[0]:].split(b'\0', 1)[0].decode()
        if symbol not in targets or addend:
            raise ValueError('Unexpected BootShim external symbol')
        blob[place:place+4] = branch(targets[symbol], origin+place)
        resolved.append(symbol)
    if sorted(resolved) != sorted(targets):
        raise ValueError('Expected exactly Android and UEFI continuation branches')
    return bytes(blob)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project', type=Path, default=Path(__file__).resolve().parent)
    ap.add_argument('--port', type=Path, default=Path(__file__).resolve().parent/'port/renegade')
    ap.add_argument('--boot', type=Path)
    ap.add_argument('--fd', type=Path)
    ap.add_argument('--recovery-port', type=Path, help='Optional standard BCB Recovery button adapter')
    ap.add_argument('--output-prefix', default='nabu-visor')
    ap.add_argument('--out-dir', required=True, type=Path)
    args = ap.parse_args()
    root, port, out = args.project.resolve(), args.port.resolve(), args.out_dir.resolve()
    stock = (args.boot or root/'tools/official/stock-boot.img').resolve()
    base_fd = (args.fd or root/'tools/official/nabu-uefi.fd').resolve()
    baseline = root/'dist/nabu-visor-boot.img'
    old_release_sha = sha(baseline.read_bytes()) if baseline.exists() else None
    source_paths = {
        'Reboot2PayloadApp.c': root/'third_party/renegade/Platform/RenegadePkg/Application/Reboot2PayloadApp/Reboot2PayloadApp.c',
        'Reboot2PayloadApp.inf': root/'third_party/renegade/Platform/RenegadePkg/Application/Reboot2PayloadApp/Reboot2PayloadApp.inf',
        'BootShim.Dualboot.S': root/'third_party/renegade/tools/BootShim/BootShim.Dualboot.S',
        'LICENSE': root/'third_party/renegade/LICENSE',
    }
    required = [stock, base_fd, root/'tools/python/ziglang/zig.exe',
                root/'tools/official/magiskboot.exe', root/'tools/official/DualBootKernelPatcher-x86_64.exe',
                *source_paths.values(), root/'third_party/visor/features/features.mk',
                root/'third_party/gnu-efi/inc/efi.h']
    missing = [str(p) for p in required if not p.is_file()]
    if missing:
        raise SystemExit('Missing build inputs/tools/submodules. See README setup instructions:\n'+'\n'.join(missing))
    sources = {}
    for file in SOURCE_HASHES:
        worktree_data = source_paths[file].read_bytes()
        # Git on Windows may check text out as CRLF; compare canonical upstream LF bytes.
        data = worktree_data.replace(b'\r\n', b'\n')
        actual = gitblob(data)
        expected = SOURCE_HASHES[file]
        if actual != expected:
            raise SystemExit('Vendored upstream source changed: '+file)
        sources[file] = {'git_blob': actual, 'sha256': sha(data), 'worktree_sha256': sha(worktree_data)}
    out.mkdir(parents=True, exist_ok=True)
    global _build_workspace
    _build_workspace = tempfile.TemporaryDirectory(prefix='nabu-visor-build-', ignore_cleanup_errors=True)
    work = Path(_build_workspace.name)
    env = os.environ.copy()
    env['ZIG_GLOBAL_CACHE_DIR'] = str(work/'zig-cache/global')
    env['ZIG_LOCAL_CACHE_DIR'] = str(work/'zig-cache/local')
    zig = root/'tools/python/ziglang/zig.exe'
    if subprocess.check_output([str(zig), 'version'], text=True).strip() != '0.14.1':
        raise SystemExit('Requires Zig 0.14.1')
    def run(*cmd, cwd=None):
        print('>', str(cmd[0]), ' '.join(map(str, cmd[1:4])), flush=True)
        subprocess.run(list(map(str, cmd)), cwd=cwd or root, env=env, check=True)
    vb = load(root/'visor_build.py', 'visor_build_candidate')
    fp = load(root/'fd_patch.py', 'fd_patch_candidate')
    bp = load(root/'patch_boot.py', 'boot_patch_candidate')
    vb.prepare()
    # Keep the upstream submodule pristine; apply the nabu patch only to a temporary copy.
    visor = work/'visor'
    shutil.copytree(root/'third_party/visor', visor, ignore=shutil.ignore_patterns('.git'))
    patch = root/'port/visor/NABU.patch'
    # Normalize only patch targets in the temporary copy, never the submodule.
    for rel in re.findall(r'^--- a/(.+)$', patch.read_text(encoding='utf-8'), re.M):
        file = visor/rel
        if not file.resolve().is_relative_to(visor.resolve()):
            raise ValueError('Patch path outside the temporary Visor tree')
        if file.is_file():
            file.write_bytes(file.read_bytes().replace(b'\r\n', b'\n'))
    run('git', 'init', '--quiet', cwd=visor)
    run('git', '-c', 'core.autocrlf=false', 'apply', '--check', '--whitespace=nowarn', patch, cwd=visor)
    run('git', '-c', 'core.autocrlf=false', 'apply', '--whitespace=nowarn', patch, cwd=visor)
    esp = root/'esp/EFI/visor'
    esp.mkdir(parents=True, exist_ok=True)
    if not (esp/'boot.conf').exists():
        shutil.copy2(root/'config/boot.conf', esp/'boot.conf')
    for file in (root/'third_party/visor/assets').rglob('*'):
        if file.is_file():
            destination = esp/file.relative_to(root/'third_party/visor/assets')
            if not destination.exists():
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(file, destination)
    if not (esp/'boot.conf.example').exists():
        shutil.copy2(root/'third_party/visor/boot.conf.example', esp/'boot.conf.example')
    # Leave the normal build and source tree untouched; shadow only the backend.
    shadow = work/'src'
    shutil.copytree(root/'src', shadow)
    shutil.copy2(port/'nabu_reset.zig', shadow/'nabu_reset.zig')
    android = shadow/'android.zig'
    android.write_text(android.read_text(encoding='utf-8').replace('_ = handle;', 'uefi.handle = handle;'), encoding='utf-8')
    overlays, recovery_sources = {}, []
    if args.recovery_port:
        recovery_port = args.recovery_port.resolve()
        adapter = load(recovery_port/'overlay.py', 'recovery_overlay')
        overlays, recovery_source = adapter.prepare(root, work, recovery_port, visor)
        recovery_sources = [recovery_source]
    def menu_path(p):
        if p in overlays:
            return overlays[p]
        if p.startswith('third_party/visor/'):
            return visor/p.removeprefix('third_party/visor/')
        return root/p
    menu_sources = [menu_path(p) for p in vb.sources()]
    flags = []
    previous = None
    for f in vb.flags():
        if f.startswith('-Ithird_party/visor/'):
            f = '-I'+str(visor/f.removeprefix('-Ithird_party/visor/'))
        elif f.startswith('-I'):
            f = '-I'+str(root/f[2:])
        elif previous == '-include':
            f = str(root/f)
        flags.append(f)
        previous = f
    common = ['-mstrict-align', '-mno-ms-bitfields', '-std=c11', '-fshort-wchar', '-ffreestanding',
              '-fno-stack-protector', '-fno-strict-aliasing', '-mno-stack-arg-probe',
              '-I'+str(port/'compat'), '-I'+str(root/'third_party/gnu-efi/inc'),
              '-I'+str(root/'third_party/gnu-efi/inc/aarch64'), '-Os']
    reset_sources = [source_paths['Reboot2PayloadApp.c'], port/'bridge.c']
    print('[1/4] Compile unchanged Renegade app + existing Visor menu', flush=True)
    driver = work/'NabuBootMenu.efi'
    run(zig, 'build-exe', shadow/'menu.zig', '-target', 'aarch64-uefi-msvc', '-O', 'ReleaseSmall',
        '-fentry=EfiMain', '--subsystem', 'efi_boot_service_driver', '-femit-bin='+str(driver),
        '-cflags', *flags, '-I'+str(port/'compat'), '--',
        *menu_sources, *reset_sources, *recovery_sources)
    app = work/'NabuAndroid.efi'
    run(zig, 'build-exe', android, '-target', 'aarch64-uefi-msvc', '-O', 'ReleaseSmall',
        '-fentry=EfiMain', '--subsystem', 'efi_application', '-femit-bin='+str(app),
        '-cflags', *common, '--', *reset_sources)
    shutil.copy2(app, root/'esp/EFI/visor/NabuAndroid.efi')
    print('[2/4] Keep all original firmware modules; add rebuilt menu only', flush=True)
    raw_fd = base_fd.read_bytes()
    fd, fd_info = fp.patch_fd(raw_fd, driver.read_bytes())
    before, after = inventory(fp, raw_fd), inventory(fp, fd)
    if set(after)-set(before) != {fp.DRIVER_GUID}:
        raise ValueError('Unexpected additional firmware module')
    # Containers may be rebuilt; all other inventoried firmware files remain identical.
    for guid, f in before.items():
        if f[18] != 0x0b and after.get(guid) != f:
            raise ValueError('Original executable firmware module changed')
    fd_path = out/(args.output_prefix+'.fd')
    fd_path.write_bytes(fd)
    print('[3/4] Pair upstream marker loops with Nabu kernel/FD continuation', flush=True)
    boot_dir = work/'boot'
    boot_dir.mkdir()
    shutil.copy2(stock, boot_dir/'stock-boot.img')
    official = root/'tools/official'
    magisk = official/'magiskboot.exe'
    run(magisk, 'unpack', '-h', 'stock-boot.img', cwd=boot_dir)
    original = (boot_dir/'kernel').read_bytes()
    android_entry = bp.branch_target(struct.unpack_from('<I', original)[0], 0)
    run(official/'DualBootKernelPatcher-x86_64.exe', 'kernel', fd_path, 'patchedKernel',
        official/'DualBoot.Sm8150.cfg', official/'ShellCode.Nabu2.bin', cwd=boot_dir)
    patched = (boot_dir/'patchedKernel').read_bytes()
    bp.verify_patched(patched, original, fd, android_entry)
    origin = len(patched)
    if origin % 16:
        raise ValueError('Expected aligned original kernel + FD')
    upstream = source_paths['BootShim.Dualboot.S'].read_text(encoding='utf-8')
    test_loop = upstream.split('_Start:', 1)[1].split('_CopyUEFI:', 1)[0]
    clear_loop = upstream.split('_ClearFlags:', 1)[1].split('_Dead:', 1)[0]
    # The original header and Nabu FD handoff occupy 0x00..0xaf. Put the
    # unchanged marker loops AFTER the FD, retaining all original kernel bytes.
    assembly = work/'RenegadeMarkerLoops.S'
    native_entry = (recovery_port/'native_entry.S') if args.recovery_port else None
    native_guard = native_entry.read_text(encoding='utf-8') if native_entry else 'b _CopyUEFI\n'
    assembly.write_text('.text\n_Start:'+test_loop+'\n'+native_guard+'\n_ClearFlags:'+clear_loop, encoding='utf-8')
    obj = work/'RenegadeMarkerLoops.o'
    run(zig, 'cc', '-target', 'aarch64-freestanding-none', '-c', assembly, '-o', obj)
    helper = helper_text(obj, origin)
    (out/'RenegadeMarkerLoops.bin').write_bytes(helper)
    result_kernel = bytearray(patched+helper)
    result_kernel[0x40:0x44] = branch(origin, 0x40)
    old_image_size = struct.unpack_from('<Q', patched, 16)[0]
    if old_image_size < len(result_kernel):
        struct.pack_into('<Q', result_kernel, 16, fp.align(len(result_kernel), 4096))
    if result_kernel[0xb0:len(original)] != original[0xb0:]:
        raise ValueError('Android kernel payload was modified')
    if result_kernel[len(original):origin] != fd or result_kernel[0x70:0xb0] != patched[0x70:0xb0]:
        raise ValueError('Nabu FD copy/handoff convention changed')
    (boot_dir/'kernel').write_bytes(result_kernel)
    run(magisk, 'repack', 'stock-boot.img', 'new-boot.img', cwd=boot_dir)
    boot = (boot_dir/'new-boot.img').read_bytes()
    if len(boot) > stock.stat().st_size:
        raise ValueError('Boot image exceeds input image capacity')
    boot += bytes(stock.stat().st_size-len(boot))
    image_path = out/(args.output_prefix+'-boot.img')
    image_path.write_bytes(boot)
    verify = work/'verify'
    verify.mkdir()
    shutil.copy2(image_path, verify/'out.img')
    run(magisk, 'unpack', '-h', 'out.img', cwd=verify)
    if (verify/'kernel').read_bytes() != result_kernel:
        raise ValueError('Repacked kernel differs')
    if (verify/'ramdisk.cpio').read_bytes() != (boot_dir/'ramdisk.cpio').read_bytes():
        raise ValueError('Android ramdisk changed')
    print('[4/4] Package existing ESP configuration/assets + paired Android EFI', flush=True)
    shutil.copy2(driver, out/driver.name)
    shutil.copy2(app, out/app.name)
    archive = out/(args.output_prefix+'-esp.zip')
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for file in sorted((root/'esp').rglob('*')):
            if file.is_file():
                src = app if file.relative_to(root/'esp').as_posix() == 'EFI/visor/NabuAndroid.efi' else file
                z.write(src, file.relative_to(root/'esp').as_posix())
    manifest = {
        'device': 'xiaomi-nabu', 'status': 'CANDIDATE_NOT_HARDWARE_TESTED', 'hardware_tested': False,
        'upstream': {'repo': 'https://github.com/edk2-porting/edk2-msm', 'commit': COMMIT, 'sources': sources},
        'input_boot': {'path': str(stock), 'sha256': sha(stock.read_bytes())},
        'input_fd': {'path': str(base_fd), 'sha256': sha(raw_fd)},
        'output_boot': {'file': image_path.name, 'sha256': sha(boot), 'size': len(boot)},
        'output_fd': {'file': fd_path.name, 'sha256': sha(fd)},
        'esp_package': {'file': archive.name, 'sha256': sha(archive.read_bytes())},
        'android_protocol': {'address': '0x17FE00000', 'value_u16': '0x5241', 'count': 128,
                             'cache': 'write-back and invalidate of 256 bytes', 'reset': 'existing FD EfiResetWarm',
                             'reader': 'scan 128 halfwords; any match clears all 256 bytes then branches to Android'},
        'kernel': {'original_size': len(original), 'fd_offset': len(original), 'helper_offset': origin,
                   'helper_size': len(helper), 'android_entry': hex(android_entry), 'sha256': sha(result_kernel)},
        'verification': {'original_executable_firmware_modules_unchanged': True,
                         'original_generic_psci_reset_retained': True, 'original_android_payload_unchanged': True,
                         'ramdisk_unchanged': True, 'original_fd_copy_and_entry_unchanged': True,
                         'repacked_kernel_matches': True},
        'notes': ['Candidate only. No real Android boot has been verified.',
                  'Both marker writer and reader must be installed as a pair.',
                  'Original generic PSCI reset remains; no old Qualcomm reset driver is transplanted.',
                  'Marker retention across this tablet reboot remains a hardware question.',
                  'Visor UI, 5-second idle behavior, config/assets and Windows BDS are unchanged.',
                  'Firmware/Fastboot button is outside this Android-only candidate and remains unresolved.'],
    }
    if args.recovery_port:
        manifest['recovery_protocol'] = {
            'partition': 'GPT partition named misc', 'offset': 0,
            'command': 'boot-recovery', 'recovery': 'recovery\n',
            'fields_changed': [[0, 32], [64, 832]],
            'other_bytes': 'preserved, including 4 KiB block tail',
            'before_reset': 'write, flush, read back and compare',
            'menu_bypass': 'call the unchanged nabu_renegade_android bridge: 128 x 0x5241 at 0x17FE00000, cache clean, warm reset',
            'reset': 'existing FD EfiResetWarm',
            'failure': 'restore original request and show error; no reboot on write failure',
            'target': 'Recovery already installed on tablet; no Recovery image replacement',
        }
        manifest['recovery_sources'] = {
            p: sha(path.read_bytes()) for p, path in overlays.items()
        }
        manifest['recovery_sources']['port/recovery/recovery.c'] = sha(recovery_sources[0].read_bytes())
        manifest['recovery_sources']['port/recovery/native_entry.S'] = sha(native_entry.read_bytes())
        manifest['native_recovery_protocol'] = {
            'input': 'original bootloader FDT pointer in x0, before the Nabu FD handoff',
            'recognition': 'valid bounded v17 FDT; root model NABU; chosen/bootargs has androidboot.hardware=qcom and omits androidboot.force_normal_boot=1',
            'action': 'use the unchanged Renegade clear loop then branch to the original kernel; no BCB write or reset',
            'preserved': 'x0-x3, original FDT and ramdisk; no stack required',
            'fallback': 'invalid or unrecognised FDT continues the existing FD/Visor flow; no boot-time BCB detection',
            'scope': 'nabu recovery-as-boot using kernel command-line parameters; not a bootconfig parser',
        }
        manifest['notes'] = [
            'Newly generated image; requires hardware verification even though the released implementation was tested.',
            'Android uses the same paired Renegade markers previously verified by the user.',
            'Windows BDS, Visor UI/config/assets, keys and 5-second idle behavior retained.',
            'The third button writes the Recovery BCB, then reuses the existing Renegade marker/warm-reset bridge to bypass Visor once.',
            'Native Recovery boot context from the original FDT bypasses Visor without an additional reboot, including the power/volume-up path. The previous build was confirmed for keys; this generated image still requires verification.',
            'The provided TWRP file is not embedded or flashed; the installed Recovery is used.',
            'No old Qualcomm reset driver, direct Linux loader or extra SimpleInit menu.',
        ]
    visor_record = root/'port/visor-upstream.json'
    if visor_record.exists():
        manifest['visor_upstream'] = json.loads(visor_record.read_text(encoding='utf-8'))
    (out/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    checksum_files = [image_path, fd_path, archive, driver, app, out/'RenegadeMarkerLoops.bin', out/'manifest.json']
    (out/'SHA256SUMS.txt').write_text(''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in checksum_files), encoding='ascii')
    if old_release_sha is not None and baseline.resolve() != image_path.resolve() and sha(baseline.read_bytes()) != old_release_sha:
        raise ValueError('Existing release image changed')
    print(json.dumps({k: manifest[k] for k in ('status','output_boot','output_fd','kernel')}, indent=2), flush=True)

_build_workspace = None
if __name__ == '__main__':
    try:
        main()
    finally:
        if _build_workspace is not None:
            _build_workspace.cleanup()
