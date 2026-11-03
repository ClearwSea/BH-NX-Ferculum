"""Create an ExeFS title override from a supplied build and matching NPDM.

Does not include game content, a sys-ticon binary, icons or account snapshots.
"""
import argparse
import hashlib
from pathlib import Path
import struct

def read_pfs(data):
    if data[:4] != b'PFS0':
        raise ValueError('Expected ExeFS PFS0')
    count, names_size = struct.unpack_from('<II', data, 4)
    if not 0 < count < 100:
        raise ValueError('Invalid entry count')
    names_at = 16 + count * 24
    payload = names_at + names_size
    if payload > len(data):
        raise ValueError('Invalid string table')
    entries = {}
    for i in range(count):
        offset, size, name_at, _ = struct.unpack_from('<QQII', data, 16 + i * 24)
        if name_at >= names_size:
            raise ValueError('Invalid entry name')
        end = data.index(b'\0', names_at + name_at, payload)
        name = data[names_at + name_at:end].decode('ascii')
        start = payload + offset
        if not name or '/' in name or '\\' in name or name in entries or start + size > len(data):
            raise ValueError('Invalid PFS entry')
        entries[name] = (start, data[start:start + size])
    if set(entries) != {'main', 'main.npdm'}:
        raise ValueError('Expected main and main.npdm only')
    return entries

def fields(npdm):
    if len(npdm) < 0x80 or npdm[:4] != b'META':
        raise ValueError('Invalid NPDM')
    aci, aci_size, acid, acid_size = struct.unpack_from('<IIII', npdm, 0x70)
    if aci_size < 0x18 or acid_size < 0x220 or aci + aci_size > len(npdm) or acid + acid_size > len(npdm):
        raise ValueError('Invalid NPDM bounds')
    if npdm[aci:aci+4] != b'ACI0' or npdm[acid+0x200:acid+0x204] != b'ACID':
        raise ValueError('Invalid NPDM sections')
    if npdm[0xc] & 1 or (npdm[0xc] >> 1) & 7 != 2:
        raise ValueError('Expected ARM32 address space 2')
    return aci + 0x10, acid + 0x210, acid + 0x218

def prepare(exefs, npdm_path, title_id, output):
    if not 0 < title_id < 1 << 64:
        raise ValueError('Invalid title ID')
    before = exefs.read_bytes()
    entries = read_pfs(before)
    at, old = entries['main.npdm']
    new = npdm_path.read_bytes()
    positions = fields(old)
    if len(new) != len(old) or fields(new) != positions:
        raise ValueError('NPDM layout mismatch')
    expected = bytearray(old)
    for pos in positions:
        struct.pack_into('<Q', expected, pos, title_id)
    if bytes(expected) != new:
        raise ValueError('NPDM differs beyond the three title IDs')
    after = bytearray(before)
    after[at:at+len(new)] = new
    if read_pfs(after)['main'][1] != entries['main'][1]:
        raise ValueError('Executable changed')
    output.mkdir(parents=True, exist_ok=False)
    (output/'exefs.nsp').write_bytes(after)
    (output/'config.ini').write_text('[override_config]\noverride_key=!L\n',encoding='utf-8')
    print('Executable SHA256:',hashlib.sha256(entries['main'][1]).hexdigest())

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exefs',type=Path,required=True)
    p.add_argument('--npdm',type=Path,required=True)
    p.add_argument('--title-id',type=lambda x:int(x,16),required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    prepare(a.exefs,a.npdm,a.title_id,a.output)
