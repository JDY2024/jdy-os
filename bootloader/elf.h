#ifndef JDY_ELF_H
#define JDY_ELF_H

/* ELF64 layout, using UEFI fixed-width types (no host libc dependency). */
typedef struct {
    UINT8 ident[16];
    UINT16 type, machine;
    UINT32 version;
    UINT64 entry, phoff, shoff;
    UINT32 flags;
    UINT16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} ELF64_HEADER;

typedef struct {
    UINT32 type, flags;
    UINT64 offset, vaddr, paddr, filesz, memsz, align;
} ELF64_PROGRAM_HEADER;

_Static_assert(sizeof(ELF64_HEADER) == 64, "ELF64 header layout");
_Static_assert(sizeof(ELF64_PROGRAM_HEADER) == 56, "ELF64 program header layout");

#define ELF_PT_LOAD 1
#define ELF_PT_DYNAMIC 2
#define ELF_PT_INTERP 3
#define ELF_PF_X 1
#endif
