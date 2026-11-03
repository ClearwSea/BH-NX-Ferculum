"""Execute the built card resource branch; never mount the cartridge RomFS."""
import contextlib,io,runpy,sys,json
from pathlib import Path
evidence=Path(sys.argv[1]) if len(sys.argv)>1 else Path('report/evidence/same-id-20261031')
identity_file=Path(sys.argv[2]) if len(sys.argv)>2 else Path('report/evidence/standalone-package-20261031/romfs/package.id')
sys.argv=['check_entry_flow.py',str(evidence),'01000C4015030000']
with contextlib.redirect_stdout(io.StringIO()): g=runpy.run_path('tools/check_entry_flow.py')
s=g['symbols'];results=[]
assert 'm3_keycard_gate' not in s
for state in ('valid','wrong','missing'):
 exists=state!='missing'
 valid=state=='valid'
 u=g['machine']();calls=[]
 from unicorn.arm_const import UC_ARM_REG_C1_C0_2,UC_ARM_REG_FPEXC
 u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
 def hook(u,a,size,data):
  for n in ['fopen','fread','fgetc','fclose','consoleInit','puts','printf','padConfigureInput','padInitialize','appletMainLoop','consoleExit','romfsMountFromCurrentProcess']:
   if n not in s or a!=(s[n]&~1):continue
   calls.append(n)
   if n=='romfsMountFromCurrentProcess':assert not valid
   value=0
   if n=='fopen':value=0x71000000 if exists else 0
   if n=='fread':u.mem_write(u.reg_read(g['UC_ARM_REG_R0']),(identity_file.read_text().strip().encode() if valid else b'a'*64));value=64
   if n in ('fgetc','romfsMountFromCurrentProcess'):value=0xffffffff
   u.reg_write(g['UC_ARM_REG_R0'],value);u.reg_write(g['UC_ARM_REG_PC'],u.reg_read(g['UC_ARM_REG_LR']));return
 u.hook_add(g['UC_HOOK_CODE'],hook)
 try:u.emu_start(s['m3_package_prepare'],0x72000000,count=100000)
 except Exception:
  pc=u.reg_read(g['UC_ARM_REG_PC']);print(exists,calls,hex(pc),sorted([(v,n) for n,v in s.items() if v<=pc],reverse=True)[:3]);raise
 assert u.reg_read(g['UC_ARM_REG_PC'])==0x72000000
 assert u.reg_read(g['UC_ARM_REG_R0'])==(0 if valid else 0xffffffff)
 results.append({'marker':state,'romfs_attempted':'romfsMountFromCurrentProcess' in calls,'game_continues':valid,'calls':calls})
p=evidence/'card-resource-check.json';p.write_text(json.dumps(results,indent=2))
print('PASS matching marker skips RomFS; absent/wrong marker requires package; unavailable package exits without game; no gate symbol')
