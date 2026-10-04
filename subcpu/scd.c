#include <stdint.h>
#include <string.h>
#include <stdio.h>

extern uint32_t vblank_vector;
extern uint16_t gen_lvl2;

extern uint32_t Sub_Start;
extern uint32_t Sub_End;

extern void Kos_Decomp(uint8_t *src, uint8_t *dst);

extern void write_byte(unsigned int dst, unsigned char val);
extern void write_word(unsigned int dst, unsigned short val);
extern void write_long(unsigned int dst, unsigned int val);
extern unsigned char read_byte(unsigned int src);
extern unsigned short read_word(unsigned int src);
extern unsigned int read_long(unsigned int src);

extern void switch_banks(void);
extern int do_md_cmd4(int cmd, int arg1, int arg2, int arg3, int arg4);
#define MD_CMD_PUT_STR 9
#define TEXT_GREEN 0x2000

static void scd_diag_print(const char *msg)
{
    char *temp = (char *)0x0C0000;
    int i = 0;
    while (msg[i] && i < 39) { temp[i] = msg[i]; i++; }
    temp[i] = 0;
    switch_banks();
    do_md_cmd4(MD_CMD_PUT_STR, 0x200000, TEXT_GREEN, 2, 16);
}

static void scd_timeout_print(const char *msg)
{
    char *temp = (char *)0x0C0000;
    int i = 0;
    while (msg[i] && i < 39) { temp[i] = msg[i]; i++; }
    temp[i] = 0;
    switch_banks();
    do_md_cmd4(MD_CMD_PUT_STR, 0x200000, TEXT_GREEN, 2, 17);
}

/* Dedicated row-2 print, used ONLY for the InitCD()-exit diagnostic below.
   Row 2 is written exactly once elsewhere ("LOADING 32XCD ENGINE...", in
   main.c, before InitCD() is even called) and never touched again by
   anything after that - unlike row 16 (scd_diag_print), which
   scd_open_file()'s later CKPT1-CKPT4 calls overwrite before the user
   ever gets to see this value. Keeping this on its own row lets us
   compare "value at InitCD()'s own exit" against "value main() reads
   immediately after InitCD() returns" on screen at the same time. */
static void scd_diag_print_row2(const char *msg)
{
    char *temp = (char *)0x0C0000;
    int i = 0;
    while (msg[i] && i < 39) { temp[i] = msg[i]; i++; }
    temp[i] = 0;
    switch_banks();
    do_md_cmd4(MD_CMD_PUT_STR, 0x200000, TEXT_GREEN, 2, 2);
}

void scd_delay(void) __attribute__((section(".data"), aligned(16)));
char wait_cmd_ack(void) __attribute__((section(".data"), aligned(16)));
void wait_do_cmd(char cmd) __attribute__((section(".data"), aligned(16)));

extern void scd_init_pcm(void);
extern void bump_fm(void);
extern int mystrlen(const char* string);

volatile int scd_last_timeout_code = 0;

void scd_delay(void)
{
    int cnt = 50;
    do {
        asm __volatile("nop");
    } while (--cnt);
}

char wait_cmd_ack(void)
{
    char ack = 0;
    int timeout = 0;

    do {
        scd_delay();
        bump_fm();
        ack = read_byte(0xA1200F); 
        timeout++;
        if (timeout > 200000) {
            scd_last_timeout_code = 2;
            return 0;
        }
    } while (!ack);

    return ack;
}

void wait_do_cmd(char cmd)
{
    int timeout = 0;

    while (read_byte(0xA1200F)) {
        scd_delay(); 
        bump_fm();
        timeout++;
        if (timeout > 200000) {
            scd_last_timeout_code = 1;
            
            char dmsg[40];
            sprintf(dmsg, "COMM:%02X CMD:%c TIMEOUT         ", read_byte(0xA1200F), cmd);
            scd_timeout_print(dmsg);
            
            return;
        }
    }
    write_byte(0xA1200E, cmd); 
}

/* Sub-CPU-native replacement. main.c (linked at 0x8000, see cd.ld) runs on the
   SUB-CPU, so the comm-port / 0xA12xxx / 0x600000 Main-CPU protocol below can
   never reach anything. open_file() (sub_stubs.c) calls the filesystem code in
   cdfs.s directly and returns (length << 32) | start_lba, or -1. */
extern int64_t open_file(const char *name);

/* Diagnostic, only used when the open fails: shows what is really in the
   root directory of the disc (rows 19+), so we can see whether IMAGE.RAW is
   on the disc at all and how it is spelled. */
