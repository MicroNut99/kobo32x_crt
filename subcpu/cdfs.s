| cdfs.s - Sub-CPU ISO9660 / CD-BIOS routines
| Extracted VERBATIM from crt.s (lines 543-1098 and the .bss block). Nothing edited.
| Only removed: ".global _start / _start:" (crt0.s already defines _start).

        .text

| ISO directory offsets (big-endian where applicable)
        .equ    RECORD_LENGTH,  0
        .equ    EXTENT,         6
        .equ    FILE_LENGTH,    14
        .equ    FILE_FLAGS,     25
        .equ    FILE_NAME_LEN,  32
        .equ    FILE_NAME,      33

| Primary Volume Descriptor offset
        .equ    PVD_ROOT, 0x9C

| CDFS Error codes
        .equ    ERR_READ_FAILED,    -2
        .equ    ERR_NO_PVD,         -3
        .equ    ERR_NO_MORE_ENTRIES,-4
        .equ    ERR_BAD_ENTRY,      -5
        .equ    ERR_NAME_NOT_FOUND, -6
        .equ    ERR_NO_DISC,        -7

| XCMD Error codes
        .equ    ERR_UNKNOWN_CMD,    -1
        .equ    ERR_CMDDONE_TIMEOUT,-2
        .equ    ERR_CMDACK_TIMEOUT, -3

|-----------------------------------------------------------------------
| Global functions
|-----------------------------------------------------------------------

| int init_cd(void);
        .global init_cd
init_cd:
        movem.l d2-d7/a2-a6,-(sp)
        lea     iso_pvd_magic,a5
        jsr     InitCD
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int seek_cd(int lba);
        .global seek_cd
seek_cd:
        move.l  4(sp),d0                /* lba */
        movem.l d2-d7/a2-a6,-(sp)
        jsr     SeekCD
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int begin_read_cd(int lba, int len);
        .global begin_read_cd
begin_read_cd:
        move.l  4(sp),d0                /* lba */
        move.l  8(sp),d1                /* length */
        movem.l d2-d7/a2-a6,-(sp)
        jsr     BeginReadCD
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int dma_cd_sector_pcm(void *buffer);
        .global dma_cd_sector_pcm
dma_cd_sector_pcm:
        move.w  #0x4,d0                 /* set CDC Mode destination device to PCM DMA */
        movea.l 4(sp),a0                /* buffer */
        movem.l d2-d7/a2-a6,-(sp)
        jsr     ReadSectorDMA
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int dma_cd_sector_prg(void *buffer);
        .global dma_cd_sector_prg
dma_cd_sector_prg:
        move.w  #0x5,d0                 /* set CDC Mode destination device to PRG RAM DMA */
        movea.l 4(sp),a0                /* buffer */
        movem.l d2-d7/a2-a6,-(sp)
        jsr     ReadSectorDMA
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int dma_cd_sector_wram(void *buffer);
        .global dma_cd_sector_wram
dma_cd_sector_wram:
        move.w  #0x7,d0                 /* set CDC Mode destination device to PRG RAM DMA */
        movea.l 4(sp),a0                /* buffer */
        movem.l d2-d7/a2-a6,-(sp)
        jsr     ReadSectorDMA
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int stop_read_cd();
        .global stop_read_cd
stop_read_cd:
        move.l  4(sp),d0                /* lba */
        move.l  8(sp),d1                /* length */
        movem.l d2-d7/a2-a6,-(sp)
        jsr     StopReadCD
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int set_cwd(char *path);
        .global set_cwd
set_cwd:
        movea.l 4(sp),a0                /* path */
        movem.l d2-d7/a2-a6,-(sp)
        jsr     SetCWD
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int first_dir_sec(void);
        .global first_dir_sec
first_dir_sec:
        movem.l d2-d7/a2-a6,-(sp)
        jsr     FirstDirSector
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int next_dir_sec(void);
        .global next_dir_sec
next_dir_sec:
        movem.l d2-d7/a2-a6,-(sp)
        jsr     NextDirSector
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int find_dir_entry(char *name);
        .global find_dir_entry
find_dir_entry:
        movea.l 4(sp),a0
        movem.l d2-d7/a2-a6,-(sp)
        jsr     FindDirEntry
        movem.l (sp)+,d2-d7/a2-a6
        rts

| int next_dir_entry(void)
        .global next_dir_entry
