| SEGA MegaCD support code
| by Chilly Willy

| MD Hardware Calls - keep in sync with the enums in hw_md.h

        .equ    MD_CMD_INIT_HW, 1
        .equ    MD_CMD_SET_SR, 2
        .equ    MD_CMD_GET_PAD, 3
        .equ    MD_CMD_CLEAR_B, 4
        .equ    MD_CMD_SET_VRAM, 5
        .equ    MD_CMD_NEXT_VRAM, 6
        .equ    MD_CMD_COPY_VRAM, 7
        .equ    MD_CMD_CLEAR_A, 8
        .equ    MD_CMD_PUT_STR, 9
        .equ    MD_CMD_PUT_CHR, 10
        .equ    MD_CMD_DELAY, 11
        .equ    MD_CMD_SET_PALETTE, 12
        .equ    MD_CMD_Z80_BUSREQUEST, 13
        .equ    MD_CMD_Z80_RESET, 14
        .equ    MD_CMD_Z80_MEMCLR, 15
        .equ    MD_CMD_Z80_MEMCPY, 16
        .equ    MD_CMD_DMA_SCREEN, 17
        .equ    MD_CMD_INIT_32X, 18
        .equ    MD_CMD_COPY_FROM_CART, 24
        .equ    MD_CMD_COPY_WORDS, 25
        .equ    MD_CMD_SAFE_CART_XFER, 26
        .equ    MD_CMD_END, 35              /* KOBO K19: + set_vdp_reg (34) */


        .text

        .align  2

| int do_md_cmd0(int cmd);
        .global do_md_cmd0
do_md_cmd0:
        move.l  4(sp),d0
        bsr     send_md_cmd
        bra     wait_md_cmd

| int do_md_cmd1(int cmd, int arg1);
        .global do_md_cmd1
do_md_cmd1:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        bsr     send_md_cmd
        bra     wait_md_cmd

| int do_md_cmd2(int cmd, int arg1, int arg2);
        .global do_md_cmd2
do_md_cmd2:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        bsr     send_md_cmd
        bra     wait_md_cmd

| int do_md_cmd3(int cmd, int arg1, int arg2, int arg3);
        .global do_md_cmd3
do_md_cmd3:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        move.l  16(sp),0x8028.w
        bsr     send_md_cmd
        bra     wait_md_cmd

| int do_md_cmd4(int cmd, int arg1, int arg2, int arg3, int arg4);
        .global do_md_cmd4
do_md_cmd4:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        move.l  16(sp),0x8028.w
        move.l  20(sp),0x802C.w
        bsr     send_md_cmd
        bra     wait_md_cmd

| void send_md_cmd0(int cmd);
        .global send_md_cmd0
send_md_cmd0:
        move.l  4(sp),d0
        bra.b   send_md_cmd

| void send_md_cmd1(int cmd, int arg1);
        .global send_md_cmd1
send_md_cmd1:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        bra.b   send_md_cmd

| void send_md_cmd2(int cmd, int arg1, int arg2);
        .global send_md_cmd2
send_md_cmd2:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        bra.b   send_md_cmd

| void send_md_cmd3(int cmd, int arg1, int arg2, int arg3);
        .global send_md_cmd3
send_md_cmd3:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        move.l  16(sp),0x8028.w
        bra.b   send_md_cmd

| void send_md_cmd4(int cmd, int arg1, int arg2, int arg3, int arg4);
        .global send_md_cmd4
send_md_cmd4:
        move.l  4(sp),d0
        move.l  8(sp),0x8020.w
        move.l  12(sp),0x8024.w
        move.l  16(sp),0x8028.w
        move.l  20(sp),0x802C.w

send_md_cmd:
        tst.b   0x800E.w
        bne.b   send_md_cmd
        move.b  d0,0x800F.w
0:
        cmp.b   0x800E.w,d0
        bne.b   0b
        cmpi.b  #MD_CMD_DMA_SCREEN,d0
        beq.b   1f
        move.b  #0,0x800F.w
1:
        rts

| int wait_md_cmd(void);
        .global wait_md_cmd
wait_md_cmd:
        move.b  #0,0x800F.w
0:
        tst.b   0x800E.w
        bne.b   0b
        move.l  0x8010.w,d0
        rts

| void switch_banks(void);
| Switch 1M Banks
        .global switch_banks
switch_banks:
        bchg    #0,0x8003.w
0:
        btst    #1,0x8003.w
        bne.b   0b
        rts

| Initialize the SCD hardware & MD code and hardware (called from crt0)
        .global init_hardware
init_hardware:
        lea     md_init_start(pc),a0
        lea     0x080000,a1
        lea     md_init_end(pc),a2
1:
        move.l  (a0)+,(a1)+
        cmpa.l  a0,a2
        bhi.b   1b
        bset    #0,0x8003.w
        move.b  #'M,0x800F.w
2:
        cmpi.b  #'M,0x800E.w
        bne.b   2b
        move.b  #0,0x800F.w
3:
        tst.b   0x800E.w
        bne.b   3b

        bset    #2,0x8003.w
        move    #0x2000,sr
        rts


| Copied to Word RAM for MD to execute
        .align  4
md_init_start:
        move.b  #'M,0xA1200E
        lea     md_start(pc),a0
        lea     0xFF1000,a1
        lea     md_init_end(pc),a2
1:
        move.l  (a0)+,(a1)+
        cmpa.l  a0,a2
        bhi.b   1b
        jmp     0xFF1000

        .align  4
md_start:
        move.l  #0,0xA12010
        move.l  #0,0xA12014
        move.l  #0,0xA12018
        move.l  #0,0xA1201C
        bsr.w   md_init_hw
        moveq   #0,d7
cmd_done:
        tst.b   0xA1200F
        bne.b   cmd_done
        tst.b   0xA1200F                /* HW10: read twice - a torn read must not end the */
        bne.b   cmd_done                /*       handshake early                            */
        move.b  #0,0xA1200E

        /* main loop */
        moveq   #0,d0
cmd_loop:
        tst.b   d7
        beq.b   md_check
        move.w  0xA15120,d0
        beq.b   md_check

mars_done:
        moveq   #0,d0
        move.w  d0,0xA15120
md_check:
        move.b  0xA1200F,d0
        beq.b   cmd_loop
        cmp.b   0xA1200F,d0             /* HW10: the command byte must read the same twice.  */
        bne.b   md_check                /* WHY (console, 68K EXC V09 SR=A71E, args 0091FFFE): */
                                        /* a read overlapping the Sub-CPU's write returned 2  */
                                        /* instead of 19 -> set_sr(0x0091FFFE): trace on,     */
                                        /* interrupts off; or a silent deadlock (wrong echo). */
        move.b  d0,0xA1200E

        /* push args on stack */
        move.l  0xA1202C,-(sp)
        move.l  0xA12028,-(sp)
        move.l  0xA12024,-(sp)
        move.l  0xA12020,-(sp)

        cmpi.b  #'M,d0
        bne.b   2f
1:
        btst    #0,0xA12003
        beq.b   1b
        jmp     0x200000
2:
        move.l  #-1,0xA12010
        cmpi.b  #MD_CMD_END,d0
        bhs.b   3f
        add.w   d0,d0
        lea     cmd_table(pc),a0
        move.w  0(a0,d0.w),d0
        jsr     0(a0,d0.w)
        move.l  d0,0xA12010
3:
        lea     16(sp),sp
        bra     cmd_done

cmd_table:
        .word   cmd_done - cmd_table
        .word   md_init_hw - cmd_table
        .word   cmd_nop - cmd_table      /* 2: HW10 set_sr disabled - never sent by the game; a
                                            garbled command 2 must not touch the status register */
        .word   get_pad - cmd_table
        .word   clear_b - cmd_table
        .word   set_vram - cmd_table
        .word   next_vram - cmd_table
        .word   copy_vram - cmd_table
        .word   clear_a - cmd_table
        .word   put_str - cmd_table
        .word   put_chr - cmd_table
        .word   delay - cmd_table
        .word   set_palette - cmd_table
        .word   z80_busrequest - cmd_table
        .word   z80_reset - cmd_table
        .word   z80_memclr - cmd_table
        .word   z80_memcpy - cmd_table
        .word   dma_screen - cmd_table
        .word   init_32x - cmd_table
        .word   read_32x_16 - cmd_table         /* 19 */
        .word   write_32x_16 - cmd_table        /* 20 */
        .word   read_32x_32 - cmd_table         /* 21 */
        .word   write_32x_32 - cmd_table        /* 22 */
        .word   copy_to_cart - cmd_table        /* 23 */
        .word   copy_from_cart - cmd_table      /* 24 */
        .word   copy_words - cmd_table          /* 25 */
        .word   safe_cart_xfer - cmd_table      /* 26 */
        .word   safe_cart_xfer_w - cmd_table    /* 27 */
        .word   bram_put - cmd_table            /* 28 */
        .word   bram_to_fb - cmd_table          /* 29 */
        .word   bram_get - cmd_table            /* 30 */
        .word   install_exc - cmd_table         /* 31 */
        .word   chilly_memcpy - cmd_table       /* 32 */
        .word   chilly_rv_memcpy - cmd_table    /* 33 */
        .word   set_vdp_reg - cmd_table         /* 34  KOBO K19 */

| KOBO K19: void set_vdp_reg(int word);
| Writes one VDP register word (0x8Rvv) to the VDP control port.  Used to switch the
| Genesis display on/off (register 1: 0x8174 = display on, 0x8134 = display off - same
| value md_init_hw uses, only bit 6 "display enable" differs; bit 5 (V-int) stays ON so
| the vblank handler keeps reading the pads).  Runs in the command loop, never inside an
| interrupt, so it cannot split another routine's VDP address write.
set_vdp_reg:
        move.l  4(sp),d0
        move.w  d0,0xC00004
        moveq   #0,d0
        rts

| void md_init_hw(void);
| initialize MD hardware
md_init_hw:
        movem.l d2-d7/a2-a5,-(sp)
        move.w  #0x2700,sr

