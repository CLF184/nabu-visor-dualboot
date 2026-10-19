"""Emulate the built AArch64 handoff (requires the host-only unicorn package).

Usage: python tests/test_native_entry.py --dist path/to/build-output
"""
import argparse
import json
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
    UC_ARM64_REG_PC,
)

BASE, FDT, MARKER = 0x40000000, 0x83000000, 0x17fe00000
REGS = (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3)


def be(value):
    return struct.pack('>I', value)


def padded(value):
    return value + bytes(-len(value) % 4)


def make_fdt(args, model=b'NABU\0', duplicate=False):
    strings = b'model\0bootargs\0'
    def prop(offset, value):
        return be(3) + be(len(value)) + be(offset) + padded(value)
    tree = be(1) + padded(b'\0') + prop(0, model)
    tree += be(1) + padded(b'chosen\0') + prop(6, args)
    if duplicate:
        tree += prop(6, args)
    tree += be(2) + be(2) + be(9)
    strings_at = 56 + len(tree)
    return (struct.pack('>10I', 0xd00dfeed, strings_at + len(strings), 56,
                        strings_at, 40, 17, 16, 0, len(strings), len(tree))
            + bytes(16) + tree + strings)


def run(helper, origin, name, fdt, expected, marked=False, pointer=FDT):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, 4096)
    start = BASE + origin
    page = start & ~4095
    uc.mem_map(page, (start - page + len(helper) + 4095) // 4096 * 4096)
    uc.mem_write(start, helper)
    uc.mem_map(FDT, 0x200000)
    uc.mem_write(FDT, bytes(fdt))
    uc.mem_map(MARKER, 4096)
    markers = bytearray(b'\x41\x41' * 128)
    if marked:
        markers[-2:] = b'\x41\x52'
    uc.mem_write(MARKER, bytes(markers))
    values = (pointer, 0x1111, 0x2222, 0x3333)
    for reg, value in zip(REGS, values):
        uc.reg_write(reg, value)
    def stop(emulator, address, size, data):
        if address in (BASE + 4, BASE + 0x70):
            emulator.emu_stop()
    uc.hook_add(UC_HOOK_CODE, stop)
    uc.emu_start(start, 0, count=10000000)
    assert uc.reg_read(UC_ARM64_REG_PC) == BASE + expected, name
    assert tuple(uc.reg_read(reg) for reg in REGS) == values, (name, 'entry registers changed')
    assert bytes(uc.mem_read(FDT, len(fdt))) == bytes(fdt), (name, 'FDT changed')
    wanted = bytes(256) if expected == 4 else bytes(markers)
    assert bytes(uc.mem_read(MARKER, 256)) == wanted, (name, 'marker protocol changed')
    print('PASS', name)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dist', type=Path, default=Path(__file__).resolve().parents[1] / 'dist')
    args = ap.parse_args()
    manifest = json.loads((args.dist / 'manifest.json').read_text(encoding='utf-8'))
    helper = (args.dist / 'RenegadeMarkerLoops.bin').read_bytes()
    origin = manifest['kernel']['helper_offset']
    common = b'androidboot.hardware=qcom rootwait ro init=/init'
    normal = common + b' androidboot.force_normal_boot=1'
    cases = [
        ('normal boot retains menu', make_fdt(normal + b'\0'), 0x70),
        ('native Recovery bypass', make_fdt(common + b'\0'), 4),
        ('charger overrides force_normal_boot', make_fdt(normal + b' androidboot.mode=charger\0'), 4),
        ('charger at start of command line', make_fdt(b'androidboot.mode=charger ' + normal + b'\0'), 4),
        ('charger without force_normal_boot', make_fdt(common + b' androidboot.mode=charger\0'), 4),
        ('other boot mode retains menu', make_fdt(normal + b' androidboot.mode=normal\0'), 0x70),
        ('charger value suffix rejected', make_fdt(normal + b' androidboot.mode=charger-test\0'), 0x70),
        ('charger embedded in another value rejected', make_fdt(normal + b' x=androidboot.mode=charger\0'), 0x70),
        ('other device retains menu', make_fdt(normal + b' androidboot.mode=charger\0', model=b'OTHER\0'), 0x70),
        ('charger without qcom hardware rejected', make_fdt(b'androidboot.force_normal_boot=1 androidboot.mode=charger\0'), 0x70),
        ('duplicate bootargs rejected', make_fdt(normal + b' androidboot.mode=charger\0', duplicate=True), 0x70),
        ('unterminated bootargs rejected', make_fdt(normal + b' androidboot.mode=charger'), 0x70),
    ]
    for name, fdt, expected in cases:
        run(helper, origin, name, fdt, expected)
    run(helper, origin, 'marked Android bypass retained', make_fdt(normal + b'\0'), 4, marked=True)
    run(helper, origin, 'missing FDT retains menu', make_fdt(normal + b'\0'), 0x70, pointer=0)
    invalid = bytearray(make_fdt(normal + b' androidboot.mode=charger\0'))
    invalid[:4] = bytes(4)
    run(helper, origin, 'invalid FDT retains menu', invalid, 0x70)


if __name__ == '__main__':
    main()
