#!/usr/bin/env python3
"""Exact-source server-side candidate assembly. Uploads Git objects only, never refs.
Authoritative commit creation and leased publication remain Connector actions.
"""
import argparse,base64,hashlib,json,os,subprocess,urllib.request
from pathlib import Path
BASE='0fa7b4fef94062e54f186fda938dd8a3d2304b7e'
EXPECTED={
'libil2cpp/vm/AssemblyShadow.cpp':('b2466af579fe79cfa3f8816c0118bd4e1ced8a07','70c0fce1a640521d620a417875e7834714021090'),
'libil2cpp/vm/AssemblyShadowFixedBoolean.h':('40162f061029cc8e14ad810515bb96a2b91f1043','7b33c34e5865a65dc8d830f67661a9e94a539868'),
'libil2cpp/vm/Runtime.cpp':('08a7e1805d7ef9a10491d5ef302c3f088c62c0d8','46663f046833ed579cd2541326411361f6636708'),
'tools/r03/runtime01/test_reporting.py':('68ca5f1feb821d66cf6b523a4ce16950744ed7b4','0f5f987027a94bcad0d81e77b64532d37b29af82'),
'tools/r03/runtime01/test_engine_observer.py':('04b26587af77e4c0f060cb04810c0cdfd6751705','f7f4f84ab38bb2a51b8b69f282f03fe93bb2571d'),
'libil2cpp/vm/AssemblyShadowTerminalRejection.h':(None,'32ee53f7a29e8784dccdbffef692f0c8c6f001a0'),
'tools/r03/runtime01/test_terminal_rejection.py':(None,'43ff14d374193cfa52a3c64605fdf7a44f82f28f')}
def blob(data):return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
def once(s,a,b):
 if s.count(a)!=1:raise RuntimeError('Nonunique source anchor: '+a[:100])
 return s.replace(a,b)
