/* Sega 32X Activation & Diagnostic Test ROM */
/* Targeted for m68k-elf-as (m68k-elf-gcc toolchain) */

.section .text
.align 2

.global init_hardware
.global main
.global EntryPoint

/* --- ROM VECTORS --- */
.org 0x00000000
    .long   0x00FFFE00          /* Initial Stack Pointer */
    .long   init_hardware       /* Start of Program (Entry for 68k) */
    .long   EntryPoint          /* Bus Error */
    .long   EntryPoint          /* Address Error */
    .long   EntryPoint          /* Illegal Instruction */
    .long   EntryPoint          /* Zero Divide */

/* --- ROM HEADER (Indented for GAS compatibility) --- */
.org 0x00000100
    .ascii  "SEGA MEGA DRIVE "
    .ascii  "USER    2026.MAY"
    .ascii  "32X ACTIVATION TEST ROM                 "
    .ascii  "32X ACTIVATION TEST ROM                 "
    .ascii  "GM 00000000-00"
    .word   0x0000
    .ascii  "J               "
    .long   0x00000000          /* ROM Start */
    .long   0x003FFFFF          /* ROM End (4MB) */
    .long   0xFF000000          /* RAM Start */
    .long   0xFFFFFFFF          /* RAM End */
    .ascii  "            "
    .ascii  "                        "
    .ascii  "JUE             "

.align 2
init_hardware:
    /* 1. Signature Check */
    move.l  (0xA130EC).l, %d0
    cmpi.l  #0x4D415253, %d0             /* Check for 'MARS' */
    beq.w   activate_32bit
    
    /* Failure: No 32X detected */
    moveq   #-2, %d0
    rts

.align 2
activate_32bit:
    move.w  #0x2700, %sr                 /* Disable Interrupts */

    /* 2. B21 Loading Indicator (Simulated) */
loading_check:
    move.b  (0xA12010).l, %d0            /* Sega CD Sub-CPU flag */
    cmpi.b  #0x01, %d0
    bne.w   hw_init_start
    move.l  (0x200000).l, %d1            /* B21 Signal (!CAS2) */
    move.l  (0x220000).l, %d1
    bra.w   loading_check

hw_init_start:
    lea     (0xA15000).l, %a0
    move.b  #1, 0x0101(%a0)              /* Power ON 32X, SH2 Reset */
    
    /* 10ms stabilization delay */
    move.w  #19170, %d1
wait_10ms:
    dbra    %d1, wait_10ms

    moveq   #0, %d0
    move.l  %d0, 0x0120(%a0)             /* Clear COMM0 */
    move.l  %d0, 0x0124(%a0)             /* Clear COMM4 */
    move.b  #3, 0x0101(%a0)              /* Release SH2 Reset */

    /* Request 32X VDP Access */
request_vdp:
    bclr    #7, 0x0100(%a0)
    bne.b   request_vdp

    /* Clear Core Registers */
    move.w  %d0, 0x0102(%a0)             /* Interrupts */
    move.w  %d0, 0x0130(%a0)             /* PWM */
    move.w  %d0, 0x0180(%a0)             /* VDP Mode */

    /* Clear Frame Buffer */
clear_f0:
    bclr    #0, 0x018B(%a0)
    bne.b   clear_f0
    moveq   #-1, %d1
    lea     (0x840000).l, %a1
loop_f0:
    move.w  %d0, (%a1)+
    dbra    %d1, loop_f0

    /* Check for SH2 Readiness (SDER check) */
    move.l  0x0120(%a0), %d0
    cmpi.l  #0x53444552, %d0            /* Check for 'SDER' */
    beq.w   fail_buzz

success_feedback:
    /* INDICATOR: White Lines on screen */
    lea     (0x840100).l, %a1
    move.l  #0xFFFFFFFF, %d0
    move.w  #500, %d1
.s_lines:
    move.l  %d0, (%a1)+
    dbra    %d1, .s_lines

    /* INDICATOR: 3-Second High Buzz */
    move.w  #0x0100, 0x0132(%a0)         /* PWM Cycle */
    move.w  #0x0005, 0x0130(%a0)         /* PWM Control */
    move.w  #0x03FF, 0x0134(%a0)         /* Sample High */
    move.w  #0x03FF, 0x0136(%a0)
    bra.w   diagnostic_wait

fail_buzz:
    /* INDICATOR: Red Lines on screen */
    lea     (0x840100).l, %a1
    move.l  #0x001F001F, %d0
    move.w  #500, %d1
.f_lines:
    move.l  %d0, (%a1)+
    dbra    %d1, .f_lines

    /* INDICATOR: 3-Second Low Buzz */
    move.w  #0x0800, 0x0132(%a0)         /* PWM Cycle */
    move.w  #0x0005, 0x0130(%a0)         /* PWM Control */
    move.w  #0x00FF, 0x0134(%a0)         /* Sample Low */
    move.w  #0x00FF, 0x0136(%a0)

.align 2
diagnostic_wait:
    moveq   #30, %d1                     /* ~3s total */
wait_o:
    move.w  #0xFFFF, %d2
wait_i:
    dbra    %d2, wait_i
    dbra    %d1, wait_o

    move.w  #0x0000, 0x0130(%a0)         /* Mute PWM */
    move.w  #0x2000, %sr                 /* Re-enable Interrupts */

.align 2
EntryPoint:
main:
    /* Infinite loop to keep status visible on screen */
    bra.w   main

/* End of File */
