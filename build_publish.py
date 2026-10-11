"""Publish validated build files with a recoverable backup and rollback."""
from pathlib import Path
import json
import os
import shutil
import tempfile
import zipfile


def publish_files(replacements, backup_dir):
    items = [(Path(src).resolve(), Path(dst).resolve()) for src, dst in replacements]
    if len({dst for _, dst in items}) != len(items):
        raise ValueError('Duplicate build publication destination')
    for src, dst in items:
        if not src.is_file() or (dst.exists() and not dst.is_file()) or src == dst:
            raise ValueError(f'Invalid build publication: {src} -> {dst}')
    backup_dir = Path(backup_dir).resolve()
    backup_dir.mkdir(parents=True, exist_ok=True)
    lock = backup_dir/'publish.lock'
    with lock.open('x', encoding='ascii') as handle:
        handle.write(str(os.getpid()))
    staged, published = [], []
    backup = backup_dir/'backup.zip'
    try:
        records = [{'destination': str(dst), 'existed': dst.exists(), 'member': f'files/{i}'}
                   for i, (_, dst) in enumerate(items)]
        fd, name = tempfile.mkstemp(prefix='backup-', suffix='.zip', dir=backup_dir)
        os.close(fd)
        temporary_backup = Path(name)
        try:
            with zipfile.ZipFile(temporary_backup, 'w', compression=zipfile.ZIP_STORED) as archive:
                archive.writestr('publication.json', json.dumps(records, ensure_ascii=False, indent=2))
                for record, (_, dst) in zip(records, items):
                    if record['existed']:
                        archive.write(dst, record['member'])
            os.replace(temporary_backup, backup)
        finally:
            temporary_backup.unlink(missing_ok=True)
        # Stage beside each destination: os.replace must remain on one volume,
        # even when the compiler workspace and output directory are on different drives.
        for src, dst in items:
            dst.parent.mkdir(parents=True, exist_ok=True)
            fd, name = tempfile.mkstemp(prefix='.nabu-publish-', dir=dst.parent)
            os.close(fd)
            pending = Path(name)
            staged.append(pending)
            shutil.copy2(src, pending)
        try:
            for i, ((_, dst), pending) in enumerate(zip(items, staged)):
                os.replace(pending, dst)
                published.append(i)
        except BaseException:
            failures = []
            with zipfile.ZipFile(backup) as archive:
                for i in reversed(published):
                    dst = items[i][1]
                    recovery_file = None
                    try:
                        if records[i]['existed']:
                            fd, name = tempfile.mkstemp(prefix='.nabu-restore-', dir=dst.parent)
                            os.close(fd)
                            recovery_file = Path(name)
                            with archive.open(records[i]['member']) as old, recovery_file.open('wb') as target:
                                shutil.copyfileobj(old, target)
                            os.replace(recovery_file, dst)
                        else:
                            dst.unlink()
                    except OSError as error:
                        failures.append(f'{dst}: {error}')
                    finally:
                        if recovery_file:
                            try:
                                recovery_file.unlink(missing_ok=True)
                            except OSError:
                                pass  # Keep attempting the remaining restorations.
            if failures:
                raise RuntimeError(f'Build publication rollback incomplete; recover from {backup}: '
                                   + '; '.join(failures))
            raise
        return backup
    finally:
        for pending in staged:
            try:
                pending.unlink(missing_ok=True)
            except OSError:
                pass  # A locked staging file must not mask the publication result.
        lock.unlink(missing_ok=True)