next_dir_entry:
        movem.l d2-d7/a2-a6,-(sp)
        jsr     NextDirEntry
        movem.l (sp)+,d2-d7/a2-a6
        rts

|----------------------------------------------------------------------|
|                      File System Support Code                        |
|----------------------------------------------------------------------|

| Initialize CD - pass PVD Magic to look for in a5

InitCD:
        /* ##################################################################
           V70 DRIVE START - WHY THIS CODE LOOKS LIKE THIS
           OLD: DRVINIT sent without checking the BIOS, then one blind wait of up to
                ~18 s for the track list.  On some boots the list never came in:
                that wait WAS the long start-up delay, and because the list was still
                missing (TOC answer 00020000) "play track 2" played silence.
           CAUSE (same as the old music bug fixed by cdda_start): a CD BIOS command
                sent while the BIOS is still busy can be dropped.  crt.s already
                talked to the drive during Sub-CPU start-up, so DRVINIT could arrive
                while the BIOS was busy and be lost.
           NOW: 1. wait for BIOS-ready (CDBSTAT status word bit 15 = 0) before DRVINIT
                2. wait up to ~3 s (180 vblank ticks) for the track list
                3. not in yet -> send DRVINIT again, up to 3 times
                cd_drvinit_tries (shown on row 1 by main.c) = how many sends it took.
           Ticks = MD vblank counter the Sub reads at 0xFF801C (main.c GET_TICKS).
           Each wait also has a pass limit in case ticks ever stand still.
           DO NOT replace this with a fixed delay.  DO NOT touch audio here.
           ################################################################## */
        clr.w   cd_drvinit_tries
        moveq   #3,d6                   /* DRVINIT attempts */
10:
        addq.w  #1,cd_drvinit_tries
        bsr     WaitBiosReady           /* 1. BIOS idle first */
        lea     drive_init_parms,a0
        move.w  #0x0010,d0              /* DRVINIT */
        jsr     0x5F22.w                /* call CDBIOS function */

        move.l  0xFF801C,d5             /* 2. track list: start tick */
        move.l  #70000,d7               /*    pass limit (~6 s) if ticks stand still */
5:
        moveq   #2,d1                   /* track 2 */
        move.w  #0x0083,d0              /* CDBTOCREAD */
        jsr     0x5F22.w                /* call CDBIOS function */
        cmpi.b  #2,d0                   /* last byte = track number: 02 = list is in */
        beq.b   6f
        move.l  0xFF801C,d0
        sub.l   d5,d0
        cmpi.l  #180,d0                 /* ~3 s */
        bhs.b   7f
        subq.l  #1,d7
        bne.b   5b
7:
        subq.w  #1,d6                   /* 3. list not in: send DRVINIT again */
        bne.b   10b
6:

        move.w  #0,d1                   /* Mode 1 (CD-ROM with full error correction) */
        move.w  #0x0096,d0              /* CDCSETMODE */
        jsr     0x5F22.w                /* call CDBIOS function */

        moveq   #-1,d0
        move.l  d0,ROOT_OFFSET
        move.l  d0,ROOT_LENGTH
        move.l  d0,CURR_OFFSET
        move.l  d0,CURR_LENGTH
        move.w  d0,DISC_TYPE            /* no disc/not recognized */

| find Primary Volume Descriptor

        moveq   #16,d2                  /* starting sector when searching for PVD */
0:
        lea     DISC_BUFFER,a0          /* buffer */
        move.l  d2,d0                   /* sector */
        moveq   #1,d1                   /* # sectors */
        move.w  d2,-(sp)
        bsr     ReadSectorsSUB
        move.w  (sp)+,d2
        tst.l   d0
        bmi.b   9f                      /* error */
        lea     DISC_BUFFER,a0
        movea.l a5,a1                   /* PVD magic */
        cmpm.l  (a0)+,(a1)+
        bne.b   1f                      /* next sector */
        cmpm.l  (a0)+,(a1)+
        bne.b   1f                      /* next sector */
        /* found PVD */
        move.l  DISC_BUFFER+PVD_ROOT+EXTENT,ROOT_OFFSET
        move.l  DISC_BUFFER+PVD_ROOT+FILE_LENGTH,ROOT_LENGTH
        move.w  #0,DISC_TYPE            /* found PVD */
        moveq   #0,d0
        rts
1:
        addq.w  #1,d2
        cmpi.w  #32,d2
        bne.b   0b                      /* check next sector */

