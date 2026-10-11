#!/usr/bin/env python3
"""一键更新 Visor 源码、nabu 补丁、boot 镜像和 ESP 包；不会操作平板。"""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import traceback
import zipfile

sys.dont_write_bytecode = True
REPO = 'https://github.com/IO-ZetZor/Visor-BootManager.git'
OUTPUTS = ('nabu-visor-boot.img', 'nabu-visor.fd', 'nabu-visor-esp.zip',
           'NabuBootMenu.efi', 'NabuAndroid.efi', 'RenegadeMarkerLoops.bin',
           'manifest.json', 'SHA256SUMS.txt', 'README.md')
STATE = '.visor-update'
SOURCE_RECORD = 'port/visor-upstream.json'


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def tree_hash(folder):
    entries = []
    for p in sorted(folder.rglob('*')):
        if '.git' in p.relative_to(folder).parts:
            continue
        if p.is_symlink():
            raise RuntimeError(f'不支持源码中的符号链接：{p}')
        if p.is_file():
            data = p.read_bytes()
            try:
                data.decode('utf-8')
                if b'\0' not in data:
                    data = data.replace(b'\r\n', b'\n')
            except UnicodeDecodeError:
                pass
            entries.append((p.relative_to(folder).as_posix(), hashlib.sha256(data).hexdigest()))
    return hashlib.sha256(json.dumps(entries, ensure_ascii=False).encode()).hexdigest()


def checked_child(root, path):
    path = Path(path).absolute()
    resolved = path.resolve()
    if resolved == root.resolve() or not resolved.is_relative_to(root.resolve()):
        raise RuntimeError(f'路径必须位于项目内：{path}')
    # Never recursively remove or replace a junction, symlink or redirected parent.
    current = path
    while current != root:
        if current.is_symlink() or (hasattr(current, 'is_junction') and current.is_junction()):
            raise RuntimeError(f'不支持重定向目录：{current}')
        current = current.parent
    return path


def remove_owned(root, path):
    path = checked_child(root, path)
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def run(*cmd, cwd, capture=False):
    env = os.environ.copy()
    env.update(PYTHONUTF8='1', PYTHONDONTWRITEBYTECODE='1', GIT_TERMINAL_PROMPT='0')
    result = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, check=True,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None,
                            timeout=300)
    return result.stdout.decode('utf-8', errors='replace').strip() if capture else None


def git(*args, cwd, capture=False):
    return run('git', '-c', 'core.autocrlf=false', '-c', 'core.hooksPath=',
               *args, cwd=cwd, capture=capture)


def copy_tree(src, dst, hardlinks=False):
    def copy_file(a, b):
        if hardlinks:
            try:
                os.link(a, b)
                return b
            except OSError:
                pass
        return shutil.copy2(a, b)
    shutil.copytree(src, dst, copy_function=copy_file,
                    ignore=shutil.ignore_patterns('.git', '__pycache__', '*.pyc', '.zig-cache'))


def obtain_source(work, ref, local):
    upstream = work/'upstream'
    if local:
        if (local/'.git').exists():
            if git('status', '--porcelain', cwd=local, capture=True):
                raise RuntimeError('--source 必须为未修改的上游源码；不要传入已移植目录。')
            commit = git('rev-parse', '--verify', 'HEAD^{commit}', cwd=local, capture=True)
        else:
            if not re.fullmatch('[0-9a-fA-F]{40}', ref):
                raise RuntimeError('本地源码没有 Git 记录时，--ref 必须指定其完整 commit。')
            commit = ref.lower()
        copy_tree(local, upstream)
        git('init', '--quiet', cwd=upstream)
    else:
        upstream.mkdir()
        git('init', '--quiet', cwd=upstream)
        git('fetch', '--depth', '1', '--no-tags', REPO, ref, cwd=upstream)
        commit = git('rev-parse', '--verify', 'FETCH_HEAD^{commit}', cwd=upstream, capture=True)
        git('checkout', '--quiet', '--detach', commit, cwd=upstream)
    return upstream, commit


def sync_assets(old, new, esp):
    """Update stock resources while retaining user-modified or custom resources."""
    kept = []
    pairs = [(p, p.relative_to(new/'assets')) for p in (new/'assets').rglob('*') if p.is_file()]
    pairs.append((new/'boot.conf.example', Path('boot.conf.example')))
    for src, rel in pairs:
        dst = esp/rel
        previous = old/'boot.conf.example' if rel == Path('boot.conf.example') else old/'assets'/rel
        if dst.exists() and (not previous.is_file() or sha(dst) != sha(previous)):
            kept.append(rel.as_posix())
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    return kept


def verify_build(out):
    for line in (out/'SHA256SUMS.txt').read_text(encoding='ascii').splitlines():
        digest, name = line.split('  ', 1)
        if name not in OUTPUTS or sha(out/name) != digest:
            raise RuntimeError(f'生成文件校验失败：{name}')
    with zipfile.ZipFile(out/'nabu-visor-esp.zip') as z:
        if z.testzip():
            raise RuntimeError('ESP 配置包损坏。')


