| ===========================================================================================
| Kobo Deluxe 32X - CARTRIDGE version - Genesis (68000) side.          (cart/cart_md.s)
| ===========================================================================================

        .equ    VDP_CTRL,   0xC00004
        .equ    VDP_DATA,   0xC00000
        .equ    COMM0,      0xA15120
        .equ    COMM2,      0xA15122
        .equ    COMM4,      0xA15124
        .equ    COMM8,      0xA15128
        .equ    COMM10,     0xA1512A
        .equ    COMM12,     0xA1512C
        .equ    ticks,      0x00FF0000          | Genesis RAM: frame counter
        .equ    pad1,       0x00FF0002          | Genesis RAM: pad 1, active-high bits
        .equ    scd_active, 0x00FF0004          | Genesis RAM: 1 if Sega CD is booted

        .global Kos_Decomp
        .global Sub_Start
        .global Sub_End

        .text
        .global _start
_start:                                         | 0x880800
        move.w  #0x2700,sr
        lea     0x00FFFFE0,sp
        jmp     main

        .org    0x40
exception:                                      | 0x880840: any 68000 exception
        move.w  #0x2700,sr
        move.l  #0xC0000000,VDP_CTRL            | backdrop RED and display on: a crash is visible
        move.w  #0x000E,VDP_DATA
        move.w  #0x8144,VDP_CTRL
1:      bra.b   1b

        .org    0x80
hblank:                                         | 0x880880
        rte

        .org    0xC0
vblank:                                         | 0x8808C0
        jmp     vblank_handler

        .org    0x100
extint:                                         | 0x880900
        rte

| -------------------------------------------------------------------------------------------
main:
        | ---- Genesis video: display OFF, V-blank interrupt ON ----
        lea     VDP_CTRL,a0
        move.w  #0x8004,(a0)
        move.w  #0x8134,(a0)
        move.w  #0x8C81,(a0)
        move.w  #0x8F02,(a0)
        move.w  #0x8700,(a0)
        move.l  #0xC0000000,(a0)
        move.w  #0x0000,VDP_DATA

        | ---- controller ports ----
        move.b  #0x40,0xA10009
        move.b  #0x40,0xA1000B
        move.b  #0x40,0xA10003
        move.b  #0x40,0xA10005

        clr.w   ticks
        move.w  #0,pad1
        move.b  #0,scd_active

| ---- Sega CD Boot Sequence ------------------------------------------------------------------
        lea     0x415800,a0
        cmpi.l  #0x53454741,0x6D(a0)
        beq.w   found_scd
        lea     0x416000,a0
        cmpi.l  #0x53454741,0x6D(a0)
        beq.w   found_scd
        lea     0x41AD00,a0
        cmpi.l  #0x53454741,0x6D(a0)
        beq.w   found_scd
        bra.w   scd_done

found_scd:
        move.w  #0xFF00,0xA12002
        move.b  #0x03,0xA12001
        move.b  #0x02,0xA12001
        move.b  #0x00,0xA12001
        move.b  #0x02,0xA12001
scd_wait_halt:
        btst    #1,0xA12001
        beq.b   scd_wait_halt

        move.w  #0x0002,0xA12002

        lea     0x420000,a1
        move.w  #0x7FFF,d1
        moveq   #0,d0
scd_clear_prg:
        move.l  d0,(a1)+
        dbra    d1,scd_clear_prg

        pea     0x420000
        move.l  a0,-(sp)
        jsr     Kos_Decomp
        addq.l  #8,sp

        lea     Sub_Start,a0
        lea     Sub_End,a2
        lea     0x426000,a1
        move.l  a2,d1
        sub.l   a0,d1
        lsr.l   #2,d1
        beq.b   scd_skip_copy
        subq.l  #1,d1
scd_copy_sub:
        move.l  (a0)+,(a1)+
        dbra    d1,scd_copy_sub
scd_skip_copy:

        move.b  #0x00,0xA1200E
        move.b  #0x2A,0xA12002
        move.b  #0x01,0xA12001
scd_wait_run:
        btst    #0,0xA12001
        beq.b   scd_wait_run

        move.l  #2000000,d1
scd_wait_ready:
        cmpi.b  #0x49,0xA1200F
        beq.b   scd_ready
        subq.l  #1,d1
        bne.b   scd_wait_ready
        bra.b   scd_done
scd_ready:

        move.b  #0x00,0xA1200E
scd_wait_ack:
        tst.b   0xA1200F
        bne.b   scd_wait_ack

        move.b  #1,scd_active

scd_done:
        | ---- 32X sync ----