| No PVD found

        moveq   #ERR_NO_PVD,d0
9:
        rts

| V70: wait until the CD BIOS is idle (CDBSTAT: status word bit 15 = 1 means busy/not ready,
| same test SPMain in crt.s and cdda_start in main.c use).  Gives up after ~2 s (120 ticks)
| or a pass limit, then lets the caller send anyway.  Uses d3/d4 (init_cd saves d2-d7).
WaitBiosReady:
        move.l  0xFF801C,d3
        move.l  #70000,d4
1:
        move.w  #0x0081,d0              /* CDBSTAT -> a0 = status table */
        jsr     0x5F22.w
        tst.w   (a0)                    /* bit 15 set = not ready */
        bpl.b   2f
        move.l  0xFF801C,d0
        sub.l   d3,d0
        cmpi.l  #120,d0
        bhs.b   2f
        subq.l  #1,d4
        bne.b   1b
2:
        rts

| Set directory entry variables to next entry of directory in disc buffer

NextDirEntry:
        lea     DISC_BUFFER,a0
        move.w  DIR_ENTRY,d2
        cmpi.w  #2048,d2
        blo.b   1f
        moveq   #ERR_NO_MORE_ENTRIES,d0
        rts
1:
        tst.b   (a0,d2.w)               /* record length */
        bne.b   2f
        move.w  #2048,DIR_ENTRY
        moveq   #ERR_NO_MORE_ENTRIES,d0
        rts
2:
        lea     (a0,d2.w),a1            /* entry */
        moveq   #0,d0
        move.b  (a1),d0                 /* record length */
        add.w   d0,d2
        move.w  d2,DIR_ENTRY            /* next entry */
        cmpi.w  #2048,d2
        bls.b   3f
        moveq   #ERR_NO_MORE_ENTRIES,d0 /* entries should NEVER cross a sector boundary */
        rts
3:
        tst.b   FILE_NAME_LEN(a1)
        bne.b   4f
        moveq   #ERR_BAD_ENTRY,d0
        rts
4:
        move.b  FILE_NAME_LEN(a1),d0
        subq.w  #1,d0
        lea     FILE_NAME(a1),a2
        lea     DENTRY_NAME,a3
5:
        move.b  (a2)+,(a3)+
        dbeq    d0,5b
        move.b  #0,(a3)                 /* make sure is null-terminated */

        lea     DENTRY_NAME,a2
        /* check for special case 0 */
        cmpi.b  #0,(a2)
        bne.b   9f
        move.l  #0x2E000000,(a2)        /* "." */
        bra.b   10f
9:
        /* check for special case 1 */
        cmpi.b  #1,(a2)
        bne.b   10f
        move.l  #0x2E2E0000,(a2)        /* ".." */
10:
        cmpi.b  #0x3B,(a2)+             /* look for ";" */
        beq.b   11f
        tst.b   (a2)
        bne     10b
        bra.b   12f
11:
        move.b  #0,-(a2)                /* apply Rockridge correction to name */
12:
        move.l  EXTENT(a1),DENTRY_OFFSET
        move.l  FILE_LENGTH(a1),DENTRY_LENGTH
        move.b  FILE_FLAGS(a1),DENTRY_FLAGS

        moveq   #0,d0
        rts

| Find entry in directory in the disc buffer using name in a0

FindDirEntry:
        move.w  DIR_ENTRY,d0
        cmpi.w  #2048,d0
        blo.b   1f
0:
        moveq   #ERR_NAME_NOT_FOUND,d0
        rts
1:
        move.l  a0,-(sp)
        bsr     NextDirEntry
        movea.l (sp)+,a0
        bmi.b   FindDirEntry
| got an entry, check the name
        lea     DENTRY_NAME,a1
        movea.l a0,a2
2:
        cmpm.b  (a1)+,(a2)+
        bne.b   FindDirEntry
        tst.b   -1(a1)
        bne.b   2b
        /* dentry holds match */
        moveq   #0,d0
        rts

| Read first sector in CWD

FirstDirSector:
        move.l  CWD_OFFSET,d0
        cmp.l   CURR_OFFSET,d0
        beq.b   0f                      /* already loaded, just reset length */
        moveq   #1,d1
        lea     DISC_BUFFER,a0          /* buffer */
        bsr     ReadSectorsSUB
        bmi.b   1f
        /* disc buffer holds first sector of dir */
        move.l  CWD_OFFSET,CURR_OFFSET
