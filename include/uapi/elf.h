#ifndef ELF_H
#define ELF_H

#include <stdint.h>

#define ELF_MAGIC 0x464C457F

/* e_ident indices */
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_OSABI    7

/* EI_CLASS values */
#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2

/* e_type */
#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2
#define ET_DYN    3

/* e_machine */
#define EM_386     3
#define EM_X86_64  62

/* p_type */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_PHDR    6

/* --- ELF32 --- */
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef  int32_t Elf32_Sword;

typedef struct {
  uint8_t  e_ident[16];
  Elf32_Half  e_type;
  Elf32_Half  e_machine;
  Elf32_Word  e_version;
  Elf32_Addr  e_entry;
  Elf32_Off   e_phoff;
  Elf32_Off   e_shoff;
  Elf32_Word  e_flags;
  Elf32_Half  e_ehsize;
  Elf32_Half  e_phentsize;
  Elf32_Half  e_phnum;
  Elf32_Half  e_shentsize;
  Elf32_Half  e_shnum;
  Elf32_Half  e_shstrndx;
} Elf32_Ehdr;

typedef struct {
  Elf32_Word  p_type;
  Elf32_Off   p_offset;
  Elf32_Addr  p_vaddr;
  Elf32_Addr  p_paddr;
  Elf32_Word  p_filesz;
  Elf32_Word  p_memsz;
  Elf32_Word  p_flags;
  Elf32_Word  p_align;
} Elf32_Phdr;

/* --- ELF64 --- */
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef  int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef  int64_t Elf64_Sxword;

typedef struct {
  uint8_t   e_ident[16];
  Elf64_Half  e_type;
  Elf64_Half  e_machine;
  Elf64_Word  e_version;
  Elf64_Addr  e_entry;
  Elf64_Off   e_phoff;
  Elf64_Off   e_shoff;
  Elf64_Word  e_flags;
  Elf64_Half  e_ehsize;
  Elf64_Half  e_phentsize;
  Elf64_Half  e_phnum;
  Elf64_Half  e_shentsize;
  Elf64_Half  e_shnum;
  Elf64_Half  e_shstrndx;
} Elf64_Ehdr;

typedef struct {
  Elf64_Word   p_type;
  Elf64_Word   p_flags;
  Elf64_Off    p_offset;
  Elf64_Addr   p_vaddr;
  Elf64_Addr   p_paddr;
  Elf64_Xword  p_filesz;
  Elf64_Xword  p_memsz;
  Elf64_Xword  p_align;
} Elf64_Phdr;

#endif
