"""Prepare optional nabu build inputs without modifying the source files."""
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import uuid

FD_SIZE = 0x300000


def digest(data):
    return hashlib.sha256(data).hexdigest()


def command(tool, *args, cwd):
    subprocess.run([str(tool), *map(str, args)], cwd=cwd, check=True)


def cpio_entries(data):
    """Read newc archives without extracting paths or executing their contents."""
    entries = {}
    at = 0
    while at + 110 <= len(data):
        if data[at:at + 6] not in (b'070701', b'070702'):
            raise ValueError('Invalid newc ramdisk archive')
        fields = [int(data[at + 6 + i * 8:at + 14 + i * 8], 16) for i in range(13)]
        length, namesize = fields[6], fields[11]
        start = at + 110
        if not namesize or start + namesize > len(data) or data[start + namesize - 1] != 0:
            raise ValueError('Invalid CPIO name')
        name = data[start:start + namesize - 1].decode('utf-8')
        at = (start + namesize + 3) & ~3
        if at + length > len(data):
            raise ValueError('Truncated CPIO payload')
        if name == 'TRAILER!!!':
            return entries
        if name in entries:
            raise ValueError('Duplicate CPIO entry: ' + name)
        entries[name] = data[at:at + length]
        at = (at + length + 3) & ~3
    raise ValueError('Missing CPIO trailer')


def unpack(tool, image, directory):
    directory.mkdir()
    command(tool, 'unpack', '-h', image, cwd=directory)
    if not (directory / 'kernel').is_file():
        raise ValueError('Expected an Android boot image containing a kernel')
    return directory


def validate_passthrough_fd(fd):
    """Validate the outer FV only: no driver discovery, decompression or rewrite."""
    if len(fd) != FD_SIZE or fd[40:44] != b'_FVH':
        raise ValueError('Expected a 3 MiB nabu FD with an outer firmware volume')
    filesystems = ('8c8ce578-8a3d-4f1c-9935-896185c32dd3',
                   '5473c07a-3dcb-4dca-bd6f-1e9689e7349a')
    if fd[16:32] not in [uuid.UUID(value).bytes_le for value in filesystems]:
        raise ValueError('Expected an FFS2 or FFS3 firmware volume')
    length = struct.unpack_from('<Q', fd, 32)[0]
    header = struct.unpack_from('<H', fd, 48)[0]
    if not 64 <= header <= length <= len(fd) or header % 2 or fd[55] != 2:
        raise ValueError('Invalid outer firmware-volume header')
    if sum(struct.unpack('<'+'H'*(header//2), fd[:header])) & 0xffff:
        raise ValueError('Invalid firmware-volume header checksum')
    blocks = 0
    for offset in range(56, header - 7, 8):
        count, size = struct.unpack_from('<II', fd, offset)
        if count == size == 0:
            if blocks == length:
                return
            break
        if not count or not size:
            break
        blocks += count * size
    raise ValueError('Invalid firmware-volume block map')


def load_uefi(path, tool, work):
    source = path.read_bytes()
    container = 'fd'
    data = source
    if source.startswith(b'ANDROID!'):
        directory = unpack(tool, path, work / 'uefi-input')
        data = (directory / 'kernel').read_bytes()
        container = 'android-boot-img'
    candidates = []
    offset = 0
    while True:
        signature = data.find(b'_FVH', offset)
        if signature < 0:
            break
        offset = signature + 4
        start = signature - 40
        if start < 0 or start + FD_SIZE > len(data):
            continue
        fd = data[start:start + FD_SIZE]
        try:
            validate_passthrough_fd(fd)
        except (ValueError, struct.error):
            continue
        candidates.append((start, fd))
    if len(candidates) != 1:
        raise ValueError('UEFI 输入必须包含唯一、有效的 3 MiB nabu FD；不支持该镜像布局')
    offset, fd = candidates[0]
    if container == 'fd' and (offset or len(data) != len(fd)):
        container = 'bootshim-and-fd'
    return fd, {'path': str(path), 'sha256': digest(source), 'container': container,
                'fd_offset_in_payload': offset, 'fd_sha256': digest(fd)}


def prepare_boot(stock, recovery, apk, mode, tool, official, work, kmi=None):
    record = {'order': ['recovery', 'root', 'visor'], 'recovery': None, 'root': None}
    prepared = stock
    if recovery:
        android = unpack(tool, stock, work / 'android-input')
        rec = unpack(tool, recovery, work / 'recovery-input')
        ramdisk = rec / 'ramdisk.cpio'
        if not ramdisk.is_file():
            raise ValueError('Recovery 镜像没有可合入 boot 的 ramdisk')
        names = cpio_entries(ramdisk.read_bytes())
        if not any(name in names for name in ('system/bin/recovery', 'sbin/recovery', 'system/bin/twrp')):
            raise ValueError('Recovery ramdisk 中没有 recovery/twrp 程序')
        kernel = (android / 'kernel').read_bytes()
        shutil.copy2(ramdisk, android / 'ramdisk.cpio')
        prepared = work / 'recovery-boot.img'
        command(tool, 'repack', stock, prepared, cwd=android)
        check = unpack(tool, prepared, work / 'recovery-check')
        if (check / 'kernel').read_bytes() != kernel or (check / 'ramdisk.cpio').read_bytes() != ramdisk.read_bytes():
            raise ValueError('Recovery 合入校验失败：原内核或 Recovery ramdisk 不一致')
        record['recovery'] = {'path': str(recovery), 'sha256': digest(recovery.read_bytes()),
                              'ramdisk_sha256': digest(ramdisk.read_bytes()), 'original_kernel_preserved': True}
    if apk:
        from root_patch import patch_root
        prepared, record['root'] = patch_root(prepared, apk, mode, tool, official, work, kmi)
    if prepared != stock:
        data = prepared.read_bytes()
        if len(data) > stock.stat().st_size:
            raise ValueError('Recovery/root 处理后的 boot 超过输入镜像容量')
        prepared.write_bytes(data + bytes(stock.stat().st_size - len(data)))
    record['prepared_boot_sha256'] = digest(prepared.read_bytes())
    return prepared, record