0:
        clr.l   CURR_LENGTH
        clr.w   DIR_ENTRY
        moveq   #0,d0
1:
        rts

| Read next sector in CWD

NextDirSector:
        addq.l  #1,CURR_OFFSET
        addi.l  #2048,CURR_LENGTH
        move.l  CWD_LENGTH,d0
        cmp.l   CURR_LENGTH,d0
        bhi.b   0f
        moveq   #ERR_NO_MORE_ENTRIES,d0
        rts
0:
        move.l  CURR_OFFSET,d0
        moveq   #1,d1
        lea     DISC_BUFFER,a0          /* buffer */
        bsr     ReadSectorsSUB
        bmi     1f
        /* disc buffer holds next sector of dir */
        clr.w   DIR_ENTRY
        moveq   #0,d0
1:
        rts

| Set current working directory using path at a0

SetCWD:
        jsr     S_PauseSPCMTrack

        cmpi.b  #0x2F,(a0)              /* check for leading "/" */
        bne.b   0f                      /* relative to cwd */
        /* start at root dir */
        addq.l  #1,a0                   /* skip over "/" */
        move.l  ROOT_OFFSET,CWD_OFFSET
        move.l  ROOT_LENGTH,CWD_LENGTH
0:
        move.l  a0,-(sp)
        bsr     FirstDirSector          /* disc buffer holds first sector of dir */
        movea.l (sp)+,a0
        bmi.b   2f
        /* check if done */
        tst.b   (a0)
        bne.b   3f
1:
        moveq   #0,d0
2:
        rts
3:
        tst.b   (a0)
        beq.b   1b                      /* done */

        /* copy next part of path to temp */
        lea     TEMP_NAME,a1
4:
        move.b  (a0)+,(a1)+
        beq.b   5f
        cmpi.b  #0x2F,-1(a0)            /* check for "/" */
        bne.b   4b
5:
        clr.b   -1(a1)                  /* null terminate string in temp */
        subq.l  #1,a0
6:
        /* check current directory sector for entry */
        move.l  a0,-(sp)
        lea     TEMP_NAME,a0
        bsr     FindDirEntry
        movea.l (sp)+,a0
        bmi.b   7f
        /* found this part of path */
        move.l  DENTRY_OFFSET,CWD_OFFSET
        move.l  DENTRY_LENGTH,CWD_LENGTH
        bra.b   0b                      /* read first sector of dir and check if done */
7:
        /* not found, try next sector */
        move.l  a0,-(sp)
        bsr     NextDirSector
        movea.l (sp)+,a0
        beq.b   6b
        moveq   #ERR_NAME_NOT_FOUND,d0
        rts

| Seek to the designated logical sector

SeekCD:
        movem.l d0-d1/a0-a1,-(sp)
0:
        movea.l sp,a0                   /* ptr to 32 bit sector start */
        move.w  #0x0018,d0              /* ROMSEEK */
        jsr     0x5F22.w                /* call CDBIOS function */

        lea     16(sp),sp               /* cleanup stack */
        rts

| Begin reading d1 sectors starting at d0

BeginReadCD:
        movem.l d0-d1/a0-a1,-(sp)
0:
        move.w  #0x0089,d0              /* CDCSTOP */
        jsr     0x5F22.w                /* call CDBIOS function */

        movea.l sp,a0                   /* ptr to 32 bit sector start and 32 bit sector count */
        move.w  #0x0020,d0              /* ROMREADN */
        jsr     0x5F22.w                /* call CDBIOS function */

        lea     16(sp),sp               /* cleanup stack */
        rts

| Read d1 sectors starting at d0 into buffer in a0 (using Sub-CPU)

        .global ReadSectorsSUB
ReadSectorsSUB:
        movem.l d0-d1/a0-a1,-(sp)
0:
        move.w  #0x0089,d0              /* CDCSTOP */
        jsr     0x5F22.w                /* call CDBIOS function */

        movea.l sp,a0                   /* ptr to 32 bit sector start and 32 bit sector count */
        move.w  #0x0020,d0              /* ROMREADN */
        jsr     0x5F22.w                /* call CDBIOS function */
1:
        move.w  #0x008A,d0              /* CDCSTAT */
        jsr     0x5F22.w                /* call CDBIOS function */
        bcs.b   1b                      /* no sectors in CD buffer */

        /* set CDC Mode destination device to Sub-CPU */
        andi.w  #0xF8FF,0x8004.w
        ori.w   #0x0300,0x8004.w