1:      cmpi.l  #0x4D5F4F4B,COMM0
        bne.b   1b
2:      cmpi.l  #0x535F4F4B,COMM4
        bne.b   2b
        move.b  #0,0xA15107
        move.w  #0,0xA15104
        move.l  #0,COMM0
        move.l  #0,COMM4

        move.w  #0x2000,sr

3:      bsr     pause
        cmpi.w  #0x0AAA,COMM10
        bne.b   3b
        move.w  #0x0BBB,COMM10

| ---- command loop with debug backdrop flash ----
loop:
        bsr     pause
        move.w  COMM0,d0
        beq.b   loop

        | DEBUG: Flash backdrop RED when any command hits COMM0
        move.l  #0xC0000000,VDP_CTRL
        move.w  #0x0E00,VDP_DATA

        cmp.w   COMM0,d0
        bne.b   loop
        moveq   #0,d1
        move.b  d0,d1

        cmpi.b  #48,d1
        beq.w   c_pad
        cmpi.b  #52,d1
        beq.w   c_text

        cmpi.b  #45,d1
        beq.w   c_forward_scd
        cmpi.b  #53,d1
        beq.w   c_forward_scd
        cmpi.b  #54,d1
        beq.w   c_forward_scd
        cmpi.b  #56,d1
        beq.w   c_forward_scd
        cmpi.b  #57,d1
        beq.w   c_forward_scd
        cmpi.b  #58,d1
        beq.w   c_forward_scd
        cmpi.b  #59,d1
        beq.w   c_forward_scd

        bra.w   done

c_pad:
        move.w  pad1,d2
        swap    d2
        move.w  ticks,d2
        move.l  d2,COMM8
        bra.w   done
c_text:
        move.w  #0x8134,d2
        tst.w   COMM2
        beq.b   4f
        move.w  #0x8174,d2
4:      move.w  d2,VDP_CTRL
        bra.w   done

c_forward_scd:
        tst.b   scd_active
        beq.b   c_fallback

        move.w  COMM2,0xA12010
        move.w  COMM8,0xA12012
        move.w  d0,0xA1200E

5:      move.b  0xA1200F,d2
        move.b  0xA1200F,d3
        cmp.b   d2,d3
        bne.b   5b
        cmp.b   d1,d2
        bne.b   5b

        move.w  0xA12014,COMM12
        move.w  0xA12016,COMM8
        move.w  #0,0xA1200E

6:      move.b  0xA1200F,d2
        move.b  0xA1200F,d3
        cmp.b   d2,d3
        bne.b   6b
        tst.b   d2
        bne.b   6b

        bra.b   done

c_fallback:
        cmpi.b  #53,d1
        beq.b   c_zero12
        cmpi.b  #56,d1
        beq.b   c_zero12
        cmpi.b  #59,d1
        beq.b   c_zero12
        cmpi.b  #57,d1
        beq.b   c_zero8
        bra.b   done

c_zero12:
        move.w  #0,COMM12
        bra.b   done
c_zero8:
        move.w  #0,COMM8
done:
        move.w  #0,COMM0
        bra.w   loop

pause:
        move.w  #200,d3
7:      dbra    d3,7b
        rts

vblank_handler:
        movem.l d0-d2/a0,-(sp)
        lea     0xA10003,a0
        bsr.b   read_pad
        move.w  d0,pad1
        addq.w  #1,ticks
        movem.l (sp)+,d0-d2/a0
        rte

read_pad:
        bsr.b   get_input
        move.w  d0,d1
        andi.w  #0x0C00,d0
        bne.b   rp_none
        bsr.b   get_input
        bsr.b   get_input
        move.w  d0,d2
        bsr.b   get_input
        andi.w  #0x0F00,d0
        cmpi.w  #0x0F00,d0
        beq.b   rp_common
        move.w  #0x010F,d2
rp_common:
        lsl.b   #4,d2
        lsl.w   #4,d2
        andi.w  #0x303F,d1
        move.b  d1,d2
        lsr.w   #6,d1
        or.w    d1,d2
        eori.w  #0x1FFF,d2
        move.w  d2,d0
        rts
rp_none:
        move.w  #0xF000,d0
        rts

get_input:
        move.b  #0x00,(a0)
        nop
        nop
        move.b  (a0),d0
        move.b  #0x40,(a0)
        lsl.w   #8,d0
        move.b  (a0),d0
        rts

| ---- Sub-CPU Payload and Decompressor ----
        .align 2
        .include "kos.s"

        .align 2
        .global Sub_Start
        .global Sub_End
Sub_Start:
        .incbin "subcpu.bin"
Sub_End: