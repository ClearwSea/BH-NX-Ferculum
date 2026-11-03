from pathlib import Path
import tarfile, hashlib, argparse
p=argparse.ArgumentParser()
p.add_argument("--root",type=Path,required=True)
p.add_argument("--output",type=Path,required=True)
a=p.parse_args()
root=a.root
src=root/'numpy-1.26.2/numpy'
build=root/'numpy-build-target/numpy'
files={}
for p in src.rglob('*.py'):
 rel=p.relative_to(src)
 if any(x in ('tests','test','__pycache__') for x in rel.parts): continue
 files[str(rel)]=p
for p in build.glob('*.py'): files[p.name]=p
out=a.output
with tarfile.open(out,'w:gz') as tar:
 for rel,p in sorted(files.items()): tar.add(p,arcname='numpy/'+rel,recursive=False)
print(len(files),out.stat().st_size,hashlib.sha256(out.read_bytes()).hexdigest())
