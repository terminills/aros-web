from pathlib import Path
import subprocess,json,hashlib,sys,os,shutil,tempfile
D=Path(__file__).parent;T=Path(os.environ.get('AROS_LLVM_BIN') or str(Path(shutil.which('ld.lld')).parent));B=Path(tempfile.mkdtemp(prefix='aros-x64-link-test-'));out=B/'cef-blink.library';out.write_bytes(b'working-old-module')
mock=B/'link.py';mock.write_text('import sys,shutil\nfrom pathlib import Path\nif sys.argv[1]=="fail":sys.exit(13)\nshutil.copyfile(sys.argv[1],sys.argv[sys.argv.index("-o")+1])\n')
def run(label,obj):
 meta=B/label;args=[sys.executable,str(D/'aros-x64-engine-link.py'),'--lld',str(T/'ld.lld'),'--objcopy',str(T/'llvm-objcopy'),'--metadata-dir',str(meta),'--',sys.executable,str(mock),str(obj),'-o',str(out)]
 with (B/(label+'.log')).open('x') as f:r=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT)
 return r.returncode,meta
(B/'valid.s').write_text('.text\n.global start\nstart:\n.cfi_startproc\n.cfi_personality 0x00, personality\nmov target(%rip), %rax\nret\n.cfi_endproc\npersonality: ret\n.data\ntarget: .quad 37\n.quad start\n')
(B/'invalid.s').write_text('.text\n.global start\nstart: .long target\n.data\ntarget: .quad 1\n')
for n in ['valid','invalid']:subprocess.run(['as','--64',str(B/(n+'.s')),'-o',str(B/(n+'.o'))],check=True)
r,meta=run('compiler-failure','fail');assert r!=0 and out.read_bytes()==b'working-old-module' and list(meta.glob('x64-attempt-*'))
r,meta=run('relocation-failure',B/'invalid.o');assert r!=0 and out.read_bytes()==b'working-old-module' and list(meta.glob('x64-attempt-*'))
r,meta=run('success',B/'valid.o');assert r==0,r;assert out.read_bytes()[:4]==b'\x7fELF';manifest=json.loads((meta/'result.json').read_text());assert hashlib.sha256(out.read_bytes()).hexdigest()==manifest['output_sha256'];assert (meta/'native-final.elf').is_file() and (meta/'native-final.map').is_file();assert not list(meta.glob('x64-attempt-*'))
result={'functional_pass':True,'checks':['compiler failure leaves prior module intact and retains diagnostics','unsupported relocation leaves prior module intact and retains intermediate ELF','successful wrapper validates and publishes compact module','exact final symbols and map retained','output SHA matches manifest'],'scope':'Host synthetic compiler-wrapper/ELF tests. Native compact198/232/234 evidence belongs to unchanged image packer, not yet this production wrapper.'};(B/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(result)