extern int set_cwd(char *path);
extern int next_dir_entry(void);
extern char DENTRY_NAME[];

static void scd_print_row(const char *msg, int row)
{
    char *temp = (char *)0x0C0000;
    int i = 0;
    while (msg[i] && i < 39) { temp[i] = msg[i]; i++; }
    temp[i] = 0;
    switch_banks();
    do_md_cmd4(MD_CMD_PUT_STR, 0x200000, TEXT_GREEN, 2, row);
}

static void scd_list_root(void)
{
    char line[48];
    int n = 0, guard = 0, r;

    if (set_cwd("/") < 0) {
        scd_print_row("ROOT DIR: set_cwd FAILED         ", 19);
        return;
    }
    while (n < 7 && guard++ < 64) {
        r = next_dir_entry();
        if (r == -5)            /* ERR_BAD_ENTRY - skip it */
            continue;
        if (r < 0)              /* end of this directory sector */
            break;
        sprintf(line, "FILE: %-24.24s", DENTRY_NAME);
        scd_print_row(line, 19 + n);
        n++;
    }
    if (n == 0)
        scd_print_row("ROOT DIR: NO ENTRIES READ          ", 19);
}

/* Look for name in the directory dir (any path, e.g. "/ROMS"), walking all of
   that directory's sectors. Returns (length << 32) | start_lba, or -1. */
extern int find_dir_entry(char *name);
extern int next_dir_sec(void);
extern int DENTRY_OFFSET;
extern int DENTRY_LENGTH;

static int64_t scd_find_in_dir(char *dir, const char *name)
{
    int r = set_cwd(dir);
    if (r < 0)
        return -1;

    while (1) {
        r = find_dir_entry((char *)name);
        if (r >= 0)
            break;
        if (r == -6) {                  /* ERR_NAME_NOT_FOUND: try next sector */
            r = next_dir_sec();
            if (r == -4)                /* ERR_NO_MORE_ENTRIES */
                return -1;
        }
        if (r < 0)
            return -1;
    }
    return ((int64_t)DENTRY_LENGTH << 32) | (uint32_t)DENTRY_OFFSET;
}

int64_t scd_open_file(const char *name)
{
    int64_t r;

    scd_diag_print("CKPT1: entered scd_open_file  ");
    scd_last_timeout_code = 0;

    r = open_file(name);                    /* disc root first          */
    if (r < 0)
        r = scd_find_in_dir("/ROMS", name); /* then the ROMS directory  */
    if (r < 0)
        scd_list_root();
    return r;
}

#if 0   /* old Main-CPU-protocol version, kept for reference */
int64_t scd_open_file(const char *name)
{
    int i;
    char *scdfn = (char *)0x600000; /* word ram on MD side (in 1M mode) */
    union {
        int32_t lo[2];
        int64_t value;
    } handle;

    scd_diag_print("CKPT1: entered scd_open_file  ");

    /* The Sub-CPU's own OpenFile handler (crt.s) calls switch_banks()
       unconditionally before reading the filename - confirmed by direct
       inspection, this is explicitly a "Switch 1M Banks" operation
       (toggles bank-select bit 0, waits for bit 1 to clear). But
       InitCD() explicitly leaves Word RAM in 2M mode
       (write_word(0xA12002, 0x0002)) and nothing has switched it to 1M
       mode since. The Sub-CPU's own bank-switch wait is very likely
       what's hanging, since it's meaningless while hardware is actually
       in 2M mode. Setting the MODE bit (bit 2) directly - no wait loop
       needed here, since this is a decode bit, not a handshake. */
    /* The Sub-CPU's own boot routine (SPInit in crt.s) already switches
       to 1M mode automatically - confirmed by direct trace - making the
       earlier bit-2 write redundant, not wrong. But its mask
       (andi.b #0xE2,0x8003.w) preserves bit 1 rather than clearing it,
       and bit 1 was left at 1 by InitCD()'s earlier 2M-mode DMNA=1
       write. switch_banks() (called by every Word-RAM-touching command
       in this firmware, including OpenFile) waits for bit 1 to become
       0 - if it's stuck at 1 from that original setting, this wait
       never exits, matching the ack timeout exactly. Clearing it here. */
    write_byte(0xA12003, (read_byte(0xA12003) | 0x04) & ~0x02);

    scd_diag_print("CKPT1b: 1M mode + bit1 clear  ");

    /* Safety check: If the comm port is already faulted (FF), try to clear it */
    if (read_byte(0xA1200F) == 0xFF) {
        write_byte(0xA1200E, 0x00);
        scd_delay();
    }

    /* Suspend the Sub-CPU PCM mixer/decoder before touching Word RAM */
    write_byte(0xA12010, 1);
    wait_do_cmd('E');
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00);

    scd_diag_print("CKPT2: mixer suspended        ");

    for (i = 0; name[i]; i++)
        *scdfn++ = name[i];
    *scdfn = 0;

    scd_diag_print("CKPT4: filename write done    ");

    write_long(0xA12010, 0x0C0000); /* word ram on CD side (in 1M mode) */
    scd_last_timeout_code = 0;
    wait_do_cmd('F');
    if (scd_last_timeout_code == 1) {
        return -1;
    }
    if (!wait_cmd_ack()) {
        return -1;
    }
    handle.lo[0] = read_long(0xA12020);
    handle.lo[1] = read_long(0xA12024);
    write_byte(0xA1200E, 0x00); // acknowledge receipt of command result

    /* Unsuspend the Sub-CPU PCM mixer/decoder */
    write_byte(0xA12010, 0);
    wait_do_cmd('E');
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00);

    return handle.value;
}

