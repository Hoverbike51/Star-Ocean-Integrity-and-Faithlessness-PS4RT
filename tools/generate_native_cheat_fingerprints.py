# SPDX-License-Identifier: GPL-2.0-or-later
# Developer tool only; never needed by players or the normal native build.
from pathlib import Path
import re, struct, hashlib, sys

root=Path(__file__).resolve().parents[1]
source=(root/'launcher/cheat_builtins.inc').read_text(encoding='utf-8')
entries=[]
for match in re.finditer(r'\{ Cheat c; c.id=(\d+);(.*?)result.push_back\(std::move\(c\)\);',source,re.S):
    identity=int(match[1]); body=match[2]
    lines=[]
    for line in re.finditer(r'c.lines.push_back\(\{0x([0-9A-Fa-f]+),Hex\("([0-9A-Fa-f]+)"\),Hex\("([0-9A-Fa-f]+)"\)',body):
        on,off=bytes.fromhex(line[2]),bytes.fromhex(line[3]);assert len(on)==len(off)
        lines.append((int(line[1],16),on,off))
    assert lines
    value=re.search(r'c.slot=0x([0-9A-Fa-f]+); c.choices=\{([^}]+)\}',body)
    slot=int(value[1],16) if value else 0
    choices=[float(s.strip().rstrip('f')) for s in value[2].split(',')] if value else []
    data=b'SO5NATIVE1'+struct.pack('<IQI',identity,slot,len(choices))
    data+=b''.join(struct.pack('<f',v) for v in choices)+struct.pack('<I',len(lines))
    for offset,on,off in sorted(lines):data+=struct.pack('<QI',offset,len(on))+on+off
    entries.append((identity,hashlib.sha256(data).hexdigest()))
assert entries and len(set(v[0] for v in entries))==len(entries) and all(v[0]!=3 for v in entries)
output='// SPDX-License-Identifier: GPL-2.0-or-later\n// Generated from the reviewed built-in patches; independent of profile names/files.\n'
output+='static const struct { int id; const char* hash; } native_fingerprints[] = {\n'
output+=''.join('    {%d,"%s"},\n'%v for v in entries)+'};\n'
target=root/'src/runtime_cheat_native.inc'
if '--check' in sys.argv:
    assert target.read_text(encoding='utf-8')==output, 'Native fingerprints are stale'
else:
    target.write_text(output,encoding='utf-8',newline='\n')
print(f'Checked {len(entries)} native fingerprints; retired ID 3 is excluded.')
