ENTRY(recomp_test)

SECTIONS
{
    . = 0;
    .text : { *(.text .text.*) }
    .MIPS.abiflags : { *(.MIPS.abiflags) }
    .reginfo : { *(.reginfo) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
}
