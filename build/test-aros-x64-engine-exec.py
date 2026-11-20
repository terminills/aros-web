from pathlib import Path
import ctypes,importlib.util,json,mmap,struct,subprocess,os,shutil,tempfile
E=Path(__file__).parent;D=Path(tempfile.mkdtemp(prefix='aros-x64-exec-test-'))
spec=importlib.util.spec_from_file_location('packer',E/'aros-x64-engine-image.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
T=Path(os.environ.get('AROS_LLVM_BIN') or str(Path(shutil.which('ld.lld')).parent))
(D/'fixture.s').write_text('''.text
.global start
start:
.cfi_startproc
.cfi_personality 0x00, personality
mov target(%rip), %rax
ret
.cfi_endproc
.global second
second:
.cfi_startproc
.cfi_personality 0x00, personality
call start
add $5, %rax
ret
.cfi_endproc
personality: ret
.data
target: .quad 37
.global entry_pointer
entry_pointer: .quad second
''')
(D/'layout.ld').write_text('SECTIONS { . = 0; .text : { *(.text) } .aros_unwind : { *(.aros_unwind) } .eh_frame : { *(.eh_frame) } .data ALIGN(4096) : { *(.data) } }\n')
subprocess.run(['as','--64',str(D/'fixture.s'),'-o',str(D/'fixture.o')],check=True)
def link(obj,out):
 subprocess.run([str(T/'ld.lld'),'-m','elf_x86_64','--emit-relocs','--no-relax','-e','0','-T',str(D/'layout.ld'),str(obj),'-o',str(out)],check=True)
link(D/'fixture.o',D/'ordinary.elf')
try:
 p.package(D/'ordinary.elf',D/'ordinary.library');before='accepted'
except ValueError as e:before=str(e)
subprocess.run([str(T/'llvm-objcopy'),'--rename-section','.eh_frame=.aros_unwind',str(D/'fixture.o'),str(D/'preserved.o')],check=True)
link(D/'preserved.o',D/'preserved.elf');result=p.package(D/'preserved.elf',D/'preserved.library')
data=(D/'preserved.library').read_bytes();off=struct.unpack_from('<Q',data,40)[0];sh=[p.SH.unpack_from(data,off+i*64) for i in range(6)];image=data[sh[1][4]:sh[1][4]+sh[1][5]];rel=sh[2]
mem=mmap.mmap(-1,len(image),prot=mmap.PROT_READ|mmap.PROT_WRITE|mmap.PROT_EXEC);mem.write(image);base=ctypes.addressof(ctypes.c_char.from_buffer(mem))
for at in range(rel[4],rel[4]+rel[5],24):
 where,info,addend=p.RELA.unpack_from(data,at);assert info==((1<<32)|1);struct.pack_into('<Q',mem,where,base+addend)
assert ctypes.CFUNCTYPE(ctypes.c_longlong)(base)()==37
ptr=struct.unpack_from('<Q',mem,4096+8)[0];assert ctypes.CFUNCTYPE(ctypes.c_longlong)(ptr)()==42
r={'functional_pass':True,'ordinary_unwind_result':before,'preserved_unwind':result,'direct_entry_result':37,'relocated_indirect_entry_result':42,'scope':'Linux host CPU execution of synthetic packed image with original CFI, PC-relative calls/data and absolute function pointer. Not native AROS engine loading or exception dispatch qualification.'}
(D/'results.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