2:
        move.w  #0x008B,d0              /* CDCREAD */
        jsr     0x5F22.w                /* call CDBIOS function */
        bcs.b   2b                      /* not ready to xfer data */

        movea.l 8(sp),a0                /* data buffer */
        lea     12(sp),a1               /* header address */
        move.w  #0x008C,d0              /* CDCTRN */
        jsr     0x5F22.w                /* call CDBIOS function */
        bcs.b   0b                      /* failed, retry */

        move.w  #0x008D,d0              /* CDCACK */
        jsr     0x5F22.w                /* call CDBIOS function */

        addq.l  #1,(sp)                 /* next sector */
        addi.l  #2048,8(sp)             /* inc buffer ptr */
        subq.l  #1,4(sp)                /* dec sector count */
        bne.b   1b

        lea     16(sp),sp               /* cleanup stack */
        rts

| Read 1 sector into buffer in a0 (using DMA mode specified in d0)

ReadSectorDMA:
        movem.l d0-d1/a0-a1,-(sp)

        move.b  d0,0x8004.w
        move.l  8(sp),d0
        lsr.l   #3,d0
        move.w  d0,0x800A.w             /* DMA destination address */

        move.w  #0x008B,d0              /* CDCREAD */
        jsr     0x5F22.w                /* call CDBIOS function */
        bcc.b   1f                      /* ready to xfer data */

        lea     16(sp),sp               /* cleanup stack */
        moveq   #0,d0
        rts

1:
        /* check for EDT (end of data transfer) to be set */
        btst    #0x7,0x8004.w
        beq.s   1b

        move.w  #0x008D,d0              /* CDCACK */
        jsr     0x5F22.w                /* call CDBIOS function */

        lea     16(sp),sp               /* cleanup stack */
        moveq   #1,d0
        rts

StopReadCD:
        movem.l d0-d1/a0-a1,-(sp)

        move.w  #0x0089,d0              /* CDCSTOP */
        jsr     0x5F22.w                /* call CDBIOS function */

        lea     16(sp),sp               /* cleanup stack */
        rts

| Sub-CPU variables

        .align  4
int3_callback:
        .long   0

int3_cntr:
        .word   0


        .align  2
track_number:
        .word   0

updates_suspend:
        .byte   0

        .align  2
root_dirname:
        .asciz  "/"

        .align  2
iso_pvd_magic:
        .asciz  "\1CD001\1"

        .align  2
drive_init_parms:
        .byte   0x01, 0xFF              /* first track (1), last track (all) */

        .align  4
DISC_TYPE:
        .word   0
DIR_ENTRY:
        .word   0
CWD_OFFSET:
        .long   0
CWD_LENGTH:
        .long   0
CURR_OFFSET:
        .long   0
        .global CURR_OFFSET
CURR_LENGTH:
        .long   0
ROOT_OFFSET:
        .long   0
ROOT_LENGTH:
        .long   0
        .global DENTRY_OFFSET
DENTRY_OFFSET:
        .long   0
        .global DENTRY_LENGTH
DENTRY_LENGTH:
        .long   0
DENTRY_FLAGS:
        .byte   0
        .global DENTRY_FLAGS

        .align  2
CDA_VOLUME:
        .global CDA_VOLUME
        .word   0

        .align  2
        .global cd_drvinit_tries        /* V70: DRVINIT sends needed (1 = first try) */
cd_drvinit_tries:
        .word   0



| ---- ADDED (not from crt.s): C-callable wrapper for ReadSectorsSUB ----
| ReadSectorsSUB takes d0 = first sector, d1 = sector count, a0 = buffer.
| void sub_read_sectors(void *ptr, int lba, int len);
        .align  2
        .global sub_read_sectors
sub_read_sectors:
        movea.l 4(sp),a0                /* buffer */
        move.l  8(sp),d0                /* lba */
        move.l  12(sp),d1               /* sector count */
        movem.l d2-d7/a2-a6,-(sp)
        jsr     ReadSectorsSUB
        movem.l (sp)+,d2-d7/a2-a6
        rts

        .bss
        .align  8
        .global DISC_BUFFER
DISC_BUFFER:
        .skip  2048*4

        .align  2
DENTRY_NAME:
        .skip  256
        .global DENTRY_NAME
TEMP_NAME:
        .skip  256