#endif /* old scd_open_file */

int64_t scd_read_directory(char *buf)
{
    int i, numwords;
    char *scdWordRam = (char *)0x600000; 
    union {
        int32_t ln[2]; 
        int64_t value;
    } res;

    memcpy(scdWordRam, buf, mystrlen(buf)+1);

    write_long(0xA12010, 0x0C0000); 
    wait_do_cmd('M'); 
    wait_cmd_ack();
    res.ln[0] = read_long(0xA12020);
    res.ln[1] = read_long(0xA12024);
    write_byte(0xA1200E, 0x00); 

    if (res.value < 0)
        return res.value;

#if 1
    numwords = (res.ln[0] + 1)/2;
    for (i = 0; i < numwords; i++) {
        ((volatile int16_t *)buf)[i] = ((volatile int16_t *)scdWordRam)[i];
    }
#else
    memcpy(buf, scdWordRam, res.ln[0]);
#endif

    return res.value;
}

/* Sub-CPU-native replacement. The Sub-CPU cannot write to cart RAM, so each
   chunk is read from disc into the Sub-CPU's Word RAM bank, the bank is handed
   to the Main CPU (switch_banks), and Main-CPU command 23 copies it to the
   cart address. ptr is the address of the first cart data byte (odd address,
   0x600001 + 2*offset), as main.c passes it. */
/* C-callable wrapper around ReadSectorsSUB (cdfs.s), which takes
   d0 = first sector, d1 = sector count, a0 = buffer. Defined here so this file
   does not depend on any other file for it. */
extern void scd_sub_read(void *ptr, int lba, int len);
__asm__(
    "       .pushsection .text\n"
    "       .align  2\n"
    "       .globl  scd_sub_read\n"
    "scd_sub_read:\n"
    "       movea.l 4(%sp),%a0\n"
    "       move.l  8(%sp),%d0\n"
    "       move.l  12(%sp),%d1\n"
    "       movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
    "       jsr     ReadSectorsSUB\n"
    "       movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
    "       rts\n"
    "       .popsection\n"
);
/* CD audio: play a track, repeating, at full volume (data track = 1, first audio track = 2).
   The same BIOS calls as PlayTrack / SetVolume in crt.s:
     MSCSTOP 0x0002, BIOS_FDRSET 0x0085 (d1 = volume 0..1024), MSCPLAYR 0x0013 (a0 -> word = track). */
extern void scd_cdda_play(int track);
__asm__(
    "       .pushsection .text\n"
    "       .align  2\n"
    "       .globl  scd_cdda_play\n"
    "scd_cdda_play:\n"
    "       move.l  4(%sp),%d1\n"
    "       movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
    "       lea     scd_cdda_track(%pc),%a0\n"
    "       move.w  %d1,(%a0)\n"
    "       move.w  #0x0002,%d0\n"
    "       jsr     0x5F22.w\n"
    "       move.w  #0x0085,%d0\n"
    "       move.w  #0x0400,%d1\n"
    "       jsr     0x5F22.w\n"
    "       lea     scd_cdda_track(%pc),%a0\n"
    "       move.w  #0x0013,%d0\n"
    "       jsr     0x5F22.w\n"
    "       movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
    "       rts\n"
    "scd_cdda_track:\n"
    "       .word   0\n"
    "       .popsection\n"
);
/* CD status from the BIOS (CDBSTAT 0x0081, as GetDiscInfo in crt.s).  Returns
     bits 31-16 = BIOS status word (its first byte: 01 = playing music, 05 = paused)
     bits 15-8  = first track number,  bits 7-0 = last track number */
