"""Windows offline adapters for APK-provided Magisk/KernelSU payloads."""
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile
import zlib

from boot_inputs import cpio_entries, command, unpack

# Official v3.3.1 release digests disambiguate the Android 12/13 5.10 modules.
# Other APK versions use their own vermagic and embedded asset names, never
# payloads copied from this reference release.
KSU_MODULE_HASHES = {
    'android12-5.10': '11373ebe84e2ff1848b0e075f6ad325b96f298f19c45db5c180a48119e476256',
    'android13-5.10': '0714eb724d0ad862e11f560f695aae18f09fd20bffe93129b17d976d253fa29d',
    'android13-5.15': '70da6b2b93329441b81d18581671ff4e0580984630095bc155b295d07e9802fd',
    'android14-5.15': 'aa0c03bc2e3f70697f66a34231f1ce593b6317dfa7b9850a3ce6fcdbf6b70c3b',
    'android14-6.1': '27d45be86a68e03aa9d8e103fe229c66245f5c3ddccd236e52272131ad156f0e',
    'android15-6.6': 'e57ce23de951be6b0dee5030f6724322fed126aa4af71828994437bfb0bb61ad',
    'android16-6.12': '7e25d68419d08535d35e3630d9f0ff1a08e97a7026d36e878991eecb13554d9f',
    'android17-6.18': 'd848bcbeb3f4daffeea4b2ad2d70aac8f79211ce9f15f200dd87c7b8d68ebe30',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def apk_read(archive, name, limit=64 * 1024 * 1024):
    info = archive.getinfo(name)
    if info.file_size > limit:
        raise ValueError('APK 资源过大：' + name)
    return archive.read(info)


def magisk_patch(image, apk, tool, official, directory):
    busybox = official / 'busybox.exe'
    if not busybox.is_file():
        raise ValueError('缺少 Windows BusyBox：tools/official/busybox.exe')
    with zipfile.ZipFile(apk) as archive:
        for name in ('boot_patch.sh', 'util_functions.sh'):
            (directory / name).write_bytes(apk_read(archive, 'assets/' + name))
        for member in archive.namelist():
            if member.startswith('lib/arm64-v8a/lib') and member.endswith('.so'):
                name = Path(member).name[3:-3]
                if name in ('magisk', 'magisk64', 'magiskinit', 'magisk32', 'init-ld'):
                    (directory / name).write_bytes(apk_read(archive, member))
        if 'lib/armeabi-v7a/libmagisk32.so' in archive.namelist():
            (directory / 'magisk32').write_bytes(apk_read(archive, 'lib/armeabi-v7a/libmagisk32.so'))
        if 'assets/stub.apk' in archive.namelist():
            (directory / 'stub.apk').write_bytes(apk_read(archive, 'assets/stub.apk'))
    if not (directory / 'magiskinit').is_file() or not any((directory / name).is_file() for name in ('magisk', 'magisk64')):
        raise ValueError('APK 缺少 ARM64 Magisk 修补资源')
    shutil.copy2(tool, directory / 'magiskboot.exe')
    shutil.copy2(Path(__file__).parent / 'port/magisk-host.sh', directory / 'host.sh')
    subprocess.run([str(busybox), 'sh', 'host.sh'], cwd=directory, check=True)
    return directory / 'new-boot.img', {'engine': 'APK boot_patch.sh via Windows BusyBox',
                                       'script_sha256': sha((directory / 'boot_patch.sh').read_bytes()),
                                       'keepverity': True, 'keepforceencrypt': True}


def embedded_elfs(data):
    """Decode RustEmbed raw-DEFLATE assets; do not execute the Android daemon."""
    seen = set()
    # KernelSU's RustEmbed compression uses include-flate raw DEFLATE streams.
    for offset, byte in enumerate(data[:-512]):
        if byte & 7 not in (2, 3, 4, 5):
            continue
        try:
            decoder = zlib.decompressobj(-15)
            if decoder.decompress(data[offset:offset + 512], 6) != b'\x7fELF\x02\x01':
                continue
            decoder = zlib.decompressobj(-15)
            blob = decoder.decompress(data[offset:], 32 * 1024 * 1024)
            if not decoder.eof:
                continue
        except zlib.error:
            continue
        if len(blob) < 64 or struct.unpack_from('<H', blob, 18)[0] != 183:
            continue
        digest = sha(blob)
        if digest not in seen:
            seen.add(digest)
            yield blob


def ksu_payloads(apk, kmi):
    with zipfile.ZipFile(apk) as archive:
        data = apk_read(archive, 'lib/arm64-v8a/libksud.so')
    names = {match.decode() for match in re.findall(rb'android\d+-\d+\.\d+(?=_kernelsu\.ko)', data)}
    modules, inits = {}, []
    for blob in embedded_elfs(data):
        kind = struct.unpack_from('<H', blob, 16)[0]
        if kind in (2, 3) and b'kernelsu.ko' in blob and b'/init' in blob:
            inits.append(blob)
        if kind != 1 or b'name=kernelsu\0' not in blob:
            continue
        digest = sha(blob)
        keys = [name for name, expected in KSU_MODULE_HASHES.items() if digest == expected]
        version = re.search(rb'vermagic=(\d+\.\d+)[^\0]*', blob)
        if not keys and version:
            explicit = re.search(rb'android(\d+)', version[0])
            candidates = {name for name in names if name.endswith('-' + version[1].decode())}
            if explicit:
                candidates &= {'android' + explicit[1].decode() + '-' + version[1].decode()}
            if len(candidates) == 1:
                keys = list(candidates)
        for key in keys:
            if key in modules:
                raise ValueError('APK 包含不唯一的 KSU 模块：' + key)
            modules[key] = blob
    if kmi not in modules or len(inits) != 1:
        raise ValueError('无法从所选 APK 唯一提取匹配 ' + kmi + ' 的 KSU 模块与 ksuinit；不使用其他版本资源代替')
    return modules[kmi], inits[0]


def ksu_patch(image, apk, tool, official, directory, kmi):
    engine = official / 'ksud.exe'
    if not engine.is_file():
        raise ValueError('缺少 Windows KSU 补丁器：tools/official/ksud.exe')
    kernel = unpack(tool, image, directory / 'inspect') / 'kernel'
    version = re.search(rb'Linux version (\d+\.\d+)[^\0\s]*', kernel.read_bytes())
    detected = re.search(rb'android(\d+)', version[0]) if version else None
    if kmi is None and detected:
        kmi = 'android' + detected[1].decode() + '-' + version[1].decode()
    if not kmi or not version or not kmi.endswith('-' + version[1].decode()):
        raise ValueError('无法识别兼容的 KSU KMI（nabu 旧版 4.14 内核不能仅靠官方 KSU APK 获得 LKM 支持）')
    module, init = ksu_payloads(apk, kmi)
    (directory / 'kernelsu.ko').write_bytes(module)
    (directory / 'ksuinit').write_bytes(init)
    command(engine, 'boot-patch', '--boot', 'input.img', '--module', 'kernelsu.ko',
            '--init', 'ksuinit', '--kmi', kmi, '--out', '.', '--out-name', 'new-boot.img', cwd=directory)
    return directory / 'new-boot.img', {'engine': 'upstream Windows ksud', 'engine_sha256': sha(engine.read_bytes()),
                                       'kmi': kmi, 'module_sha256': sha(module), 'init_sha256': sha(init)}


def patch_root(image, apk, mode, tool, official, work, kmi=None):
    directory = work / ('root-' + mode)
    directory.mkdir()
    shutil.copy2(image, directory / 'input.img')
    if mode == 'magisk':
        output, record = magisk_patch(image, apk, tool, official, directory)
    else:
        output, record = ksu_patch(image, apk, tool, official, directory, kmi)
    if not output.is_file():
        raise ValueError('APK 修补程序没有生成 boot 镜像')
    checked = unpack(tool, output, directory / 'verify')
    entries = cpio_entries((checked / 'ramdisk.cpio').read_bytes())
    required = '.backup/.magisk' if mode == 'magisk' else 'kernelsu.ko'
    if required not in entries:
        raise ValueError('修补程序退出成功，但生成的 ramdisk 缺少 ' + required)
    record.update(mode=mode, apk=str(apk), apk_sha256=sha(apk.read_bytes()),
                  output_sha256=sha(output.read_bytes()))
    return output, record
