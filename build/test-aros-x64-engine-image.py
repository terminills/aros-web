from pathlib import Path
import importlib.util,subprocess,json,struct,os,shutil,tempfile
E=Path(__file__).parent;D=Path(tempfile.mkdtemp(prefix='aros-x64-image-test-'));spec=importlib.util.spec_from_file_location('packer',E/'aros-x64-engine-image.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
(D/'fixture.s').write_text('''.section .text,"ax",@progbits
.global start
start:
nop
.quad target
.long target - .
.quad target - .
.weak optional
.quad optional
.quad absolute
.section .data,"aw",@progbits
.global target
target: .quad 0x55
''')
(D/'layout.ld').write_text('SECTIONS { . = 0; .text : { *(.text) } .data ALIGN(4096) : { *(.data) } }\n')
ld=os.environ.get('AROS_LLD') or shutil.which('ld.lld');assert ld, 'set AROS_LLD to ld.lld'
subprocess.run(['as','--64',str(D/'fixture.s'),'-o',str(D/'fixture.o')],check=True)
subprocess.run([ld,'-m','elf_x86_64','--emit-relocs','--no-relax','-e','0','--defsym=absolute=4660','-T',str(D/'layout.ld'),str(D/'fixture.o'),'-o',str(D/'fixture.elf')],check=True)
r=p.package(D/'fixture.elf',D/'fixture.library');assert r['runtime_relocations']==1,r
data=(D/'fixture.library').read_bytes();off=struct.unpack_from('<Q',data,40)[0];sections=[p.SH.unpack_from(data,off+i*64) for i in range(6)];image=bytearray(data[sections[1][4]:sections[1][4]+sections[1][5]]);rela=sections[2]
for base in [0x200000,0x123456780000]:
 relocated=bytearray(image)
 for at in range(rela[4],rela[4]+rela[5],24):
  where,info,addend=p.RELA.unpack_from(data,at);assert info==((1<<32)|1);struct.pack_into('<Q',relocated,where,base+addend)
 assert struct.unpack_from('<Q',relocated,1)[0]==base+4096
 assert struct.unpack_from('<i',relocated,9)[0]==4096-9
 assert struct.unpack_from('<q',relocated,13)[0]==4096-13
 assert struct.unpack_from('<Q',relocated,21)[0]==0
 assert struct.unpack_from('<Q',relocated,29)[0]==4660
assert data[7]==15
bad=bytearray((D/'fixture.elf').read_bytes());struct.pack_into('<H',bad,18,183);(D/'wrong-machine.elf').write_bytes(bad)
try:p.package(D/'wrong-machine.elf',D/'bad.library');raise AssertionError('wrong machine accepted')
except ValueError:pass
(D/'absolute32.s').write_text('.text\n.global start\nstart: .long target\n.data\ntarget: .quad 1\n')
subprocess.run(['as','--64',str(D/'absolute32.s'),'-o',str(D/'absolute32.o')],check=True)
subprocess.run([ld,'-m','elf_x86_64','--emit-relocs','--no-relax','-e','0','-T',str(D/'layout.ld'),str(D/'absolute32.o'),'-o',str(D/'absolute32.elf')],check=True)
try:p.package(D/'absolute32.elf',D/'bad32.library');raise AssertionError('absolute32 accepted')
except ValueError:pass
result={'functional_pass':True,'fixture':r,'checks':['R_X86_64_64 image pointer slides at low/high address','PC32 and PC64 remain correct','undefined weak stays zero','SHN_ABS constant stays constant','wrong machine rejected','32-bit image pointer rejected','native AROS OSABI'],'scope':'Host synthetic ELF pack/load model, not CPU execution or native engine qualification.'};(D/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
