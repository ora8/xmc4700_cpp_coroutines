/* Memory map for XMC4700-F144x2048 (Relax Kit): 2 MB flash (cached alias 0x08000000),
 * 256 KB contiguous RAM (PSRAM/DSRAM1/DSRAM2 from 0x20000000 on the data bus). */
ENTRY(Reset_Handler)
MEMORY {
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2M
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Segmente ausdrücklich festlegen: Flash nur lesen/ausführen (FLAGS 5 = R+X), RAM lesen/
 * schreiben (6 = R+W). Sonst macht die als beschreibbar markierte .init_array-Tabelle
 * das ganze Flash-Segment zu RWX (Linker-Warnung "LOAD segment with RWX permissions"). */
PHDRS {
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  bss  PT_LOAD FLAGS(6);
}
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH :text
  .text : {
    *(.text*) *(.rodata*)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
  } > FLASH
  .ARM.extab : { *(.ARM.extab*) } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH

  /* Konstruktoren globaler C++-Objekte; __libc_init_array ruft sie über diese Symbole auf.
   * Fehlten sie, liefen die Konstruktoren (z. B. von I2c, Bme280) nie. */
  .preinit_array : {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > FLASH
  .init_array : {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > FLASH
  .fini_array : {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH

  _sidata = LOADADDR(.data);
  .data : {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH :data
  .bss (NOLOAD) : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*) *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM :bss
  . = ALIGN(8);
  end = .;
  /DISCARD/ : { *(.note*) *(.comment*) }
}
