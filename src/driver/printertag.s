/* Classic Amiga printer-driver segment tag.  Must be linked first. */
    .text
    .even
    .globl _oap_printer_tag
    .globl _oap_ped
    .extern _oap_printer_name
    .extern _oap_init
    .extern _oap_expunge
    .extern _oap_open
    .extern _oap_close
    .extern _oap_commands
    .extern _oap_special
    .extern _oap_render
    .extern _oap_conv

_oap_printer_tag:
    .long 0x70004e75
    .word 35
    .word 0
_oap_ped:
    .long _oap_printer_name
    .long _oap_init
    .long _oap_expunge
    .long _oap_open
    .long _oap_close
    .byte 3
    .byte 0x0a
    .byte 80
    .byte 1
    .word 1
    .long 2480
    .long 3507
    .word 300
    .word 300
    .long _oap_commands
    .long _oap_special
    .long _oap_render
    .long 30
    .long 0
    .long 1
    .long _oap_conv
    .long 0
    .long 0
    .long 0