| init joyports
        lea     0xA10000,a5
        move.b  #0x40,0x09(a5)
        move.b  #0x40,0x0B(a5)
        move.b  #0x40,0x03(a5)
        move.b  #0x40,0x05(a5)

        lea     0xC00000,a3
        lea     0xC00004,a4

| wait on VDP DMA
        move.w  #0x8114,(a4)
0:
        move.w  (a4),d0
        btst    #1,d0
        bne.b   0b

        moveq   #0,d0
        move.w  #0x8000,d5
        move.w  #0x0100,d7

| Set VDP registers
        lea     InitVDPRegs(pc),a5
        moveq   #18,d1
1:
        move.b  (a5)+,d5
        move.w  d5,(a4)
        add.w   d7,d5
        dbra    d1,1b

| clear VRAM
        move.w  #0x8F02,(a4)
        move.l  #0x40000000,(a4)
        move.w  #0x7FFF,d1
2:
        move.w  d0,(a3)
        dbra    d1,2b

| Clear CRAM
        lea     InitVDPRAM(pc),a5
        move.l  (a5)+,(a4)
        move.l  (a5)+,(a4)
        moveq   #31,d3
3:
        move.l  d0,(a3)
        dbra    d3,3b

| Clear VSRAM
        move.l  (a5)+,(a4)
        moveq   #19,d4
4:
        move.l  d0,(a3)
        dbra    d4,4b

| halt Z80 and init FM chip
        move.w  #0x100,0xA11100
        move.w  #0x100,0xA11200

| reset YM2612
        lea     FMReset(pc),a5
        lea     0xA00000,a0
        move.w  #0x4000,d1
        moveq   #26,d2
5:
        move.b  (a5)+,d1
        move.b  (a5)+,0(a0,d1.w)
        nop
        nop
        dbra    d2,5b

        moveq   #0x30,d0
        moveq   #0x5F,d2
6:
        move.b  d0,0x4000(a0)
        nop
        nop
        move.b  #0xFF,0x4001(a0)
        nop
        nop
        move.b  d0,0x4002(a0)
        nop
        nop
        move.b  #0xFF,0x4003(a0)
        nop
        nop
        addq.b  #1,d0
        dbra    d2,6b

| reset PSG
        lea     PSGReset(pc),a5
        lea     0xC00000,a0
        move.b  (a5)+,0x0011(a0)
        move.b  (a5)+,0x0011(a0)
        move.b  (a5)+,0x0011(a0)
        move.b  (a5),0x0011(a0)

| load font tile data
        move.w  #0x8F02,(a4)
        move.l  #0x40000000,(a4)
        lea     font_data(pc),a0
        move.w  #0x6B*8-1,d2
7:
        move.l  (a0)+,d0
        move.l  d0,d1
        not.l   d1
        andi.l  #0x11111111,d0
        andi.l  #0x00000000,d1
        or.l    d1,d0
        move.l  d0,(a3)
        dbra    d2,7b

| set default palette
        move.l  #0xC0000000,(a4)
        move.l  #0x00000CCC,(a3)
        move.l  #0xC0200000,(a4)
        move.l  #0x00000000,(a3)        /* HW13: green text colour (line 1, colour 1, was 0x00A0)
                                           = BLACK - any green line is invisible.  White (line 0)
                                           and the red crash text (line 2) are unchanged. */
        move.l  #0xC0400000,(a4)
        move.l  #0x0000000A,(a3)

        lea     vblank_int(pc),a0
        move.l  a0,0xFFFD08

        move.w  #0x8174,(a4)
        movem.l (sp)+,d2-d7/a2-a5
        move    #0x2000,sr
        rts

InitVDPRegs:
        .byte   0x04, 0x14, 0x30, 0x2C, 0x07, 0x54, 0x00, 0x00
        .byte   0x00, 0x00, 0x00, 0x00, 0x81, 0x2B, 0x00, 0x01
        .byte   0x01, 0x00, 0x00

        .align  2

InitVDPRAM:
        .word   0x8104, 0x8F01
        .word   0xC000, 0x0000
        .word   0x4000, 0x0010

FMReset:
        .byte   0,0x22, 1,0x00
        .byte   0,0x27, 1,0x00
        .byte   0,0x28, 1,0x00, 1,0x04, 1,0x01, 1,0x05, 1,0x02, 1,0x06
        .byte   0,0x2A, 1,0x80, 0,0x2B, 1,0x00
        .byte   0,0xB4, 1,0x00, 0,0xB5, 1,0x00, 0,0xB6, 1,0x00
        .byte   2,0xB4, 3,0x00, 2,0xB5, 3,0x00, 2,0xB6, 3,0x00

PSGReset:
        .byte   0x9f, 0xbf, 0xdf, 0xff

        .align  4

        .include "font.s"

        .align  4

| HW10: harmless stand-in for commands the game never sends at runtime
cmd_nop:
        moveq   #0,d0
        rts

set_sr:
        moveq   #0,d0
        move.w  sr,d0
        move.l  4(sp),d1
        move.w  d1,sr
        rts

