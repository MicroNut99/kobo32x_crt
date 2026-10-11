/* Kobo cartridge 68000 program: code in ROM (0x880800, cart_md.o first - fixed handler spots),
   C data copied to Genesis RAM 0xFF1000 at start, bss after it */
SECTIONS
{
  .text 0x880800 : { cart_md.o(.text) *(.text*) *(.rodata*) . = ALIGN(2); }
  _data_rom = .;
  .data 0xFF1000 : AT(_data_rom) { _data_start = .; *(.data*) . = ALIGN(2); _data_end = .; }
  .bss : { _bss_start = .; *(.bss*) *(COMMON) . = ALIGN(2); _bss_end = .; }
  /DISCARD/ : { *(.comment) *(.note*) }
}
