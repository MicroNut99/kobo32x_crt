| ===========================================================================================
| Kobo Deluxe 32X - CARTRIDGE version - Genesis (68000) side.          (cart/cart_md.s)
|
| Linked at 0x880800 = ROM offset 0x800, seen through the 32X's fixed cartridge window.  The
| 32X vector jump list at ROM 0x200 (part of the standard boot block) sends the 68000 to fixed
| spots here (the same layout as D32XR's crt0):
|     0x880800 reset / start    0x880840 exception    0x880880 H-blank
|     0x8808C0 V-blank          0x880900 external interrupt
|
| Its job is what the Sega CD Sub-CPU + 68000 listener did in the CD version: answer Kobo's
| commands in 32X COMM0 (written by the SH2, cleared here when done).
|     48  pad          COMM8 = pad bits, COMM10 = 68000 frame counter (Kobo's clock)
|     52  text on/off  COMM2 = 1 on / 0 off (Genesis display; Kobo keeps it off)
|     53  sound pack   COMM12 = 0: no sound effects        (C2: Sega CD PCM)
|     56/59 backup RAM COMM12 = 0: "NO BACKUP RAM"         (C2: Sega CD backup RAM)
|     57  BRAM word    COMM8 = 0
|     45/54/58 and anything else: acknowledged             (C2: CD music)
| Rules kept from the hardware work: COMM0 is polled with a pause between reads (never
| hammered while the SH2 writes it), and read twice - acted on only if both reads match.
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
        | ---- Genesis video: display OFF, V-blank interrupt ON (Kobo is all 32X picture) ----
        lea     VDP_CTRL,a0
        move.w  #0x8004,(a0)
        move.w  #0x8134,(a0)                    | display off, V-int on, mode 5
        move.w  #0x8C81,(a0)                    | H40
        move.w  #0x8F02,(a0)                    | auto-increment 2
        move.w  #0x8700,(a0)                    | backdrop = colour 0
        move.l  #0xC0000000,(a0)
        move.w  #0x0000,VDP_DATA                | colour 0 = black

        | ---- controller ports ----
        move.b  #0x40,0xA10009
        move.b  #0x40,0xA1000B
        move.b  #0x40,0xA10003
        move.b  #0x40,0xA10005

        clr.w   ticks
        move.w  #0,pad1
        | ---- C2: C runtime (scd.c): copy .data from ROM to RAM, clear .bss ----
        lea     _data_rom,a0
        lea     _data_start,a1
        lea     _data_end,a2
6:      cmpa.l  a2,a1
        bhs.b   7f
        move.b  (a0)+,(a1)+
        bra.b   6b
7:      lea     _bss_start,a1
        lea     _bss_end,a2
8:      cmpa.l  a2,a1
        bhs.b   9f
        clr.b   (a1)+
        bra.b   8b
9:      | ---- C2: wake the Sega CD (D32XR InitCD: Sub-CPU BIOS + Sub-CPU program).  Interrupts
        |      on first: the Sub-CPU BIOS needs level-2 interrupts from our V-blank. ----
        move.w  #0x2000,sr
        jsr     InitCD
        move.w  d0,cd_ok                        | 0 = no Sega CD -> stay silent
        move.w  #0x2700,sr

        | ---- 32X: wait for both SH2s (the boot ROM leaves "M_OK" / "S_OK"), then clear them -
        |      Kobo's SH2 start-up waits until COMM0 is no longer "M_OK" ----
1:      cmpi.l  #0x4D5F4F4B,COMM0               | "M_OK"
        bne.b   1b
2:      cmpi.l  #0x535F4F4B,COMM4               | "S_OK"
        bne.b   2b
        move.b  #0,0xA15107                     | RV off
        move.w  #0,0xA15104                     | cartridge bank 0
        move.l  #0,COMM0
        move.l  #0,COMM4

        move.w  #0x2000,sr                      | V-blank interrupts on

        | ---- Kobo's start-up handshake: SH2 writes 0x0AAA to COMM10, we answer 0x0BBB ----
3:      bsr     pause
        cmpi.w  #0x0AAA,COMM10
        bne.b   3b
        move.w  #0x0BBB,COMM10

| ---- command loop ---------------------------------------------------------------------------
loop:
        bsr     pause
        move.w  COMM0,d0
        beq.b   loop
        cmp.w   COMM0,d0                        | read twice - a torn read never matches
        bne.b   loop
        moveq   #0,d1
        move.b  d0,d1                           | low byte = command (high byte: effect id)
        cmpi.b  #48,d1
        beq.b   c_pad
        cmpi.b  #52,d1
        beq.b   c_text
        cmpi.b  #53,d1
        beq.b   c_zero12
        cmpi.b  #56,d1
        beq.b   c_zero12
        cmpi.b  #59,d1
        beq.b   c_zero12
        cmpi.b  #57,d1
        beq.b   c_zero8
        cmpi.b  #45,d1
        beq.b   c_music
        cmpi.b  #54,d1
        beq.b   c_stop
        bra.b   done                            | 58, anything else: acknowledge

c_pad:
        move.w  pad1,d2
        swap    d2
        move.w  ticks,d2
        move.l  d2,COMM8                        | COMM8 = pad, COMM10 = frame counter
        bra.b   done
c_text:
        move.w  #0x8134,d2                      | off
        tst.w   COMM2
        beq.b   4f
        move.w  #0x8174,d2                      | on
4:      move.w  d2,VDP_CTRL
        bra.b   done
c_zero12:
        move.w  #0,COMM12                       | 0 = not available (no Sega CD yet)
        bra.b   done
c_music:                                        | C2: CD audio track COMM2, repeating
        tst.w   cd_ok
        beq.b   done
        move.l  #1,-(sp)
        moveq   #0,d0
        move.w  COMM2,d0
        move.l  d0,-(sp)
        jsr     scd_play_cdda_track
        addq.l  #8,sp
        bra.b   done
c_stop:
        tst.w   cd_ok
        beq.b   done
        jsr     scd_stop_cdda_playback
        bra.b   done
c_zero8:
        move.w  #0,COMM8
done:
        move.w  #0,COMM0                        | command finished
        bra     loop

pause:                                          | ~0.25 ms between COMM0 reads
        move.w  #200,d3
5:      dbra    d3,5b
        rts

| ---- V-blank: read pad 1 (3/6-button, same routine as the CD version), count frames ---------
vblank_handler:
        movem.l d0-d2/a0,-(sp)
        lea     0xA10003,a0
        bsr.b   read_pad
        move.w  d0,pad1
        addq.w  #1,ticks
        tst.w   gen_lvl2                        | C2: Sub-CPU BIOS needs level-2 interrupts
        beq.b   1f
        move.w  0xA12000,d0
        ori.w   #0x0100,d0
        move.w  d0,0xA12000
1:
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

| ---- C2: helpers and variables used by D32XR's scd.c ---------------------------------------
        .global write_byte, write_word, write_long, read_byte, read_word, read_long
write_byte:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.b  d0,(a0)
        rts
write_word:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.w  d0,(a0)
        rts
write_long:
        movea.l 4(sp),a0
        move.l  8(sp),(a0)
        rts
read_byte:
        movea.l 4(sp),a0
        moveq   #0,d0
        move.b  (a0),d0
        rts
read_word:
        movea.l 4(sp),a0
        moveq   #0,d0
        move.w  (a0),d0
        rts
read_long:
        movea.l 4(sp),a0
        move.l  (a0),d0
        rts

        .global Sub_Start, Sub_End              | D32XR's Sub-CPU program (src-md/cd/cd.bin)
        .align  2
Sub_Start:
        .incbin "cd.bin"
Sub_End:

        .bss
        .align  2
        .global gen_lvl2, vblank_vector
gen_lvl2:       .space  2
cd_ok:          .space  2
vblank_vector:  .space  4

        .text
        .global mystrlen, bump_fm, scd_init_pcm
mystrlen:
        movea.l 4(sp),a0
        moveq   #0,d0
10:     tst.b   (a0)+
        beq.b   11f
        addq.l  #1,d0
        bra.b   10b
11:     rts
bump_fm:
        rts
scd_init_pcm:                                   | D32XR scd_pcm.c: initialise the PCM driver -> silent chip
        move.l  #0x49,-(sp)                     | wait_do_cmd('I')
        jsr     wait_do_cmd
        addq.l  #4,sp
        jsr     wait_cmd_ack
        move.b  #0,0xA1200E                     | acknowledge the result
        rts
