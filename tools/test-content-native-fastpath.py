"""Compile inserted production native branches; verify every forwarded argument."""
import importlib.util,re,subprocess
from pathlib import Path
root=Path('D:/Projects/WiiU/tooling/sources/ContentRedirectionModule')
spec=importlib.util.spec_from_file_location('patch',Path('tools/prepare-content-native-fastpath.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.prepare(root)
code=['#include <cstdint>\n#include <cassert>\n#include <cstdio>\nusing FSStatus=int;using FSError=int;using FSFileHandle=int;using FSAFileHandle=int;using FSAClientHandle=int;using FSErrorFlag=int;using FSAReadFlag=int;using FSAFilePosition=uint32_t;\nstruct FSClient{};struct FSCmdBlock{};struct FSStat{};struct FSAStat{};struct FSAsyncData{};\n#define DECL_FUNCTION(ret,name,...) ret name(__VA_ARGS__)\nuint64_t expected[16];int nativeCalls=0,layerCalls=0,answer=0;']
tests=[]
for file,names in m.FUNCTIONS.items():
 s=(root/file).read_text()
 for name in names:
  match=re.search(r'DECL_FUNCTION\(([^,]+), '+name+r',\s*(.*?)\) \{',s,re.S);ret,params=match.groups()
  args=[];types=[]
  for param in params.split(','):
   param=param.strip();v=re.search(r'(\w+)$',param).group(1);args.append(v);types.append(param[:-len(v)].strip())
  prefix=s[match.end():].split('\n    }',1)[0]+'\n    }'
  checks=''.join(f'assert((uint64_t)({v})==expected[{i}]);' for i,v in enumerate(args))
  code.append(f'{ret} real_{name}({params}){{{checks}++nativeCalls;return answer;}}\n'+match.group(0)+prefix+'\n++layerCalls;return -888;\n}')
  vals=[('h' if v=='handle' else (f'reinterpret_cast<{t}>(uintptr_t({4096+i*256}))' if '*' in t else f'static_cast<{t}>({i+10})')) for i,(v,t) in enumerate(zip(args,types))]
  setup=''.join(f'expected[{i}]=(uint64_t)({val});' for i,val in enumerate(vals))
  tests.append('{for(int h:{0,0x03bb09b3,0x30000000,0x3fffffff,-1}){for(int result:{0,-17,12345}){answer=result;'+setup+f'int n=nativeCalls,l=layerCalls;auto got={name}('+','.join(vals)+');bool tagged=(uint32_t(h)&0xf0000000u)==0x30000000u;assert(got==(tagged?-888:answer));assert(nativeCalls==n+!tagged&&layerCalls==l+tagged);}}}')
code.insert(0,'#include <initializer_list>')
code.append('int main(){'+''.join(tests)+'puts("PASS: 11 production native branches, native/tagged/invalid handles, all arguments, errors and async pointers");}')
out=Path('artifacts/botw-launch-diagnostics/native-fastpath-test.cpp');out.write_text('\n'.join(code))
