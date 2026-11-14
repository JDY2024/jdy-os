OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(KernelMain)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* Read + execute */
    rodata PT_LOAD FLAGS(4); /* Read */
    data PT_LOAD FLAGS(6);   /* Read + write */
}

SECTIONS
{
    . = 0x100000;

    .text ALIGN(0x1000) :
    {
        *(.text .text.*)
    } :text

    .rodata ALIGN(0x1000) :
    {
        *(.rodata .rodata.*)
    } :rodata

    .data ALIGN(0x1000) :
    {
        *(.data .data.*)
    } :data

    .bss ALIGN(0x1000) (NOLOAD) :
    {
        *(.bss .bss.*)
        *(COMMON)
    } :data
}
