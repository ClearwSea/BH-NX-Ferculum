"""Package the standalone ARM build, then independently extract and verify it.

Run in the approved Linux build workspace with hacBrewPack and hactool installed.
The key file is supplied separately; key values are never written to diagnostics.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tarfile

TITLE_ID = 0x0100B32017000000
assert TITLE_ID & 0x1FFF == 0, 'Base application ID must not use update/DLC bits'


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def extract_pfs(path, out):
    out.mkdir(parents=True, exist_ok=True)
    with path.open('rb') as f:
        magic, count, strings_size, _ = struct.unpack('<4sIII', f.read(16))
        assert magic == b'PFS0' and 0 < count < 100
        entries = [struct.unpack('<QQII', f.read(24)) for _ in range(count)]
        names = f.read(strings_size)
        start = f.tell()
        seen = set()
        for offset, size, name_at, _ in entries:
            name = names[name_at:names.index(b'\0', name_at)].decode('ascii')
            assert name and '/' not in name and '\\' not in name and name not in seen
            seen.add(name)
            assert start + offset + size <= path.stat().st_size
            f.seek(start + offset)
            with (out / name).open('wb') as w:
                left = size
                while left:
                    b = f.read(min(left, 4 * 1024 * 1024))
                    assert b
                    w.write(b)
                    left -= len(b)
    return seen


def verify_tree(root):
    manifest = (root / 'manifest.tsv').read_bytes()
    assert hashlib.sha256(manifest).hexdigest() == (root / 'package.id').read_text().strip()
    seen = set()
    for line in manifest.decode().splitlines():
        mode, size, digest, name = line.split('\t')
        assert mode in ('R', 'S') and name not in seen
        assert not Path(name).is_absolute() and '..' not in Path(name).parts
        seen.add(name)
        file = root / 'payload' / name
        assert file.stat().st_size == int(size) and sha(file) == digest, name
    assert seen == {p.relative_to(root / 'payload').as_posix() for p in (root / 'payload').rglob('*') if p.is_file()}
    return len(seen)


def build(root, keyfile, package_name='digital-package', title_id=TITLE_ID, prepared_romfs=None):
    assert title_id & 0x1FFF == 0, 'Invalid base application ID'
    # Preserve an existing Windows drive mapping: legacy packers have MAX_PATH
    # buffers, and resolving a mapped drive back to UNC needlessly lengthens it.
    root = Path(os.path.abspath(root))
    keyfile = keyfile.resolve()
    keys = {}
    for line in keyfile.read_text().splitlines():
        name, value = [s.strip() for s in line.split('=', 1)]
        keys[name] = value
    assert set(keys) == {'header_key', 'key_area_key_application_00'}
    assert all(re.fullmatch('[0-9a-fA-F]{%d}' % n, keys[k]) for k, n in [('header_key', 64), ('key_area_key_application_00', 32)])
    keyfile.chmod(0o600)
    assert package_name and Path(package_name).name == package_name
    package = root / package_name
    package.mkdir(exist_ok=True)

    def run(command, log):
        r = subprocess.run([str(s) for s in command], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = r.stdout.decode('utf-8', errors='replace')
        for value in keys.values():
            output = re.sub(re.escape(value), '[REDACTED]', output, flags=re.I)
        output = re.sub(r'(?im)^.*key area key 2:.*$', 'Key area key 2: [REDACTED]', output)
        (package / log).write_text(output, encoding='utf-8')
        assert r.returncode == 0, f'{log}: tool exited {r.returncode}'
        return output

    romfs = Path(os.path.abspath(prepared_romfs)) if prepared_romfs else package / 'romfs'
    if prepared_romfs is None:
        romfs.mkdir(exist_ok=True)
        with tarfile.open(root / 'digital-payload.tar') as tar:
            tar.extractall(romfs, filter='data')
    # Payload archive may include a top-level romfs folder.
    if (romfs / 'romfs' / 'manifest.tsv').exists():
        romfs = romfs / 'romfs'
    files = verify_tree(romfs)
    print(f'Payload verified: {files} files', flush=True)
    assert extract_pfs(root / 'digital-local.nsp', package / 'exefs') == {'main', 'main.npdm'}
    original_npdm = (package / 'exefs/main.npdm').read_bytes()
    assert original_npdm[0xc] & 1 == 0 and (original_npdm[0xc] >> 1) & 7 == 2
    run([root / 'tools/hacBrewPack/hacbrewpack', '-k', keyfile,
         '--titleid', f'{title_id:016x}', '--keygeneration', '1', '--nologo',
         '--exefsdir', package / 'exefs', '--romfsdir', romfs,
         '--controldir', package / 'control', '--nspdir', package / 'out',
         '--ncadir', package / 'nca', '--tempdir', package / 'temp',
         '--backupdir', package / 'backup'], 'pack.log')
    nsp = package / 'out' / f'{title_id:016x}.nsp'
    ncas = package / 'verify/ncas'
    names = extract_pfs(nsp, ncas)
    assert len(names) == 3 and all(n.endswith('.nca') for n in names)
    content = {}
    for name in sorted(names):
        nca = ncas / name
        assert sha(nca)[:32] == name.split('.')[0]
        dest = package / 'verify' / name
        dest.mkdir(parents=True, exist_ok=True)
        output = run([root / 'tools/hactool/hactool', '--suppresskeys', '--disablekeywarns',
                      '-k', keyfile, '-y', '--exefsdir', dest / 'exefs',
                      '--romfsdir', dest / 'romfs', '--section0dir', dest / 'section0', nca], name + '.verify.log')
        assert not re.search(r'(?im)^.*(?:hash|level).*FAIL', output), name + ': integrity failure'
        typ = re.search(r'Content Type:\s+(\w+)', output).group(1)
        assert typ not in content
        assert re.search(r'Title ID:\s+' + f'{title_id:016x}', output, re.I)
        content[typ] = {'name': name, 'sha256': sha(nca), 'bytes': nca.stat().st_size, 'dir': dest}
    assert set(content) == {'Program', 'Control', 'Meta'}
    program = content['Program']['dir']
    assert verify_tree(program / 'romfs') == files
    assert sha(program / 'exefs/main') == sha(package / 'exefs/main')
    final_npdm = (program / 'exefs/main.npdm').read_bytes()
    aci, _, acid, _ = struct.unpack_from('<IIII', final_npdm, 0x70)
    assert all(struct.unpack_from('<Q', final_npdm, p)[0] == title_id for p in (aci + 0x10, acid + 0x210, acid + 0x218))
    # hacBrewPack replaces only ACID public key/signature; capabilities stay intact.
    expected = bytearray(original_npdm)
    expected[acid:acid + 0x200] = final_npdm[acid:acid + 0x200]
    assert final_npdm == expected, 'Unexpected NPDM permission/ABI changes'
    control = content['Control']['dir'] / 'romfs'
    assert sha(control / 'icon_AmericanEnglish.dat') == sha(package / 'control/icon_AmericanEnglish.dat')
    nacp = (control / 'control.nacp').read_bytes()
    assert len(nacp) == 0x4000
    assert nacp[:0x200].split(b'\0')[0].decode() == '崩壊3rd 2017'
    cnmt = next((content['Meta']['dir'] / 'section0').glob('*.cnmt')).read_bytes()
    assert struct.unpack_from('<Q', cnmt)[0] == title_id and cnmt[0xc] == 0x80
    ext, count = struct.unpack_from('<HH', cnmt, 0xe)
    assert count == 2
    for i in range(count):
        row = cnmt[0x20 + ext + i * 0x38:0x20 + ext + (i + 1) * 0x38]
        item = content[{1: 'Program', 3: 'Control'}[row[0x36]]]
        assert row[:32].hex() == item['sha256']
        assert row[32:48].hex() == item['name'].split('.')[0]
        assert int.from_bytes(row[48:54], 'little') == item['bytes']
    for item in content.values():
        item.pop('dir')
    report = dict(status='OFFLINE_PASS', title_id=f'{title_id:016X}', nsp_bytes=nsp.stat().st_size,
                  nsp_sha256=sha(nsp), payload_files=files, content=content,
                  caveat='Homebrew signatures require compatible CFW; hardware installation/startup remains untested.')
    (package / 'verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--keys', type=Path, required=True)
    p.add_argument('--package-dir', default='digital-package')
    p.add_argument('--title-id', type=lambda s: int(s, 16), default=TITLE_ID)
    p.add_argument('--prepared-romfs', type=Path)
    a = p.parse_args()
    build(a.root, a.keys, a.package_dir, a.title_id, a.prepared_romfs)
