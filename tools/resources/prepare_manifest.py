"""Hash an external payload and generate the matching program resource identity."""
import argparse
import hashlib
from pathlib import Path

def prepare(romfs, header, seeds):
    payload=romfs/'payload'
    if not payload.is_dir() or payload.is_symlink():
        raise ValueError('Expected a regular payload directory')
    rows=[];seen=set()
    for p in sorted(payload.rglob('*')):
        if p.is_symlink():
            raise ValueError('Symlinks are not supported')
        if not p.is_file():
            continue
        name=p.relative_to(payload).as_posix()
        if len(name.encode('utf-8'))>350 or ':' in name or '\\' in name or any(ord(c)<32 for c in name):
            raise ValueError('Unsupported payload path')
        with p.open('rb') as f:
            digest=hashlib.file_digest(f,'sha256').hexdigest()
        rows.append(f"{'S' if name in seeds else 'R'}\t{p.stat().st_size}\t{digest}\t{name}\n")
        seen.add(name)
    if not rows or not seeds<=seen:
        raise ValueError('Empty payload or missing seed path')
    raw=''.join(rows).encode('utf-8');identity=hashlib.sha256(raw).hexdigest()
    if header.is_relative_to(payload):
        raise ValueError('Header must be outside the payload')
    for p,data in [(romfs/'manifest.tsv',raw),(romfs/'package.id',(identity+'\n').encode()),(header,('/* Generated resource identity. */\n#define M3_PACKAGE_ID "'+identity+'"\n').encode())]:
        with p.open('xb') as f:f.write(data)
    print('Package ID:',identity,'files:',len(rows))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--romfs',type=lambda s:Path(s).resolve(),required=True)
    p.add_argument('--header',type=lambda s:Path(s).resolve(),required=True)
    p.add_argument('--seed',action='append',default=[],help='Relative payload path initialized only when missing')
    a=p.parse_args();prepare(a.romfs,a.header,set(a.seed))