get_pad:
        move.l  d2,-(sp)
        move.l  8(sp),d0
        cmpi.w  #1,d0
        bhi     no_pad
        add.w   d0,d0
        addi.l  #0xA10003,d0
        movea.l d0,a0
        bsr.b   get_input
        move.w  d0,d1
        andi.w  #0x0C00,d0
        bne.b   no_pad
        bsr.b   get_input
        bsr.b   get_input
        move.w  d0,d2
        bsr.b   get_input
        andi.w  #0x0F00,d0
        cmpi.w  #0x0F00,d0
        beq.b   common
        move.w  #0x010F,d2
common:
        lsl.b   #4,d2
        lsl.w   #4,d2
        andi.w  #0x303F,d1
        move.b  d1,d2
        lsr.w   #6,d1
        or.w    d1,d2
        eori.w  #0x1FFF,d2
        move.w  d2,d0
        move.l  (sp)+,d2
        rts

no_pad:
        .ifdef  HAS_SMS_PAD
        move.b  (a0),d0
        andi.w  #0x003F,d0
        eori.w  #0x003F,d0
        .else
        move.w  #0xF000,d0
        .endif
        move.l  (sp)+,d2
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

clear_b:
        moveq   #0,d0
        lea     0xC00000,a0
        move.w  #0x8F02,4(a0)
        move.l  #0x60000003,d1
        move.l  d1,4(a0)
        move.w  #64*32-1,d1
1:
        move.w  d0,(a0)
        dbra    d1,1b
        rts

set_vram:
        lea     0xC00000,a1
        move.w  #0x8F02,4(a1)
        move.l  4(sp),d1
        lsl.l   #2,d1
        lsr.w   #2,d1
        swap    d1
        ori.l   #0x40000000,d1
        move.l  d1,4(a1)
        move.l  8(sp),d0
        move.w  d0,(a1)
        rts

next_vram:
        move.l  4(sp),d0
        move.w  d0,0xC00000
        rts

copy_vram:
        lea     0xC00000,a1
        move.w  #0x8F02,4(a1)
        move.l  4(sp),d1
        lsl.l   #2,d1
        lsr.w   #2,d1
        swap    d1
        ori.l   #0x40000000,d1
        move.l  d1,4(a1)
        movea.l 8(sp),a0
        move.l  12(sp),d0
        subq.w  #1,d0
0:
        move.w  (a0)+,(a1)
        dbra    d0,0b
        rts

clear_a:
        moveq   #0,d0
        lea     0xC00000,a0
        move.w  #0x8F02,4(a0)
        move.l  #0x40000003,d1
        move.l  d1,4(a0)
        move.w  #64*32-1,d1
1:
        move.w  d0,(a0)
        dbra    d1,1b
        rts

put_str:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        lea     0xC00000,a1
        move.w  #0x8F02,4(a1)
        move.l  16(sp),d1
        lsl.l   #6,d1
        or.l    12(sp),d1
        add.w   d1,d1
        swap    d1
        ori.l   #0x40000003,d1
        move.l  d1,4(a1)
1:
        move.b  (a0)+,d0
        subi.b  #0x20,d0
        move.w  d0,(a1)
        tst.b   (a0)
        bne.b   1b
        rts

put_chr:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        lea     0xC00000,a1
        move.w  #0x8F02,4(a1)
        move.l  16(sp),d1
        lsl.l   #6,d1
        or.l    12(sp),d1
        add.w   d1,d1
        swap    d1
        ori.l   #0x40000003,d1
        move.l  d1,4(a1)
        move.l  a0,d1
        move.b  d1,d0
        subi.b  #0x20,d0
        move.w  d0,(a1)
        rts

delay:
        move.l  4(sp),d0
        add.l   0xA1201C,d0
0:
        cmp.l   0xA1201C,d0
        bgt.b   0b
        rts

set_palette:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.l  12(sp),d1
        add.w   d0,d0
        swap    d0
        ori.l   #0xC0000000,d0
        subq.w  #1,d1
        lea     0xC00000,a1
        move.w  #0x8F02,4(a1)
        move.l  d0,4(a1)
0:
        move.w  (a0)+,(a1)
        dbra    d1,0b
        rts

z80_busrequest:
        move.l  4(sp),d0
        andi.w  #0x0100,d0
        move.w  d0,0xA11100
0:
        move.w  0xA11100,d1
        and.w   d0,d1
        bne.b   0b
        rts

z80_reset:
        move.l  4(sp),d0
        andi.w  #0x0100,d0
        move.w  d0,0xA11200
        rts

z80_memclr:
        movea.l 4(sp),a1
        move.l  8(sp),d0
        subq.w  #1,d0
        moveq   #0,d1
0:
        move.b  d1,(a1)+
        dbra    d0,0b
        rts

z80_memcpy:
        movea.l 4(sp),a1
        movea.l 8(sp),a0
        move.l  12(sp),d0
        subq.w  #1,d0
