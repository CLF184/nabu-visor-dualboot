#!/usr/bin/env python3
"""生成 nabu Visor 启动镜像与 ESP 配置包；同名输出直接覆盖。"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project',type=Path,default=Path(__file__).resolve().parent,help=argparse.SUPPRESS)
    ap.add_argument('--boot',type=Path,help='原始 Android boot.img 路径；省略则用 tools/official/stock-boot.img')
    ap.add_argument('--fd',type=Path,help='nabu UEFI .fd 或 .img；省略则用 tools/official/nabu-uefi.fd')
    ap.add_argument('--recovery',type=Path,help='可选 Recovery 镜像；省略则保留原 boot 的 ramdisk，指定时保留 Android 内核')
    root_apk=ap.add_mutually_exclusive_group()
    root_apk.add_argument('--magisk-apk',type=Path,help='可选 Magisk APK；root APK 均省略时不执行 root 修补')
    root_apk.add_argument('--ksu-apk',type=Path,help='可选 KSU APK；与 Magisk 最多选一个，也可均省略；不兼容时报错')
    ap.add_argument('--ksu-kmi',help='可选 KSU KMI，如 android14-6.1；通常自动识别，须与输入内核匹配')
    ap.add_argument('--out-dir',type=Path,help='输出文件夹；默认 dist，文件夹可已存在，同名生成文件直接覆盖')
    args=ap.parse_args()
    root=args.project.resolve()
    out=(args.out_dir or root/'dist').resolve()
    cmd=[sys.executable,'-X','utf8',str(root/'image_builder.py'),
         '--project',str(root),'--out-dir',str(out),
         '--recovery-port',str(root/'port/recovery'),
         '--output-prefix','nabu-visor']
    if args.ksu_kmi and not args.ksu_apk:
        ap.error('--ksu-kmi 只能与 --ksu-apk 一起使用')
    for name in ('boot','fd','recovery','magisk_apk','ksu_apk'):
        if getattr(args,name):cmd.extend(['--'+name.replace('_','-'),str(getattr(args,name).resolve())])
    if args.ksu_kmi:cmd.extend(['--ksu-kmi',args.ksu_kmi])
    print('输出目录：'+str(out),flush=True)
    print('同名生成文件将直接覆盖，其他文件保留。',flush=True)
    env=os.environ.copy();env['PYTHONUTF8']='1'
    subprocess.run(cmd,check=True,cwd=root,env=env)

if __name__=='__main__':main()
