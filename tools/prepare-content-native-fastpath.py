"""Bypass ContentRedirection workers for handles outside its reserved 0x3 tag."""
import argparse,re,subprocess
from pathlib import Path
REVISION='31b319b6b4370caf4ab173f0db91cf838c6819d0'
FUNCTIONS={
 'src/FSReplacements.cpp':['FSReadFileGeneric','FSCloseFileAsync','FSGetStatFileAsync','FSSetPosFileAsync','FSGetPosFileAsync'],
 'src/FSAReplacements.cpp':['FSAReadFile','FSAReadFileWithPos','FSACloseFile','FSAGetStatFile','FSASetPosFile','FSAGetPosFile'],
}
def patch(original,names):
 for name in names:
  match=re.search(r'DECL_FUNCTION\([^,]+, '+name+r',.*?\) \{',original,re.S)
  if not match:raise ValueError('Missing function '+name)
  begin=match.end();depth=1;end=begin
  while depth:
   if original[end]=='{':depth+=1
   elif original[end]=='}':depth-=1
   end+=1
  body=original[begin:end-1]
  calls=re.findall(r'return (real_'+name+r'\([^;]+\));',body)
  if len(calls)!=1:raise ValueError('Unexpected native fallback '+name)
  fast='\n    // All layer-owned file handles are tagged 0x3 by FSWrapper. Native\n    // handles need no layer lookup, heap allocation or priority-0 worker hop.\n    if ((static_cast<uint32_t>(handle) & 0xF0000000u) != 0x30000000u) {\n        return '+calls[0]+';\n    }\n'
  original=original[:begin]+fast+original[begin:]
 return original

def prepare(root,apply=False):
 rev=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
 if rev!=REVISION:raise ValueError('Unexpected ContentRedirection revision')
 for name,names in FUNCTIONS.items():
  original=subprocess.check_output(['git','-C',str(root),'show',REVISION+':'+name],text=True)
  expected=patch(original,names);p=root/name;actual=p.read_text()
  if actual!=expected:
   if not apply or actual!=original:raise ValueError('Unexpected edits: '+name)
   p.write_text(expected,newline='\n')
 # The shortcut depends on the actual allocator, not a guessed native handle ABI.
 fs=(root/'src/FSWrapper.cpp').read_text()
 assert 'fileHandle->handle = (((uint32_t) fileHandle.get()) & 0x0FFFFFFF) | 0x30000000;' in fs
 exports=(root/'src/export.cpp').read_text()
 assert 'make_unique_nothrow<FSWrapper>' in exports
 print('PASS: 11 native read/metadata paths bypass workers; tagged handles retain original bodies')
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('source',type=Path);a.add_argument('--apply',action='store_true');args=a.parse_args();prepare(args.source,args.apply)