def install(root, replacements, workspace, before_publish=None, on_rollback=None):
    """Back up all replaced items, then restore them if any publication step fails."""
    items = [(checked_child(root, dst), src) for dst, src in replacements]
    backup = root/STATE/'backup.zip'
    backup_tmp = workspace/'backup.zip'
    records = [{'path': dst.relative_to(root).as_posix(), 'existed': dst.exists()}
               for dst, _ in items]
    with zipfile.ZipFile(backup_tmp, 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('_update_backup.json', json.dumps(records, ensure_ascii=False, indent=2))
        for dst, _ in items:
            paths = dst.rglob('*') if dst.is_dir() else [dst]
            for file in paths:
                if file.is_file():
                    checked_child(root, file)
                    z.write(file, file.relative_to(root).as_posix())
    os.replace(backup_tmp, backup)
    retired = workspace/'retired'
    retired.mkdir()
    moved, installed = [], []
    try:
        if before_publish:
            before_publish()
        for i, (dst, src) in enumerate(items):
            dst.parent.mkdir(parents=True, exist_ok=True)
            if dst.exists():
                saved = retired/str(i)
                os.replace(dst, saved)
                moved.append((dst, saved))
            os.replace(src, dst)
            installed.append(dst)
    except BaseException:
        try:
            for dst in reversed(installed):
                remove_owned(root, dst)
            for dst, saved in reversed(moved):
                os.replace(saved, dst)
            if on_rollback:
                on_rollback()
        except BaseException as error:
            raise RuntimeError(f'发布失败且自动恢复未完成，请从 {backup} 恢复：{error}') from error
        raise
    return backup


@contextmanager
def update_lock(root):
    state = checked_child(root, root/STATE)
    state.mkdir(exist_ok=True)
    lock = state/'update.lock'
    try:
        handle = lock.open('x', encoding='ascii')
    except FileExistsError:
        raise RuntimeError('已有更新进程或上次更新被中断。确认没有进程运行后删除 .visor-update/update.lock。')
    try:
        with handle:
            handle.write(str(os.getpid()))
        yield state
    finally:
        lock.unlink(missing_ok=True)


def update(args):
    root = args.project.resolve()
    out = checked_child(root, (args.out_dir or root/'dist').resolve())
    if out.is_relative_to(root/STATE) or any(out.is_relative_to(root/name) for name in
                                           ('third_party', 'src', 'port', 'tools', 'esp', 'build', 'config')):
        raise RuntimeError('更新输出目录不能位于源码、配置、依赖或更新工作目录中。')
    if not shutil.which('git'):
        raise RuntimeError('请先安装 Git for Windows，并确保 git 在 PATH 中。')
    patch = root/'port/visor/NABU.patch'
    record = json.loads((root/SOURCE_RECORD).read_text(encoding='utf-8'))
    module = root/'third_party/visor'
    if not (module/'.git').exists():
        raise RuntimeError('Visor 子模块未初始化，请运行 git submodule update --init --depth 1。')
    if git('status', '--porcelain', cwd=module, capture=True):
        raise RuntimeError('Visor 子模块存在本地修改，脚本不会覆盖；适配请维护在 port/visor/NABU.patch。')
    previous_commit = git('rev-parse', '--verify', 'HEAD^{commit}', cwd=module, capture=True)
    if previous_commit != record['commit'] or tree_hash(module) != record['tree_sha256']:
        raise RuntimeError('Visor 源码或补丁有本地改动。请先把改动合并到补丁并更新来源记录；脚本不会覆盖它们。')
    with update_lock(root) as state:
        with tempfile.TemporaryDirectory(prefix='work-', dir=state, ignore_cleanup_errors=True) as tmp:
            work = Path(tmp)
            print('[1/4] 获取上游源码：'+args.ref, flush=True)
            upstream, commit = obtain_source(work, args.ref, args.source.resolve() if args.source else None)
            print('上游 commit：'+commit, flush=True)
            if commit == record['commit'] and not any((args.check_only, args.boot, args.fd, args.out_dir)):
                print('当前已是该上游版本，无需替换或重新编译。需要重新打包时运行 py build.py。', flush=True)
                return
            print('[2/4] 检查并应用完整 nabu 补丁', flush=True)
            for rel in re.findall(r'^--- a/(.+)$', patch.read_text(encoding='utf-8'), re.M):
                file = upstream/rel
                if not file.resolve().is_relative_to(upstream.resolve()):
                    raise RuntimeError('补丁路径越过临时源码目录。')
                if file.is_file():
                    file.write_bytes(file.read_bytes().replace(b'\r\n', b'\n'))
            git('apply', '--check', '--whitespace=nowarn', patch, cwd=upstream)
            project = work/'project'
            project.mkdir()
            for name in ('build.py', 'image_builder.py', 'build_publish.py', 'fd_patch.py', 'patch_boot.py', 'visor_build.py'):
                shutil.copy2(root/name, project/name)
            for name in ('src', 'port', 'config', 'esp'):
                copy_tree(root/name, project/name)
            # Only read compiler/tool files through hardlinks; stage all editable sources separately.
            copy_tree(root/'tools', project/'tools', hardlinks=True)
            copy_tree(root/'third_party/gnu-efi', project/'third_party/gnu-efi')
            copy_tree(root/'third_party/renegade', project/'third_party/renegade')
            copy_tree(upstream, project/'third_party/visor')
            kept = sync_assets(root/'third_party/visor', project/'third_party/visor', project/'esp/EFI/visor')
            next_record = {'repo': REPO, 'commit': commit, 'ref': args.ref,
                           'updated_at_utc': datetime.now(timezone.utc).isoformat(),
                           'patch_sha256': sha(patch), 'tree_sha256': tree_hash(project/'third_party/visor')}
            (project/SOURCE_RECORD).write_text(json.dumps(next_record, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
            run(sys.executable, '-B', '-X', 'utf8', '-c', 'import visor_build; visor_build.prepare()', cwd=project)
            outputs = work/'outputs'
            print('[3/4] 构建完整 ARM64 菜单、boot 和配置包', flush=True)
            run(sys.executable, '-B', '-X', 'utf8', project/'build.py', '--boot',
                (args.boot or root/'tools/official/stock-boot.img').resolve(), '--fd',
                (args.fd or root/'tools/official/nabu-uefi.fd').resolve(), '--out-dir', outputs, cwd=project)
            verify_build(outputs)
            shutil.copy2(outputs/'NabuAndroid.efi', project/'esp/EFI/visor/NabuAndroid.efi')
            (outputs/'README.md').write_text(
                '# 更新后生成的镜像\n\n上游 commit：`'+commit+'`。构建校验已通过，尚未真机验证。\n'
                'boot 镜像需手动刷入，ESP 配置包需手动复制到平板；脚本不操作设备。\n', encoding='utf-8')
            if args.check_only:
                print('[4/4] 检查通过：未替换当前源码、配置或成品。', flush=True)
                return
            print('[4/4] 备份并更新当前源码、资源和同名成品', flush=True)
            replacements = [(root/'esp', project/'esp'), (root/SOURCE_RECORD, project/SOURCE_RECORD)]
            replacements += [(root/'build'/name, project/'build'/name)
                             for name in ('visor_features.h', 'visor_manifest.c')]
            replacements += [(out/name, outputs/name) for name in OUTPUTS]
            def checkout_new():
                if git('status', '--porcelain', cwd=module, capture=True) or tree_hash(module) != record['tree_sha256']:
                    raise RuntimeError('更新期间 Visor 子模块被修改，停止发布。')
                git('fetch', '--depth', '1', '--no-tags', REPO, commit, cwd=module)
                git('checkout', '--quiet', '--detach', commit, cwd=module)
            def checkout_old():
                git('checkout', '--quiet', '--detach', previous_commit, cwd=module)
            backup = install(root, replacements, work, checkout_new, checkout_old)
            print(f'更新完成。输出：{out}\n上一版备份：{backup}\n尚未刷机；新镜像需要真机确认。', flush=True)
            if kept:
                print('保留自定义资源：'+', '.join(kept), flush=True)


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ref', default='main', help='上游分支、tag 或完整 commit；默认 main 最新提交')
    ap.add_argument('--boot', type=Path, help='原始 Android boot.img；默认工程自带输入')
    ap.add_argument('--fd', type=Path, help='原始 nabu FD；默认工程自带输入')
    ap.add_argument('--out-dir', type=Path, help='工程内的输出目录；默认 dist，同名成品覆盖')
    ap.add_argument('--check-only', action='store_true', help='下载、打补丁和编译检查，不替换当前文件')
    ap.add_argument('--source', type=Path, help='用本地原始上游源码代替下载；无 Git 记录时需 --ref 完整 commit')
    ap.add_argument('--project', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = ap.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_./-]*', args.ref):
        ap.error('--ref 必须为分支、tag 或 commit')
    try:
        update(args)
    except (Exception, KeyboardInterrupt) as error:
        details = traceback.format_exc()
        state = args.project.resolve()/STATE
        try:
            checked_child(args.project.resolve(), state)
            state.mkdir(exist_ok=True)
            (state/'last-error.log').write_text(details, encoding='utf-8')
        except (OSError, RuntimeError):
            pass
        print(f'更新失败：{error}\n详细日志：{state / "last-error.log"}\n补丁冲突或接口变化需要人工合并，脚本不会跳过适配或刷机。', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
