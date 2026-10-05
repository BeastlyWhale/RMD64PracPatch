OUTPUT_ARCH(mips)
ENTRY(payload_entry)

SECTIONS
{
    . = 0x807C0000;

    .entry  : { *(.entry) }
    .text   : { *(.text) *(.text.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data   : { *(.data) *(.data.*) *(.sdata) *(.sdata.*) }

    /* Nothing may land in bss: the payload is shipped as a flat ROM blob and only .entry/.text/.rodata/.data are copied in. changable state lives at the fixed CTX_BASE page, which boot.S zeroes. */
    .bss (NOLOAD) : { *(.bss) *(.bss.*) *(.sbss) *(.sbss.*) *(COMMON) }
    payload_end = .;

    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.bss) == 0, "payload must have no .bss")
