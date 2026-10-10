#!/usr/bin/env python3
"""Shared verification of the official Nabu kernel/FD handoff before adding Renegade marker loops. Use build.py to generate an image."""
from __future__ import annotations
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OFFICIAL = ROOT / 'tools/official'


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def branch_target(word: int, pc: int) -> int:
    """解码 AArch64 B 指令目标（用于验证分流头部）。"""
    assert word >> 26 == 0b000101, f'{word:#x} is not a B instruction'
    imm = word & 0x03FFFFFF
    if imm & 0x02000000:
        imm -= 0x04000000
    return pc + imm * 4


def verify_patched(kernel: bytes, original: bytes, fd: bytes, android_entry: int):
    if len(kernel) != len(original) + len(fd):
        raise SystemExit('打补丁后的内核大小不符合预期')
    if kernel[len(original):len(original) + len(fd)] != fd:
        raise SystemExit('FD 并未原样附加在原始内核之后')
    w0 = struct.unpack_from('<I', kernel, 0x00)[0]
    w1 = struct.unpack_from('<I', kernel, 0x04)[0]
    if branch_target(w0, 0x00) != 0x40:
        raise SystemExit(f'入口 0x00 未指向分流代码（目标 {branch_target(w0,0):#x}）')
    if branch_target(w1, 0x04) != android_entry:
        raise SystemExit(f'0x04 未指回 Android 入口（目标 {branch_target(w1,0):#x}，期望 {android_entry:#x}）')
    cfg = struct.unpack_from('<QQQ', kernel, 0x20)
    if cfg != (0x9FC00000, 0x300000, len(original)):
        raise SystemExit(f'分流配置不符: {[hex(v) for v in cfg]}')
    # 分流代码区（0x40..0xAF）必须与官方音量键分流代码逐字节一致
    sc = (OFFICIAL / 'ShellCode.Nabu2.bin').read_bytes()
    if kernel[0x40:len(sc)] != sc[0x40:]:
        raise SystemExit('分流代码与官方 ShellCode.Nabu2.bin 不一致')


if __name__ == "__main__":
    from build import main
    main()
