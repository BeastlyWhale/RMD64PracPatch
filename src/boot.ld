OUTPUT_ARCH(mips)
ENTRY(rmd_boot)

SECTIONS
{
    . = 0x800A62C0;
    .boot : { *(.boot) *(.text) *(.text.*) *(.rodata*) }
    boot_end = .;
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.note*) *(.bss) *(COMMON) }
}
ASSERT(boot_end <= 0x800A66EC, "boot stub overflows the 0x434-byte hole")
