"""固定版本的上游 Visor 功能清单、编译参数与清单生成工具。
当前 build.py 将完整 ARM64 菜单编译为 FD 内的 DXE 驱动。
上游：IO-ZetZor/Visor-BootManager commit 71a05b5945e37903cfeeaf69c76220fdf5861ac9。
"""
from pathlib import Path
import re
import json

ROOT = Path(__file__).resolve().parent


def registry():
    text = (ROOT / 'third_party/visor/features/features.mk').read_text().replace('\\\n', ' ')
    return dict(re.findall(r'^([A-Za-z_0-9]+)[ \t]*:=[ \t]*([^\n]*)$', text, re.M))


def feature_sets():
    reg = registry()
    all_features = reg['FEAT_ALL'].split()
    # 与上游 PROFILE=full ARCH=aarch64 一致：只按架构约束剔除功能。
    enabled = set(all_features)
    while True:
        previous = enabled.copy()
        for f in list(enabled):
            arches = reg.get('FEAT_' + f + '_ARCH', '').split()
            deps = reg.get('FEAT_' + f + '_DEPS', '').split()
            if (arches and 'aarch64' not in arches) or any(d not in enabled for d in deps):
                enabled.remove(f)
        if previous == enabled:
            break
    return sorted(enabled), sorted(set(all_features) - enabled)


def sources():
    reg = registry()
    on, off = feature_sets()
    units = reg['CORE_SRC'].split()
    for f in on:
        units += reg.get('FEAT_' + f + '_SRC', '').split() + reg.get('FEAT_' + f + '_SRC_GUI', '').split()
    for f in off:
        units += ['stubs/' + s for s in reg.get('FEAT_' + f + '_STUB', '').split()]
    gnuefi = ['data.c', 'misc.c', 'str.c', 'dpath.c', 'print.c', 'va_print.c', 'guid.c', 'error.c', 'console.c', 'event.c',
              'runtime/rtstr.c', 'runtime/rtdata.c', 'runtime/vm.c', 'runtime/efirtlib.c', 'aarch64/math.c']
    return (['src/visor_platform.c', 'src/visor_landscape.c', 'src/visor_efi_support.c', 'build/visor_manifest.c']
            + [f'third_party/visor/src/{s}' for s in sorted(set(units))]
            + [f'third_party/gnu-efi/lib/{s}' for s in gnuefi])


def visor_version():
    try:
        m = re.search(r'^pkgver=(.+)$', (ROOT / 'third_party/visor/PKGBUILD').read_text(), re.M)
        if m:
            return m.group(1).strip()
    except OSError:
        pass
    return '1.5.7'


def prepare():
    on, off = feature_sets()
    (ROOT / 'build').mkdir(exist_ok=True)
    header = ['#ifndef VISOR_FEATURES_H', '#define VISOR_FEATURES_H', '#define VISOR_PROFILE "full"']
    header += [f'#define VISOR_HAS_{f.upper()} {int(f in on)}' for f in registry()['FEAT_ALL'].split()]
    (ROOT / 'build/visor_features.h').write_text('\n'.join(header + ['#endif', '']))
    manifest = {'v': 1, 'ver': visor_version(), 'arch': 'aarch64', 'profile': 'full',
                'feat': on, 'nabu_adapter': 'fd-dxe-bds'}
    payload = 'VISORFT1' + json.dumps(manifest, separators=(',', ':')) + 'VISORFTEND'
    (ROOT / 'build/visor_manifest.c').write_text(
        'const char visor_build_manifest[] = ' + json.dumps(payload) + ';\n')


def flags(host=False):
    return (['-mstrict-align'] if not host else []) + [
        '-mno-ms-bitfields', '-std=c11', '-fshort-wchar', '-ffreestanding', '-fno-stack-protector',
        '-fno-strict-aliasing', '-mno-stack-arg-probe',
        '-DNABU_EMBEDDED=1', '-Isrc', '-Isrc/compat', '-Ithird_party/visor/src/include',
        '-Ithird_party/gnu-efi/inc', '-Ithird_party/gnu-efi/inc/aarch64', '-Ibuild',
        '-include', 'build/visor_features.h', f'-DVISOR_VERSION=L"{visor_version()}"',
        '-Os', '-Wno-incompatible-pointer-types']
