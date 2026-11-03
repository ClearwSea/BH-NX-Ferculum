"""SD JSON 替换原语研究 / Journaled JSON replacement experiment.

For one owning process, with callers holding the same transaction lock.
Used by the NS storage adapter. fsync/readback is not a power-loss guarantee.
"""
import hashlib
import json
import os
from pathlib import Path


def _sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _write_sync(path, raw):
    with path.open('wb') as output:
        output.write(raw)
        output.flush()
        os.fsync(output.fileno())


class JsonJournal:
    def __init__(self, destination, fault=lambda point: None):
        self.path = Path(destination)
        self.pending = self.path.with_name(self.path.name + '.nx-pending')
        self.before = self.path.with_name(self.path.name + '.nx-before')
        self.install = self.path.with_name(self.path.name + '.nx-install')
        self.fault = fault

    def recover(self):
        if not self.pending.exists():
            if self.before.exists() or self.install.exists():
                raise ValueError('Unexplained journal sidecar; preserve for inspection')
            return self.path.read_bytes()
        # An incomplete journal fails closed while preserving the original.
        record = json.loads(self.pending.read_bytes())
        raw = record['after_utf8'].encode('utf-8')
        json.loads(raw)
        if record.get('version') != 1 or _sha(raw) != record['after_sha256']:
            raise ValueError('Invalid pending JSON transaction')
        current = self.path.read_bytes() if self.path.exists() else None
        if current is not None and _sha(current) == record['after_sha256']:
            pass  # Commit completed; finish interrupted sidecar cleanup.
        else:
            if self.before.exists():
                if _sha(self.before.read_bytes()) != record['before_sha256']:
                    raise ValueError('Previous account differs from journal')
                if current is not None:
                    raise ValueError('Unexpected account alongside pending transaction')
            else:
                if current is None or _sha(current) != record['before_sha256']:
                    raise ValueError('Account differs from pending transaction')
                os.rename(self.path, self.before)
                self.fault('old_moved')
            _write_sync(self.install, raw)
            self.fault('new_synced')
            os.rename(self.install, self.path)
            self.fault('new_installed')
        if self.path.read_bytes() != raw:
            raise ValueError('Installed JSON readback mismatch')
        if self.before.exists():
            self.before.unlink()
        self.fault('old_removed')
        self.pending.unlink()
        self.fault('journal_removed')
        return raw

    def replace(self, source):
        if self.pending.exists() or self.before.exists() or self.install.exists():
            self.recover()
        source = Path(source)
        raw = source.read_bytes()
        json.loads(raw)
        record = {'version': 1, 'before_sha256': _sha(self.path.read_bytes()),
                  'after_sha256': _sha(raw), 'after_utf8': raw.decode('utf-8')}
        # No existing pending transaction is overwritten.
        with self.pending.open('xb') as output:
            output.write(json.dumps(record, ensure_ascii=False).encode('utf-8'))
            output.flush()
            os.fsync(output.fileno())
        self.fault('journal_synced')
        # This process just validated and wrote this journal. Do not reopen,
        # JSON-decode and hash its embedded entire account a second time.
        # Restart/interrupted paths still use the fully validating recover().
        os.rename(self.path, self.before)
        self.fault('old_moved')
        _write_sync(self.install, raw)
        self.fault('new_synced')
        os.rename(self.install, self.path)
        self.fault('new_installed')
        if self.path.read_bytes() != raw:
            raise ValueError('Installed JSON readback mismatch')
        self.before.unlink()
        self.fault('old_removed')
        self.pending.unlink()
        self.fault('journal_removed')
        source.unlink()


def probe(root):
    """Disposable process-interruption cases; never use a game data directory."""
    root = Path(root)
    root.mkdir(exist_ok=False)
    before = b'{"schema":1,"uid":1024,"probe":0}\n'
    after = b'{"schema":1,"uid":1024,"probe":1}\n'
    points = ('none', 'journal_synced', 'old_moved', 'new_synced',
              'new_installed', 'old_removed', 'journal_removed')
    outcomes = {}

    class Interrupted(BaseException):
        pass

    for point in points:
        directory = root / point
        directory.mkdir()
        destination, source = directory / 'account.json', directory / 'input.tmp'
        destination.write_bytes(before)
        source.write_bytes(after)

        def fault(observed):
            if observed == point:
                raise Interrupted()

        try:
            JsonJournal(destination, fault).replace(source)
        except Interrupted:
            pass
        # The original server removes its temporary in a finally block.
        # Recovery must work even if that source no longer exists.
        if source.exists():
            source.unlink()
        recovered = JsonJournal(destination).recover()
        assert recovered == after
        assert not list(directory.glob('*.nx-*'))
        outcomes[point] = 'PASS'
    directory = root / 'incomplete-journal'
    directory.mkdir()
    path = directory / 'account.json'
    path.write_bytes(before)
    journal = JsonJournal(path)
    journal.pending.write_bytes(b'{')
    try:
        journal.recover()
    except ValueError:
        assert path.read_bytes() == before
        outcomes['incomplete-journal'] = 'REFUSED_WITH_ORIGINAL_PRESERVED'
    else:
        raise AssertionError('Incomplete journal accepted')
    return {'cases': outcomes, 'scope': 'software interruption; no power cut tested'}