def prepare(root,controls,out):
 assert subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()==BASE
 for name,(old,new) in EXPECTED.items():
  p=root/name
  if old is None:
   assert not p.exists();data=(controls/name).read_bytes();assert blob(data)==new
   p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
  else:assert blob(p.read_bytes())==old,(name,blob(p.read_bytes()))
 p=root/'libil2cpp/vm/AssemblyShadowFixedBoolean.h';s=p.read_text()
 s=once(s,'#include "vm/Object.h"','#include "vm/Object.h"\n#include "vm/AssemblyShadowTerminalRejection.h"')
 s=once(s,'        reserve.object.store(object, std::memory_order_release);','        reserve.object.store(object, std::memory_order_release);\n        InitializeTerminalRejection();')
 s=once(s,'    reserve.physicalClass = nullptr;\n}','    reserve.physicalClass = nullptr;\n    ShutdownTerminalRejection();\n}');p.write_text(s)
 p=root/'libil2cpp/vm/AssemblyShadow.cpp';s='#include "AssemblyShadowTerminalRejection.h"\n'+p.read_text()
 a=s.index('    void RaiseTerminalExecutionRejected()\n    {');b=s.index('\n    Transaction& Current()',a)
 s=s[:a]+'''    void RaiseTerminalExecutionRejected()
    {
        // First error/recovery is already sealed. The precreated transport
        // exception must not allocate, invoke a .ctor or prepare stack frames.
        assembly_shadow_reporting::RethrowTerminalRejection();
    }
'''+s[b:]
 s=once(s,'    FixedDiagnosticExceptionScope diagnostic;\n    Exception::Raise(Exception::GetInvalidOperationException(detail.c_str()));','    assembly_shadow_reporting::RethrowTerminalRejection();');p.write_text(s)
 p=root/'libil2cpp/vm/Runtime.cpp';s=once(p.read_text(),'    void Runtime::UnhandledException(Il2CppException* exc)\n    {','''    void Runtime::UnhandledException(Il2CppException* exc)
    {
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
        // A finalizer can be denied by the same terminal business-entry guard.
        // Report that exact rejection natively; re-invoking managed AppDomain
        // handlers would be another forbidden call and abort recursively.
        // Real non-rejection exceptions retain the original unhandled policy.
        if (assembly_shadow_reporting::TryReportRejectedUnhandled(exc)) return;
#endif''');p.write_text(s)
 p=root/'tools/r03/runtime01/test_reporting.py';s=p.read_text()
 s=once(s,'inline Il2CppException rejection;','inline Il2CppException rejection, reservedRejection;\ninline Il2CppClass rejectionClass;\ninline int rejectionConstructors=0;')
 s=once(s,'Disabled=0, Committed=6, FailedAfterCommit=9','Disabled=0, Committed=6, Failed=8, FailedAfterCommit=9')
 s=once(s,'static uint32_t New(Il2CppObject* obj,bool pinned) { assert(!denied && pinned && obj==&boxedTrue);++handleCreates;return handleNull?0:4; }','static uint32_t New(Il2CppObject* obj,bool pinned) { assert(!denied && pinned && (obj==&boxedTrue || obj==&reservedRejection));++handleCreates;return handleNull?0:(obj==&boxedTrue?4:12); }')
 s=once(s,'static Il2CppObject* GetTarget(uint32_t h) { assert(!denied && h==4);++handleReads;return handleWrong?&boxedFalse:&boxedTrue; }','static Il2CppObject* GetTarget(uint32_t h) { assert(!denied && (h==4||h==12));++handleReads;return handleWrong?&boxedFalse:(h==4?&boxedTrue:&reservedRejection); }')
 s=once(s,'assert(h==4);++handleFrees;','assert(h==4||h==12);++handleFrees;')
 s=once(s,'struct Class { static void Init','''struct Exception {
 static Il2CppException* GetInvalidOperationException(const char*) {
  assert(!denied);++rejectionConstructors;
  rejectionClass.image=&corlibImage;rejectionClass.name="InvalidOperationException";
  rejectionClass.namespaze="System";rejectionClass.initialized_and_no_error=true;
  rejectionClass.cctor_finished_or_no_cctor=true;reservedRejection.klass=&rejectionClass;
  return &reservedRejection;
 }
 [[noreturn]] static void Rethrow(Il2CppException* e) { throw Il2CppExceptionWrapper{e}; }
};
struct Class { static void Init''')
 s=s.replace('handleCreates==1 && handleReads==1','handleCreates==2 && handleReads==2')
 s=once(s,'ShutdownFixedHandledResult();assert(handleFrees==1','ShutdownFixedHandledResult();assert(handleFrees==2')
 s=once(s,'assert(boxes==2 && handleCreates==2)','assert(boxes==2 && handleCreates==4)')
 s=once(s,'ShutdownFixedHandledResult();assert(handleFrees==2);','ShutdownFixedHandledResult();assert(handleFrees==4);')
 s=once(s,"'vm/Class.h','gc/GCHandle.h'","'vm/Class.h','vm/Exception.h','gc/GCHandle.h'");p.write_text(s)
 p=root/'tools/r03/runtime01/test_engine_observer.py';s=once(p.read_text(),"'vm/Object.h','vm/Class.h','gc/GCHandle.h'","'vm/Object.h','vm/Class.h','vm/Exception.h','gc/GCHandle.h'");p.write_text(s)
 rows=[]
 for name,(_,expected) in EXPECTED.items():
  data=(root/name).read_bytes();assert blob(data)==expected,(name,blob(data),expected)
  rows.append({'path':name,'gitBlob':expected,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
 out.mkdir(parents=True,exist_ok=True)
 (out/'candidate.json').write_text(json.dumps({'base':BASE,'files':rows,'branchesUpdated':False},indent=2)+'\n')
 subprocess.run(['git','-C',str(root),'diff','--check'],check=True)
 print(json.dumps({'result':'ExactCandidatePrepared','files':len(rows),'base':BASE}))
def objects(root,out):
 record=json.loads((out/'candidate.json').read_text());assert record['base']==BASE
 def api(path,data):
  assert path in ('git/blobs','git/trees')
  req=urllib.request.Request('https://api.github.com/repos/night-outlook/il2cpp_plus/'+path,data=json.dumps(data).encode(),method='POST',headers={'Authorization':'Bearer '+os.environ['GITHUB_TOKEN'],'Accept':'application/vnd.github+json','User-Agent':'PinnedPrimaryCandidateAssembler'})
  with urllib.request.urlopen(req,timeout=60) as r:return json.load(r)
 rows=[]
 for entry in record['files']:
  data=(root/entry['path']).read_bytes();assert blob(data)==entry['gitBlob'] and hashlib.sha256(data).hexdigest()==entry['sha256']
  result=api('git/blobs',{'content':base64.b64encode(data).decode(),'encoding':'base64'});assert result['sha']==entry['gitBlob']
  rows.append({'path':entry['path'],'mode':'100644','type':'blob','sha':result['sha']})
 tree=subprocess.check_output(['git','-C',str(root),'rev-parse',BASE+'^{tree}'],text=True).strip()
 result=api('git/trees',{'base_tree':tree,'tree':rows})
 (out/'remote-tree.json').write_text(json.dumps({'baseCommit':BASE,'baseTree':tree,'candidateTree':result['sha'],'entries':rows,'commitsCreated':False,'refsUpdated':False},indent=2)+'\n')
 print('Candidate tree:',result['sha'],'; no commit or ref created')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--controls',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--objects-only',action='store_true');a=p.parse_args()
 if a.objects_only:objects(a.source,a.output)
 else:prepare(a.source,a.controls,a.output)
