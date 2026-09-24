/* Minimal ELF inspection surface for Linux-configured Abseil.  AROS has no
 * glibc dynamic-linker API; the eventual stubs report no loaded ELF modules
 * and no Linux VDSO. */
#ifndef AROS_NODE_LINK_H
#define AROS_NODE_LINK_H

#include <stddef.h>
#include <stdint.h>

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;
typedef uint16_t Elf64_Section;
typedef uint16_t Elf64_Versym;

typedef struct {
    unsigned char e_ident[16];
    Elf64_Half e_type, e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff, e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize, e_phentsize, e_phnum;
    Elf64_Half e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word sh_name, sh_type;
    Elf64_Xword sh_flags;
    Elf64_Addr sh_addr;
    Elf64_Off sh_offset;
    Elf64_Xword sh_size;
    Elf64_Word sh_link, sh_info;
    Elf64_Xword sh_addralign, sh_entsize;
} Elf64_Shdr;

typedef struct {
    Elf64_Word st_name;
    unsigned char st_info, st_other;
    Elf64_Section st_shndx;
    Elf64_Addr st_value;
    Elf64_Xword st_size;
} Elf64_Sym;

typedef struct {
    Elf64_Word p_type, p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr, p_paddr;
    Elf64_Xword p_filesz, p_memsz, p_align;
} Elf64_Phdr;

typedef struct {
    Elf64_Sxword d_tag;
    union {
        Elf64_Xword d_val;
        Elf64_Addr d_ptr;
    } d_un;
} Elf64_Dyn;

typedef struct {
    Elf64_Half vd_version, vd_flags, vd_ndx, vd_cnt;
    Elf64_Word vd_hash, vd_aux, vd_next;
} Elf64_Verdef;

typedef struct {
    Elf64_Word vda_name, vda_next;
} Elf64_Verdaux;

#define ELFMAG "\177ELF"
#define SELFMAG 4
#define EV_CURRENT 1
#define ElfW(type) Elf64_##type

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2
#define SHN_UNDEF 0
#define SHN_LORESERVE 0xff00
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4
#define STT_COMMON 5
#define STT_TLS 6
#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_ST_TYPE(i) ((i) & 0xf)
#define ELF64_ST_INFO(b, t) ((((unsigned)(b)) << 4) + ((t) & 0xf))
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_DYNSYM 11
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define DT_NULL 0
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_VERSYM 0x6ffffff0
#define DT_VERDEF 0x6ffffffc
#define DT_VERDEFNUM 0x6ffffffd

typedef struct {
    unsigned long a_type;
    union {
        unsigned long a_val;
    } a_un;
} Elf64_auxv_t;

#define AT_SYSINFO_EHDR 33

struct dl_phdr_info {
    ElfW(Addr) dlpi_addr;
    const char *dlpi_name;
    const ElfW(Phdr) *dlpi_phdr;
    ElfW(Half) dlpi_phnum;
};

#ifdef __cplusplus
extern "C" {
#endif

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *),
                    void *data);
unsigned long getauxval(unsigned long type);

#ifdef __cplusplus
}
#endif

#endif