0:
        move.b  (a0)+,d1
        move.b  d1,(a1)+
        dbra    d0,0b
        rts

dma_screen:
        move.l  4(sp),d0
        move.l  8(sp),d1
        movem.l d2-d7/a2-a6,-(sp)
        move.w  #0x2700,sr
        move.l  #0x93E0948C,d2
        tst.b   d1
        beq.b   0f
        move.l  #0x934094AD,d2
0:
        move.l  #0x96009500,d3
        lsr.l   #1,d0
        move.b  d0,d3
        swap    d3
        lsr.l   #8,d0
        move.b  d0,d3
        move.l  #0x81149700,d4
        lsr.l   #8,d0
        andi.w  #0x007F,d0
        move.b  d0,d4
        swap    d4
        move.l  #0xC0000080,d5
        moveq   #0,d0
        lea     0xC00000,a2
        lea     0xC00004,a3
        moveq   #31,d1
        move.l  #0xC0000000,(a3)
1:
        move.l  d0,(a2)
        dbra    d1,1b
        move.w  #0x8F00,(a3)
        cmpi.l  #0x934094AD,d2
        beq.b   dma_wide
        move.w  #0x8C00,(a3)
        bra.w   dma_narrow

dma_wide:
        moveq   #0,d0
        move.w  #0x8154,(a3)
        move.l  #0x40000000,(a3)
1:
        btst    #3,1(a3)
        beq.b   1b
2:
        btst    #3,1(a3)
        bne.b   2b

        move.l  d0,(a2)
        move.l  d0,(a2)
        move.l  d0,(a2)
        move.l  d0,(a2)
        move.l  d0,(a2)
        move.l  d0,(a2)
        move.w  d0,(a2)
        nop
        nop
        nop
        nop
        nop
        nop
        move.l  d2,(a3)
        nop
        nop
        move.l  d3,(a3)
        nop
        nop
        move.l  d4,(a3)
        nop
        nop
        move.l  d5,(a3)
        btst    #7,0xA1200F
        beq.b   4f
        bset    #1,0xA12003
3:
        btst    #1,0xA12003
        bne.b   3b
4:
        pea     0.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA12018
        pea     1.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA1201A
        move.l  0xA1201C,d0
        addq.l  #1,d0
        move.l  d0,0xA1201C
        move.w  0xA12000,d0
        ori.w   #0x0100,d0
        move.w  d0,0xA12000
        moveq   #0x7F,d0
        and.b   0xA1200F,d0
        cmpi.b  #MD_CMD_DMA_SCREEN,d0
        bne.b   exit_wide
        bra.w   dma_wide
exit_wide:
        bsr.w   md_init_hw
        movem.l (sp)+,d2-d7/a2-a6
        rts

dma_narrow:
        moveq   #0,d0
        move.w  #0x8154,(a3)
        move.l  #0x40000000,(a3)
1:
        btst    #3,1(a3)
        beq.b   1b
2:
        btst    #3,1(a3)
        bne.b   2b

        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        move.w  d0,(a2)
        nop
        nop
        nop
        nop
        nop
        nop
        move.l  d2,(a3)
        nop
        nop
        move.l  d3,(a3)
        nop
        nop
        move.l  d4,(a3)
        nop
        nop
        move.l  d5,(a3)
        btst    #7,0xA1200F
        beq.b   4f
        bset    #1,0xA12003
3:
        btst    #1,0xA12003
        bne.b   3b
4:
        pea     0.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA12018
        pea     1.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA1201A
        move.l  0xA1201C,d0
        addq.l  #1,d0
        move.l  d0,0xA1201C
        move.w  0xA12000,d0
        ori.w   #0x0100,d0
        move.w  d0,0xA12000
        moveq   #0x7F,d0
        and.b   0xA1200F,d0
        cmpi.b  #MD_CMD_DMA_SCREEN,d0
        bne.b   exit_narrow
        bra.w   dma_narrow
exit_narrow:
        bsr.w   md_init_hw
        movem.l (sp)+,d2-d7/a2-a6
        rts

init_32x:
        pea     19.l
        pea     12.l
        pea     0.l
        pea     message0(pc)
        bsr     put_str
        lea     16(sp),sp

        move.l  0xA130EC,d0
        cmpi.l  #0x4D415253,d0
        beq.b   0f
        moveq   #-2,d0
        rts
0:
        pea     20.l
        pea     12.l
        pea     0.l
        pea     message1(pc)
        bsr     put_str
        lea     16(sp),sp

        lea     0xA15000,a0
        move.l  #500000,d1
1:
        btst    #7,0x0101(a0)
        bne.b   2f
        subq.l  #1,d1
        bne.b   1b
        moveq   #-5,d0
        rts
2:
        move.w  #0x2700,sr
        move.b  #1,0x0101(a0)
        move.w  #19170,d1
1:
        dbra    d1,1b

        pea     21.l
        pea     12.l
        pea     0.l
        pea     message2(pc)
        bsr     put_str
        lea     16(sp),sp
        lea     0xA15000,a0

        moveq   #0,d0
        move.l  d0,0x0120(a0)
        move.l  d0,0x0124(a0)
        move.b  #3,0x0101(a0)
