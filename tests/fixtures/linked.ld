ENTRY(recomp_test)

SECTIONS
{
    . = 0x08804000;
    .text : { *(.text .text.*) }
    .MIPS.abiflags : { *(.MIPS.abiflags) }
    .reginfo : { *(.reginfo) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
}

