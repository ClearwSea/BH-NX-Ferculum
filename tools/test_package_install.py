"""Compile and test the real installer on normal files and interrupted copies."""
import argparse,subprocess,tempfile,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);a=p.parse_args()
with tempfile.TemporaryDirectory() as t:
 root=Path(t);driver=root/'driver.c';exe=root/'test'
 driver.write_text('''#include <stdio.h>
#include <stdlib.h>
#include "package_install.h"
static int cb(const char *n,uint64_t b,void *c){(void)n;(void)b;return !c;}
int main(int argc,char **argv){char e[512];int r=m3_install_tree(argv[1],argv[2],cb,argc>3?(void*)1:0,e);if(r)puts(e);return r?1:0;}
''')
 subprocess.run(['cc','-DM3_STANDALONE=1','-DPACKAGE_HOST_TEST','-I',str(a.source),str(driver),str(a.source/'package_install.c'),'-lcrypto','-o',str(exe)],check=True)
 source=root/'source';payload=source/'payload';payload.mkdir(parents=True)
 values={'game.apk':b'package'*100000,'server-stage3/data/account.json':b'{"progress":1}'}
 def manifest():
  lines=[]
  for n,b in values.items():
   f=payload/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b)
   lines.append(f"{'S' if n.endswith('account.json') else 'R'}\t{len(b)}\t{hashlib.sha256(b).hexdigest()}\t{n}\n")
  (source/'manifest.tsv').write_text(''.join(lines))
 manifest();dst=root/'install'
 def run(out,ok=True,cancel=False):
  r=subprocess.run([str(exe),str(source),str(out)]+(['cancel'] if cancel else []),capture_output=True,text=True)
  assert (r.returncode==0)==ok,r.stdout+r.stderr
 run(dst)
 save=dst/'server-stage3/data/account.json';save.write_bytes(b'{"progress":999}')
 run(dst);assert save.read_bytes()==b'{"progress":999}'
 (dst/'game.apk').write_bytes(b'unknown');run(dst,False);assert (dst/'game.apk').read_bytes()==b'unknown'
 interrupted=root/'interrupted';run(interrupted,False,True);assert not (interrupted/'game.apk').exists();run(interrupted);assert (interrupted/'game.apk').read_bytes()==values['game.apk']
 save=interrupted/'server-stage3/data/account.json';save.unlink();(save.parent/'initialized').write_text('yes');run(interrupted,False);assert not save.exists()
 manifest();(payload/'game.apk').write_bytes(b'bad');bad=root/'bad';run(bad,False);assert not (bad/'game.apk').exists()
 for path in ['../escape','/absolute','foo/../escape','foo\\escape','foo//escape']:
  (source/'manifest.tsv').write_text('R\t1\t'+'0'*64+'\t'+path+'\n');run(root/'unsafe',False)
 manifest();linkroot=root/'linked';linkroot.mkdir();(linkroot/'game.apk').symlink_to(dst/'game.apk');run(linkroot,False)
 print('PASS: clean install, save preservation, unknown resource refusal, interruption/resume, missing initialized save refusal, corrupt input, traversal and symlink refusal')