2:
        bclr    #7,0x0100(a0)
        bne.b   2b
        move.w  d0,0x0102(a0)
        move.w  d0,0x0104(a0)
        move.w  d0,0x0106(a0)
        move.l  d0,0x0108(a0)
        move.l  d0,0x010C(a0)
        move.w  d0,0x0110(a0)
        move.w  d0,0x0130(a0)
        move.w  d0,0x0132(a0)
        move.w  d0,0x0138(a0)
        move.w  d0,0x0180(a0)
        move.w  d0,0x0182(a0)
3:
        bclr    #0,0x018B(a0)
        bne.b   3b
        moveq   #-1,d1
        lea     0x840000,a1
4:
        move.w  d0,(a1)+
        dbra    d1,4b
5:
        bset    #0,0x018B(a0)
        beq.b   5b
        moveq   #-1,d1
        lea     0x840000,a1
6:
        move.w  d0,(a1)+
        dbra    d1,6b
11:
        bclr    #0,0x018B(a0)
        bne.b   11b

        moveq   #0x7F,d1
        lea     0xA15200,a1
7:
        move.l  d0,(a1)+
        dbra    d1,7b

        pea     22.l
        pea     12.l
        pea     0.l
        pea     message3(pc)
        bsr     put_str
        lea     16(sp),sp
        lea     0xA15000,a0

        move.l  0x0120(a0),d0
        cmpi.l  #0x53444552,d0
        bne.b   8f
        move.w  #0x2000,sr
        moveq   #-3,d0
        rts
8:
        pea     23.l
        pea     12.l
        pea     0.l
        pea     message4(pc)
        bsr     put_str
        lea     16(sp),sp
        lea     0xA15000,a0

        movea.l 4(sp),a0
        move.l  8(sp),d0
        lea     0x840000,a1
9:
        move.l  (a0)+,(a1)+
        subq.l  #4,d0
        bgt.b   9b

        pea     24.l
        pea     12.l
        pea     0.l
        pea     0x840000.l
        bsr     put_str
        lea     16(sp),sp

        lea     0xA15000,a0
10:
        bset    #7,0x0100(a0)
        beq.b   10b

        move.l  #0x5F43445F,0x0120(a0)

        pea     25.l
        pea     12.l
        pea     0.l
        pea     message5(pc)
        bsr     put_str
        lea     16(sp),sp
        lea     0xA15000,a0

        clr.w   -(sp)
        move.l  #1000000,d1
0:
        move.l  0x0120(a0),d0
        cmpi.l  #0x4D5F4F4B,d0
        beq.b   1f
        subq.l  #1,d1
        bne.b   0b
        ori.w   #1,(sp)
1:
        move.l  #250000,d1
2:
        move.l  0x0124(a0),d0
        cmpi.l  #0x535F4F4B,d0
        beq.b   3f
        subq.l  #1,d1
        bne.b   2b
        ori.w   #2,(sp)
3:
        pea     26.l
        pea     12.l
        pea     0.l
        pea     message6(pc)
        bsr     put_str
        lea     16(sp),sp
        lea     0xA15000,a0

        moveq   #0,d7
        moveq   #0,d0
        move.l  d0,0x0120(a0)
        move.l  d0,0x0124(a0)

        move.w  #0x2000,sr
        moveq   #0,d0
        move.w  (sp)+,d0
        rts
15:
        addq.l  #2,sp
        move.l  d0,0xA12018
        move.w  #0x2000,sr
        moveq   #-3,d0
        rts

read_32x_16:
        movea.l 4(sp),a0
        moveq   #0,d0
        move.w  (a0),d0
        rts

write_32x_16:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.w  d0,(a0)
        rts

read_32x_32:
        movea.l 4(sp),a0
        move.l  (a0),d0
        rts

write_32x_32:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.l  d0,(a0)
        rts

copy_to_cart:
        movea.l 4(sp),a0
        movea.l 8(sp),a1
        move.l  12(sp),d0
        subq.l  #1,d0
        bmi.b   2f
1:
        move.b  (a0)+,(a1)
        addq.l  #2,a1
        dbra    d0,1b
2:
        moveq   #0,d0
        rts

safe_cart_xfer:
        movem.l d2-d3/a2,-(sp)
        movea.l 16(sp),a2
        move.l  20(sp),d2
        move.l  24(sp),d3

        move.w  sr,-(sp)
        move.w  #0x2700,sr

        bset    #0,0xA15107

        movea.l a2,a0
        movea.l d2,a1
        move.l  d3,d0
        beq.b   3f
2:
        move.b  (a0)+,(a1)+
        subq.l  #1,d0
        bne.b   2b
3:
        bclr    #0,0xA15107

        move.w  (sp)+,sr

        moveq   #0,d0
        movem.l (sp)+,d2-d3/a2
        rts

