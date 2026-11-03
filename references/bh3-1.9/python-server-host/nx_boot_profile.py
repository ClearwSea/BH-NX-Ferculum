"""Bounded startup timings; no account data, periodic threads or signal hooks."""
import json
import time
from pathlib import Path
_records = []
_origin = time.perf_counter()
PYTHON_ROOT = '/switch/bh3_python_probe'
SERVER_ROOT = Path(__file__).resolve().parent

def mark(name):
    _records.append({'event': name, 'us': round((time.perf_counter()-_origin)*1e6)})

def timed_verify(original):
    def verify():
        mark('manifest-verify-begin')
        try:
            return original()
        finally:
            mark('manifest-verify-end')
    return verify

class Native:
    def __init__(self, native):
        self.native = native
    def __getattr__(self, name):
        return getattr(self.native, name)
    def ready(self, verified):
        mark('ready-before-profile-write')
        try:
            (SERVER_ROOT / 'startup-python.json').write_text(
                json.dumps({'clock': 'perf_counter, relative to Python profiler import',
                            'verified_files': verified, 'events': _records}, indent=2), encoding='utf-8')
        except OSError:
            pass  # Diagnostics must not prevent server readiness.
        return self.native.ready(verified)

def run(native):
    mark('nx-server-import-begin')
    import nx_server
    mark('nx-server-import-end')
    nx_server.serve(Native(native), str(Path(PYTHON_ROOT) / 'runtime'),
                    str(SERVER_ROOT / 'data'))