extern int scd_cd_status(void);
/* Start of a track from the BIOS TOC (CDBTOCREAD 0x0083, as GetTrackInfo in crt.s).
   Returns MMSSFFTN: minute, second, frame, track number (one byte each). */
extern int scd_track_info(int track);
__asm__(
    "       .pushsection .text\n"
    "       .align  2\n"
    "       .globl  scd_track_info\n"
    "scd_track_info:\n"
    "       move.l  4(%sp),%d1\n"
    "       movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
    "       move.w  #0x0083,%d0\n"
    "       jsr     0x5F22.w\n"
    "       movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
    "       rts\n"
    "       .popsection\n"
);
__asm__(
    "       .pushsection .text\n"
    "       .align  2\n"
    "       .globl  scd_cd_status\n"
    "scd_cd_status:\n"
    "       movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
    "       move.w  #0x0081,%d0\n"
    "       jsr     0x5F22.w\n"
    "       moveq   #0,%d0\n"
    "       move.w  0(%a0),%d0\n"
    "       swap    %d0\n"
    "       move.w  16(%a0),%d0\n"
    "       movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
    "       rts\n"
    "       .popsection\n"
);
/* V49: one CD BIOS call with d0 = function, d1 = value, a0 = pointer.  Returns d0.
   Used by cdda_start() in main.c so it can wait for the BIOS between commands. */
extern int scd_bios_call(int fn, int d1, void *a0);
__asm__(
    "       .pushsection .text\n"
    "       .align  2\n"
    "       .globl  scd_bios_call\n"
    "scd_bios_call:\n"
    "       move.l  4(%sp),%d0\n"
    "       move.l  8(%sp),%d1\n"
    "       movea.l 12(%sp),%a0\n"
    "       movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
    "       jsr     0x5F22.w\n"
    "       movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
    "       rts\n"
    "       .popsection\n"
);
extern int do_md_cmd3(int cmd, int arg1, int arg2, int arg3);
#define MD_CMD_COPY_TO_CART   23
#define SCD_STAGE_SECTORS     16          /* 32 KB per chunk */

void scd_read_sectors(void *ptr, int lba, int len, void (*wait)(void))
{
    unsigned int dst = (unsigned int)ptr;
    int n;

    while (len > 0) {
        n = (len > SCD_STAGE_SECTORS) ? SCD_STAGE_SECTORS : len;
        scd_sub_read((void *)0x0C0000, lba, n);   /* disc -> Sub Word RAM bank */
        switch_banks();                               /* give that bank to Main */
        do_md_cmd3(MD_CMD_COPY_TO_CART, 0x200000, (int)dst, n * 2048);
        lba += n;
        dst += n * 2048 * 2;            /* RAM cart: 2 address bytes per data byte */
        len -= n;
        if (wait)
            wait();
    }
}

#if 0   /* old Main-CPU-protocol version, kept for reference */
void scd_read_sectors(void *ptr, int lba, int len, void (*wait)(void))
{
    char ack = 0;
    if (!wait)
        wait = scd_delay;

    write_long(0xA12010, (uintptr_t)ptr);
    write_long(0xA12014, lba);
    write_long(0xA12018, len);
    wait_do_cmd('H');
    do {
        wait();
        ack = read_byte(0xA1200F); 
    } while (!ack);
    write_byte(0xA1200E, 0x00); 
}

#endif /* old scd_read_sectors */

void scd_switch_to_bank(int bank)
{
    write_byte(0xA12010, bank);
    wait_do_cmd('J');
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00); 
}

void scd_play_spcm_track(const char *name, int repeat)
{
    char *scdWordRam = (char *)0x600000; 

    memcpy(scdWordRam, name, mystrlen(name)+1);
    write_long(0xA12010, 0x0C0000); 
    write_long(0xA12014, repeat);
    wait_do_cmd('Q'); 
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00); 
}

void scd_stop_spcm_track(void)
{
    wait_do_cmd('R'); 
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00); 
}

void scd_resume_spcm_track(void)
{
    wait_do_cmd('X'); 
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00); 
}

int scd_get_spcm_playback_status(void)
{
    return read_byte(0xA1202E);
}

void scd_set_volume(int volume)
{
    write_word(0xA12010, volume);
    wait_do_cmd('V'); 
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00); 
}