safe_cart_xfer_w:
        movem.l d2-d3/a2,-(sp)
        movea.l 16(sp),a2
        move.l  20(sp),d2
        move.l  24(sp),d3

        move.w  sr,-(sp)
        move.w  #0x2700,sr
        bset    #0,0xA15107

        movea.l a2,a0
        movea.l d2,a1
        move.l  d3,d0
        ble.b   3f
2:
        move.w  (a0)+,(a1)+
        subq.l  #2,d0
        bgt.b   2b
3:
        bclr    #0,0xA15107
        move.w  (sp)+,sr
        moveq   #0,d0
        movem.l (sp)+,d2-d3/a2
        rts

bram_put:
        movem.l d2-d3/a2,-(sp)
        movea.l 16(sp),a2
        move.l  20(sp),d2
        move.l  24(sp),d3
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        bset    #0,0xA15107
        movea.l a2,a0
        movea.l d2,a1
        move.l  d3,d0
        ble.b   3f
2:
        move.b  (a0)+,(a1)
        addq.l  #2,a1
        subq.l  #1,d0
        bne.b   2b
3:
        bclr    #0,0xA15107
        move.w  (sp)+,sr
        moveq   #0,d0
        movem.l (sp)+,d2-d3/a2
        rts

bram_get:
        movem.l d2-d3/a2,-(sp)
        movea.l 16(sp),a2
        move.l  20(sp),d2
        move.l  24(sp),d3
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        bset    #0,0xA15107
        movea.l a2,a0
        movea.l d2,a1
        move.l  d3,d0
        ble.b   3f
2:
        move.b  (a0),(a1)+
        addq.l  #2,a0
        subq.l  #1,d0
        bne.b   2b
3:
        bclr    #0,0xA15107
        move.w  (sp)+,sr
        moveq   #0,d0
        movem.l (sp)+,d2-d3/a2
        rts

bram_to_fb:
        movem.l d2-d3/a2,-(sp)
        movea.l 16(sp),a2
        move.l  20(sp),d2
        move.l  24(sp),d3
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        bset    #0,0xA15107
        movea.l a2,a0
        movea.l d2,a1
        move.l  d3,d0
        lsr.l   #1,d0
        beq.b   3f
        moveq   #0,d1
2:
        move.b  (a0),d1
        addq.l  #2,a0
        lsl.w   #8,d1
        move.b  (a0),d1
        addq.l  #2,a0
        ori.w   #0x8000,d1
        move.w  d1,(a1)+
        subq.l  #1,d0
        bne.b   2b
3:
        bclr    #0,0xA15107
        move.w  (sp)+,sr
        moveq   #0,d0
        movem.l (sp)+,d2-d3/a2
        rts

copy_from_cart:
        movea.l 4(sp),a0
        movea.l 8(sp),a1
        move.l  12(sp),d0
        lsr.l   #1,d0
        beq.b   2f
        moveq   #0,d1
1:
        move.b  (a0),d1
        addq.l  #2,a0
        lsl.w   #8,d1
        move.b  (a0),d1
        addq.l  #2,a0
        ori.w   #0x8000,d1
        move.w  d1,(a1)+
        subq.l  #1,d0
        bne.b   1b
2:
        moveq   #0,d0
        rts

copy_words:
        movea.l 4(sp),a0
        movea.l 8(sp),a1
        move.l  12(sp),d0
        lsr.l   #1,d0
        beq.b   2f
        subq.l  #1,d0
        move.l  16(sp),d1
        tst.l   d1
        bne.b   3f
1:
        move.w  (a0)+,(a1)+
        dbra    d0,1b
2:
        moveq   #0,d0
        rts
3:
        move.w  (a0)+,d1
        or.w    #0x8000,d1
        move.w  d1,(a1)+
        dbra    d0,3b
        moveq   #0,d0
        rts

message0:
        .asciz  "Init 32X V2.9"
message1:
        .asciz  "Found 32X"
message2:
        .asciz  "Activated 32X"
message3:
        .asciz  "Cleared 32X"
message4:
        .asciz  "32X Waiting"
message5:
        .asciz  "32X Handshake"
message6:
        .asciz  "32X GO!"

        .align  4

vblank_int:
        movem.l d0-d1/a0-a1,-(sp)

        pea     0.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA12018

        pea     1.w
        bsr.w   get_pad
        addq.l  #4,sp
        move.w  d0,0xA1201A

        move.l  0xA1201C,d0
        addq.l  #1,d0
        move.l  d0,0xA1201C
        btst    #0,0xA15101             /* HW7: only once the 32X is on (ADEN bit) */
        beq.b   1f
        move.w  d0,0xA15126             /* HW7: 68K heartbeat -> 32X COMM6 (SH2 reads it 1x/s) */
1:

        move.w  0xA12000,d0
        ori.w   #0x0100,d0
        move.w  d0,0xA12000

        movem.l (sp)+,d0-d1/a0-a1
        rte

chilly_memcpy:
        movea.l 4(sp),a1
        movea.l 8(sp),a0
        move.l  12(sp),d0
0:
        move.b  (a0)+,d1
        move.b  d1,(a1)+
        subi.l  #1,d0
        bgt.b   0b
        moveq.l #0,d0
        rts

