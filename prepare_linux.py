"""Prepare Visor -> upstream rEFInd -> Linux using the Ubuntu project's efi.tar.

Only ESP files are generated; boot.img, FD and Secure Boot policy are untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import tarfile
import tempfile
import zipfile

from build_publish import publish_files

ROOT = Path(__file__).resolve().parent
REFIND_DIR = PurePosixPath('EFI/visor/refind')
REFIND_EFI = r'\EFI\visor\refind\bootaa64.efi'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def config_value(value):
    if any(c in value for c in '\r\n\0"{}'):
        raise ValueError('Unsupported character in rEFInd configuration value')
    return value


def linux_path(value):
    value = config_value(value).replace('/', '\\')
    if not value.startswith('\\') or any(p in ('.', '..') for p in value.split('\\')):
        raise ValueError('Linux kernel must be an absolute EFI path, e.g. \\EFI\\ubuntu\\6.14.11.efi')
    if value.casefold() == REFIND_EFI.casefold():
        raise ValueError('Linux kernel cannot point back to rEFInd')
    return value


def prepare_config(data, name, kernel):
    """Change one plain EFI entry; preserve all other bytes and line endings."""
    config_value(name)
    text = data.decode('utf-8-sig')
    newline = '\r\r\n' if '\r\r\n' in text else '\r\n' if '\r\n' in text else '\n'
    matches = []
    pattern = re.compile(r'^[ \t]*entry[ \t]*\{[ \t]*\r*\n(?P<body>.*?)^[ \t]*\}[ \t]*\r*(?:\n|$)', re.M | re.S)
    for match in pattern.finditer(text):
        names = re.findall(r'^[ \t]*name[ \t]*=([^\r\n]*)', match['body'], re.M)
        if names and names[0].strip() == name:
            matches.append(match)
    if len(matches) > 1:
        raise ValueError('Duplicate Linux entry name; refusing to change multiple entries')
    if matches:
        match = matches[0]
        body = match['body']
        # Unmodified rEFInd cannot read Visor encrypted containers or enforce its hash pin.
        for key in ('sha256', 'encrypted', 'initrd_encrypted', 'luks'):
            values = re.findall(r'^[ \t]*' + key + r'[ \t]*=([^\r\n]*)', body, re.M)
            if any(v.strip() not in ('', '0', 'false', 'none') for v in values):
                raise ValueError(f'Linux entry uses {key}; configure its payload in rEFInd explicitly first')
        actions = re.findall(r'^[ \t]*nabu_action[ \t]*=([^\r\n]*)', body, re.M)
        if any(v.strip() not in ('', 'linux') for v in actions):
            raise ValueError('Refusing to replace an Android/Windows action with Linux')
        # Remove only the previous config-only rEFInd handoff's options.
        body = re.sub(r'^[ \t]*cmdline[ \t]*=refind -c linux.conf\r*(?:\n|$)', '', body, flags=re.M)
        for key, value in (('kernel', kernel), ('nabu_action', 'linux')):
            line = re.compile(r'^([ \t]*)' + key + r'[ \t]*=[^\r\n]*(\r*)$', re.M)
            if len(line.findall(body)) > 1:
                raise ValueError(f'Duplicate {key} in Linux entry')
            if line.search(body):
                body = line.sub(lambda m: m[1] + key + '=' + value + m[2], body)
            else:
                body += '    ' + key + '=' + value + newline
        text = text[:match.start('body')] + body + text[match.end('body'):]
    else:
        text += ('' if not text or text.endswith('\n') else newline) + newline
        text += newline.join(('entry {', '    name=' + name,
                              r'    icon=\EFI\visor\icons\linux.png',
                              '    kernel=' + kernel,
                              '    nabu_action=linux', '}', ''))
    result = text.encode('utf-8')
    return (b'\xef\xbb\xbf' + result) if data.startswith(b'\xef\xbb\xbf') else result


def read_refind(archive):
    files = {}
    prefix = 'EFI/BOOT/'
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            if not member.name.startswith(prefix) or member.isdir():
                continue
            relative = member.name[len(prefix):]
            if (not member.isfile() or not relative or '\\' in relative or ':' in relative
                    or any(p in ('.', '..', '') for p in relative.split('/'))):
                raise ValueError('Unexpected rEFInd archive member: ' + member.name)
            target = (REFIND_DIR / relative).as_posix()
            if target in files:
                raise ValueError('Duplicate rEFInd archive member: ' + member.name)
            files[target] = tar.extractfile(member).read()
    image = files.get((REFIND_DIR / 'bootaa64.efi').as_posix(), b'')
    if len(image) < 64 or image[:2] != b'MZ':
        raise ValueError('efi.tar must contain the Nabu rEFInd EFI/BOOT/bootaa64.efi')
    pe = struct.unpack_from('<I', image, 60)[0]
    if pe > len(image) - 24 or image[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', image, pe+4)[0] != 0xaa64:
        raise ValueError('rEFInd must be an ARM64 EFI image')
    if (REFIND_DIR / 'refind.conf').as_posix() not in files:
        raise ValueError('efi.tar is missing the upstream refind.conf')
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--efi-tar', type=Path, required=True, help='TheMojoMan/xiaomi-nabu efi.tar')
    parser.add_argument('--boot-conf', type=Path, default=ROOT/'esp/EFI/visor/boot.conf', help='existing Visor boot.conf to preserve')
    parser.add_argument('--kernel', required=True, help=r'Linux UKI path on ESP, e.g. \EFI\ubuntu\6.14.11.efi')
    parser.add_argument('--entry-name', default='Ubuntu')
    parser.add_argument('--out-dir', type=Path, default=ROOT/'dist/linux-esp')
    args = parser.parse_args()
    kernel = linux_path(args.kernel)
    name = config_value(args.entry_name)
    original = args.boot_conf.read_bytes()
    files = read_refind(args.efi_tar)
    files['EFI/visor/boot.conf'] = prepare_config(original, name, kernel)
    license_file = ROOT/'port/refind/LICENSE.txt'
    if license_file.is_file():
        files[(REFIND_DIR/'LICENSE.txt').as_posix()] = license_file.read_bytes()
    provenance = {'status': 'NOT_HARDWARE_VERIFIED', 'boot_image_changed': False,
                  'fd_changed': False, 'secure_boot_policy_changed': False,
                  'requires_linux_action_firmware': True,
                  'source': 'https://github.com/TheMojoMan/xiaomi-nabu',
                  'refind_source': 'https://github.com/rodriguezst/rEFInd-nabu',
                  'archive_sha256': sha(args.efi_tar.read_bytes()),
                  'refind_sha256': sha(files[(REFIND_DIR/'bootaa64.efi').as_posix()]),
                  'kernel': kernel, 'entry': name,
                  'files': {p: sha(b) for p, b in sorted(files.items())}}
    out = args.out_dir.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nabu-linux-', dir=out.parent) as temp:
        work = Path(temp)
        replacements = []
        for path, data in files.items():
            source = work/path
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(data)
            replacements.append((source, out/path))
        manifest = work/'linux-manifest.json'
        manifest.write_text(json.dumps(provenance, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
        replacements.append((manifest, out/manifest.name))
        package = work/'nabu-linux-esp.zip'
        with zipfile.ZipFile(package, 'w', zipfile.ZIP_DEFLATED) as z:
            for path, data in sorted(files.items()):
                z.writestr(path, data)
        replacements.append((package, out/package.name))
        backup = publish_files(replacements, out/'.build-backup')
    print(f'ESP package: {out/package.name}\nLinux EFI: {kernel}\nBackup: {backup}')
    print('Copy the generated EFI/visor/refind directory and merge boot.conf into the ESP. Install a boot image built with nabu_action=linux support.')


if __name__ == '__main__':
    main()