int64_t scd_get_disc_info(void)
{
    union {
        int16_t lo[4];
        int64_t value;
    } res;

    wait_do_cmd('D'); 
    wait_cmd_ack();
    res.lo[0] = read_word(0xA12020); 
    res.lo[1] = read_word(0xA12022); 
    res.lo[2] = read_word(0xA12024); 
    res.lo[3] = 0;
    write_byte(0xA1200E, 0x00); 

    return res.value;
}

int64_t scd_get_track_info(int track)
{
    union {
        int32_t lo[2];
        int64_t value;
    } res;

    write_word(0xA12010, track);
    wait_do_cmd('T'); 
    wait_cmd_ack();
    res.lo[0] = read_long(0xA12020); 
    res.lo[1] = read_byte(0xA12024) & 0xff; 
    write_byte(0xA1200E, 0x00); 

    return res.value;
}

void scd_play_cdda_track(int track, int repeat)
{
    write_word(0xA12010, track);
    write_byte(0xA12012, repeat);
    wait_do_cmd('P'); 
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00); 
}

void scd_stop_cdda_playback(void)
{
    wait_do_cmd('S'); 
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00); 
}

void scd_toggle_cdda_pause(void)
{
    wait_do_cmd('Z'); 
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00); 
}

void scd_open_tray(void)
{
    wait_do_cmd('Y'); 
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00); 
}

int scd_stream_cd(int lba, int len)
{
    int wram_ofs;
    write_long(0xA12010, lba);
    write_long(0xA12014, len);
    wait_do_cmd('['); 
    wait_cmd_ack();
    wram_ofs = read_long(0xA12020);
    write_byte(0xA1200E, 0x00); 
    return wram_ofs;
}

void scd_stream_cmd(char cmd)
{
    wait_do_cmd(cmd);
    wait_cmd_ack();
    write_byte(0xA1200E, 0x00); 
}

/* Sub-CPU-native replacement. Runs the real CD init (DRVINIT, CDCSETMODE,
   PVD scan) from cdfs.s. Returns 0 on success (this is what main.c tests:
   "CD DRIVE READY" prints only when it returns 0), non-zero error code
   otherwise: 2=read failed, 3=no PVD, 7=no disc. */
extern int init_cd(void);

uint16_t InitCD(void)
{
    char dbuf[40];
    int r = init_cd();

    sprintf(dbuf, "INSIDE-INITCD RESULT: %d         ", r);
    scd_diag_print_row2(dbuf);

    if (r < 0)
        return (uint16_t)(-r);
    return 0;
}

#if 0   /* old Main-CPU boot version, kept for reference */
uint16_t InitCD(void)
{
    char *bios;

    bios = (char *)0x415800;
    if (memcmp(bios + 0x6D, "SEGA", 4))
    {
        bios = (char *)0x416000;
        if (memcmp(bios + 0x6D, "SEGA", 4))
        {
            if (memcmp(bios + 0x6D, "WONDER", 6))
            {
                bios = (char *)0x41AD00; 
                if (memcmp(bios + 0x6D, "SEGA", 4))
                    return 0; 
            }
        }
    }

    write_word(0xA12002, 0xFF00);
    write_byte(0xA12001, 0x03);
    write_byte(0xA12001, 0x02);
    write_byte(0xA12001, 0x00);

    write_byte(0xA12001, 0x02);
    while (!(read_byte(0xA12001) & 2)) write_byte(0xA12001, 0x02); 

    write_word(0xA12002, 0x0002); 
    memset((char *)0x420000, 0, 0x20000); 
    Kos_Decomp((uint8_t *)bios, (uint8_t *)0x420000);

    memcpy((char *)0x426000, (char *)&Sub_Start, (int)&Sub_End - (int)&Sub_Start);

    write_byte(0xA1200E, 0x00); 
    write_byte(0xA12002, 0x2A); 
    write_byte(0xA12001, 0x01); 
    while (!(read_byte(0xA12001) & 1)) write_byte(0xA12001, 0x01); 

    gen_lvl2 = 1; 

    while (read_byte(0xA1200F) != 'I')
    {
        static int timeout = 0;
        timeout++;
        if (timeout > 2000000)
        {
            gen_lvl2 = 0;
            return 0; 
        }
    }

    while (read_byte(0xA1200F) != 0x00) ;

    {
        unsigned char c_immediate = read_byte(0xA1200F);
        char dbuf[40];
        sprintf(dbuf, "INSIDE-INITCD COMM: %02X        ", c_immediate);
        scd_diag_print_row2(dbuf);
    }

    scd_init_pcm();

    return 0x1; 
}
#endif /* old InitCD */