chilly_rv_memcpy:
        bset    #0,0xA15107
        movea.l 4(sp),a1
        movea.l 8(sp),a0
        move.l  12(sp),d0
0:
        move.b  (a0)+,d1
        move.b  d1,(a1)+
        subi.l  #1,d0
        bgt.b   0b
        bclr    #0,0xA15107
        moveq.l #0,d0
        rts

| =============================================================================
| 68K EXCEPTION TRAP (V71, restored in K29) - DIAGNOSTIC, changes nothing unless the 68K
| faults.  Hooks vectors 2..11 (bus, address, illegal, div0, CHK, TRAPV, privilege, trace,
| line-A, line-F): where a vector points into RAM (the BIOS jump table), "jmp <our stub>" is
| written there; vectors pointing to ROM are left alone.  Returns how many were hooked.
| On a fault: row 21 "68K EXC Vnn  FRAME:", row 22 = first 7 words of the exception frame
|   bus/address error (V02/V03): FC  ADDR-HI ADDR-LO  IR  SR  PC-HI PC-LO
|   all others:                  SR  PC-HI PC-LO  (rest = stack contents)
| then the 68K stops (the message stays on screen).  Needs the Genesis display ON to be seen.
| =============================================================================
install_exc:
        movem.l d2/a2-a3,-(sp)
        lea     exc_stubs(pc),a3
        moveq   #0,d1                   /* hooked count */
        moveq   #2,d2                   /* vector number 2..11 */
1:
        move.l  d2,d0
        lsl.l   #2,d0
        movea.l d0,a0
        movea.l (a0),a2                 /* where this vector goes */
        cmpa.l  #0xFF0000,a2
        blo.b   2f                      /* not RAM - leave it */
        move.w  #0x4EF9,(a2)            /* jmp abs.l */
        move.l  a3,2(a2)                /*     -> our stub */
        addq.l  #1,d1
2:
        addq.l  #8,a3                   /* next stub (8 bytes each) */
        addq.l  #1,d2
        cmpi.l  #12,d2
        bne.b   1b
        move.l  d1,d0
        movem.l (sp)+,d2/a2-a3
        rts

        .align  2
exc_stubs:                              /* each: move.w #vec,-(sp) / bra.w exc_common = 8 bytes */
        move.w  #2,-(sp)
        bra.w   exc_common
        move.w  #3,-(sp)
        bra.w   exc_common
        move.w  #4,-(sp)
        bra.w   exc_common
        move.w  #5,-(sp)
        bra.w   exc_common
        move.w  #6,-(sp)
        bra.w   exc_common
        move.w  #7,-(sp)
        bra.w   exc_common
        move.w  #8,-(sp)
        bra.w   exc_common
        move.w  #9,-(sp)
        bra.w   exc_common
        move.w  #10,-(sp)
        bra.w   exc_common
        move.w  #11,-(sp)
        bra.w   exc_common

exc_common:
        move.w  #0x2700,sr
        move.w  (sp)+,d6                /* vector number */
        movea.l sp,a5                   /* exception frame */
        move.w  #0x8174,0xC00004        /* K29: Genesis display ON so the message shows */
        lea     exc_buf(pc),a0
        lea     exc_txt1(pc),a1
3:      move.b  (a1)+,(a0)+             /* "68K EXC V" */
        bne.b   3b
        subq.l  #1,a0
        move.l  d6,d0
        moveq   #1,d1                   /* 2 hex digits */
        bsr     exc_hex
        lea     exc_txt2(pc),a1
4:      move.b  (a1)+,(a0)+
        bne.b   4b
        pea     21.l
        pea     2.l
        pea     0x4000.l                /* red */
        pea     exc_buf(pc)
        bsr     put_str
        lea     16(sp),sp

        lea     exc_buf(pc),a0
        moveq   #6,d5                   /* 7 words */
5:      move.w  (a5)+,d0
        moveq   #3,d1                   /* 4 hex digits */
        bsr     exc_hex
        move.b  #' ',(a0)+
        dbra    d5,5b
        clr.b   (a0)
        pea     22.l
        pea     2.l
        pea     0x4000.l
        pea     exc_buf(pc)
        bsr     put_str
        lea     16(sp),sp
6:      bra.b   6b                      /* stop here - leave the message on screen */

| d0 = value, d1 = digits-1 (low digits of d0), a0 = output -> advanced
exc_hex:
        move.l  d1,d3
        addq.l  #1,d3
        lsl.l   #2,d3                   /* bits to print */
        ror.l   d3,d0                   /* bring them to the top */
7:      rol.l   #4,d0
        move.b  d0,d4
        andi.b  #15,d4
        addi.b  #'0',d4
        cmpi.b  #'9',d4
        bls.b   8f
        addq.b  #7,d4
8:      move.b  d4,(a0)+
        dbra    d1,7b
        rts

exc_txt1:
        .asciz  "68K EXC V"
exc_txt2:
        .asciz  "  FRAME:                    "
        .align  2
exc_buf:
        .space  48

        .align  4
md_init_end:
