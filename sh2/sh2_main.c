#include <stddef.h>   /* K21b: size_t for our own memset/memcpy (compiler header, no C library) */
#include "doomdef.h"
#include "mars.h"
#include "32x.h"

/* ===========================================================================
 * KOBO DELUXE 32XCD - MILESTONE 1  (sh2_main.c, master SH2)
 *
 * WHAT THIS BUILD DOES
 *   1. Boots exactly like the working V80 engine (same COMM4/COMM10 start-up).
 *   2. Loads KOBOGFX.BIN (made by tools/kobo_gfx.py) from CD into the RAM cart with
 *      the proven CMD 38 (open) + CMD 46 (chunk -> cart, verified) pipeline.
 *   3. Reads the pack back from the cart: palette -> 32X CRAM, the level's tile set and
 *      the star tiles -> SDRAM (drawing from SDRAM is much faster than from the cart).
 *   4. Builds level 1's map with Kobo's own maze generator (map.cpp, random.h - ported
 *      to C below, same maths, so the maps look like Kobo's).
 *   5. Every frame: asks the Sub-CPU for the pad (CMD 48), scrolls, draws the 224x224
 *      playfield, flips.  D-pad = scroll, C = diagnostic text on/off.
 *
 * WHY 256-COLOUR MODE: Kobo's graphics are one 256-colour palette (see kobo_gfx.py);
 *   1 byte per pixel halves the drawing work compared with the 15-bit mode V80 used.
 * WHY THE PAD GOES THROUGH A COMMAND: the SH2 must not poll a comm register the 68K is
 *   writing (32X manual p.69 - and the COMM6 crash we found).  CMD 48 uses the same
 *   one-at-a-time COMM0 protocol as every other command.
 * =========================================================================== */

void pri_vbi_handler(void) {}

#define SC_CMD_CLR       (*(volatile uint16_t *)0x2000401A)
void pri_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_cmd_handler(void) { SC_CMD_CLR = 0; }
void sec_dma1_handler(void) {}

/* C2b: SOUND EFFECTS ON THE 32X PWM, played by the slave SH2.  KOBOSFX.BIN in ROM at 0x320000:
   "KSFX", count, table at +8 (start page, volume, fd, loop, length), samples at +256 (8-bit
   sign-magnitude, 0xFF = end).  16276 Hz out -> step = fd << 6 (16.16).  4 channels, mono. */
#define SFX_PACK   ((const volatile uint8_t *)(0x22000000 + 0x320000))
#define PWM_CTRL   (*(volatile uint16_t *)0x20004030)
#define PWM_CYCLE  (*(volatile uint16_t *)0x20004032)
#define PWM_MONO   (*(volatile uint16_t *)0x20004038)
#define PWM_CYC    1414
#define PWM_MID    707
volatile uint8_t pwm_mail[8];
volatile uint8_t pwm_widx;
void pwm_post(int id) {
    volatile uint8_t *m = (volatile uint8_t *)((uint32_t)pwm_mail | 0x20000000);
    volatile uint8_t *w = (volatile uint8_t *)((uint32_t)&pwm_widx | 0x20000000);
    uint8_t k = *w;
    m[k & 7] = (uint8_t)id;
    *w = (uint8_t)(k + 1);
}
void secondary(void) {
    static uint32_t pos[4], end[4], step[4];
    static int vol[4], on[4];
    volatile uint8_t *m = (volatile uint8_t *)((uint32_t)pwm_mail | 0x20000000);
    volatile uint8_t *w = (volatile uint8_t *)((uint32_t)&pwm_widx | 0x20000000);
    const volatile uint8_t *img = SFX_PACK + 256;
    uint8_t ridx;
    int n, i, next = 0;
    MARS_SYS_INTMSK |= 0x0002;
    __asm__ __volatile__("ldc %0,sr" : : "r"(0) : "memory");
    for (i = 0; i < 4; i++) on[i] = 0;
    n = (SFX_PACK[0] == 'K' && SFX_PACK[1] == 'S') ? ((SFX_PACK[4] << 8) | SFX_PACK[5]) : 0;
    ridx = *w;
    PWM_MONO = 1; PWM_MONO = 1; PWM_MONO = 1;
    PWM_CYCLE = PWM_CYC;
    PWM_CTRL = 0x0185;
    while (1) {
        int mix = PWM_MID;
        while (ridx != *w) {
            int id = m[ridx & 7];
            ridx++;
            if (id >= 1 && id <= n) {
                const volatile uint8_t *t = SFX_PACK + 8 + (id - 1) * 8;
                uint32_t st = (uint32_t)t[0] << 8;
                uint32_t len = ((uint32_t)t[6] << 8) | t[7];
                uint32_t fd = ((uint32_t)t[2] << 8) | t[3];
                pos[next] = st << 16; end[next] = (st + len) << 16;
                step[next] = fd << 6; vol[next] = t[1]; on[next] = 1;
                next = (next + 1) & 3;
            }
        }
        for (i = 0; i < 4; i++) {
            uint32_t p; uint8_t b; int mag;
            if (!on[i]) continue;
            p = pos[i] >> 16;
            b = p < 65536 ? img[p] : 0xFF;
            if (b == 0xFF || pos[i] >= end[i]) { on[i] = 0; continue; }
            mag = (int)(((uint32_t)(b & 0x7F) * (uint32_t)vol[i]) >> 8);
            if (b & 0x80) mix += mag; else mix -= mag;
            pos[i] += step[i];
        }
        if (mix < 1) mix = 1;
        if (mix > PWM_CYC - 1) mix = PWM_CYC - 1;
        while (PWM_MONO & 0x8000) { }
        PWM_MONO = (uint16_t)mix;
    }
}

volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;

uint32_t _text_end;
uint32_t _data_size;
uint32_t _bss_start;
uint32_t _bss_end;

int main(void);
void _main(void) { main(); }

/* K21b - OUR OWN memset / memcpy.  WHY: this program links with -nostdlib (no C library),
   and GCC may turn a plain "fill/copy" loop into a call to memset/memcpy - K21's damage-table
   clear did exactly that ("undefined reference to memset").  These keep the link working if
   it ever happens again.  The volatile pointers stop GCC from turning THESE loops into calls
   to themselves. */
void *memset(void *d, int c, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)d;
    while (n--) *p++ = (uint8_t)c;
    return d;
}
/* K33b: memmove too - K33's sound-queue shift loop became a memmove call ("undefined reference
   to memmove").  Overlap-safe; volatile so GCC cannot turn it into a call to itself. */
void *memmove(void *d, const void *s, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)d;
    const volatile uint8_t *q = (const volatile uint8_t *)s;
    if (p < q) { while (n--) *p++ = *q++; }
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
void *memcpy(void *d, const void *s, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)d;
    const volatile uint8_t *q = (const volatile uint8_t *)s;
    while (n--) *p++ = *q++;
    return d;
}

__attribute__((used)) const char sh2_build_marker[] = "SH2-KOBO-32X-CARTRIDGE";

/* =====================================================================================
 * STORAGE MODE - must match Demo/main.c KOBO_NOCART
 *
 *   1 = NO-CART (development / Fusion): the RAM cart is never touched.  The pieces of
 *       KOBOGFX.BIN the SH2 needs are read straight from CD:  CMD 38 opens the file,
 *       CMD 50 copies one 32 KB chunk into the frame buffer, the SH2 copies what it needs
 *       into SDRAM.  Runs in Fusion (which cannot emulate the 4 MB cart) AND on the
 *       console with or without the cart plugged in.
 *       Limit: everything the SH2 keeps must fit in SDRAM (M1 needs ~23 KB).
 *
 *   0 = CART (real hardware + MicroNut99 cart): the whole pack is stored in the cart once
 *       (CMD 38 + CMD 46, verified - proven on hardware since V76) and the SH2 reads it
 *       directly at 0x22000000.  Needed later when the game data outgrows SDRAM.
 *
 * Both modes load the SAME file (KOBOGFX.BIN) and use the SAME bank numbers; only fetch()
 * and the start-up load differ.
 * ===================================================================================== */
#define KOBO_NOCART 0   /* HW1: CART MODE - real hardware with the cart */
/* CARTRIDGE ROM (kobo32x_crt): 1 = Kobo is a normal 32X cartridge.  KOBOGFX.BIN is part of the
   ROM at 0x200000 (= PACK_CART_OFF, where cart mode reads it) and IMAGE.RAW at 0x300000: nothing
   is loaded from CD.  The Genesis side is cart/cart_md.s, answering the same COMM commands. */
#define KOBO_ROMCART 1
#define SPLASH_ROM_OFF 0x300000

/* ---- DIAGNOSTIC STRIP (right side, x 224..319) - keep it: the dev's "screen test".
   1 = drawn every frame: version, pad squares, heartbeat, CMD 48 timeout, palette grid,
       tile/star check with checksum boxes, CD/pack readouts.  0 = strip left black.  */
#define KOBO_DIAG_STRIP 0          /* K30: 0 = Kobo's right-hand PANEL (score, radar, ships, stage);
                                      1 = the diagnostic strip instead (all its code is kept) */
#define KOBO_FIXED_STEP 0          /* K32b: 0 = one logic step per drawn frame (the NORMAL speed the dev
                                      likes in Fusion).  1 = fixed step of KOBO_STEP_MS, timed by
                                      the 68K's vblank counter: same speed on any frame rate - use it
                                      on real hardware (slower drawing = slower game with 0) and tune
                                      KOBO_STEP_MS until it matches Fusion.  30 = Kobo Classic
                                      (felt like "God" level in Fusion). */
#define KOBO_STEP_MS    30
#define KOBO_START_PAUSE 0         /* K36: pause is on MODE (6-button pad).  1 = START pauses too.
                                      The old "START cuts the audio" came from the boot-engine's CD
                                      player code, which no longer exists - try 1 to test START. */
/* K37: attract/demo mode starts after the menu has been idle for one play of Track02.
   build.sh writes track_len.h (TRACK02_SECONDS) from Track02.wav; without it: 120 s. */
#include "track_len.h"            /* K37d: always included - build.sh rewrites it from Track02.wav */
#undef  TRACK02_SECONDS
#define TRACK02_SECONDS 60        /* K37e: demo delay set by hand (seconds of idle menu) */
#ifndef TRACK02_SECONDS
#error "track_len.h must define TRACK02_SECONDS (run build.sh)"
#endif
#define KOBO_HW_DIAG    0          /* HW1: 1 = hardware readout at the bottom-left of the playfield:
                                      P = Sub-CPU heartbeat (COMM14, counts up while it runs),
                                      C = command waiting in COMM0, T = unanswered pad requests,
                                      F = frames per second.  0 = off (release). */
#define KOBO_HW_PCM     1          /* HW2: 1 = load the sound pack into the PCM chip (CMD 53).  0 = no
                                      sound effects (music still plays).  Turned on in HW3 if HW2 works. */
#define KOBO_HW_BRAM    1          /* HW2: 1 = high scores/options in backup RAM (BIOS _BURAM).  0 = kept
                                      for this session only.  Turned on in HW4. */
#define KOBO_HW_QUIET   0          /* HW9: 1 = after 4B10 the SH2 sends NOTHING (no pad requests); pad = 0.
                                      Must match subcpu/main.c.  The controller does not work in this test. */
#define KOBO_SPLASH     1          /* K30: 1 = IMAGE.RAW splash while loading (+ SPLASH_HOLD),
                                      0 = no splash, no hold (dev request).  With 0 the file
                                      is not read; IMAGE.RAW may be missing from the disc. */
#define KOBO_DIAG_GRID  0          /* K24: 1 = palette grid in the strip (as K9-K23), 0 = the RADAR
                                      is drawn in that space instead.  The grid code stays. */
#define KOBO_DEBUG_KEYS 0          /* K27: 1 = X (6-button pad) skips to the next level (testing).
                                      Set 0 for the finished game. */
#define KOBO_VERSION    37         /* shown as "K30" top of the strip + version colour block */

/* ---- 32X registers (SH2 side, cache-through addresses) ---------------------------- */
#define SC_COMM2         (*(volatile uint16_t *)0x20004022)
#define SC_COMM8         (*(volatile uint16_t *)0x20004028)
#define SC_COMM10        (*(volatile uint16_t *)0x2000402A)
#define SC_COMM8_32      (*(volatile uint32_t *)0x20004028)
#define SC_COMM12_32     (*(volatile uint32_t *)0x2000402C)
#define SC_SYS_ADAPTER_B (*(volatile uint8_t  *)0x20004000)
#define SC_VDP_DISPMODE  (*(volatile uint16_t *)0x20004100)
#define SC_VDP_FBCTL     (*(volatile uint16_t *)0x2000410A)
#define SC_CRAM          ((volatile uint16_t *)0x20004200)
#define SC_FRAMEBUFFER   ((volatile uint16_t *)0x24000000)
#define SC_FB8           ((volatile uint8_t  *)0x24000200)   /* pixels start after the line table */

/* ---- commands to the Sub-CPU (main.c) --------------------------------------------- */
#define CMD_OPEN_FILE     38
#define CMD_PLAY_CDDA     45
#define CMD_CHUNK_TO_CART 46
#define CMD_GET_PAD       48      /* M1: reply = pad 1 bits in COMM8 */
#define CMD_FILE_CHUNK_FB 50      /* no-cart: COMM2 = chunk number, COMM8 = file length.
                                     Sub-CPU reads 32 KB of the open file from CD and the 68K
                                     copies it raw into the frame buffer at byte 0x200.     */
#define FETCH_BYTES       16384   /* K31: no-cart loading window = 16 KB (CMD 50 chunk size) */
#define TSET_CACHE        ((volatile uint8_t *)0x24016000)   /* K31: 5 tile sets x 6 KB in the
                                     frame buffer's spare memory (0x16000..0x1D800, both
                                     buffers): level changes copy from here, never from CD */
#define FB_CHUNK_BUF      ((volatile uint8_t *)0x24012000)   /* = 68K 0x852000.  K30: OFF-SCREEN
                                     (the 320x224 picture ends at 0x11A00), so loading never
                                     overwrites what is on screen - main.c CMD 50 matches */
#define CMD_FILE_CHUNK_AT 51      /* K15 splash: like CMD 50, but chunk c lands at frame buffer
                                     byte 0x200 + c * 32 KB, so a whole picture fits in order */
#define FILE_SPLASH       0       /* main.c: file number 0 = "IMAGE.RAW" (the splash screen)  */
#define SPLASH_W          320     /* IMAGE.RAW: 320 x 200, 15-bit colour, 2 bytes per pixel   */
#define SPLASH_H          200
#define SPLASH_TOP        12      /* (224 - 200) / 2: black bars above and below             */
#define SPLASH_HOLD       300     /* K16: splash stays up at least this many frames, counted from
                                     when it appears (loading time included).  300 = 5 s at 60 Hz
                                     (NTSC); a PAL console runs 50 Hz, so 300 = 6 s there.      */

#define FILE_KOBOGFX      16      /* main.c maps file number 16 to "KOBOGFX.BIN"   */
#define CART_RAM_SH2      0x22000000
#define CART_SLOT_SIZE    0x20000 /* main.c: cart offset = file number * 0x20000    */
#define PACK_CART_OFF     (FILE_KOBOGFX * CART_SLOT_SIZE)   /* cart mode only */
#define PACK_MAX          0x100000
#define CHUNK_BYTES       32768

#define CDDA_ENABLE       1       /* AUDIO - DO NOT TOUCH */
#define CDDA_TRACK       2       /* title / menu / level music (Track02.wav) */
#define CDDA_CREDITS     3       /* K34: credits screen (Track03.wav) */
#define CDDA_TRANSITION  4       /* K34: between levels (Track04.wav) */

/* pad bits (same as hw_md.h / 32x.h) */
#define PAD_UP     0x0001
#define PAD_DOWN   0x0002
#define PAD_LEFT   0x0004
#define PAD_RIGHT  0x0008
#define PAD_C      0x0020
#define PAD_B      0x0010
#define PAD_A      0x0040
#define PAD_X      0x0400             /* K27: 6-button pad */
#define PAD_MODE   0x0800             /* K36: 6-button pad */
#define PAD_START  0x0080

/* ---- screen layout ----------------------------------------------------------------- */
#define SCR_W      320
#define SCR_H      224
#define PF_W       224            /* Kobo's playfield is WSIZE = 224 x 224 (config.h) */
#define PF_H       224

/* ---- status codes shown by the Sub-CPU on row 10 (STATE:) -------------------------- */
#define ST_LOADING      0x4B01
#define ST_PACK_OK      0x4B02
#define ST_RUNNING      0x4B10
/* HW2: start-up steps, shown by the Sub-CPU as "STATE:xxxx" (green text row) - a hang stops
   on the last step reached */
#define ST_GFX_OK       0x4B03    /* graphics copied into SDRAM */
#define ST_SFX_OK       0x4B04    /* sound pack step done (or skipped) */
#define ST_BRAM_OK      0x4B05    /* backup-RAM step done (or skipped) */
#define ST_TILES_OK     0x4B06    /* tile-set cache done */
#define ST_PANEL_OK     0x4B07    /* panel drawn */
#define ST_MUSIC_OK     0x4B08    /* music started */
#define ST_ERR_OPEN     0x4BE1    /* KOBOGFX.BIN not found on the CD */
#define ST_ERR_VERIFY   0x4BE2    /* a chunk did not verify in the cart */
#define ST_ERR_MAGIC    0x4BE3    /* the cart does not start with 'KGFX' */

static uint32_t frames = 0;
static uint16_t vb_last = 0;
static uint16_t text_shown = 0;         /* HW13: text layer hidden from the splash on (as in Fusion) */

/* K17 player ship state (world pixels, direction 1..8) - see PLAYER SHIP below */
static int ship_x = 32 * 16, ship_y = 96 * 16;   /* scene 1 start (chips 32, 96)  */
static int ship_di = 1;

/* K20 bolts (myship.cpp): world position, speed per step, age (0 = free), direction */
#define MAX_BOLTS  40
#define BEAMV1     12             /* straight speed            */
#define BEAMV2     8              /* diagonal speed (12 * 2/3) */
static int bolt_x[MAX_BOLTS], bolt_y[MAX_BOLTS], bolt_dx[MAX_BOLTS], bolt_dy[MAX_BOLTS];
static int bolt_st[MAX_BOLTS], bolt_di[MAX_BOLTS];
static int bolts_alive = 0;
static uint32_t score = 0;         /* K21 */
static int n_cannons = 0, enemies_alive = 0, ship_hits = 0, gfx2_ok = 0;   /* K22 */
/* K23 game flow (Classic skill: 5 lives, any hit is fatal) */
static int scene_no = 0, lives = 5, dead_timer = 0, game_over = 0, clear_timer = 0, gen_count = 0;
static int loop_no = 0;            /* K27: completed rounds of 50 levels (Kobo "level") */
static int tset_loaded = 0;        /* K27: tile set in SDRAM (0..4) */
static int screen_reset = 0;       /* K27: frame buffers need a fresh start (tile set reloaded) */

/* strip / screen colours - picked from the palette at start-up (pick_colours) */
static uint8_t col_white, col_grey, col_red;     /* picked from the palette at start-up */
static uint8_t col_black = 1, col_green = 1;     /* black: darkest of 1..254 (0 and 255 show
                                                    the Genesis layer - K8 finding)        */

/* HW1 (K29): 16-bit write - COMM14 (the other half) belongs to the Sub-CPU's heartbeat */
static void show_status(uint16_t code) { *(volatile uint16_t *)0x2000402C = code; }

static void count_vblank(void) {
    uint16_t v = SC_VDP_FBCTL & 0x8000;
    if (v && !vb_last) frames++;
    vb_last = v;
}

/* PRI bit 7 of DISPMODE decides whether the Genesis text layer is in front of the 32X
   picture.  K18: FOUND IN FUSION (K17): with bit 7 SET the text is in front, with it CLEAR
   the 32X picture is in front - the opposite of how K11 read the manual (K17 started with
   the text showing although it asked for "text off").  Same as V80, which set bit 7 to
   show its text.  To verify on hardware.
   on = text visible = bit 7 set;  off = text hidden = bit 7 clear. */
#define DISP_PRI_TEXT_FRONT  0x0080
static void wait_cmd(uint16_t cmd);
#define CMD_TEXT_ONOFF    52      /* K19: COMM2 = 1 on / 0 off - Genesis display on/off */
static void set_text(uint16_t on) {
    uint16_t m = SC_VDP_DISPMODE;
    /* K19: switch the Genesis display itself on/off (main.c CMD 52 -> VDP register 1).
       Off = all Genesis text blanked to black, whatever the priority bit does. */
    SC_COMM2 = on ? 1 : 0;
    wait_cmd(CMD_TEXT_ONOFF);
    if (on) m = (uint16_t)(m | DISP_PRI_TEXT_FRONT);
    else    m = (uint16_t)(m & ~DISP_PRI_TEXT_FRONT);
    SC_VDP_DISPMODE = m;
    text_shown = on;
}

static void take_fb(void) {
    SC_SYS_ADAPTER_B = 0x80;
    while ((MARS_SYS_INTMSK & 0x8000) == 0) { }
}

static void flip_wait(void) {
    uint16_t cur = SC_VDP_FBCTL & 1;
    SC_VDP_FBCTL = cur ^ 1;
    while ((SC_VDP_FBCTL & 1) == cur);
}

/* HW6: COMM COLLISION RULE (32X manual; the V70 COMM6 crash): a COMM register must not be
   hammered by one CPU while the other writes it.  HW5 on the console: after ~20 s the Sega CD
   side stopped (Sub heartbeat frozen, pad request left in COMM0) - the SH2 read COMM0 millions
   of times a second while the 68K wrote it.  Every wait now pauses ~20 us between reads
   (50x fewer reads, same timeouts). */
static void comm_pause(void) {
    volatile int k;
    for (k = 0; k < 64; k++) { }
}
static void wait_cmd(uint16_t cmd) {
    uint32_t n;
    MARS_SYS_COMM0 = cmd;
    for (n = 0; n < 800000UL && MARS_SYS_COMM0 != 0; n++) { count_vblank(); comm_pause(); }
}

/* ====================================================================================
 * WORK AREA - WHY the big arrays are NOT normal C arrays:
 * crt0.s clears all zero-initialised memory (.bss) byte by byte, cache off, BEFORE it
 * writes COMM4 = 0 to release the slave SH2.  The Sub-CPU writes COMM4 = 0x1234 as the
 * "go" signal around the same time.  With ~47 KB of arrays in .bss the clear took long
 * enough that crt0's COMM4 = 0 landed AFTER 0x1234 and erased it -> main() waited forever
 * (seen as COMM10 = 0003 on the console).  These arrays are always filled before use, so
 * they live in a fixed SDRAM area instead; .bss is back to V80's small size and timing.
 *   0x06020000 - 0x0602BFFF  (48 KB)   code/data/bss end near 0x06002000, master stack
 *   starts at 0x0603F400 and grows down, slave stack above it.
 * ==================================================================================== */
#define WORK_BASE      0x06020000
#define WORK_MAP       (WORK_BASE + 0x0000)    /* 64 x 128 x 2 = 16 KB  map cells     */
#define WORK_SITEX     (WORK_BASE + 0x4000)    /* 1024 x 4    =  4 KB  maze sites x   */
#define WORK_SITEY     (WORK_BASE + 0x5000)    /* 1024 x 4    =  4 KB  maze sites y   */
#define WORK_TILES     (WORK_BASE + 0x6000)    /* 24 x 256    =  6 KB  tile set       */
#define WORK_STARS     (WORK_BASE + 0x7800)    /* 64 x 256    = 16 KB  star tiles     */
#define WORK_PACKHDR   (WORK_BASE + 0xB800)    /* 0x208+512   ~  1 KB  pack header    */
#define WORK_PLAYER    (WORK_BASE + 0xC000)    /* 16 x 20x20  = 6400 B  ship frames    */
#define WORK_BOLTGFX   (WORK_BASE + 0xD900)    /* 16 x 8x8    = 1 KB   bolt frames    */
#define WORK_DAMAGE    (WORK_BASE + 0xDD00)    /* 64 x 128    = 8 KB   damage per cell */
#define WORK_EN4GFX    (WORK_BASE + 0xFD00)    /* 16 x 20x20  = 6400 B  enemy4 (missile3) */
#define WORK_BEAMGFX   (WORK_BASE + 0x11600)   /* 16 x 8x8    = 1 KB   beam bullets   */
#define WORK_EXPLGFX   (WORK_BASE + 0x11A00)   /*  8 x 24x24  = 4608 B explosion     */
#define WORK_ENEMIES   (WORK_BASE + 0x12C00)   /* 48 x 48 B   = 2304 B enemy table   */
#define WORK_CANNONS   (WORK_BASE + 0x13600)   /* 96 x 20 B   = 1920 B node/core list */
#define WORK_FONT      (WORK_BASE + 0x12C00)   /* K30: 43 x 7x9  = 2709 B menu/panel font (old enemy area) */
#define WORK_FONTW     (WORK_BASE + 0x13700)   /* K30: 43 glyph widths */
#define WORK_EN1GFX    (WORK_BASE + 0x13D80)   /* 16 x 20x20  = 6400 B  enemy1 (missile1) */
#define WORK_EN3GFX    (WORK_BASE + 0x15680)   /* 16 x 20x20  = 6400 B  enemy3 (missile2) */
#define WORK_ROCKGFX   (WORK_BASE + 0x16F80)   /* 32 x 16x16  = 8 KB    rock1          */
#define WORK_RINGGFX   (WORK_BASE + 0x18F80)   /* 16 x 16x16  = 4 KB    ring           */
#define WORK_EN6GFX    (WORK_BASE + 0x19F80)   /* 16 x 20x20  = 6400 B  enemy6 (bmr-purple) */
#define WORK_BOMBGFX   (WORK_BASE + 0x1B880)   /* 16 x 12x12  = 2304 B  bomb           */
#define WORK_DETOGFX   (WORK_BASE + 0x1C180)   /*  8 x 20x20  = 3200 B  bomb explosion */
/* K27 - SECOND WORK AREA in the SDRAM between the program and WORK_BASE (it was unused).
   RULE: the SH2 program (code + data + bss) must end below WORK2_BASE (48 KB; it is ~20 KB).
   main() checks this at start-up (status 4BE6 = program too big). */
#define WORK2_BASE     0x0600C000
#define WORK2_FIGHTER  (WORK2_BASE + 0x00000)  /* 16 x 20x20  = 6400 B  enemy2 (fighter)    */
#define WORK2_BMRG     (WORK2_BASE + 0x01900)  /* 16 x 20x20  = 6400 B  enemy5 (bmr-green)  */
#define WORK2_BMRP     (WORK2_BASE + 0x03200)  /* 16 x 20x20  = 6400 B  enemy7 (bmr-pink)   */
#define WORK2_BIGSHIP  (WORK2_BASE + 0x04B00)  /* 16 x 36x36  = 20 KB   mother ships        */
#define WORK2_ROCK2    (WORK2_BASE + 0x09C00)  /* 32 x 16x16  = 8 KB    rock2               */
#define WORK2_ROCK3    (WORK2_BASE + 0x0BC00)  /* 48 x 16x16  = 12 KB   rock3 (shiny)       */
#define WORK2_ENEMIES  (WORK2_BASE + 0x0EC00)  /* 128 x 48 B  = 6 KB    enemy table         */
#define WORK2_CANNONS  (WORK2_BASE + 0x10400)  /* 512 x 20 B  = 10 KB   guns (max 422 seen) */
#define WORK2_END      (WORK2_BASE + 0x12C00)  /* 0x0601EC00 < WORK_BASE 0x06020000          */
#define RLE_A          (WORK_BASE + 0x9800)    /* K35: freed half of the star tiles, 8 KB  */
#define RLE_B          0x0601EC00              /* K35: gap after WORK2, 5 KB: explo5, bolt
                                                  and bullet hits (K35c) = 4.8 KB           */
#define RLE_C          (WORK_BASE + 0x1CE00)   /* K35: after WORK_END: rock + ring explosions
                                                  (K35c) = 5.8 KB -> 0x0603E4DC; the stack
                                                  keeps 0x0603E4DC..0x0603F400 = 3.8 KB     */
#define RLE_FB         ((volatile uint8_t *)0x2401D800)   /* K35: spare frame-buffer memory
                                                  after the tile-set cache, 10 KB, BOTH
                                                  buffers (loaded with the tile sets)       */
#define WORK_END       (WORK_BASE + 0x1CE00)   /* 0x0603CE00 - master stack top 0x0603F400
                                                  (~9.7 KB left for the stack; this program
                                                  needs well under 2 KB).  rock2/rock3 do not
                                                  fit: all rocks use rock1 for now. */

/* ====================================================================================
 * Kobo's random number generator (random.h) - same maths, so maps match Kobo's style.
 * gamerand = map generation / game AI, pubrand = cosmetic (tile variants).
 * ==================================================================================== */
typedef struct { uint32_t seed; } krand_t;
static krand_t gamerand, pubrand;

static uint32_t krand_get(krand_t *r) { r->seed = r->seed * 1566083941UL + 1; return r->seed; }
/* WHY no "x >> n" with a variable n anywhere: the SH-2 has no variable-shift instruction,
   GCC then calls __lshrsi3_r0 from libgcc, and this project links with -nostdlib.
   Kobo's get(bits) = top 'bits' bits of the next number; only 2, 6 and 8 are used. */
static uint32_t krand_2(krand_t *r) { return krand_get(r) >> 30; }
static uint32_t krand_6(krand_t *r) { return krand_get(r) >> 26; }
static uint32_t krand_8(krand_t *r) { return krand_get(r) >> 24; }
static uint32_t krand_1(krand_t *r) { return krand_get(r) >> 31; }   /* K22 */
static uint32_t krand_4(krand_t *r) { return krand_get(r) >> 28; }   /* K22 */

/* unsigned a % b without libgcc: shift-subtract, taking a's bits from the top with fixed
   one-bit shifts (b is always small here) */
static uint32_t umod(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    int i;
    for (i = 0; i < 32; i++) {
        r = (r << 1) | (a >> 31);
        a <<= 1;
        if (r >= b) r -= b;
    }
    return r;
}

/* ====================================================================================
 * Kobo's map (map.h / map.cpp) - ported to C, logic unchanged.
 * A map cell: low byte = state bits, high byte = tile number (or star number in space).
 * ==================================================================================== */
#define MAP_SIZEX_LOG2 6
#define MAP_SIZEX      64
#define MAP_SIZEY      128
#define SITE_MAX       1024
#define WALL           1
#define U_MASK         (1 << 0)
#define R_MASK         (1 << 1)
#define D_MASK         (1 << 2)
#define L_MASK         (1 << 3)
#define CORE           (1 << 4)
#define HARD           (1 << 5)
#define SPACE          (1 << 6)
#define IS_SPACE(x)    ((x) & SPACE)

#define map_data  ((uint16_t *)WORK_MAP)      /* [MAP_SIZEX * MAP_SIZEY], see WORK AREA */
#define sitex     ((int *)WORK_SITEX)         /* [SITE_MAX] */
#define sitey     ((int *)WORK_SITEY)         /* [SITE_MAX] */
static int site_max;

static uint16_t *mpos(int x, int y) {
    x &= MAP_SIZEX - 1;
    y &= MAP_SIZEY - 1;
    return &map_data[(y << MAP_SIZEX_LOG2) + x];
}

static void map_init(void) {
    int i, j;
    for (i = 0; i < MAP_SIZEX; i++)
        for (j = 0; j < MAP_SIZEY; j++)
            *mpos(i, j) = SPACE;
}

static void maze_push(int x, int y) {
    sitex[site_max] = x;
    sitey[site_max++] = y;
    *mpos(x, y) = WALL;
}

static int maze_pop(void) {
    int i;
    if (site_max == 0) return 1;
    i = (int)umod(krand_get(&gamerand), (uint32_t)site_max);
    site_max--;
    if (i != site_max) {
        int tx = sitex[site_max], ty = sitey[site_max];
        sitex[site_max] = sitex[i];
        sitey[site_max] = sitey[i];
        sitex[i] = tx;
        sitey[i] = ty;
    }
    return 0;
}

static void maze_move_and_push(int x, int y, int d) {
    int x1 = x, y1 = y;
    switch (d) {
      case 1: x1 += 2; break;
      case 2: y1 += 2; break;
      case 3: x1 -= 2; break;
      case 4: y1 -= 2; break;
    }
    maze_push(x1, y1);
    *mpos((x + x1) / 2, (y + y1) / 2) = WALL;
}

static int maze_judge(int cx, int cy, int dx, int dy, int x, int y) {
    if ((x < cx - dx) || (x > cx + dx) || (y < cy - dy) || (y > cy + dy)) return 0;
    if (*mpos(x, y) == WALL) return 0;
    return 1;
}

static void make_maze(int x, int y, int difx, int dify) {
    int i, j, vx, vy;
    for (i = x - difx; i <= x + difx; i++)
        for (j = y - dify; j <= y + dify; j++)
            *mpos(i, j) = SPACE;
    site_max = 0;
    if (krand_8(&gamerand) < 128) {
        *mpos(x, y) = CORE | R_MASK | L_MASK;
        maze_push(x - 1, y);
        maze_push(x + 1, y);
    } else {
        *mpos(x, y) = CORE | U_MASK | D_MASK;
        maze_push(x, y - 1);
        maze_push(x, y + 1);
    }
    for (;;) {
        int dirs[4], dirs_max = 0;
        if (maze_pop()) break;
        vx = sitex[site_max];
        vy = sitey[site_max];
        if (maze_judge(x, y, difx, dify, vx + 2, vy + 0)) dirs[dirs_max++] = 1;
        if (maze_judge(x, y, difx, dify, vx + 0, vy + 2)) dirs[dirs_max++] = 2;
        if (maze_judge(x, y, difx, dify, vx - 2, vy + 0)) dirs[dirs_max++] = 3;
        if (maze_judge(x, y, difx, dify, vx + 0, vy - 2)) dirs[dirs_max++] = 4;
        if (dirs_max == 0) continue;
        i = (int)umod(krand_get(&gamerand), (uint32_t)dirs_max);
        maze_move_and_push(vx, vy, dirs[i]);
        maze_push(vx, vy);
    }
}

/* map.cpp packs this table into 0x00030210 and reads it with a variable shift;
   same values as a plain table (see the no-variable-shift note above) */
static const uint8_t hard_tile[16] = { 0, 0, 1, 0, 2, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0 };

static int bits2tile(int n) {
    if (n & CORE)       return (n & (U_MASK | D_MASK)) ? 6 : 7;
    else if (n & HARD)  return hard_tile[n & 15];
    else if (n == 5)    return krand_2(&pubrand) ? 13 : 4;
    else if (n == 10)   return krand_2(&pubrand) ? 18 : 5;
    else                return n + 8;
}

static void map_clearpos(int x, int y) {
    *mpos(x, y) = (uint16_t)((krand_6(&gamerand) << 8) | SPACE);
}

static void map_convert(unsigned ratio) {
    int i, j, p;
    for (i = 0; i < MAP_SIZEX; i++)
        for (j = 0; j < MAP_SIZEY; j++) {
            p = *mpos(i, j) & CORE;
            if (IS_SPACE(*mpos(i, j))) { map_clearpos(i, j); continue; }
            if ((j > 0) && !IS_SPACE(*mpos(i, j - 1)))             p |= U_MASK;
            if ((i < MAP_SIZEX - 1) && !IS_SPACE(*mpos(i + 1, j))) p |= R_MASK;
            if ((j < MAP_SIZEY - 1) && !IS_SPACE(*mpos(i, j + 1))) p |= D_MASK;
            if ((i > 0) && !IS_SPACE(*mpos(i - 1, j)))             p |= L_MASK;
            if ((p == U_MASK) || (p == R_MASK) || (p == D_MASK) || (p == L_MASK))
                if (krand_8(&gamerand) < ratio) p |= HARD;
            *mpos(i, j) = (uint16_t)((bits2tile(p) << 8) | p);
        }
}

/* Level 1 from scenes.cpp:  ratio 0, start (32,96), two bases */
static const int scene1_ratio = 0, scene1_startx = 32, scene1_starty = 96;
static const int scene1_base[2][4] = { {48, 64, 4, 4}, {24, 80, 4, 4} };

/* K27 - ALL 50 LEVELS, generated by a script from Kobo Deluxe scenes.cpp (no hand copying).
   Like Kobo, only the first enemy_max sets / base_max bases of a level are used.
   {ratio, startx, starty, i1, i2, k1, k2, nsets, {{count, speed, type}..}, nbases, {{x,y,h,v}..}}
   ratio = chance (of 256) that a pipe end is indestructible (256 = all: levels 25, 37).
   i1/i2 = node/core firing interval, k1/k2 = what they fire.  Types (ET_ numbers): 1 enemy4,
   2 beam, 4 enemy1, 5 enemy3, 6 rock, 7 ring, 8 enemy6, 9 bomb1, 10 bomb2, 12 enemy2,
   13 enemy5, 14 enemy7, 15-18 enemy_m1-m4 (mother ships).  Stored as bytes: ~9 KB. */
typedef struct { uint16_t ratio; uint8_t startx, starty, i1, i2, k1, k2, nsets, set[8][3], nbases, base[38][4]; } kscene_t;
static const kscene_t kscene[50] = {
 {0,32,96,128,16,2,2,1,{{10,5,1}},2,{{48,64,4,4},{24,80,4,4}}},   /* 1 */
 {0,32,96,64,64,2,2,3,{{10,5,1},{10,5,1},{10,5,4}},4,{{16,48,6,6},{48,48,6,6},{16,64,6,6},{48,64,6,6}}},   /* 2 */
 {64,32,64,64,16,2,2,1,{{20,3,6}},6,{{16,40,4,4},{16,60,4,4},{16,80,4,4},{48,40,4,4},{48,60,4,4},{48,80,4,4}}},   /* 3 */
 {0,32,96,32,8,2,2,1,{{10,5,1}},3,{{14,55,6,6},{55,14,6,6},{35,70,6,6}}},   /* 4 */
 {0,32,96,128,128,2,2,3,{{10,5,8},{10,5,8},{10,5,1}},2,{{32,32,6,6},{32,64,6,6}}},   /* 5 */
 {0,32,96,32,8,4,4,0,{{0,0,0}},1,{{32,64,8,8}}},   /* 6 */
 {0,32,64,64,32,7,4,2,{{30,5,5},{20,5,8}},8,{{32,54,3,3},{42,54,3,3},{42,64,3,3},{42,74,3,3},{32,74,3,3},{22,74,3,3},{22,64,3,3},{22,54,3,3}}},   /* 7 */
 {0,32,64,64,8,1,4,3,{{20,5,7},{20,5,7},{20,5,7}},8,{{48,64,4,4},{16,64,4,4},{32,80,4,4},{32,48,4,4},{44,76,4,4},{20,76,4,4},{44,52,4,4},{20,52,4,4}}},   /* 8 */
 {0,32,96,16,8,2,2,1,{{20,5,10}},1,{{32,32,7,7}}},   /* 9 */
 {0,32,96,128,4,9,9,1,{{100,5,6}},1,{{32,64,12,12}}},   /* 10 */
 {0,32,96,128,32,12,4,5,{{30,5,6},{50,5,5},{20,5,8},{10,5,14},{20,5,9}},6,{{5,64,3,3},{15,64,3,3},{25,64,3,3},{35,64,3,3},{45,64,3,3},{55,64,3,3}}},   /* 11 */
 {0,47,33,128,16,2,2,3,{{10,5,8},{10,5,8},{100,5,6}},3,{{15,88,12,12},{21,20,12,12},{50,70,12,12}}},   /* 12 */
 {0,32,64,128,32,10,4,2,{{30,5,9},{20,5,9}},5,{{24,66,3,3},{23,80,3,3},{44,50,3,3},{39,102,3,3},{15,43,3,3}}},   /* 13 */
 {0,24,107,128,8,12,2,4,{{10,5,5},{10,5,8},{100,5,6},{10,5,12}},3,{{9,36,5,5},{44,19,5,5},{46,79,5,5}}},   /* 14 */
 {0,36,94,64,32,1,9,2,{{10,5,8},{50,5,6}},5,{{16,54,4,4},{47,56,4,4},{24,70,4,4},{31,14,4,4},{27,31,4,4}}},   /* 15 */
 {0,31,37,64,8,8,8,3,{{20,5,4},{10,5,5},{100,5,6}},5,{{41,84,6,10},{12,115,6,5},{22,92,6,10},{11,26,8,8},{51,65,9,7}}},   /* 16 */
 {0,53,17,32,32,2,4,1,{{10,5,5}},10,{{14,99,6,6},{37,18,6,6},{13,67,6,6},{19,46,6,6},{41,59,6,6},{39,97,6,6},{47,32,6,6},{56,100,6,6},{19,25,6,6},{54,76,6,6}}},   /* 17 */
 {0,32,80,32,8,4,7,4,{{40,2,6},{20,5,4},{40,5,6},{20,5,4}},3,{{27,104,5,5},{49,105,5,5},{32,90,5,5}}},   /* 18 */
 {0,32,64,128,8,7,4,0,{{0,0,0}},16,{{32,91,3,3},{42,89,3,3},{51,83,3,3},{57,74,3,3},{59,64,3,3},{57,54,3,3},{51,45,3,3},{42,39,3,3},{32,37,3,3},{22,39,3,3},{13,45,3,3},{7,54,3,3},{5,64,3,3},{7,74,3,3},{13,83,3,3},{22,89,3,3}}},   /* 19 */
 {0,7,84,128,16,14,13,0,{{0,0,0}},20,{{37,82,6,6},{13,99,6,6},{30,44,6,6},{19,14,6,6},{55,85,6,6},{23,69,6,6},{16,35,6,6},{11,52,6,6},{36,116,6,6},{39,97,6,6},{50,114,6,6},{39,22,6,6},{55,43,6,6},{52,71,6,6},{56,100,6,6},{54,25,6,6},{51,57,6,6},{7,115,6,6},{47,8,6,6},{8,70,6,6}}},   /* 20 */
 {0,26,30,128,16,4,2,2,{{20,5,5},{50,5,6}},6,{{27,79,5,5},{6,105,5,5},{11,50,5,5},{11,23,5,5},{56,57,5,5},{7,70,5,5}}},   /* 21 */
 {0,38,120,128,16,7,2,6,{{10,5,7},{10,5,7},{10,5,7},{10,5,7},{10,5,7},{1,5,15}},30,{{55,25,4,4},{49,12,4,4},{30,84,4,4},{33,97,4,4},{24,21,4,4},{28,52,4,4},{50,117,4,4},{22,95,4,4},{46,56,4,4},{36,10,4,4},{15,77,4,4},{32,74,4,4},{7,15,4,4},{13,107,4,4},{6,51,4,4},{11,62,4,4},{57,76,4,4},{57,95,4,4},{11,90,4,4},{46,93,4,4},{34,27,4,4},{58,56,4,4},{40,45,4,4},{10,31,4,4},{44,35,4,4},{25,122,4,4},{24,62,4,4},{51,107,4,4},{16,50,4,4},{25,110,4,4}}},   /* 22 */
 {0,13,92,64,32,9,9,4,{{20,5,4},{10,5,12},{10,5,14},{20,5,8}},5,{{40,95,15,15},{44,28,13,18},{15,16,12,15},{14,50,13,12},{49,62,12,11}}},   /* 23 */
 {127,32,41,128,16,4,2,3,{{50,5,6},{20,5,12},{30,5,10}},8,{{38,73,4,4},{27,98,4,4},{27,71,4,4},{56,89,4,4},{39,6,4,4},{12,67,4,4},{58,14,4,4},{14,21,4,4}}},   /* 24 */
 {256,32,96,32,32,2,4,2,{{50,5,8},{100,5,6}},2,{{25,53,7,6},{40,70,5,4}}},   /* 25 */
 {0,32,64,64,16,7,7,0,{{0,0,0}},32,{{8,8,5,5},{24,8,5,5},{40,8,5,5},{56,8,5,5},{8,24,5,5},{24,24,5,5},{40,24,5,5},{56,24,5,5},{8,40,5,5},{24,40,5,5},{40,40,5,5},{56,40,5,5},{8,56,5,5},{24,56,5,5},{40,56,5,5},{56,56,5,5},{8,72,5,5},{24,72,5,5},{40,72,5,5},{56,72,5,5},{8,88,5,5},{24,88,5,5},{40,88,5,5},{56,88,5,5},{8,104,5,5},{24,104,5,5},{40,104,5,5},{56,104,5,5},{8,120,5,5},{24,120,5,5},{40,120,5,5},{56,120,5,5}}},   /* 26 */
 {0,32,64,32,32,2,13,3,{{20,5,4},{50,5,9},{1,5,16}},4,{{49,64,6,10},{40,40,11,4},{36,95,13,4},{13,74,8,10}}},   /* 27 */
 {4,45,107,32,16,4,13,1,{{50,5,6}},4,{{15,108,11,18},{13,62,12,19},{46,42,11,41},{16,23,13,13}}},   /* 28 */
 {0,32,60,128,4,12,2,5,{{20,5,13},{10,5,10},{50,5,6},{1,5,15},{10,5,14}},5,{{40,46,13,9},{20,74,12,8},{24,106,11,6},{29,120,14,6},{54,90,5,8}}},   /* 29 */
 {0,23,71,64,16,13,2,3,{{2,5,8},{2,5,8},{1,5,18}},20,{{38,120,5,5},{43,64,5,5},{32,106,5,5},{14,52,5,5},{48,76,5,5},{9,110,5,5},{12,34,5,5},{40,90,5,5},{38,33,5,5},{23,9,5,5},{22,120,5,5},{9,90,5,5},{6,15,5,5},{41,46,5,5},{43,13,5,5},{25,88,5,5},{8,65,5,5},{57,49,5,5},{52,108,5,5},{6,77,5,5}}},   /* 30 */
 {0,32,120,64,8,7,7,0,{{0,0,0}},16,{{24,8,6,6},{24,24,6,6},{24,40,6,6},{24,56,6,6},{24,72,6,6},{24,88,6,6},{24,104,6,6},{24,120,6,6},{40,8,6,6},{40,24,6,6},{40,40,6,6},{40,56,6,6},{40,72,6,6},{40,88,6,6},{40,104,6,6},{40,120,6,6}}},   /* 31 */
 {0,32,96,128,16,2,2,0,{{0,0,0}},1,{{32,64,24,24}}},   /* 32 */
 {0,24,113,32,16,1,1,5,{{30,5,13},{50,5,9},{1,5,16},{20,5,12},{100,5,6}},5,{{17,70,13,21},{46,32,10,10},{49,63,10,11},{18,25,14,21},{45,109,10,17}}},   /* 33 */
 {0,54,120,64,16,8,2,1,{{10,5,10}},15,{{8,87,6,15},{26,72,9,12},{37,110,9,6},{36,31,16,5},{32,11,17,9},{46,51,6,12},{54,96,5,10},{12,39,5,11},{13,116,9,9},{40,93,7,6},{7,64,6,6},{7,10,5,8},{47,77,8,5},{28,46,5,6},{56,14,5,5}}},   /* 34 */
 {0,32,64,128,32,5,7,8,{{10,5,4},{1,5,15},{10,5,10},{10,5,12},{1,5,16},{10,5,1},{10,5,13},{10,5,14}},8,{{32,48,7,7},{48,48,7,7},{48,64,7,7},{48,80,7,7},{32,80,7,7},{16,80,7,7},{16,64,7,7},{16,48,7,7}}},   /* 35 */
 {0,32,64,16,64,2,12,5,{{10,10,4},{10,10,12},{10,10,10},{1,10,15},{1,10,16}},10,{{16,20,4,4},{16,40,4,4},{16,60,4,4},{16,80,4,4},{16,100,4,4},{48,20,4,4},{48,40,4,4},{48,60,4,4},{48,80,4,4},{48,100,4,4}}},   /* 36 */
 {256,32,96,64,64,2,7,0,{{0,0,0}},1,{{32,58,18,18}}},   /* 37 */
 {0,32,72,64,4,7,9,1,{{10,5,6}},20,{{16,48,3,3},{24,48,3,3},{32,48,3,3},{40,48,3,3},{48,48,3,3},{16,64,3,3},{24,64,3,3},{32,64,3,3},{40,64,3,3},{48,64,3,3},{16,80,3,3},{24,80,3,3},{32,80,3,3},{40,80,3,3},{48,80,3,3},{16,96,3,3},{24,96,3,3},{32,96,3,3},{40,96,3,3},{48,96,3,3}}},   /* 38 */
 {0,32,50,32,16,13,12,6,{{1,5,16},{50,5,6},{50,5,6},{50,5,6},{1,5,15},{50,5,6}},8,{{34,14,4,4},{16,22,4,4},{6,35,4,4},{22,38,4,4},{45,40,4,4},{6,52,4,4},{46,66,4,4},{30,70,4,4}}},   /* 39 */
 {0,28,11,128,16,9,4,1,{{10,5,8}},35,{{12,48,9,9},{14,80,6,5},{42,14,7,3},{52,112,4,9},{33,103,5,6},{17,12,4,3},{49,78,5,4},{49,50,4,5},{19,32,5,5},{32,77,6,4},{9,113,4,9},{36,38,7,6},{50,96,8,3},{57,7,3,5},{53,34,5,7},{11,94,5,3},{27,63,3,6},{44,4,3,3},{48,63,5,6},{37,51,6,3},{28,116,3,3},{8,31,4,5},{4,18,3,3},{13,69,6,4},{21,96,3,3},{36,114,3,3},{55,87,5,3},{58,63,3,7},{35,87,4,4},{35,25,3,4},{20,107,3,5},{13,4,3,3},{23,21,4,4},{5,8,3,5},{47,22,7,3}}},   /* 40 */
 {0,32,96,64,16,2,2,1,{{40,10,10}},1,{{32,64,18,18}}},   /* 41 */
 {0,54,46,64,32,14,13,3,{{10,5,10},{10,5,4},{10,5,8}},20,{{8,106,5,8},{7,32,5,13},{39,83,9,7},{54,63,8,9},{28,20,6,13},{16,61,14,10},{55,25,6,13},{53,106,8,7},{38,115,5,7},{22,109,7,14},{12,82,7,8},{39,51,5,5},{57,89,5,7},{41,21,5,7},{10,10,9,6},{20,41,5,5},{38,66,5,5},{54,121,7,5},{37,100,5,6},{41,7,5,5}}},   /* 42 */
 {0,22,107,64,8,12,9,7,{{20,5,1},{30,5,10},{20,5,12},{20,5,8},{30,5,10},{30,5,14},{1,5,17}},5,{{49,38,8,14},{20,86,5,10},{19,28,14,16},{19,57,17,8},{48,86,8,18}}},   /* 43 */
 {0,32,64,64,8,12,7,4,{{20,7,10},{20,8,6},{1,5,15},{20,8,6}},24,{{46,64,3,3},{18,64,3,3},{32,79,3,3},{32,48,3,3},{43,75,3,3},{21,75,3,3},{43,53,3,3},{21,53,3,3},{32,91,3,3},{42,89,3,3},{51,83,3,3},{57,74,3,3},{59,64,3,3},{57,54,3,3},{51,45,3,3},{42,39,3,3},{32,37,3,3},{22,39,3,3},{13,45,3,3},{7,54,3,3},{5,64,3,3},{7,74,3,3},{13,83,3,3},{22,89,3,3}}},   /* 44 */
 {0,31,87,64,16,9,7,1,{{20,5,10}},10,{{18,66,15,15},{52,59,7,54},{31,101,8,7},{39,41,4,30},{9,97,7,13},{21,44,11,5},{25,118,5,5},{20,16,8,14},{42,122,10,4},{5,16,4,4}}},   /* 45 */
 {0,59,34,64,16,2,2,4,{{15,5,6},{1,5,17},{15,5,6},{1,5,15}},35,{{46,19,9,10},{45,115,10,4},{14,60,5,8},{48,60,5,10},{21,45,8,5},{11,83,5,13},{39,97,13,8},{36,49,4,3},{18,8,5,4},{40,80,9,5},{54,44,8,4},{13,27,9,3},{15,110,3,5},{9,122,5,4},{28,63,6,6},{35,39,3,3},{6,5,5,3},{6,113,4,3},{6,40,4,5},{29,116,3,9},{29,9,4,4},{59,65,3,10},{21,86,3,7},{4,60,3,6},{6,18,5,4},{29,22,5,6},{52,4,10,3},{18,19,4,3},{46,34,5,3},{57,91,3,4},{59,103,3,6},{25,35,4,3},{19,122,3,4},{54,80,3,3},{5,101,3,3}}},   /* 46 */
 {0,24,31,64,32,13,7,5,{{10,5,4},{10,5,14},{10,5,4},{10,5,14},{10,5,10}},35,{{29,37,19,2},{24,109,3,11},{7,62,3,4},{47,92,2,4},{50,112,7,12},{54,22,8,10},{35,104,5,7},{32,69,9,5},{27,46,17,5},{32,14,9,10},{12,28,8,4},{5,94,4,18},{46,57,7,2},{46,83,11,3},{58,76,4,2},{56,94,4,4},{59,63,2,3},{16,120,3,4},{14,75,2,11},{17,18,2,2},{7,6,6,4},{37,28,6,2},{6,47,2,3},{50,72,2,2},{57,44,2,9},{18,8,2,6},{46,8,2,2},{25,90,4,2},{4,37,3,3},{17,98,2,2},{27,58,9,2},{27,81,5,3},{57,7,5,2},{7,120,2,6},{36,92,3,3}}},   /* 47 */
 {0,19,92,64,128,4,12,3,{{10,5,4},{10,5,10},{20,5,6}},38,{{16,54,3,3},{47,56,3,4},{24,70,4,3},{31,14,3,3},{27,31,4,3},{36,94,4,4},{21,19,3,4},{36,68,4,3},{53,4,3,3},{36,80,4,4},{17,36,4,3},{34,118,4,3},{40,13,3,4},{55,121,4,4},{43,26,4,3},{12,71,4,4},{58,108,3,4},{51,36,4,4},{23,94,3,3},{46,73,4,4},{9,25,4,3},{27,48,3,3},{50,91,3,3},{14,122,3,3},{17,113,3,4},{16,8,3,4},{36,38,3,4},{25,103,4,3},{55,70,3,4},{28,56,3,3},{23,85,3,3},{50,108,3,3},{25,122,3,3},{14,82,4,4},{50,16,4,4},{38,49,4,3},{4,6,3,3},{7,38,3,3}}},   /* 48 */
 {0,38,67,32,8,2,2,6,{{10,5,12},{50,5,9},{20,5,4},{10,5,12},{1,5,15},{20,5,4}},22,{{28,104,10,6},{43,28,12,7},{22,72,7,10},{11,16,8,6},{50,102,8,4},{12,40,5,6},{49,80,11,5},{6,95,4,13},{51,61,6,10},{30,14,4,4},{17,118,6,6},{34,52,6,7},{40,119,15,6},{31,91,15,4},{53,10,8,9},{8,61,4,7},{7,28,5,4},{58,92,4,4},{19,55,5,5},{25,29,4,5},{58,44,4,4},{9,76,4,4}}},   /* 49 */
 {0,32,120,8,8,2,4,1,{{80,5,10}},1,{{32,60,30,45}}},   /* 50 */
};
#define NUM_SCENES 50


/* ====================================================================================
 * The graphics pack in the cart (layout: tools/kobo_gfx.py).  The cart is 16-bit, so
 * every read is a 16-bit read.
 * ==================================================================================== */
#define BANK_TILES1   0           /* 0..4 = the five tile sets  (full KOBOGFX.BIN, both modes) */
#define BANK_STARS    30          /* flatstars1.png              */
#define BANK_PLAYER   5           /* player.png: 16 frames of 20 x 20 (kobo_gfx.py order) */
#define SHIP_W        20
#define SHIP_H        20
#define SHIP_FRAMES   16
#define player_gfx    ((uint8_t *)WORK_PLAYER)
#define BANK_BOLT     13          /* bolt.png: 16 frames of 8 x 8 = 4 directions x 4 animation */
#define BOLT_W        8
#define bolt_gfx      ((uint8_t *)WORK_BOLTGFX)
#define TILE_FRAMES   24
#define STAR_FRAMES   32          /* K35: 32 of Kobo's 64 star tiles (the map picks one at random,
                                     frame & 31) - frees 8 KB for RLE explosions */

#define tileset   ((uint8_t (*)[256])WORK_TILES)   /* [TILE_FRAMES][256] current tile set */
#define stars     ((uint8_t (*)[256])WORK_STARS)   /* [STAR_FRAMES][256]                  */
#define PACK_MAX_BANKS 64
#define PACK_HDR_SIZE (0x208 + PACK_MAX_BANKS * 16)   /* K30b: was 32 banks - the K30 pack has 34, so
                                               the font's table entries (banks 32, 33) were never
                                               read: garbage text.  0x608 bytes, still ends
                                               before WORK_PLAYER (0xB800 + 0x608 < 0xC000). */
#define pack_hdr  ((uint8_t *)WORK_PACKHDR)       /* magic, palette, bank table          */

static void give_fb(void) {
    SC_SYS_ADAPTER_B = 0;
    while ((MARS_SYS_INTMSK & 0x8000) != 0) { }
}

static void wait_cmd_no_vdp(uint16_t cmd) {   /* while the 68K owns the FB: no VDP reads */
    MARS_SYS_COMM0 = cmd;
    while (MARS_SYS_COMM0 != 0) comm_pause();          /* HW6: slow polling */
}

static uint32_t pack_len = 0;              /* file length (no-cart) / 0 (cart)          */
static uint32_t fb_chunk = 0xFFFFFFFF;     /* which 32 KB chunk is in the frame buffer   */
static uint32_t n_cmd50 = 0;               /* strip readout: CMD 50 requests sent         */
static uint32_t cmd50_reply = 0;           /* strip readout: Sub-CPU reply (see main.c)   */
static uint32_t cmd50_log[3];              /* K11: replies of the first 3 CMD 50 requests */
/* K12: the tile / star offsets from the bank table, captured right after the header was
   read: as they sit in the FRAME BUFFER, and as they landed in the SH2's copy (pack_hdr). */
static uint32_t snap_t_fb, snap_t_hdr, snap_s_fb, snap_s_hdr;
static uint32_t fb32(uint32_t off) {   /* 4 bytes from the frame-buffer copy of chunk 0 */
    volatile uint8_t *b = FB_CHUNK_BUF + off;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

/* copy 'len' bytes from pack offset 'off' into SDRAM at 'dst' */
static void fetch(uint32_t off, uint8_t *dst, uint32_t len) {
#if KOBO_NOCART
    /* CD -> (Sub-CPU, 68K) -> frame buffer + 0x200 -> SDRAM.
       The file is read in the same 32 KB chunks the image loader always used; the last
       chunk read stays in the frame buffer, so neighbouring fetches do not re-read the CD.
       WHY the frame buffer: it is the only memory both the 68K and the SH2 can reach.
       The 68K may only write it while the SH2 has handed it over (give_fb = FM 0). */
    while (len) {
        uint32_t chunk  = off >> 14;              /* K31: 16 KB chunks (was 32 KB) */
        uint32_t within = off & (FETCH_BYTES - 1);
        uint32_t n = FETCH_BYTES - within, i;
        if (n > len) n = len;
        if (chunk != fb_chunk) {
            /* K14: clear the transfer area first.  WHY: K12/K13 showed some ZERO bytes of the
               file never landing in the frame buffer - the SH2 program's old bytes (copied into
               the frame buffer at boot) showed through instead.  With the area already zero,
               a skipped zero write leaves the right value. */
            {
                volatile uint16_t *z = (volatile uint16_t *)FB_CHUNK_BUF;
                uint32_t k;
                for (k = 0; k < FETCH_BYTES / 2; k++) z[k] = 0;
            }
            SC_COMM8_32 = pack_len;               /* CMD 50 reads the file length from COMM8 */
            SC_COMM2 = (uint16_t)chunk;
            *(volatile uint16_t *)0x2000402C = 0;   /* HW2: 16-bit - COMM14 is the Sub heartbeat */
            give_fb(); wait_cmd_no_vdp(CMD_FILE_CHUNK_FB); take_fb();
            cmd50_reply = SC_COMM12_32;           /* chunk<<24 | sectors<<16 | length>>8 (K11) */
            if (n_cmd50 < 3) cmd50_log[n_cmd50] = cmd50_reply;
            n_cmd50++;
            fb_chunk = chunk;
        }
        for (i = 0; i < n; i++) dst[i] = FB_CHUNK_BUF[within + i];
        off += n; dst += n; len -= n;
    }
#else
    /* the cart is 16-bit: 16-bit reads only */
    uint32_t i;
    for (i = 0; i < len; i += 2) {
        uint16_t w = *(volatile uint16_t *)(CART_RAM_SH2 + PACK_CART_OFF + off + i);
        dst[i] = (uint8_t)(w >> 8);
        if (i + 1 < len) dst[i + 1] = (uint8_t)w;
    }
#endif
}

static uint16_t hdr16(uint32_t off) { return ((uint16_t)pack_hdr[off] << 8) | pack_hdr[off + 1]; }
static uint32_t hdr32(uint32_t off) { return ((uint32_t)hdr16(off) << 16) | hdr16(off + 2); }

/* copy 'count' 16x16 frames of bank 'bank' into SDRAM.
   Returns 1 if the copy matches the pack's checksum (sum of the bank's first 4096 bytes,
   written by kobo_gfx.py into the bank table), 0 if not - shown as a green/red box. */
static int copy_bank(int bank, uint8_t *dst, uint32_t n) {
    uint32_t sum = 0, i;
    fetch(hdr32(0x208 + bank * 16 + 8), dst, n);
    if (n > 4096) n = 4096;
    for (i = 0; i < n; i++) sum += dst[i];
    return sum == hdr32(0x208 + bank * 16 + 12);
}
static int copy_frames(int bank, uint8_t (*dst)[256], int count) {
    return copy_bank(bank, &dst[0][0], (uint32_t)count * 256);
}
static int tiles_ok = 0, stars_ok = 0, ship_ok = 0;

static void load_palette(void) {
    int i;
    for (i = 0; i < 256; i++) {
        uint16_t c = hdr16(8 + i * 2);
        /* K8 - NEVER PURE BLACK.  WHY: seen in Fusion (K7 data check): every pixel whose
           colour was exactly black (palette entries 0 and 255 = 0x0000; tile and star
           backgrounds use 255) showed the Genesis layer instead of black - the 32X treated
           it as see-through.  All other colours were right.  0x0400 = blue 1/31, which
           looks black on screen but is a real colour.  (Index 0 stays the "transparent"
           index for sprites; those pixels are simply never drawn, so its colour is unused.) */
        if ((c & 0x7FFF) == 0) c = 0x0400;
        /* K11: NO through bit (bit 15).  Per the 32X manual a through-bit pixel is shown on the
           OPPOSITE side of the Genesis layer; in Fusion the dark ones showed the Genesis layer.
           Front/back is now decided only by the PRI bit in DISPMODE - see set_text(). */
        while ((SC_VDP_FBCTL & 0x2000) == 0) { }  /* PEN: palette writable only in blanking */
        SC_CRAM[i] = c;
    }
}

#if KOBO_NOCART
/* no-cart: just open KOBOGFX.BIN (CMD 38) and remember its length; fetch() reads it.
   returns 0 = ok, -1 = not found */
static int load_pack(void) {
    SC_COMM2 = FILE_KOBOGFX;
    wait_cmd(CMD_OPEN_FILE);
    pack_len = SC_COMM8_32;
    if ((int32_t)pack_len <= 0) return -1;
    fb_chunk = 0xFFFFFFFF;
    return 0;
}
#else
/* cart: load KOBOGFX.BIN from CD into the cart; returns number of bad chunks, -1 = not found */
static int load_pack(void) {
    uint32_t len, nchunks, c;
    int bad = 0;
#if KOBO_ROMCART
    return 0;                         /* CARTRIDGE: the pack is part of the ROM already */
#endif
    SC_COMM2 = FILE_KOBOGFX;
    wait_cmd(CMD_OPEN_FILE);
    len = SC_COMM8_32;
    if ((int32_t)len <= 0) return -1;
    if (len > PACK_MAX) len = PACK_MAX;
    SC_COMM8_32 = len;
    nchunks = (len + CHUNK_BYTES - 1) / CHUNK_BYTES;
    for (c = 0; c < nchunks; c++) {
        SC_COMM2 = (uint16_t)c;
        *(volatile uint16_t *)0x2000402C = 0;   /* HW2: 16-bit - COMM14 is the Sub heartbeat */
        wait_cmd(CMD_CHUNK_TO_CART);
        if (*(volatile uint16_t *)0x2000402C != 0) bad++;   /* HW4: 16-bit COMM12 (COMM14 = heartbeat) */
    }
    return bad;
}
#endif

/* ====================================================================================
 * Frame buffer (256-colour mode): line table = 224 words, line y starts at byte
 * 0x200 + y*320, i.e. word offset 0x100 + y*160.
 * ==================================================================================== */
static void init_buffer(void) {
    volatile uint16_t *fb = SC_FRAMEBUFFER;
    int i;
    for (i = 0; i < SCR_H; i++) fb[i] = (uint16_t)(0x100 + i * (SCR_W / 2));
    {   /* fill with the darkest real colour: index 0 shows the Genesis layer (K8) */
        uint16_t w = (uint16_t)((col_black << 8) | col_black);
        for (i = 0; i < SCR_W * SCR_H / 2; i++) fb[0x100 + i] = w;
    }
}

static void draw_playfield(int vx, int vy) {
    volatile uint8_t *fb = SC_FB8;
    int ox = vx & 15, oy = vy & 15, mx = vx >> 4, my = vy >> 4;
    int tx, ty, x, y;
    for (ty = 0; ty < PF_H / 16 + 1; ty++) {
        int sy0 = ty * 16 - oy;
        int y0 = sy0 < 0 ? -sy0 : 0;
        int y1 = sy0 + 16 > PF_H ? PF_H - sy0 : 16;
        for (tx = 0; tx < PF_W / 16 + 1; tx++) {
            int sx0 = tx * 16 - ox;
            int x0 = sx0 < 0 ? -sx0 : 0;
            int x1 = sx0 + 16 > PF_W ? PF_W - sx0 : 16;
            uint16_t n = *mpos(mx + tx, my + ty);
            int f = n >> 8;
            const uint8_t *src = IS_SPACE(n) ? stars[f & (STAR_FRAMES - 1)]
                                             : tileset[f < TILE_FRAMES ? f : 0];
            for (y = y0; y < y1; y++) {
                volatile uint8_t *d = fb + (sy0 + y) * SCR_W + sx0;
                const uint8_t *s = src + y * 16;
                for (x = x0; x < x1; x++) d[x] = s[x];
            }
        }
    }
}

/* ---- K4 DIAGNOSTIC STRIP (right side, x 224..319) - drawn by the SH2 itself, so it
   works even when the Genesis text layer is hidden or the 68K side has stopped. ---- */
static uint32_t pad_timeouts = 0;
static uint16_t md_ticks = 0;                    /* K32: from the CMD 48 reply */

/* K33 SOUND EFFECTS - numbers = tools/kobo_sfx.py EFFECTS order (KOBOSFX.BIN).  Requests are
   queued here and sent one per frame in the upper byte of the pad request (CMD 48); the
   Sub-CPU plays them on the Sega CD PCM chip.  0 = none. */
#define SFX_SHOT      1
#define SFX_BEAM      2
#define SFX_METAL     3
#define SFX_NODE      4
#define SFX_CORE      5
#define SFX_ENEMY1    6
#define SFX_ENEMY2    7
#define SFX_RING      8
#define SFX_ROCK      9
#define SFX_DETO      10
#define SFX_PLAYER    11
#define SFX_LAUNCH    12
#define SFX_GAMEOVER  13
#define SFX_TICK      14
#define SFX_PLAY      15
#define CMD_LOAD_SFX  53
static uint8_t sfx_q[8];                 /* K33b: ring buffer (no shifting -> no memmove) */
static int sfx_head = 0, sfx_n = 0, sfx_ok = 0;
static int opt_music = 1, opt_sfx = 1, opt_fire = 0;   /* K36 OPTIONS; HW11: music ON again (HW7 test over) */
static int demo_mute = 0;                                /* K37c: 1 while the demo plays */

/* ====================================================================================
 * DIFFICULTY (Kobo game.cpp game_t::set): CLASSIC, NEWBIE, GAMER, ELITE, GOD.
 *   CLASSIC = the game as it was (speed, one hit kills, two bolts every step).
 *   speed: relative to Classic, which stays at one game step per drawn frame (256 = 1.0):
 *          Kobo ms per step 30 / 40 / 30 / 27 / 25  ->  256, 192, 256, 284, 307.
 *   health: ship health (1 = any hit kills).  coll: damage the SHIP does to what it rams
 *          (Kobo game.damage; 0 = 1000 = always kills).  load: steps between nose / tail
 *          shots (0 or 1 = every step).  heat / cool: gun temperature per shot / per step
 *          (0 = never overheats; overheated at 256, fires again below 128).  alt: alternate
 *          nose and tail.  rock: rock health / damage.  bonus: extra ship at / every.
 * ==================================================================================== */
#define SKILL_CLASSIC 0
#define NUM_SKILLS    5
static const char *const skill_name[NUM_SKILLS] = { "CLASSIC", "NEWBIE", "GAMER", "ELITE", "GOD" };
static const uint16_t sk_speed[NUM_SKILLS]   = { 256, 192, 256, 284, 307 };
static const uint8_t  sk_lives[NUM_SKILLS]   = { 5, 5, 5, 4, 3 };
static const uint16_t sk_bonus1[NUM_SKILLS]  = { 2000, 5000, 8000, 10000, 10000 };
static const uint16_t sk_bonusn[NUM_SKILLS]  = { 3000, 7000, 8000, 10000, 10000 };
static const uint8_t  sk_health[NUM_SKILLS]  = { 1, 100, 60, 50, 40 };
static const uint8_t  sk_coll[NUM_SKILLS]    = { 0, 100, 100, 50, 30 };
static const uint8_t  sk_noseload[NUM_SKILLS]= { 1, 1, 0, 1, 1 };
static const uint8_t  sk_tailload[NUM_SKILLS]= { 1, 0, 1, 2, 2 };
static const uint8_t  sk_noseheat[NUM_SKILLS]= { 0, 14, 10, 26, 30 };
static const uint8_t  sk_tailheat[NUM_SKILLS]= { 0, 7, 20, 39, 45 };
static const uint8_t  sk_cool[NUM_SKILLS]    = { 0, 5, 5, 5, 5 };
static const uint8_t  sk_alt[NUM_SKILLS]     = { 0, 1, 0, 0, 0 };
static const uint16_t sk_rockhp[NUM_SKILLS]  = { 5100, 200, 500, 5100, 5100 };
static const uint16_t sk_rockdmg[NUM_SKILLS] = { 1000, 50, 50, 1000, 1000 };
static const uint8_t  sk_hp10[NUM_SKILLS]    = { 0, 10, 6, 5, 4 };     /* 10 % of health (core)  */
static const uint8_t  sk_hp25[NUM_SKILLS]    = { 0, 25, 15, 12, 10 };  /* 25 % (stage cleared)  */
static int opt_skill = SKILL_CLASSIC;                    /* saved in backup RAM */
static int ship_health = 1, health_fade_t = 0;
static int nose_temp = 0, tail_temp = 0, nose_hot = 0, tail_hot = 0;
static int nose_wait = 0, tail_wait = 0, alt_side = 0;
static uint32_t bonus_next = 2000;
static void ship_die(void);
static void sfx(int id) {
    int i;
    if (!opt_sfx || demo_mute) return;                     /* K36: off; K37c: silent in the demo */
    pwm_post(id);                                          /* C2b: 32X PWM effects */
    for (i = 0; i < sfx_n; i++) if (sfx_q[(sfx_head + i) & 7] == id) return;   /* already waiting */
    if (sfx_n < 8) { sfx_q[(sfx_head + sfx_n) & 7] = (uint8_t)id; sfx_n++; }
}

static void pick_colours(void) {
    int i, best_w = -1, best_r = -1, best_k = 999, best_g = -99;
    col_white = col_grey = col_red = 1;
    for (i = 1; i < 255; i++) {
        uint16_t c = hdr16(8 + i * 2);
        int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        int w = r + g + b;
        int red = r * 3 - g - b;
        if (w > best_w) { best_w = w; col_white = (uint8_t)i; }
        if (red > best_r) { best_r = red; col_red = (uint8_t)i; }
        if (w >= 27 && w <= 33) col_grey = (uint8_t)i;      /* roughly 1/3 brightness */
        if (w < best_k) { best_k = w; col_black = (uint8_t)i; }
        if (g - r - b > best_g) { best_g = g - r - b; col_green = (uint8_t)i; }   /* green, not yellow */
    }
}

static void box(int x, int y, int w, int h, uint8_t c) {
    volatile uint8_t *fb = SC_FB8;
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            fb[(y + j) * SCR_W + x + i] = c;
}

/* ---- 3x5 pixel font for the strip (hex digits + a few letters).  One byte per row, bits
   4/2/1 = left/middle/right pixel (no variable shifts - see the note at krand_2). ---- */
static const uint8_t font3x5[][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},      /* 0 1 2 3 4 */
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},      /* 5 6 7 8 9 */
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,7,4,7},      /* A B C D E */
    {7,4,7,4,4},{5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,6,5,5},      /* F K L N R */
    {5,5,5,5,2},{0,0,0,0,0},{7,2,2,2,2},{5,5,2,5,5},{5,5,2,2,2},      /* V space T X Y */
    {7,4,5,5,7},{5,5,7,5,5},{7,2,2,2,7},{5,7,7,5,5},{7,5,5,5,7},      /* K30: G H I M O */
    {7,5,7,4,4},{5,5,5,5,7},{5,5,7,7,5}                               /*      P U W     */
};
static int glyph(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    switch (c) { case 'K': return 16; case 'L': return 17; case 'N': return 18;
                 case 'R': return 19; case 'S': return 5; case 'V': return 20; case 'T': return 22; case 'X': return 23; case 'Y': return 24;
                 case 'G': return 25; case 'H': return 26; case 'I': return 27; case 'M': return 28;
                 case 'O': return 29; case 'P': return 30; case 'U': return 31; case 'W': return 32; }
    return 21;
}
static void put_text(int x, int y, const char *t, uint8_t c) {
    for (; *t; t++, x += 4) {
        const uint8_t *g = font3x5[glyph(*t)];
        int r;
        for (r = 0; r < 5; r++) {
            volatile uint8_t *d = SC_FB8 + (y + r) * SCR_W + x;
            d[0] = (g[r] & 4) ? c : col_black;
            d[1] = (g[r] & 2) ? c : col_black;
            d[2] = (g[r] & 1) ? c : col_black;
            d[3] = col_black;
        }
    }
}
/* write 'digits' hex digits of v into t (fixed 4-bit shifts only) */
static char *hex(char *t, uint32_t v, int digits) {
    int i;
    for (i = digits - 1; i >= 0; i--) { int n = v & 15; t[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10); v >>= 4; }
    t[digits] = 0;
    return t + digits;
}

/* Readouts (bottom of the strip):
     top:    K9 + version block   (the build running - no Genesis text needed)
     L       pack file length (hex)
     N / R   CMD 50 requests sent / last reply: chunk(2) sectors(2) first data word(4)
     S / D   stars bank offset in the pack / first 2 bytes the SH2 holds for the stars */
static void diag_text(void) {
    char t[32], *p;
    {   /* "K" + version in decimal (no division: repeated subtraction, see krand_2 note) */
        int v = KOBO_VERSION, tens = 0;
        while (v >= 10) { v -= 10; tens++; }
        t[0] = 'K'; t[1] = (char)('0' + tens); t[2] = (char)('0' + v); t[3] = 0;
    }
    put_text(228, 4, t, col_white);
    {   /* K21: score, 6 decimal digits (repeated subtraction - no division) */
        static const uint32_t pw[6] = { 100000, 10000, 1000, 100, 10, 1 };
        char sc[8]; uint32_t v = score; int k;
        for (k = 0; k < 6; k++) { int d = 0; while (v >= pw[k]) { v -= pw[k]; d++; } sc[k] = (char)('0' + d); }
        sc[6] = 0;
        put_text(248, 4, sc, col_white);
    }
    box(300, 3, 14, 7, (uint8_t)((KOBO_VERSION * 16 + 8) & 0xFF)); /* version colour block */
    /* K12 layout (expected values in strip_reference.png):
         T <frame buffer> <SH2 copy>   tile set 1 offset from the bank table  (000003F8 both)
         S <frame buffer> <SH2 copy>   star offset from the bank table        (00032F78 both)
         1/2/3 <reply>  N<count>       CMD 50 replies: chunk(2) sectors(2)
                                       K14: last 4 = the word in WORD RAM at chunk offset
                                       0x210 (tile offset high word) - must be 0000 */
    p = t; *p++ = 'T'; p = hex(p, snap_t_fb, 8); *p++ = ' '; p = hex(p, snap_t_hdr, 8);
    put_text(228, 194, t, col_white);
    p = t; *p++ = 'S'; p = hex(p, snap_s_fb, 8); *p++ = ' '; p = hex(p, snap_s_hdr, 8);
    put_text(228, 200, t, col_white);
    {
        int k;
        for (k = 0; k < 3; k++) {
            p = t; *p++ = (char)('1' + k); *p++ = ' '; p = hex(p, cmd50_log[k], 8);
            if (k == 0) { *p++ = ' '; *p++ = 'N'; p = hex(p, n_cmd50, 2); }
            put_text(228, 206 + k * 6, t, col_white);   /* last line ends at y 222 */
        }
    }
}

/* K24 RADAR - the whole world (64 x 128 map cells) as 32 x 64 pixels, one pixel per 2 x 2
   cells, at x 256..287, y 80..143 (where the palette grid was).  Kobo shows bases and the
   ship on its radar: green = pipes, white = a core still standing, red = the ship (blinks).
   The whole map is read every frame (8192 cells) - cheap next to drawing the playfield. */
static void draw_radar(uint32_t frame) {
    volatile uint8_t *fb = SC_FB8;
    int rx, ry;
    for (ry = 0; ry < MAP_SIZEY / 2; ry++)
        for (rx = 0; rx < MAP_SIZEX / 2; rx++) {
            int a = *mpos(rx * 2, ry * 2),     b = *mpos(rx * 2 + 1, ry * 2);
            int c = *mpos(rx * 2, ry * 2 + 1), d = *mpos(rx * 2 + 1, ry * 2 + 1);
            uint8_t col = col_black;
            if (!IS_SPACE(a) || !IS_SPACE(b) || !IS_SPACE(c) || !IS_SPACE(d)) col = col_green;
            if ((!IS_SPACE(a) && (a & CORE)) || (!IS_SPACE(b) && (b & CORE)) ||
                (!IS_SPACE(c) && (c & CORE)) || (!IS_SPACE(d) && (d & CORE))) col = col_white;
            fb[(80 + ry) * SCR_W + 256 + rx] = col;
        }
    if (frame & 8) {                                   /* ship: 2 x 2 red dot, blinking */
        int sx = 256 + (int)((uint32_t)ship_x >> 5), sy = 80 + (int)((uint32_t)ship_y >> 5);
        box(sx > 286 ? 286 : sx, sy > 142 ? 142 : sy, 2, 2, col_red);
    }
}



/* ====================================================================================
 * K35 RLE SPRITES + EXPLOSION EFFECTS + LOGO
 *   kobo_gfx.py stores run-length encoded copies of the explosion sets and the logo
 *   (banks 37-46): per row, runs of [skip][count][data]; count bit 7 = one colour repeated,
 *   else literal pixels; skip 0xFF = end of row.  Only visible pixels are stored (60 KB of
 *   explosions -> 18 KB) and see-through runs are skipped when drawing.
 *   Where they live: SDRAM gaps (RLE_A/B/C) for the big explosions, spare frame-buffer
 *   memory (RLE_FB, both buffers) for the logo and the small explosions.
 * ==================================================================================== */
#define FX_EXPLO3  0
#define FX_EXPLO4  1
#define FX_EXPLO5  2
#define FX_ROCK    3
#define FX_RING    4
#define FX_BOLT    5
#define FX_BULLET  6
#define FX_LOGOF   7
#define FX_LOGOO   8
#define FX_DELUXE  9
#define FX_COUNT   10
static const uint8_t fx_bank[FX_COUNT] = { 38, 39, 40, 41, 43, 37, 42, 44, 45, 46 };
static const uint8_t *fx_src[FX_COUNT];
static uint8_t fx_w[FX_COUNT], fx_h[FX_COUNT], fx_n[FX_COUNT];
static int rle_ok = 0;

static void fx_info(int id, const void *where) {
    int b = fx_bank[id];
    fx_src[id] = (const uint8_t *)where;
    fx_w[id] = pack_hdr[0x208 + b * 16 + 2];
    fx_h[id] = pack_hdr[0x208 + b * 16 + 3];
    fx_n[id] = (uint8_t)hdr16(0x208 + b * 16 + 4);
}
static uint32_t fx_len(int id) { return hdr16(0x208 + fx_bank[id] * 16 + 6); }

/* draw frame 'fr' of RLE sprite 'id' with its top-left at playfield (sx, sy), clipped */
static void draw_rle(int id, int fr, int sx, int sy) {
    const uint8_t *d = fx_src[id], *p;
    volatile uint8_t *fb = SC_FB8;
    int y, h = fx_h[id];
    if (!d) return;
    p = d + ((d[fr * 2] << 8) | d[fr * 2 + 1]);
    for (y = 0; y < h; y++) {
        int x = sx, py = sy + y, vis = py >= 0 && py < PF_H;
        volatile uint8_t *row = fb + py * SCR_W;
        for (;;) {
            int sk = *p++, c, k;
            if (sk == 0xFF) break;
            x += sk;
            c = *p++;
            if (c & 0x80) {
                uint8_t col = *p++;
                c &= 0x7F;
                if (vis) for (k = 0; k < c; k++) if (x + k >= 0 && x + k < PF_W) row[x + k] = col;
            } else {
                if (vis) for (k = 0; k < c; k++) if (x + k >= 0 && x + k < PF_W) row[x + k] = p[k];
                p += c;
            }
            x += c;
        }
    }
}
/* ====================================================================================
 * K30 PANEL + MENU (Kobo's dashboard, kobo.cpp: HIGHSCORE / SCORE boxes, radar 64 x 128 at
 * one pixel per map cell, SHIPS / STAGE boxes; Kobo's icefont).
 *   The panel picture (KOBOGFX bank 31, 80 x 224, made by kobo_gfx.py from screen2.png) is
 *   drawn ONCE into both frame buffers, straight from the pack (fetch, one row at a time) -
 *   it is never kept in SDRAM.  Nothing overwrites it afterwards: loading uses the
 *   off-screen part of the frame buffer (FB_CHUNK_BUF).  Each frame only the value lines
 *   and the radar are redrawn, on colours sampled from the panel picture.
 *   Our screen is 224 lines (Kobo 240): the panel's three bands are stacked tighter.
 * ==================================================================================== */
#define BANK_PANEL   31
#define BANK_FONT    32
#define BANK_FONTW   33
#define FONT_N       43                       /* kobo_gfx.py FONT_CHARS */
#define font_gfx     ((uint8_t *)WORK_FONT)
#define font_w       ((uint8_t *)WORK_FONTW)
#define PANEL_X      232                      /* the 80-pixel panel picture, x 232..311 */
#define RADAR_X      236                      /* radar 64 x 128 */
#define RADAR_Y      46
static uint8_t col_hi, col_sc, col_sh, col_st, col_radar;   /* sampled from the panel */
static uint32_t hiscore = 0;

static int font_idx(char c) {                 /* " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-:.!?/" */
    if (c >= '0' && c <= '9') return 1 + (c - '0');
    if (c >= 'A' && c <= 'Z') return 11 + (c - 'A');
    switch (c) { case '-': return 37; case ':': return 38; case '.': return 39;
                 case '!': return 40; case '?': return 41; case '/': return 42; }
    return 0;
}
static int font_len(const char *t) {
    int n = 0;
    for (; *t; t++) n += font_w[font_idx(*t)] + 1;
    return n ? n - 1 : 0;
}
static void font_text(int x, int y, const char *t) {   /* see-through background */
    volatile uint8_t *fb = SC_FB8;
    for (; *t; t++) {
        int i = font_idx(*t), gx, gy;
        const uint8_t *g = font_gfx + i * 63;
        for (gy = 0; gy < 9; gy++)
            for (gx = 0; gx < 7; gx++) {
                uint8_t c = g[gy * 7 + gx];
                int px = x + gx, py = y + gy;
                if (c && px >= 0 && px < SCR_W && py >= 0 && py < SCR_H) fb[py * SCR_W + px] = c;
            }
        x += font_w[i] + 1;
    }
}
static void font_center(int y, const char *t) { font_text(PF_W / 2 - font_len(t) / 2, y, t); }

static void put_caption(int x, int y, const char *t, uint8_t c) {   /* 3x5 font, see-through */
    for (; *t; t++, x += 4) {
        const uint8_t *g = font3x5[glyph(*t)];
        int r;
        for (r = 0; r < 5; r++) {
            volatile uint8_t *d = SC_FB8 + (y + r) * SCR_W + x;
            if (g[r] & 4) d[0] = c;
            if (g[r] & 2) d[1] = c;
            if (g[r] & 1) d[2] = c;
        }
    }
}
static void dec(char *t, uint32_t v, int digits) {   /* decimal, repeated subtraction */
    static const uint32_t pw[6] = { 100000, 10000, 1000, 100, 10, 1 };
    int k;
    for (k = 6 - digits; k < 6; k++) { int d = 0; while (v >= pw[k]) { v -= pw[k]; d++; } *t++ = (char)('0' + d); }
    *t = 0;
}

/* K35c: copy n bytes into the frame buffer with 16-BIT writes.  WHY: byte writes of 0x00 to
   the 32X frame buffer do not reliably land in Fusion (the K14 finding, now seen for SH2 byte
   writes too) - the old contents stay.  The buffer that had shown the splash kept splash
   pixels wherever the copied data had a zero byte: the menu logo "twitched" (every other
   frame came from the damaged copy) and a ring's explosion on level 7 drew logo rows. */
static void fb_put16(volatile uint8_t *dst, const uint8_t *src, uint32_t n) {
    volatile uint16_t *d = (volatile uint16_t *)dst;
    uint32_t i;
    for (i = 0; i < n; i += 2)
        d[i >> 1] = (uint16_t)((src[i] << 8) | (i + 1 < n ? src[i + 1] : 0));
}

/* K31/K35c: the 5 tile sets and the menu logo into the spare frame-buffer memory of BOTH
   buffers, BEFORE the music.  Each item is read from CD ONCE into a bounce buffer (the WORK2
   enemy + gun tables, 16 KB - not in use until a game starts: build_level refills them), then
   written into this buffer, flip, the other buffer, flip. */
#define BOUNCE ((uint8_t *)WORK2_ENEMIES)
static void cache_put_both(volatile uint8_t *fbdst, uint32_t n) {
    fb_put16(fbdst, BOUNCE, n);
    flip_wait();
    fb_put16(fbdst, BOUNCE, n);
    flip_wait();
    fb_chunk = 0xFFFFFFFF;                     /* the loading window belongs to one buffer */
}
static void cache_tilesets(void) {
    int ts, k;
    for (ts = 0; ts < 5; ts++) {
        fb_chunk = 0xFFFFFFFF;
        copy_bank(BANK_TILES1 + ts, BOUNCE, TILE_FRAMES * 256);
        show_status((uint16_t)(0x4B60 + ts));      /* HW4: tile set ts copied from the cart */
        cache_put_both(TSET_CACHE + ts * (TILE_FRAMES * 256), TILE_FRAMES * 256);
        show_status((uint16_t)(0x4B70 + ts));      /* HW4: tile set ts written to both buffers */
    }
    /* K35d: the logo is no longer kept here (SDRAM LOGO_RAM instead) */
    {
        volatile uint8_t *src = TSET_CACHE + tset_loaded * (TILE_FRAMES * 256);
        uint8_t *dst = &tileset[0][0];
        for (k = 0; k < TILE_FRAMES * 256; k++) dst[k] = src[k];
    }
}

/* K34: switch the CD music track (CMD 45, the unchanged audio path; the Sub-CPU's
   cdda_start stops the current track and plays + repeats the new one).  Only audio - no
   CD data read, so the rule "no data reads after the music starts" still holds. */
static void music(int track) {
#if CDDA_ENABLE
    if (!opt_music) { wait_cmd(54); return; }              /* K36: music off = CMD 54 (stop) */
    SC_COMM2 = (uint16_t)track; wait_cmd(CMD_PLAY_CDDA);
#else
    (void)track;
#endif
}

/* K36 HIGH SCORES + SAVE (Sega CD backup RAM, file KOBODELUXE_, 128 bytes; main.c CMD 56-59):
   bytes 0-3 "KD01", 4 music, 5 sound fx, 6 fire buttons, 8.. 10 entries x 8 bytes
   (name[3], level, score u32 big-endian).  Exchanged one word at a time over COMM. */
typedef struct { char name[3]; uint8_t level; uint32_t score; } hs_t;
static hs_t hs[10];
static uint8_t save_buf[128];
static int bram_ok = 0;
static void hs_defaults(void) {
    static const char nm[10][3] = { {'K','O','B'},{'O','D','L'},{'X','K','O'},{'D','A','V'},{'A','K','I'},
                                    {'C','H','W'},{'M','N','U'},{'S','E','G'},{'C','D','X'},{'P','L','R'} };
    int i;
    for (i = 0; i < 10; i++) {
        hs[i].name[0] = nm[i][0]; hs[i].name[1] = nm[i][1]; hs[i].name[2] = nm[i][2];
        hs[i].level = (uint8_t)(10 - i); hs[i].score = (uint32_t)(1000 - i * 100);
    }
}
static void save_game(void) {
    int i;
#if !KOBO_HW_BRAM
    return;                                        /* HW2: backup RAM off */
#endif
    for (i = 0; i < 128; i++) save_buf[i] = 0;
    save_buf[0] = 'K'; save_buf[1] = 'D'; save_buf[2] = '0'; save_buf[3] = '1';
    save_buf[4] = (uint8_t)opt_music; save_buf[5] = (uint8_t)opt_sfx; save_buf[6] = (uint8_t)opt_fire;
    save_buf[7] = (uint8_t)opt_skill;                     /* DIFFICULTY */
    for (i = 0; i < 10; i++) {
        uint8_t *e = save_buf + 8 + i * 8;
        e[0] = (uint8_t)hs[i].name[0]; e[1] = (uint8_t)hs[i].name[1]; e[2] = (uint8_t)hs[i].name[2];
        e[3] = hs[i].level;
        e[4] = (uint8_t)(hs[i].score >> 24); e[5] = (uint8_t)(hs[i].score >> 16);
        e[6] = (uint8_t)(hs[i].score >> 8);  e[7] = (uint8_t)hs[i].score;
    }
    for (i = 0; i < 64; i++) {
        SC_COMM2 = (uint16_t)i; SC_COMM8 = (uint16_t)((save_buf[i * 2] << 8) | save_buf[i * 2 + 1]);
        wait_cmd(58);
    }
    *(volatile uint16_t *)0x2000402C = 0;   /* HW2: 16-bit - COMM14 is the Sub heartbeat */ wait_cmd(59);
    bram_ok = *(volatile uint16_t *)0x2000402C;
}
static void load_game(void) {
    int i;
    hs_defaults();
    *(volatile uint16_t *)0x2000402C = 0;   /* HW2: 16-bit - COMM14 is the Sub heartbeat */ wait_cmd(56);
    bram_ok = *(volatile uint16_t *)0x2000402C;
    if (!bram_ok) return;
    for (i = 0; i < 64; i++) {
        uint16_t v;
        SC_COMM2 = (uint16_t)i; wait_cmd(57);
        v = SC_COMM8;
        save_buf[i * 2] = (uint8_t)(v >> 8); save_buf[i * 2 + 1] = (uint8_t)v;
    }
    if (save_buf[0] != 'K' || save_buf[1] != 'D' || save_buf[2] != '0' || save_buf[3] != '1') return;
    opt_music = save_buf[4] & 1; opt_sfx = save_buf[5] & 1; opt_fire = 0;   /* K37g: fire option gone */
    opt_skill = save_buf[7] < NUM_SKILLS ? save_buf[7] : SKILL_CLASSIC;     /* DIFFICULTY */
    for (i = 0; i < 10; i++) {
        const uint8_t *e = save_buf + 8 + i * 8;
        hs[i].name[0] = (char)e[0]; hs[i].name[1] = (char)e[1]; hs[i].name[2] = (char)e[2];
        hs[i].level = e[3];
        hs[i].score = ((uint32_t)e[4] << 24) | ((uint32_t)e[5] << 16) | ((uint32_t)e[6] << 8) | e[7];
    }
}
static int hs_rank(uint32_t sc) {             /* place 0..9 the score would take, 10 = none */
    int i;
    if (sc == 0) return 10;
    for (i = 0; i < 10; i++) if (sc > hs[i].score) return i;
    return 10;
}
static void hs_insert(int r, const char *nm, uint32_t sc, int lv) {
    int i;
    for (i = 9; i > r; i--) hs[i] = hs[i - 1];
    hs[r].name[0] = nm[0]; hs[r].name[1] = nm[1]; hs[r].name[2] = nm[2];
    hs[r].score = sc; hs[r].level = (uint8_t)lv;
}
static char font_ok_char(char c) {            /* names use A-Z only */
    return (c >= 'A' && c <= 'Z') ? c : 'A';
}

/* K34 credits (scroll up over the drifting level); K35: "CD32X" as one word */
static const char *const credits[] = {
    "KOBO DELUXE", "", "SEGA CD32X EDITION", "", "", "ORIGINAL GAME", "XKOBO", "AKIRA HIGUCHI", "",
    "KOBO DELUXE", "DAVID OLOFSON", "",
    "D32XR 32X CODE", "VICTOR LUCHITS", "",                      /* K37b order: Victor,  */
    "SEGA CD AND 32X FRAMEWORK", "CHILLY WILLY", "",           /*         Chilly,      */
    "SEGA CD32X PORT", "MICRONUT99", "",                        /*         MicroNut99   */
    "", "THANK YOU FOR PLAYING!", 0
};

/* K37: shared by the HIGH SCORES screen and the demo */
static void draw_hs_table(int hi, int t) {
    int i;
    font_center(24, "HIGH SCORES");
    for (i = 0; i < 10; i++) {
        char l[24], *q = l;
        q[0] = (char)(i == 9 ? '1' : ' '); q[1] = (char)(i == 9 ? '0' : '1' + i);   /* no division */
        q[2] = '.'; q[3] = ' '; q += 4;
        *q++ = hs[i].name[0]; *q++ = hs[i].name[1]; *q++ = hs[i].name[2]; *q++ = ' '; *q++ = ' ';
        dec(q, hs[i].score, 6); q += 6; *q++ = ' '; *q++ = 'L';
        dec(q, hs[i].level, 2);
        if (i != hi || (t & 16)) font_text(40, 50 + i * 14, l);   /* new entry blinks */
    }
}
static int draw_credits(int y0) {             /* returns the y after the last line */
    int i, y;
    for (i = 0, y = y0; credits[i]; i++, y += 14)
        if (y > -10 && y < PF_H) font_center(y, credits[i]);
    return y;
}
static void draw_logo(void) {
    draw_rle(FX_LOGOF, 0, PF_W / 2 - fx_w[FX_LOGOF] / 2, 12);
    draw_rle(FX_LOGOO, 0, PF_W / 2 - fx_w[FX_LOGOO] / 2, 12);
    draw_rle(FX_DELUXE, 0, PF_W / 2 - fx_w[FX_DELUXE] / 2, 78);
    font_center(90, "SEGA CD32X");                     /* K37: dev request */
}

/* K37 DEMO (Kobo's intro, states.cpp / manage.cpp run_intro): pages title 7 s, instructions
   19.7 s, title 5.5 s, high scores 11.7 s, title 5.5 s, credits 13.7 s, repeat.  The view
   scrolls upward (3 px per step) and the ship weaves by itself (two sine waves, 0.3 x the
   view, speeds 1 and 1.73).  Any button -> menu. */
static const uint16_t demo_ms[6] = { 7000, 19700, 5500, 11700, 5500, 13700 };   /* Kobo config.h */
static const uint16_t demo_t60[6] = { 420, 1182, 330, 702, 330, 822 };   /* the same in 60 Hz frames */
static const uint16_t demo_t50[6] = { 350, 985, 275, 585, 275, 685 };    /*             50 Hz frames */
static int sar7(int x) { return x >= 0 ? (int)((uint32_t)x >> 7) : -(int)(((uint32_t)(-x) + 127) >> 7); }
static const int8_t sin64[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12 };
static const char *const instructions[] = {
    "HOW TO PLAY", "", "DESTROY EVERY BASE:", "SHOOT THE CORE IN ITS", "MIDDLE - OR ITS ENDS", "",
    "D-PAD: FLY", "A B: FIRE (BOTH WAYS)", "X: PAUSE", "",
    "AVOID ENEMY SHIPS", "BULLETS AND ROCKS", "", "GOOD LUCK!", 0 };

/* draw the panel picture into BOTH frame buffers (flips twice) and sample its colours */
static void draw_panel(void) {
    uint8_t row[80];
    uint32_t off = hdr32(0x208 + BANK_PANEL * 16 + 8);
    int pass, y, x;
    for (pass = 0; pass < 2; pass++) {
        fb_chunk = 0xFFFFFFFF;                 /* each frame buffer has its own window */
        for (y = 0; y < SCR_H; y++) {
            volatile uint8_t *d = SC_FB8 + y * SCR_W;
            fetch(off + (uint32_t)y * 80, row, 80);
            for (x = PF_W; x < PANEL_X; x++) d[x] = col_black;
            for (x = 0; x < 80; x++) d[PANEL_X + x] = row[x];
            for (x = PANEL_X + 80; x < SCR_W; x++) d[x] = col_black;
            if (y == 13)  col_hi = row[38];
            if (y == 35)  col_sc = row[38];
            if (y == 195) col_sh = row[38];
            if (y == 217) col_st = row[38];
            if (y == 110) col_radar = row[36];
        }
        flip_wait();
    }
}

static void value_box(int x0, int x1, int y, uint8_t bg, const char *t) {
    box(x0, y, x1 - x0 + 1, 9, bg);
    font_text(x1 - 1 - font_len(t), y, t);
}

/* every frame: the four boxes' values + the radar */
static void draw_panel_info(uint32_t frame) {
    char t[8];
    volatile uint8_t *fb = SC_FB8;
    int rx, ry;
    put_caption(247, 3, "HIGHSCORE", col_white);
    put_caption(247, 25, "SCORE", col_white);
    put_caption(259, 185, "SHIPS", col_white);
    put_caption(259, 207, "STAGE", col_white);
    dec(t, hiscore, 6); value_box(246, 305, 9, col_hi, t);
    dec(t, score, 6);   value_box(246, 305, 31, col_sc, t);
    dec(t, (uint32_t)(lives > 0 ? lives : 0), 1); value_box(258, 291, 191, col_sh, t);
    dec(t, (uint32_t)(scene_no + 1), 2);          value_box(258, 291, 213, col_st, t);
    for (ry = 0; ry < MAP_SIZEY; ry++) {       /* radar: one pixel per map cell */
        volatile uint8_t *d = fb + (RADAR_Y + ry) * SCR_W + RADAR_X;
        for (rx = 0; rx < MAP_SIZEX; rx++) {
            int m = *mpos(rx, ry);
            d[rx] = IS_SPACE(m) ? col_radar : ((m & CORE) ? col_white : col_green);
        }
    }
    if (opt_skill != SKILL_CLASSIC) {
        /* DIFFICULTY: health bar (x 224-227) and gun-heat bar (x 228-231) in the gap between the
           playfield and the panel, 128 px tall next to the radar.  Health scale: full = normal
           maximum (bonus health above it shows full).  Colours: green > 1/2, yellow, red < 1/4. */
        static const uint16_t hmul[NUM_SKILLS] = { 0, 327, 546, 655, 819 };   /* 128*256/health */
        static uint8_t col_yel = 0;
        int hp = ship_health < 0 ? 0 : ship_health, hh, th, t;
        if (!col_yel) {                        /* nearest yellow in the palette, once */
            int i, best = -1;
            for (i = 1; i < 255; i++) {
                uint32_t c = hdr16(8 + i * 2);
                int r = (int)(c & 31), g = (int)((c >> 5) & 31), b = (int)((c >> 10) & 31);
                int v = r + g - 2 * b - (r > g ? r - g : g - r);
                if (v > best) { best = v; col_yel = (uint8_t)i; }
            }
        }
        hh = (int)(((uint32_t)hp * hmul[opt_skill]) >> 8); if (hh > 128) hh = 128;
        t = nose_temp > tail_temp ? nose_temp : tail_temp;
        th = (int)((uint32_t)t >> 1); if (th > 128) th = 128;
        box(224, RADAR_Y, 4, 128 - hh, col_black);
        box(224, RADAR_Y + 128 - hh, 4, hh, hh > 64 ? col_green : hh > 32 ? col_yel : col_red);
        box(228, RADAR_Y, 4, 128 - th, col_black);
        box(228, RADAR_Y + 128 - th, 4, th, (nose_hot || tail_hot) ? col_red : col_yel);
    }
    if (frame & 8)                             /* the ship: blinking red dot */
        box(RADAR_X + (int)((uint32_t)ship_x >> 4) - 1, RADAR_Y + (int)((uint32_t)ship_y >> 4) - 1, 2, 2, col_red);
}

/* 12 pad squares (UP DOWN LEFT RIGHT B C A START / Z Y X MODE), heartbeat, CMD 48 timeout */
static void draw_diag(uint16_t pad, uint32_t frame, int timed_out) {
    int b;
    uint16_t bit = 1;
    for (b = 0; b < 12; b++, bit <<= 1) {
        int x = 232 + (b < 8 ? b : b - 8) * 10;
        int y = b < 8 ? 16 : 28;
        box(x, y, 8, 8, (pad & bit) ? col_white : col_grey);
    }
    box(232, 48, 16, 16, (frame & 1) ? col_white : col_black);  /* heartbeat: blinks every frame */
    box(256, 48, 16, 16, timed_out ? col_red : col_grey);  /* red = Sub-CPU did not answer  */
    {   /* K17: ship world X, Y, direction + ship graphics checksum box */
        char t[24], *p = t;
        *p++ = 'X'; p = hex(p, (uint32_t)ship_x, 3); *p++ = ' ';
        *p++ = 'Y'; p = hex(p, (uint32_t)ship_y, 3); *p++ = ' ';
        *p++ = 'D'; p = hex(p, (uint32_t)ship_di, 1); *p++ = ' ';
        *p++ = 'B'; p = hex(p, (uint32_t)bolts_alive, 2);             /* K20: bolts in flight */
        put_text(228, 69, t, col_white);
        box(314, 68, 5, 7, ship_ok ? col_green : col_red);
        p = t;                                                   /* K22 */
        *p++ = 'E'; p = hex(p, (uint32_t)enemies_alive, 2); *p++ = ' ';
        if (game_over) { *p++ = 'D'; *p++ = 'E'; *p++ = 'A'; *p++ = 'D'; }   /* K23 */
        else { *p++ = 'L'; p = hex(p, (uint32_t)lives, 1); }
        {   /* K27: level number in decimal (1..50) */
            int lv = scene_no + 1, tens = 0;
            while (lv >= 10) { lv -= 10; tens++; }
            *p++ = ' '; *p++ = 'S'; *p++ = (char)('0' + tens); *p++ = (char)('0' + lv); *p++ = ' ';
        }
        *p++ = 'G'; p = hex(p, (uint32_t)n_cannons, 2);
        put_text(228, 75, t, col_white);
        box(314, 74, 5, 6, gfx2_ok ? col_green : col_red);
    }

    /* K7 DATA CHECK - compare with strip_reference.png (made on the PC from KOBOGFX.BIN):
       a) all 256 palette colours as a 16 x 16 grid of 4 x 4 squares (row = high nibble)
       b) tile set frames 0..3, then star frames 0..3, exactly as they sit in SDRAM
       Right colours + wrong pictures = the pack DATA reaching the SH2 is wrong.
       Wrong colours everywhere          = the PALETTE / colour mode is wrong. */
    {
        int i, f, x, y;
#if KOBO_DIAG_GRID
        for (i = 0; i < 256; i++)
            box(240 + (i & 15) * 4, 80 + (i >> 4) * 4, 4, 4, (uint8_t)i);
#else
        (void)i;
        draw_radar(frame);                              /* K24: radar in the grid's place */
#endif
        for (f = 0; f < 4; f++)
            for (y = 0; y < 16; y++)
                for (x = 0; x < 16; x++) {
                    SC_FB8[(152 + y) * SCR_W + 228 + f * 22 + x] = tileset[f][y * 16 + x];
                    SC_FB8[(176 + y) * SCR_W + 228 + f * 22 + x] = stars[f][y * 16 + x];
                }
        /* checksum boxes right of each row: green = copy matches the pack, red = wrong data */
        box(314, 152, 5, 16, tiles_ok ? col_green : col_red);
        box(314, 176, 5, 16, stars_ok ? col_green : col_red);
    }
    diag_text();
}

/* CMD 48 with a SHORT wait: if the Sub-CPU does not answer, the frame loop keeps going and
   the red square shows it (the normal wait_cmd waits 40,000,000 passes = many seconds). */
/* ====================================================================================
 * K17 PLAYER SHIP - Kobo's own rules (myship.cpp / gamectl.cpp):
 *   direction 1..8 = up, up-right, right, down-right, down, down-left, left, up-left.
 *   The D-pad sets the direction; with nothing pressed the ship KEEPS FLYING (XKobo style).
 *   Per step: 3 pixels straight (vo), 2 pixels on each axis diagonally (vd).
 *   The world wraps (1024 x 2048 pixels); the view is centred on the ship.
 *   Picture: player.png frame (direction - 1) * 2  (even frames = the 8 directions).
 *   Timing: one step per drawn frame for now (Kobo runs ~33 steps/s) - tuned later.
 * ==================================================================================== */

static int pad_to_dir(uint16_t pad) {           /* 0 = no direction pressed */
    int up = (pad & PAD_UP) != 0, dn = (pad & PAD_DOWN) != 0;
    int lf = (pad & PAD_LEFT) != 0, rt = (pad & PAD_RIGHT) != 0;
    int lr = lf - rt, ud = up - dn;             /* same sums as gamecontrol_t::change() */
    if (lr > 0) return ud > 0 ? 8 : (ud < 0 ? 6 : 7);
    if (lr < 0) return ud > 0 ? 2 : (ud < 0 ? 4 : 3);
    return ud > 0 ? 1 : (ud < 0 ? 5 : 0);
}

static void ship_step(uint16_t pad) {
    int d = pad_to_dir(pad);
    const int vd = 2, vo = 3;
    if (d) ship_di = d;
    switch (ship_di) {
      case 1: ship_y -= vo; break;
      case 2: ship_y -= vd; ship_x += vd; break;
      case 3: ship_x += vo; break;
      case 4: ship_x += vd; ship_y += vd; break;
      case 5: ship_y += vo; break;
      case 6: ship_y += vd; ship_x -= vd; break;
      case 7: ship_x -= vo; break;
      case 8: ship_x -= vd; ship_y -= vd; break;
    }
    ship_x &= MAP_SIZEX * 16 - 1;               /* wrap */
    ship_y &= MAP_SIZEY * 16 - 1;
}

/* ====================================================================================
 * K21 BASES - Kobo's rules (myship.cpp hit_structure, enemy.cpp cannon/core/pipe_1/pipe_2):
 *   - a bolt that touches any pipe cell (HIT_MASK) is stopped;
 *   - NODES (pipe ends: one direction bit, not CORE/HARD) and the CORE have 20 health,
 *     a bolt does 20 (game.bolt_damage, Classic skill) -> 1 hit  (K22 fix; K21 used 5);
 *   - node destroyed (+10): a "pipe_1" demolition runs back along the pipe, one cell every
 *     4 steps, and at the junction removes just that branch;
 *   - core destroyed (+200): "pipe_2" demolitions run out along all 4 directions, clear
 *     every cell, split at junctions (+10 each) and destroy the nodes they reach (+30).
 *   Damage is kept per map cell (8 KB work area, cleared when the level is built).
 *   Not yet: explosions and the ship crashing into pipes (with the enemies).
 * ==================================================================================== */
#define HIT_MASK    (CORE | U_MASK | R_MASK | D_MASK | L_MASK)
#define DIR_BITS    (U_MASK | R_MASK | D_MASK | L_MASK)
#define CELL_HEALTH 20
#define HIT_BOLT    5              /* bolt HIT RADIUS (config.h HIT_BOLT)                      */
#define BOLT_DAMAGE 20             /* K22: game.bolt_damage, Classic skill (K21 wrongly used 5) */
#define MAX_DEMO    64
#define cell_damage ((uint8_t *)WORK_DAMAGE)
/* K35d: with Kobo Classic, BOLT_DAMAGE (20) >= CELL_HEALTH (20): one bolt destroys a node or core,
   so the per-cell damage table is never needed and its 8 KB hold the MENU LOGO instead
   (LOGO_RAM).  A skill with weaker bolts would need the table back - the check below stops
   the build if both uses would collide. */
#define LOGO_RAM    WORK_DAMAGE
#if BOLT_DAMAGE < CELL_HEALTH
#error "cell_damage is needed (bolts weaker than nodes) - move the logo (LOGO_RAM) elsewhere"
#endif

typedef struct { int x, y, a, type, count; } demo_t;   /* type 1 = pipe_1, 2 = pipe_2, 0 = free */
static demo_t demo[MAX_DEMO];
static void spawn_wave(void);                   /* K22, below */
static void fx_new(int id, int x_cs, int y_cs, int h, int v);   /* K35, below */

static int one_dir(int bits) {                  /* exactly one direction bit set */
    bits &= DIR_BITS;
    return bits == U_MASK || bits == R_MASK || bits == D_MASK || bits == L_MASK;
}
static void set_cell(int x, int y, int bits) {  /* new bits + matching tile (0 = empty space) */
    if (bits == 0) map_clearpos(x & (MAP_SIZEX - 1), y & (MAP_SIZEY - 1));
    else *mpos(x, y) = (uint16_t)((bits2tile(bits) << 8) | bits);
}
static void demo_start(int type, int x, int y, int dir) {
    int i;
    for (i = 0; i < MAX_DEMO; i++) if (!demo[i].type) break;
    if (i == MAX_DEMO) return;
    demo[i].type = type; demo[i].count = 4; demo[i].a = 0;
    demo[i].x = x; demo[i].y = y;
    if (type == 2) {                            /* make_pipe_2: clear here, step out */
        set_cell(x, y, 0);
        switch (dir) {
          case 1: demo[i].a = D_MASK; demo[i].y--; break;
          case 3: demo[i].a = L_MASK; demo[i].x++; break;
          case 5: demo[i].a = U_MASK; demo[i].y++; break;
          default:demo[i].a = R_MASK; demo[i].x--; break;
        }
    }
}
static int step_from(int d, int *dx, int *dy) { /* direction bit -> move + opposite bit */
    *dx = 0; *dy = 0;
    if (d == U_MASK) { *dy = -1; return D_MASK; }
    if (d == R_MASK) { *dx = 1;  return L_MASK; }
    if (d == D_MASK) { *dy = 1;  return U_MASK; }
    if (d == L_MASK) { *dx = -1; return R_MASK; }
    return 0;
}
static void demos_step(void) {
    int i;
    for (i = 0; i < MAX_DEMO; i++) {
        demo_t *m = &demo[i];
        int p, rest, dx, dy, an;
        if (!m->type) continue;
        if (++m->count < 4) continue;
        m->count = 0;
        p = *mpos(m->x, m->y) & 0xFF;
        if (IS_SPACE(p)) { m->type = 0; continue; }
        rest = p ^ m->a;
        if (m->type == 1) {                                     /* pipe_1 */
            an = one_dir(rest) && !(rest & (CORE | HARD)) ? step_from(rest & DIR_BITS, &dx, &dy) : 0;
            if (an) { set_cell(m->x, m->y, 0); m->x += dx; m->y += dy; m->a = an; continue; }
            if (!(p & CORE)) set_cell(m->x, m->y, (p ^ m->a) & (HIT_MASK | HARD));
            m->type = 0;
        } else {                                                /* pipe_2 */
            if (rest == 0) { score += 30; set_cell(m->x, m->y, 0); m->type = 0; continue; }
            if (rest == HARD) { set_cell(m->x, m->y, 0); m->type = 0; continue; }
            an = one_dir(rest) && !(rest & (CORE | HARD)) ? step_from(rest & DIR_BITS, &dx, &dy) : 0;
            set_cell(m->x, m->y, 0);
            if (an) { m->x += dx; m->y += dy; m->a = an; continue; }
            {   /* junction: split.  Copy the position FIRST - the first new branch may
                   reuse this very slot (it is free now) and move its x/y. */
                int jx = m->x, jy = m->y;
                m->type = 0;
                if (rest & U_MASK) demo_start(2, jx, jy, 1);
                if (rest & R_MASK) demo_start(2, jx, jy, 3);
                if (rest & D_MASK) demo_start(2, jx, jy, 5);
                if (rest & L_MASK) demo_start(2, jx, jy, 7);
            }
            score += 10;
        }
    }
}
/* a bolt at world pixel (bx, by): returns 1 if it hit the base (bolt is used up) */
static int bolt_hits_base(int bx, int by) {
    int cx = (bx >> 4) & (MAP_SIZEX - 1), cy = (by >> 4) & (MAP_SIZEY - 1);
    int p = *mpos(cx, cy) & 0xFF;
    uint8_t *dmg;
    if (IS_SPACE(p) || !(p & HIT_MASK)) return 0;
    if (!(p & CORE) && (!one_dir(p) || (p & HARD))) {             /* plain pipe: bolt stops */
        static uint32_t last_metal = 0;
        if (((++last_metal) & 3) == 0) sfx(SFX_METAL);          /* K33: every 4th pipe hit */
        fx_new(FX_BULLET, bx * 256, by * 256, 0, 0);             /* K35: spark on the pipe */
        return 1;
    }
#if BOLT_DAMAGE >= CELL_HEALTH
    (void)dmg;                                  /* K35d: one bolt kills - no damage to keep */
    {
#else
    dmg = &cell_damage[(cy << MAP_SIZEX_LOG2) + cx];
    *dmg += BOLT_DAMAGE;
    if (*dmg >= CELL_HEALTH) {
        *dmg = 0;
#endif
        if (p & CORE) {                         /* kill_core */
            score += 200;
            if (opt_skill != SKILL_CLASSIC) {           /* DIFFICULTY: +10 % health per core */
                ship_health += sk_hp10[opt_skill];      /* table, no division (no libgcc) */
                if (ship_health > 2 * sk_health[opt_skill]) ship_health = 2 * sk_health[opt_skill];
            }
            sfx(SFX_CORE);                      /* K33 */
            fx_new(FX_EXPLO4, (cx * 16 + 8) * 256, (cy * 16 + 8) * 256, 0, 0);   /* K35 */
            spawn_wave();                       /* K22: manage.cpp destroyed_a_core */
            demo_start(2, cx, cy, 3); demo_start(2, cx, cy, 7);
            demo_start(2, cx, cy, 1); demo_start(2, cx, cy, 5);
        } else {                                /* kill_cannon */
            score += 10;
            sfx(SFX_NODE);                      /* K33 */
            fx_new(FX_EXPLO5, (cx * 16 + 8) * 256, (cy * 16 + 8) * 256, 0, 0);   /* K35 */
            demo_start(1, cx, cy, 0);
        }
    }
    return 1;
}

/* ====================================================================================
 * K20 FIRING - Kobo's classic (XKobo) shot, myship.cpp xkobo_shot / shot_single / move:
 *   A or B held = every step fires TWO bolts from the ship: one in the flying direction,
 *   one straight backwards.  Speed 12 px/step straight, 8 diagonally.  A bolt disappears
 *   when it is 128 px (VIEWLIMIT/2 + 16) or more from the ship on either axis.
 *   Picture: bolt.png frame ((dir-1) & 3) * 4 + animtab[age & 7].
 *   Bolts do not hit anything yet (milestone 4: bases and enemies).
 * ==================================================================================== */
static int wrapd(int d, int size) {             /* shortest signed distance on a wrapping axis */
    d &= size - 1;
    return d >= size / 2 ? d - size : d;
}

static void shot_single(int dir) {
    int i;
    for (i = 0; i < MAX_BOLTS; i++) if (!bolt_st[i]) break;
    if (i == MAX_BOLTS) return;                 /* all 40 in flight */
    bolt_st[i] = 1; bolt_di[i] = dir;
    bolt_x[i] = ship_x; bolt_y[i] = ship_y;
    switch (dir) {
      case 1: bolt_dx[i] = 0;       bolt_dy[i] = -BEAMV1; break;
      case 2: bolt_dx[i] = BEAMV2;  bolt_dy[i] = -BEAMV2; break;
      case 3: bolt_dx[i] = BEAMV1;  bolt_dy[i] = 0;       break;
      case 4: bolt_dx[i] = BEAMV2;  bolt_dy[i] = BEAMV2;  break;
      case 5: bolt_dx[i] = 0;       bolt_dy[i] = BEAMV1;  break;
      case 6: bolt_dx[i] = -BEAMV2; bolt_dy[i] = BEAMV2;  break;
      case 7: bolt_dx[i] = -BEAMV1; bolt_dy[i] = 0;       break;
      default:bolt_dx[i] = -BEAMV2; bolt_dy[i] = -BEAMV2; break;
    }
}

static void bolts_step(uint16_t pad) {
    int i, n = 0;
    /* DIFFICULTY: nose / tail reload, gun heat, Newbie's alternating fire.  Classic: no heat,
       both guns every step - exactly as before. */
    if (nose_wait) nose_wait--;
    if (tail_wait) tail_wait--;
    nose_temp -= sk_cool[opt_skill]; if (nose_temp < 0) nose_temp = 0;
    tail_temp -= sk_cool[opt_skill]; if (tail_temp < 0) tail_temp = 0;
    if (nose_hot && nose_temp < 128) nose_hot = 0;
    if (tail_hot && tail_temp < 128) tail_hot = 0;
    if (pad & (PAD_A | PAD_B)) {                 /* K37g: A or B fires (C disabled, fire option removed) */
        static int shot_snd = 0;
        int fired = 0;
        int nose_ok = !nose_hot && !nose_wait && (!sk_alt[opt_skill] || alt_side == 0);
        int tail_ok = !tail_hot && !tail_wait && (!sk_alt[opt_skill] || alt_side == 1);
        if (nose_ok) {
            shot_single(ship_di);
            nose_wait = sk_noseload[opt_skill] > 1 ? sk_noseload[opt_skill] - 1 : 0;
            nose_temp += sk_noseheat[opt_skill];
            if (nose_temp >= 256) { nose_temp = 256; nose_hot = 1; }
            fired = 1;
        }
        if (tail_ok) {
            shot_single(ship_di > 4 ? ship_di - 4 : ship_di + 4);
            tail_wait = sk_tailload[opt_skill] > 1 ? sk_tailload[opt_skill] - 1 : 0;
            tail_temp += sk_tailheat[opt_skill];
            if (tail_temp >= 256) { tail_temp = 256; tail_hot = 1; }
            fired = 1;
        }
        if (sk_alt[opt_skill]) alt_side ^= 1;
        if (fired && ++shot_snd >= 3) { shot_snd = 0; sfx(SFX_SHOT); }   /* K33: every 3rd shot */
    }
    for (i = 0; i < MAX_BOLTS; i++) {
        if (!bolt_st[i]) continue;
        bolt_st[i]++;
        bolt_x[i] = (bolt_x[i] + bolt_dx[i]) & (MAP_SIZEX * 16 - 1);
        bolt_y[i] = (bolt_y[i] + bolt_dy[i]) & (MAP_SIZEY * 16 - 1);
        {
            int ax = wrapd(bolt_x[i] - ship_x, MAP_SIZEX * 16);
            int ay = wrapd(bolt_y[i] - ship_y, MAP_SIZEY * 16);
            if (ax < 0) ax = -ax;
            if (ay < 0) ay = -ay;
            if (ax >= PF_W / 2 + 16 || ay >= PF_H / 2 + 16) { bolt_st[i] = 0; continue; }
        }
        if (bolt_hits_base(bolt_x[i], bolt_y[i])) { bolt_st[i] = 0; continue; }   /* K21 */
        n++;
    }
    bolts_alive = n;
}

static void draw_sprite(const uint8_t *src, int w, int h, int sx, int sy);
static void draw_bolts(void) {
    static const uint8_t animtab[8] = { 3, 2, 1, 0, 1, 2, 1, 2 };
    int i;
    for (i = 0; i < MAX_BOLTS; i++) {
        int f, sx, sy;
        if (!bolt_st[i]) continue;
        f = (((bolt_di[i] - 1) & 3) << 2) + animtab[bolt_st[i] & 7];
        sx = PF_W / 2 + wrapd(bolt_x[i] - ship_x, MAP_SIZEX * 16) - BOLT_W / 2;
        sy = PF_H / 2 + wrapd(bolt_y[i] - ship_y, MAP_SIZEY * 16) - BOLT_W / 2;
        draw_sprite(bolt_gfx + f * BOLT_W * BOLT_W, BOLT_W, BOLT_W, sx, sy);
    }
}


/* ====================================================================================
 * K22 ENEMIES - level 1 (scenes.cpp scene 1: 10 x enemy4 at speed 5; nodes fire "beam"
 * every 128 steps, cores every 16).  Kobo rules from enemy.cpp / enemies.h / screen.cpp:
 *   positions in "CS" units = pixels * 256 (PIXEL2CS), speed h/v in CS per step;
 *   enemy4: health 20, hit radius 6, steers towards the ship (move_enemy_template(4, 96)),
 *           picture missile3 frame (direction 1..16) - 1, score 1 (enemy_kind.score);
 *   beam:   health 1, not shootable, damage 20, hit radius 2, gone at 144 px from the ship,
 *           aimed at the ship (shot_template: shift 6, spread 32 nodes / 0 cores);
 *   explosion: explo1e, 8 frames, one per step, drifts at half the enemy's speed.
 *   Nodes/cores fire only while the ship is within 120 px (VIEWLIMIT/2 + 8).
 *   Ship hits are only COUNTED (strip H..): lives and game over come later.
 * ==================================================================================== */
#define BANK_EN4      12          /* missile3.png 16 x 20x20 */
#define BANK_BEAM     23          /* bullet5b.png 16 x 8x8   */
#define BANK_EXPL     15          /* explo1e.png   8 x 24x24 */
#define en4_gfx       ((uint8_t *)WORK_EN4GFX)
#define beam_gfx      ((uint8_t *)WORK_BEAMGFX)
#define expl_gfx      ((uint8_t *)WORK_EXPLGFX)
#define MAX_ENEMIES   128
#define ET_NONE   0
#define ET_ENEMY4 1
#define ET_BEAM   2
#define ET_EXPL   3
#define ET_ENEMY1 4               /* K25 */
#define ET_ENEMY3 5
#define ET_ROCK   6
#define ET_RING   7
#define en1_gfx   ((uint8_t *)WORK_EN1GFX)
#define en3_gfx   ((uint8_t *)WORK_EN3GFX)
#define rock_gfx  ((uint8_t *)WORK_ROCKGFX)
#define ring_gfx  ((uint8_t *)WORK_RINGGFX)
#define ROCK_HEALTH (255 * 20)    /* game.rock_health, Classic: practically indestructible */
#define ET_ENEMY6 8               /* K26 */
#define ET_BOMB1  9
#define ET_BOMB2  10
#define ET_DETO   11              /* bomb detonation: 8 frames, like an explosion */
#define en6_gfx   ((uint8_t *)WORK_EN6GFX)
#define bomb_gfx  ((uint8_t *)WORK_BOMBGFX)
#define deto_gfx  ((uint8_t *)WORK_DETOGFX)
#define ET_FX     19              /* K35: RLE explosion effect, pad[2] = FX_ id */
#define ET_ENEMY2 12              /* K27 */
#define ET_ENEMY5 13
#define ET_ENEMY7 14
#define ET_M1     15              /* mother ships enemy_m1 .. m4 */
#define ET_M4     18
#define en2_gfx    ((uint8_t *)WORK2_FIGHTER)
#define en5_gfx    ((uint8_t *)WORK2_BMRG)
#define en7_gfx    ((uint8_t *)WORK2_BMRP)
#define big_gfx    ((uint8_t *)WORK2_BIGSHIP)
#define rock2_gfx  ((uint8_t *)WORK2_ROCK2)
#define rock3_gfx  ((uint8_t *)WORK2_ROCK3)
typedef struct { int type, x, y, h, v, di, health, count, a_dir, pad[3]; } enemy_t;   /* 48 bytes */
typedef struct { int cx, cy, a, b, count; } cannon_t;                           /* 20 bytes */
#define enemy         ((enemy_t *)WORK2_ENEMIES)    /* K27: moved to WORK2, 128 entries */
#define cannon        ((cannon_t *)WORK2_CANNONS)   /* K27: moved to WORK2, 512 entries */
#define MAX_CANNONS   512
#define WX_CS  ((MAP_SIZEX * 16) << 8)     /* world width  in CS units */
#define WY_CS  ((MAP_SIZEY * 16) << 8)     /* world height in CS units */
/* CS -> pixels.  WHY unsigned: a SIGNED ">> 8" makes GCC call __ashiftrt_r4_8 from libgcc
   (the SH-2 only shifts signed values 1 bit at a time) and we link without libgcc.
   Positions are always wrapped to 0..world size, so they are never negative. */
#define CS2PX(v) ((int)((uint32_t)(v) >> 8))

static int iabs(int a) { return a < 0 ? -a : a; }

/* speed2dir(h, v, 16) without division: 1 = up, clockwise, 22.5 degrees each.
   Sector borders at 11.25/33.75/56.25/78.75 degrees = tan 0.199/0.668/1.497/5.027. */
static int speed2dir16(int h, int v) {
    int ax = iabs(h), ay = iabs(v), k = 0;
    if (!h && !v) return 0;
    if (ax * 1000 > ay * 199)  k++;
    if (ax * 1000 > ay * 668)  k++;
    if (ax * 1000 > ay * 1497) k++;
    if (ax * 1000 > ay * 5027) k++;         /* k = 0 (vertical) .. 4 (horizontal) */
    if (h >= 0 && v <= 0) return 1 + k;     /* up    .. right */
    if (h >= 0)           return 9 - k;     /* down  .. right */
    if (v > 0)            return 9 + k;     /* down  .. left  */
    return k == 0 ? 1 : 17 - k;             /* left  .. up    */
}

static enemy_t *enemy_new(int type, int x, int y, int h, int v) {
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) if (enemy[i].type == ET_NONE) break;
    if (i == MAX_ENEMIES) return 0;
    enemy[i].type = type; enemy[i].x = x & (WX_CS - 1); enemy[i].y = y & (WY_CS - 1);
    enemy[i].h = h; enemy[i].v = v; enemy[i].count = 0; enemy[i].di = 1;
    enemy[i].health = (type == ET_BEAM || type == ET_EXPL) ? 1 : 20;
    enemy[i].pad[0] = 0;                               /* 1 = fired by a gun (released far away) */
    if (type == ET_BEAM) enemy[i].di = 1 + (int)krand_4(&pubrand);
    if (type == ET_ENEMY2) enemy[i].count = (int)(krand_get(&gamerand) & 63);   /* make_enemy2 */
    if (type >= ET_M1 && type <= ET_M4) {              /* make_enemy_m1..m4 */
        enemy[i].health = 20 * 26; enemy[i].count = (int)(krand_get(&gamerand) & 15);
    }
    if (type == ET_ENEMY6 || type == ET_ENEMY5 || type == ET_ENEMY7) {   /* make_enemy */
        enemy[i].count = (int)(krand_get(&gamerand) & 127); enemy[i].a_dir = 0;
    }
    if (type == ET_ROCK) {                             /* make_rock */
        enemy[i].health = sk_rockhp[opt_skill];              /* DIFFICULTY (game.rock_health) */
        enemy[i].di = 1 + (int)(krand_get(&gamerand) >> 27);        /* gamerand.get(5) + 1 */
        enemy[i].a_dir = krand_1(&gamerand) ? 1 : -1;
        enemy[i].pad[1] = (int)umod(krand_get(&gamerand), 3);      /* K27: rock1 / rock2 / rock3 */
    }
    return &enemy[i];
}

/* screen.cpp generate_fixed_enemies: one enemy set of the level (enemy4 stand-in, see the
   level table), placed at least 224 px from the ship; the next call uses the next set */
static void spawn_wave(void) {
    static const int sint[16] = { 0, 12, 23, 30, 32, 30, 23, 12, 0, -12, -23, -30, -32, -30, -23, -12 };
    static const int cost[16] = { 32, 30, 23, 12, 0, -12, -23, -30, -32, -30, -23, -12, 0, 12, 23, 30 };
    const kscene_t *sc = &kscene[scene_no];
    int j, num, sp, ty;
    if (sc->nsets == 0) return;                               /* level 6: no fixed enemies */
    num = sc->set[gen_count][0]; sp = sc->set[gen_count][1]; ty = sc->set[gen_count][2];
    if (++gen_count >= sc->nsets) gen_count = 0;             /* next set next time */
    for (j = 0; j < num; j++) {
        int x = (int)umod(krand_get(&gamerand), MAP_SIZEX * 16 - 224 * 2) - (MAP_SIZEX * 16 / 2 - 224);
        int y = (int)umod(krand_get(&gamerand), MAP_SIZEY * 16 - 224 * 2) - (MAP_SIZEY * 16 / 2 - 224);
        int t = (int)krand_4(&gamerand);
        x += x < 0 ? -224 : 224;
        y += y < 0 ? -224 : 224;
        enemy_new(ty, (x + ship_x) * 256, (y + ship_y) * 256,
                  sp * sint[t] * 4, sp * cost[t] * 4);        /* PIXEL2CS(sp * sint) / 64 */
    }
}

static int is_gun(int m) {                     /* node or core still standing */
    if (IS_SPACE(m)) return 0;
    m &= 0xFF;
    return m == U_MASK || m == R_MASK || m == D_MASK || m == L_MASK || (m & CORE);
}

/* screen.cpp prepare(): every node gets a cannon, every core a core (b = interval - 1) */
static void find_cannons(void) {
    int i, j;
    n_cannons = 0;
    for (j = 0; j < MAP_SIZEY; j++)
        for (i = 0; i < MAP_SIZEX; i++) {
            int m = *mpos(i, j);
            if (!is_gun(m) || n_cannons >= MAX_CANNONS) continue;
            cannon[n_cannons].cx = i; cannon[n_cannons].cy = j; cannon[n_cannons].count = 0;
            {   /* K27: Kobo screen.cpp prepare(): interval >> level, at least 4 */
                int iv = (m & CORE) ? kscene[scene_no].i2 : kscene[scene_no].i1, k;
                for (k = 0; k < loop_no; k++) iv >>= 1;
                if (iv < 4) iv = 4;
                cannon[n_cannons].b = iv - 1;
            }
            cannon[n_cannons].a = (int)(krand_get(&gamerand) & (uint32_t)cannon[n_cannons].b);
            n_cannons++;
        }
}

/* shot_template: shot of 'kind' from (px,py) aimed at the ship, spread rnd;
   speed = distance / 64 for beams (shift 6), distance / 32 for anything else (shift 5) */
static void fire_shot2(int kind, int px, int py, int shift, int rnd, int maxspeed);
static void fire_shot(int kind, int px, int py, int rnd) {
    fire_shot2(kind, px, py, kind == ET_BEAM ? 6 : 5, rnd, 0);   /* K27: cannons' rule */
}
/* shot_template(kind, shift, rnd, maxspeed): aimed at the ship; speed = distance * 256 >> shift */
static void fire_shot2(int kind, int px, int py, int shift, int rnd, int maxspeed) {
    int vx = ship_x - px, vy = ship_y - py;
    vx = wrapd(vx, MAP_SIZEX * 16); vy = wrapd(vy, MAP_SIZEY * 16);
    if (rnd) {
        vx += (int)(krand_get(&gamerand) & (uint32_t)(rnd - 1)) - (rnd >> 1);
        vy += (int)(krand_get(&gamerand) & (uint32_t)(rnd - 1)) - (rnd >> 1);
    }
    if (kind == ET_BEAM && iabs(vx) < 140 && iabs(vy) < 140) {   /* K33: close enough to hear */
        static int beam_snd = 0;
        if (++beam_snd >= 2) { beam_snd = 0; sfx(SFX_BEAM); }
    }
    if (kind != ET_BEAM && kind != ET_RING) sfx(SFX_LAUNCH);     /* K33: launches (m1/m2, guns) */
    if (shift == 6)      { vx *= 4;  vy *= 4; }              /* PIXEL2CS(v) / (1 << 6) */
    else if (shift == 5) { vx *= 8;  vy *= 8; }              /* PIXEL2CS(v) / (1 << 5) */
    else                 { vx *= 16; vy *= 16; }             /* PIXEL2CS(v) / (1 << 4) */
    if (maxspeed > 0) {
        if (vx > maxspeed) vx = maxspeed; else if (vx < -maxspeed) vx = -maxspeed;
        if (vy > maxspeed) vy = maxspeed; else if (vy < -maxspeed) vy = -maxspeed;
    }
    {
        enemy_t *e = enemy_new(kind, px * 256 + vx, py * 256 + vy, vx, vy);
        if (e) e->pad[0] = 1;                                /* a gun's shot */
    }
}

/* shot_template_8_dir: 8 shots in fixed directions from (px, py) */
static void fire_8dir(int kind, int px, int py) {
    static const int vx8[8] = { 0, 200, 300, 200, 0, -200, -300, -200 };
    static const int vy8[8] = { -300, -200, 0, 200, 300, 200, 0, -200 };
    int i;
    for (i = 0; i < 8; i++) enemy_new(kind, px * 256, py * 256, vx8[i], vy8[i]);
}

static void fx_new(int id, int x_cs, int y_cs, int h, int v) {   /* K35 */
    enemy_t *f = enemy_new(ET_FX, x_cs, y_cs, h, v);
    if (f) { f->pad[2] = id; f->count = 0; }
}

static void enemy_die(enemy_t *e) {                          /* kill_default */
    int x = e->x, y = e->y, h = e->h >> 1, v = e->v >> 1;
    if (e->type == ET_ENEMY4 || e->type == ET_ENEMY3 || e->type == ET_RING) score += 1;
    if (e->type == ET_ENEMY1 || e->type == ET_ENEMY6) score += 2;
    if (e->type == ET_BOMB1)  score += 5;
    if (e->type == ET_BOMB2)  score += 20;
    if (e->type == ET_ENEMY2) score += 10;                     /* K27 */
    if (e->type == ET_ENEMY5 || e->type == ET_ENEMY7) score += 5;
    if (e->type >= ET_M1 && e->type < ET_M4) score += 50;
    if (e->type == ET_M4)     score += 100;
    if (e->type == ET_ROCK)   score += 10;
    {   /* K33: explosion sound by type */
        static int alt = 0;
        if (e->type == ET_RING) sfx(SFX_RING);
        else if (e->type == ET_ROCK) sfx(SFX_ROCK);
        else if (e->type != ET_BEAM) { sfx((alt ^= 1) ? SFX_ENEMY1 : SFX_ENEMY2); }
    }
    {   /* K35: Kobo's explosion kinds - ring / rock / one of three for everything else */
        static int alt3 = 0;
        int t = e->type;
        e->type = ET_NONE;
        if (t == ET_RING) fx_new(FX_RING, x, y, h, v);
        else if (t == ET_ROCK) fx_new(FX_ROCK, x, y, h, v);
        else { fx_new(FX_EXPLO3 + alt3, x, y, h, v); alt3 = alt3 < 2 ? alt3 + 1 : 0; }
    }
}

/* K37c DEMO AUTOPILOT: the ship plays a real level.  It heads for the nearest node or core,
   fires all the time, and looks 20 and 36 px ahead - if a pipe is there it turns (45, then
   90, then 135 degrees, whichever way is free first).  Returns a pad value. */
static const int8_t dir_dx[9] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };
static const int8_t dir_dy[9] = { 0, -1, -1, 0, 1, 1, 1, 0, -1 };
static const uint16_t dir_pad[9] = { 0, PAD_UP, PAD_UP | PAD_RIGHT, PAD_RIGHT, PAD_DOWN | PAD_RIGHT,
                                     PAD_DOWN, PAD_DOWN | PAD_LEFT, PAD_LEFT, PAD_UP | PAD_LEFT };
static int pipe_at(int px, int py) {
    int m = *mpos((int)(((uint32_t)px >> 4) & (MAP_SIZEX - 1)), (int)(((uint32_t)py >> 4) & (MAP_SIZEY - 1)));
    return !IS_SPACE(m) && (m & HIT_MASK);
}
static int dir_free(int d) {
    return !pipe_at(ship_x + dir_dx[d] * 20, ship_y + dir_dy[d] * 20) &&
           !pipe_at(ship_x + dir_dx[d] * 36, ship_y + dir_dy[d] * 36);
}
static uint16_t demo_ai(void) {
    /* K37f: no more getting stuck - (1) after dodging a pipe, keep that direction for 30 frames;
       (2) if the distance to the target has not shrunk for 180 frames, skip that gun for a while. */
    static int commit_dir = 0, commit_left = 0, best_d = 0x7FFFFFFF, no_progress = 0;
    static int skip_gun = -1, skip_left = 0, cur_gun = -1;
    int i, bd = 0x7FFFFFFF, tx = 0, ty = 0, found = 0, want, k, gi = -1;
    if (commit_left > 0 && dir_free(commit_dir)) { commit_left--; return (uint16_t)(dir_pad[commit_dir] | PAD_A); }
    commit_left = 0;
    if (skip_left > 0 && --skip_left == 0) skip_gun = -1;
    for (i = 0; i < n_cannons; i++) {
        int m = *mpos(cannon[i].cx, cannon[i].cy), dx, dy, d;
        if (!is_gun(m) || i == skip_gun) continue;
        dx = wrapd(cannon[i].cx * 16 + 8 - ship_x, MAP_SIZEX * 16);
        dy = wrapd(cannon[i].cy * 16 + 8 - ship_y, MAP_SIZEY * 16);
        d = iabs(dx) + iabs(dy);
        if (d < bd) { bd = d; tx = dx; ty = dy; found = 1; gi = i; }
    }
    if (!found) return PAD_A;
    if (gi != cur_gun) { cur_gun = gi; best_d = bd; no_progress = 0; }
    else if (bd < best_d - 8) { best_d = bd; no_progress = 0; }
    else if (++no_progress > 180) { skip_gun = gi; skip_left = 600; cur_gun = -1; no_progress = 0; }
    {   /* direction 1..8 towards the target */
        int v = iabs(ty) * 2 > iabs(tx) ? (ty < 0 ? -1 : 1) : 0;
        int h = iabs(tx) * 2 > iabs(ty) ? (tx < 0 ? -1 : 1) : 0;
        for (want = 1; want <= 8; want++) if (dir_dx[want] == h && dir_dy[want] == v) break;
        if (want > 8) want = ship_di;
    }
    if (!dir_free(want)) {
        static const int8_t turn[6] = { 1, -1, 2, -2, 3, -3 };
        for (k = 0; k < 6; k++) {
            int d = ((want - 1 + turn[k] + 8) & 7) + 1;
            if (dir_free(d)) { want = d; commit_dir = d; commit_left = 30; break; }
        }
    }
    return (uint16_t)(dir_pad[want] | PAD_A);
}

/* K23 - the ship is destroyed: explosion, bolts gone, 60-step pause (then respawn or game over) */
static void ship_die(void) {
    int k;
    if (dead_timer || game_over) return;
    dead_timer = 60;
    sfx(SFX_PLAYER);                                   /* K33 */
    fx_new(FX_EXPLO4, ship_x * 256, ship_y * 256, 0, 0);            /* K35: the ship blows up */
    fx_new(FX_EXPLO3, (ship_x + 6) * 256, (ship_y - 5) * 256, 64, -64);
    for (k = 0; k < MAX_BOLTS; k++) bolt_st[k] = 0;
    enemy_new(ET_EXPL, ship_x * 256, ship_y * 256, 0, 0);
}

static int cores_left(void) {
    int i, n = 0;
    for (i = 0; i < n_cannons; i++) {
        int m = *mpos(cannon[i].cx, cannon[i].cy);
        if (!IS_SPACE(m) && (m & CORE)) n++;
    }
    return n;
}

/* K23 - (re)start at the level's start point: ship, bolts, enemies, first enemy set */
static void respawn(void) {
    int k;
    ship_health = sk_health[opt_skill];                 /* DIFFICULTY */
    nose_temp = tail_temp = nose_hot = tail_hot = nose_wait = tail_wait = 0;
    ship_x = kscene[scene_no].startx * 16;
    ship_y = kscene[scene_no].starty * 16;
    ship_di = 1;
    for (k = 0; k < MAX_BOLTS; k++) bolt_st[k] = 0;
    for (k = 0; k < MAX_ENEMIES; k++) enemy[k].type = ET_NONE;
    gen_count = 0;
    spawn_wave();
}

/* K23 - build level 'sc' (0-based) from the level table: maze, guns, enemies */
static void build_level(int sc) {
    const kscene_t *s = &kscene[sc];
    uint32_t k;
#if BOLT_DAMAGE < CELL_HEALTH
    volatile uint8_t *dz = cell_damage;         /* volatile: keep it a loop, not a memset call */
#endif
    int i;
    scene_no = sc;
    {   /* K27: tile set = (level / 10) % 5, as screen.cpp; reloaded only when it changes */
        int t = sc, ts = 0;
        while (t >= 10) { t -= 10; ts++; }
        while (ts >= 5) ts -= 5;
        if (ts != tset_loaded) {
            {   /* K31: from the frame-buffer cache (cache_tilesets) - NO CD read here: a data
                   read while the CD audio plays silences it.  Both buffers hold the cache. */
                volatile uint8_t *src = TSET_CACHE + ts * (TILE_FRAMES * 256);
                uint8_t *dst = &tileset[0][0];
                int k;
                for (k = 0; k < TILE_FRAMES * 256; k++) dst[k] = src[k];
            }
            tset_loaded = ts;
            /* K30: no screen reset - loading uses the off-screen part of the frame buffer */
        }
    }
    map_init();
    for (i = 0; i < s->nbases; i++)
        make_maze(s->base[i][0], s->base[i][1], s->base[i][2], s->base[i][3]);
    map_convert((unsigned)s->ratio);
#if BOLT_DAMAGE < CELL_HEALTH
    for (k = 0; k < MAP_SIZEX * MAP_SIZEY; k++) dz[k] = 0;
#endif
    for (k = 0; k < MAX_DEMO; k++) demo[k].type = 0;
    find_cannons();
    respawn();
}

/* K26 helpers - same results as Kobo's C, without libgcc (see the notes at krand_2/CS2PX):
   sar4 = signed x >> 4 (rounds down, like the SH-2's arithmetic shift), div24 = x / 24
   (C division rounds towards zero).  |x| stays below 100 px * 256 here. */
static int sar4(int x) { return x >= 0 ? (int)((uint32_t)x >> 4) : -(int)(((uint32_t)(-x) + 15) >> 4); }
static int div24(int x) {
    uint32_t m = (uint32_t)(x < 0 ? -x : x), q = 0;
    while (m >= 24 * 64) { m -= 24 * 64; q += 64; }
    while (m >= 24)      { m -= 24;      q++; }
    return x < 0 ? -(int)q : (int)q;
}

/* move_bomb1 / move_bomb2: when the ship is lined up with the bomb (within 100 px along one
   axis and 30 / 20 px across it), the bomb bursts into beams aimed around the ship. */
static void bomb_burst(enemy_t *e, int dx, int dy) {
    int x = CS2PX(e->x), y = CS2PX(e->y), i, t;
    int vx1 = div24(-dx * 256), vy1 = div24(-dy * 256);
    int vx2 = vx1, vx3 = vx1, vy2 = vy1, vy3 = vy1, vx4, vy4, vx5, vy5;
    int turns = e->type == ET_BOMB1 ? 4 : 6;
    for (i = 0; i < turns; i++) {
        t = vx2; vx2 += sar4(vy2); vy2 -= sar4(t);
        t = vx3; vx3 -= sar4(vy3); vy3 += sar4(t);
    }
    e->type = ET_NONE;                                  /* release() */
    sfx(SFX_DETO);                                      /* K33 */
    if (turns == 4) {
        enemy_new(ET_BEAM, x * 256, y * 256, vx2, vy2);
        enemy_new(ET_BEAM, x * 256, y * 256, vx3, vy3);
    } else {
        vx4 = vx2; vy4 = vy2; vx5 = vx3; vy5 = vy3;
        for (i = 0; i < 6; i++) {
            t = vx2; vx2 += sar4(vy2); vy2 -= sar4(t);
            t = vx3; vx3 -= sar4(vy3); vy3 += sar4(t);
        }
        enemy_new(ET_BEAM, x * 256, y * 256, vx1, vy1);
        enemy_new(ET_BEAM, x * 256, y * 256, vx2, vy2);
        enemy_new(ET_BEAM, x * 256, y * 256, vx3, vy3);
        enemy_new(ET_BEAM, x * 256, y * 256, vx4, vy4);
        enemy_new(ET_BEAM, x * 256, y * 256, vx5, vy5);
    }
    enemy_new(ET_DETO, x * 256, y * 256, vx1, vy1);
}

static void enemies_step(void) {
    int i, n = 0;
    for (i = 0; i < n_cannons; i++) {                        /* move_cannon / move_core */
        cannon_t *c = &cannon[i];
        int m = *mpos(c->cx, c->cy), px, py, norm;
        c->count = (c->count + 1) & c->b;
        if (c->count != c->a || !is_gun(m)) continue;
        px = c->cx * 16 + 8; py = c->cy * 16 + 8;
        norm = iabs(wrapd(px - ship_x, MAP_SIZEX * 16));
        if (iabs(wrapd(py - ship_y, MAP_SIZEY * 16)) > norm) norm = iabs(wrapd(py - ship_y, MAP_SIZEY * 16));
        if (norm < PF_W / 2 + 8)
            fire_shot((m & CORE) ? kscene[scene_no].k2 : kscene[scene_no].k1, px, py, (m & CORE) ? 0 : 32);
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &enemy[i];
        int dx, dy, norm, hit, k;
        if (e->type == ET_NONE) continue;
        if (e->type == ET_FX) {                              /* K35: RLE effect, its own frames */
            e->x = (e->x + e->h) & (WX_CS - 1); e->y = (e->y + e->v) & (WY_CS - 1);
            if (++e->count >= fx_n[e->pad[2]]) e->type = ET_NONE;
            continue;
        }
        if (e->type == ET_EXPL || e->type == ET_DETO) {      /* 8 frames, then gone */
            e->x = (e->x + e->h) & (WX_CS - 1); e->y = (e->y + e->v) & (WY_CS - 1);
            if (++e->count >= 8) e->type = ET_NONE;
            continue;
        }
        e->x = (e->x + e->h) & (WX_CS - 1);                  /* _enemy::move */
        e->y = (e->y + e->v) & (WY_CS - 1);
        dx = wrapd(CS2PX(e->x) - ship_x, MAP_SIZEX * 16);
        dy = wrapd(CS2PX(e->y) - ship_y, MAP_SIZEY * 16);
        norm = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
        if (e->type == ET_ENEMY4 || e->type == ET_ENEMY1 || e->type == ET_ENEMY3) {
            /* move_enemy_template(quick, maxspeed): enemy4 (4,96), enemy1 (2,256), enemy3 (32,96) */
            int nd, q = e->type == ET_ENEMY4 ? 4 : (e->type == ET_ENEMY1 ? 2 : 32);
            int ms = e->type == ET_ENEMY1 ? 256 : 96;
            if (dx > 0)      { if (e->h > -ms) e->h -= q; }
            else if (dx < 0) { if (e->h <  ms) e->h += q; }
            if (dy > 0)      { if (e->v > -ms) e->v -= q; }
            else if (dy < 0) { if (e->v <  ms) e->v += q; }
            nd = speed2dir16(e->h, e->v);
            if (nd > 0) e->di = nd;
            hit = 6;
        } else if (e->type == ET_ENEMY2) {                   /* move_enemy2: fighter */
            int nd;
            if (dx > 0)      { if (e->h > -192) e->h -= 4; }
            else if (dx < 0) { if (e->h <  192) e->h += 4; }
            if (dy > 0)      { if (e->v > -192) e->v -= 4; }
            else if (dy < 0) { if (e->v <  192) e->v += 4; }
            nd = speed2dir16(e->h, e->v);
            if (nd > 0) e->di = nd;
            if (--e->count <= 0) {                           /* a beam every 32 steps, close by */
                if (norm < PF_W / 2 + 8) fire_shot2(ET_BEAM, CS2PX(e->x), CS2PX(e->y), 5, 0, 0);
                e->count = 32;
            }
            hit = 6;
        } else if (e->type == ET_ENEMY5 || e->type == ET_ENEMY7) {   /* move_enemy5 / move_enemy7 */
            int q = 0, nd;
            if (e->a_dir == 0) {
                if (norm > PF_W / 2 - 32) q = 6; else e->a_dir = 1;
            } else {
                if (norm < PF_W) q = 4; else e->a_dir = 0;
            }
            if (q == 4) {                                    /* template_2 / template_3: circle */
                if (e->type == ET_ENEMY5) { e->h = -dy * 16; e->v =  dx * 16; }
                else                      { e->h =  dy * 16; e->v = -dx * 16; }
            }
            if (q) {
                if (dx > 0)      { if (e->h > -192) e->h -= q; }
                else if (dx < 0) { if (e->h <  192) e->h += q; }
                if (dy > 0)      { if (e->v > -192) e->v -= q; }
                else if (dy < 0) { if (e->v <  192) e->v += q; }
                nd = speed2dir16(e->h, e->v);
                if (nd > 0) e->di = nd;
            }
            if (--e->count <= 0) {                           /* a beam every 8 steps, from afar */
                e->count = 8;
                if (norm > PF_W / 2 - 32) fire_shot2(ET_BEAM, CS2PX(e->x), CS2PX(e->y), 6, 0, 0);
            }
            hit = 6;
        } else if (e->type >= ET_M1 && e->type <= ET_M4) {   /* move_enemy_m1 .. m4: mother ships */
            int q = e->type == ET_M4 ? 2 : 3, ms = e->type == ET_M4 ? 96 : 128;
            int px = CS2PX(e->x), py = CS2PX(e->y);
            if (dx > 0)      { if (e->h > -ms) e->h -= q; }   /* move_enemy_m: no turning picture */
            else if (dx < 0) { if (e->h <  ms) e->h += q; }
            if (dy > 0)      { if (e->v > -ms) e->v -= q; }
            else if (dy < 0) { if (e->v <  ms) e->v += q; }
            if (e->type <= ET_M1 + 1) { if (++e->di > 16) e->di = 1; }   /* m1, m2 spin one way */
            else                      { if (--e->di < 1)  e->di = 16; }  /* m3, m4 the other   */
            if ((e->count--) <= 0) {
                if (e->type == ET_M1) {
                    e->count = 4;
                    if (norm < PF_W / 2 - 16) fire_shot2(ET_ENEMY1, px, py, 4, 0, 0);
                } else if (e->type == ET_M1 + 1) {
                    e->count = 8;
                    if (norm < PF_W / 2 + 8) fire_shot2(ET_ENEMY2, px, py, 4, 128, 192);
                } else if (e->type == ET_M1 + 2) {
                    e->count = 64;
                    if (norm < PF_W / 2 + 8) fire_8dir(ET_BOMB2, px, py);
                } else {
                    static const int shot[8] = { ET_ENEMY1, ET_ENEMY2, ET_BOMB2, ET_RING,
                                                 ET_ENEMY1, ET_ENEMY2, ET_RING, ET_ENEMY1 };
                    e->count = 64;
                    if (norm < PF_W / 2 + 8) fire_8dir(shot[krand_get(&gamerand) & 7], px, py);
                }
            }
            if (e->health < 200) {                           /* broken up: 8 pieces fly out */
                int t = e->type;
                e->type = ET_NONE;
                fire_8dir(t == ET_M1 ? ET_ENEMY2 : (t == ET_M1 + 1 ? ET_BOMB2 : ET_ROCK), px, py);
                continue;
            }
            hit = 12;
        } else if (e->type == ET_ENEMY6) {                   /* move_enemy6 */
            int q, ms = 192, nd;
            if (e->a_dir == 0) {
                if (norm > PF_W / 2) q = 6; else { e->a_dir = 1; q = 0; }
            } else {
                if (norm < PF_W) q = 5; else { e->a_dir = 0; q = 0; }
            }
            if (q == 5) { e->h = -dy * 8; e->v = dx * 8; }   /* template_2: circle the ship */
            if (q) {
                if (dx > 0)      { if (e->h > -ms) e->h -= q; }
                else if (dx < 0) { if (e->h <  ms) e->h += q; }
                if (dy > 0)      { if (e->v > -ms) e->v -= q; }
                else if (dy < 0) { if (e->v <  ms) e->v += q; }
                nd = speed2dir16(e->h, e->v);
                if (nd > 0) e->di = nd;
            }
            if (--e->count <= 0) {                           /* a beam every 128 steps */
                e->count = 128;
                if (norm > PF_W / 2 - 32) fire_shot(ET_BEAM, CS2PX(e->x), CS2PX(e->y), 0);
            }
            hit = 6;
        } else if (e->type == ET_BOMB1 || e->type == ET_BOMB2) {   /* move_bomb1 / move_bomb2 */
            int h1 = iabs(dx), v1 = iabs(dy), w = e->type == ET_BOMB1 ? 30 : 20;
            if (e->pad[0] && norm >= PF_W / 2 + 32) { e->type = ET_NONE; continue; }  /* gun's bomb, far away */
            if ((h1 < 100 && v1 < w) || (h1 < w && v1 < 100)) { bomb_burst(e, dx, dy); continue; }
            if (e->type == ET_BOMB1) { if (++e->di > 16) e->di = 1; }
            else                     { if (--e->di < 1)  e->di = 16; }
            hit = 5;
        } else if (e->type == ET_ROCK) {                     /* move_rock: spins */
            if (e->pad[1] == 2) {                            /* rock3: 48 frames */
                e->di += e->a_dir;
                if (e->di < 1) e->di += 48; else if (e->di > 48) e->di -= 48;
            } else e->di = ((e->di + e->a_dir - 1) & 31) + 1;   /* rock1 / rock2: 32 frames */
            hit = 4;
        } else if (e->type == ET_RING) {                     /* move_ring: spins, flies straight */
            if (e->pad[0] && norm >= PF_W / 2 + 32) { e->type = ET_NONE; continue; }
            e->di += (int)krand_1(&pubrand) + 1;
            if (e->di > 16) e->di = 1;
            hit = 4;
        } else {                                             /* move_beam */
            if (norm >= PF_W / 2 + 32) { e->type = ET_NONE; continue; }
            e->di += (int)krand_1(&pubrand) + 1;
            if (e->di > 16) e->di = 1;
            hit = 2;
        }
        if (!dead_timer && !game_over && norm < hit + 5) {   /* touches the ship (HIT_MYSHIP 5) */
            /* DIFFICULTY (Kobo enemies.h): the object damages the ship, the ship damages it.
               Damage: beam 20, ring 30, bomb 70, rock by skill, everything else 50.
               Classic: health 1 -> any hit kills (as before). */
            int dmg = e->type == ET_BEAM ? 20 : e->type == ET_RING ? 30 :
                      (e->type == ET_BOMB1 || e->type == ET_BOMB2) ? 70 :
                      e->type == ET_ROCK ? sk_rockdmg[opt_skill] : 50;
            int coll = sk_coll[opt_skill] ? sk_coll[opt_skill] : 1000;
            ship_hits++;
            ship_health -= dmg;
            if (ship_health <= 0) ship_die();
            else sfx(SFX_METAL);                             /* hurt, not dead */
            if (e->type == ET_BEAM) { e->type = ET_NONE; continue; }
            e->health -= coll;
            if (e->health <= 0) { enemy_die(e); continue; }
        }
        if (e->type != ET_BEAM && norm < PF_W / 2 + 8) {     /* player bolts: hit_bolt */
            for (k = 0; k < MAX_BOLTS; k++) {
                if (!bolt_st[k]) continue;
                if (iabs(wrapd(bolt_x[k] - CS2PX(e->x), MAP_SIZEX * 16)) >= hit + HIT_BOLT) continue;
                if (iabs(wrapd(bolt_y[k] - CS2PX(e->y), MAP_SIZEY * 16)) >= hit + HIT_BOLT) continue;
                bolt_st[k] = 0;
                e->health -= BOLT_DAMAGE;
                if (e->health > 0) fx_new(FX_BOLT, bolt_x[k] * 256, bolt_y[k] * 256, 0, 0);   /* K35 */
                if (e->health <= 0) break;
            }
            if (e->health <= 0) { enemy_die(e); continue; }
        }
        n++;
    }
    enemies_alive = n;
}

static void draw_enemies(void) {
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &enemy[i];
        int sx, sy;
        if (e->type == ET_NONE) continue;
        sx = PF_W / 2 + wrapd(CS2PX(e->x) - ship_x, MAP_SIZEX * 16);
        sy = PF_H / 2 + wrapd(CS2PX(e->y) - ship_y, MAP_SIZEY * 16);
        if (sx < -20 || sx > PF_W + 20 || sy < -20 || sy > PF_H + 20) continue;
        if (e->type == ET_ENEMY4)
            draw_sprite(en4_gfx + (e->di - 1) * 400, 20, 20, sx - 10, sy - 10);
        else if (e->type == ET_ENEMY1)
            draw_sprite(en1_gfx + (e->di - 1) * 400, 20, 20, sx - 10, sy - 10);
        else if (e->type == ET_ENEMY3)
            draw_sprite(en3_gfx + (e->di - 1) * 400, 20, 20, sx - 10, sy - 10);
        else if (e->type == ET_ROCK)
            draw_sprite((e->pad[1] == 0 ? rock_gfx : (e->pad[1] == 1 ? rock2_gfx : rock3_gfx))
                        + (e->di - 1) * 256, 16, 16, sx - 8, sy - 8);
        else if (e->type == ET_ENEMY2)
            draw_sprite(en2_gfx + (e->di - 1) * 400, 20, 20, sx - 10, sy - 10);
        else if (e->type == ET_ENEMY5)
            draw_sprite(en5_gfx + (e->di - 1) * 400, 20, 20, sx - 10, sy - 10);
        else if (e->type == ET_ENEMY7)
            draw_sprite(en7_gfx + (e->di - 1) * 400, 20, 20, sx - 10, sy - 10);
        else if (e->type >= ET_M1 && e->type <= ET_M4)
            draw_sprite(big_gfx + (e->di - 1) * 1296, 36, 36, sx - 18, sy - 18);
        else if (e->type == ET_RING)
            draw_sprite(ring_gfx + (e->di - 1) * 256, 16, 16, sx - 8, sy - 8);
        else if (e->type == ET_ENEMY6)
            draw_sprite(en6_gfx + (e->di - 1) * 400, 20, 20, sx - 10, sy - 10);
        else if (e->type == ET_BOMB1 || e->type == ET_BOMB2)
            draw_sprite(bomb_gfx + (e->di - 1) * 144, 12, 12, sx - 6, sy - 6);
        else if (e->type == ET_DETO)
            draw_sprite(deto_gfx + e->count * 400, 20, 20, sx - 10, sy - 10);
        else if (e->type == ET_FX) {                                   /* K35 */
            int id = e->pad[2];
            draw_rle(id, e->count, sx - fx_w[id] / 2, sy - fx_h[id] / 2);
        }
        else if (e->type == ET_BEAM)
            draw_sprite(beam_gfx + (e->di - 1) * 64, 8, 8, sx - 4, sy - 4);
        else
            draw_sprite(expl_gfx + e->count * 576, 24, 24, sx - 12, sy - 12);
    }
}

/* draw a w x h sprite at playfield position (sx, sy); colour 0 = see-through; clipped to
   the 224 x 224 playfield so it never touches the diagnostic strip */
static void draw_sprite(const uint8_t *src, int w, int h, int sx, int sy) {
    volatile uint8_t *fb = SC_FB8;
    int x, y;
    for (y = 0; y < h; y++) {
        int py = sy + y;
        if (py < 0 || py >= PF_H) continue;
        for (x = 0; x < w; x++) {
            int px = sx + x;
            uint8_t c = src[y * w + x];
            if (c && px >= 0 && px < PF_W) fb[py * SCR_W + px] = c;
        }
    }
}

static uint16_t get_pad(int *timed_out) {
    uint32_t n;
    {   /* K33: one queued sound effect rides along in the upper byte */
        int id = 0, i;
        if (sfx_n) { id = sfx_q[sfx_head]; sfx_head = (sfx_head + 1) & 7; sfx_n--; }
        (void)i;
        MARS_SYS_COMM0 = (int16_t)(CMD_GET_PAD | (id << 8));
    }
    for (n = 0; n < 40000UL && MARS_SYS_COMM0 != 0; n++) comm_pause();   /* HW6: slow polling */
    *timed_out = (MARS_SYS_COMM0 != 0);
    if (*timed_out) { pad_timeouts++; return 0; }
    md_ticks = SC_COMM10;                        /* K32: 68K vblank counter (low 16 bits) */
    return SC_COMM8;
}

/* ====================================================================================
 * K15 SPLASH SCREEN - IMAGE.RAW (320 x 200, 15-bit colour) shown while the game loads.
 *   1. CMD 38 opens file 0 (IMAGE.RAW); the Sub-CPU keeps its start sector and length.
 *   2. The picture area is cleared first: zero words written by the 68K into the frame
 *      buffer do not land (K12-K14 finding), so black pixels must already be zero.
 *   3. CMD 51 per 32 KB chunk: CD -> Word RAM -> frame buffer at 0x200 + chunk * 32 KB.
 *   4. Line table: 200 picture lines centred in the 224-line screen, the 12 lines above and
 *      below point at an all-black line (line 202, cleared in step 2).
 *   5. 15-bit colour mode, 32X layer in front (text hidden), flip = the splash is on screen.
 *   After the flip the OTHER frame buffer is the one we write, so loading the game (fetch)
 *   does not disturb the splash.  Returns 0, or -1 if IMAGE.RAW is missing (no splash).
 * ==================================================================================== */
static int show_splash(void) {
    volatile uint16_t *fb = SC_FRAMEBUFFER;
    uint32_t len, c, n, i;
#if KOBO_ROMCART
    {   /* CARTRIDGE: the splash is in the ROM - copy it straight into the frame buffer */
        const volatile uint16_t *src = (const volatile uint16_t *)(CART_RAM_SH2 + SPLASH_ROM_OFF);
        for (i = 0; i < SPLASH_W * (SPLASH_H + 3); i++) fb[0x100 + i] = 0;
        for (i = 0; i < SPLASH_W * SPLASH_H; i++) fb[0x100 + i] = src[i];
        (void)len; (void)c; (void)n;
    }
#else
    SC_COMM2 = FILE_SPLASH;
    wait_cmd(CMD_OPEN_FILE);
    len = SC_COMM8_32;
    if ((int32_t)len <= 0) return -1;
    if (len > SPLASH_W * SPLASH_H * 2) len = SPLASH_W * SPLASH_H * 2;
    /* picture + 3 spare lines.  The last chunk is copied in whole 2 KB sectors, so ~1 KB past
       the picture (lines 200-201) can hold leftover sector bytes: the black line for the
       bars is line 202, which nothing writes.  (0x200 + 203 * 640 bytes < 128 KB frame buffer) */
    for (i = 0; i < SPLASH_W * (SPLASH_H + 3); i++) fb[0x100 + i] = 0;
    n = (len + CHUNK_BYTES - 1) / CHUNK_BYTES;
    for (c = 0; c < n; c++) {
        SC_COMM2 = (uint16_t)c;
        give_fb(); wait_cmd_no_vdp(CMD_FILE_CHUNK_AT); take_fb();
    }
#endif
    /* K19: pure black 0x0000 is SEE-THROUGH on the 32X (the Genesis layer shows there - same
       finding as K8 for the palette).  Make every 0x0000 pixel of the picture and the bar line
       0x0400 (blue 1/31: looks black, is a real colour). */
    for (i = 0; i < SPLASH_W * SPLASH_H; i++)
        if (fb[0x100 + i] == 0) fb[0x100 + i] = 0x0400;
    for (i = 0; i < SPLASH_W; i++) fb[0x100 + (SPLASH_H + 2) * SPLASH_W + i] = 0x0400;
    for (i = 0; i < SCR_H; i++) {
        int src = (int)i - SPLASH_TOP;
        fb[i] = (uint16_t)((src >= 0 && src < SPLASH_H) ? (0x100 + src * SPLASH_W)
                                                         : (0x100 + (SPLASH_H + 2) * SPLASH_W));
    }
    SC_VDP_DISPMODE = (SC_VDP_DISPMODE & 0x8000) | MARS_VDP_MODE_32K;  /* K18: bit 7 clear = splash in front, text hidden */
    flip_wait();
    frames = 0;                       /* K16: start of the SPLASH_HOLD count */
    return 0;
}

int main(void) {
    int r, i, vx, vy;
    uint32_t frames_drawn = 0;
    uint16_t pad, prev = 0;

    /* K1b DIAGNOSTIC: crt0.s leaves checkpoint 3 ("about to jump into main") in COMM10.
       Each step below writes the next number, so the value the Sub-CPU shows after
       "SH2 READY FOR ISOLATED TEST:" is the last step reached:
         0003 main() never started     0004 marker check     0005 waiting for COMM4 = 0x1234
         0006 interrupt mask           0007 interrupts on    0AAA ready (normal)
       Only the SH2 writes COMM10 during this phase; the Sub-CPU only reads it. */
    SC_COMM10 = 0x0004;
    if (sh2_build_marker[0] != 'S') while (1) {}

    /* ---- start-up: identical to the working V80 engine ---- */
    MARS_SYS_COMM0 = 0;
    SC_COMM10 = 0x0005;
    /* K28 (carried into K30): NO wait for COMM4 = 0x1234.  On hardware the boot ROM's copy
       of the (growing) program made crt0.s clear COMM4 AFTER the Sub-CPU wrote 0x1234 ->
       main() waited forever (strip "0005").  The COMM10 0x0AAA / 0x0BBB handshake below
       works in either order, so nothing is lost. */
    SC_COMM10 = 0x0006;
    MARS_SYS_INTMSK |= 0x0002;
    SC_COMM12_32 = 0x0EEE;
    SC_COMM10 = 0x0007;
    __asm__ __volatile__("ldc %0,sr" : : "r"(0) : "memory");
    SC_COMM10 = 0x0AAA;
    while (SC_COMM10 != 0x0BBB) { }

    take_fb();

    /* ---- K27: the program must end below the second work area (see WORK2_BASE) ---- */
    if ((uint32_t)&_bss_end >= WORK2_BASE) { show_status(0x4BE6); while (1) count_vblank(); }

    /* ---- 0. K15 splash screen (IMAGE.RAW) - stays on screen while everything below loads.
       K19: Genesis text off first, so nothing shows over the splash. (If anything fails
       BEFORE this point the boot text is still on screen - it only goes off here.) ---- */
    set_text(text_shown);
#if KOBO_SPLASH
    show_splash();
#endif

    /* ---- 1. graphics pack: cart mode = CD -> cart (verified); no-cart mode = open the file ---- */
    show_status(ST_LOADING);
    r = load_pack();
    if (r < 0) { show_status(ST_ERR_OPEN); while (1) count_vblank(); }
    if (r > 0) { show_status((uint16_t)(ST_ERR_VERIFY)); while (1) count_vblank(); }
    fetch(0, pack_hdr, PACK_HDR_SIZE);
    if (hdr16(6) > PACK_MAX_BANKS) { show_status(0x4BE5); while (1) count_vblank(); }   /* K30b */
    snap_t_fb  = fb32(0x208 + BANK_TILES1 * 16 + 8);   /* K12: frame buffer (chunk 0 still there) */
    snap_s_fb  = fb32(0x208 + BANK_STARS * 16 + 8);
    snap_t_hdr = hdr32(0x208 + BANK_TILES1 * 16 + 8);  /* K12: SH2's copy                       */
    snap_s_hdr = hdr32(0x208 + BANK_STARS * 16 + 8);
    if (hdr16(0) != 0x4B47 || hdr16(2) != 0x4658) { show_status(ST_ERR_MAGIC); while (1) count_vblank(); }
    show_status(ST_PACK_OK);

    /* ---- 2. pack -> SDRAM (via fetch), palette, 256-colour mode ----
       NOTE: in no-cart mode fetch() uses the frame buffer as its transfer area, so all
       fetching must be finished before init_buffer() below sets up the screen. */
    tiles_ok = copy_frames(BANK_TILES1 + 0, tileset, TILE_FRAMES);   /* level 1-10 = tile set 1 */
    stars_ok = copy_frames(BANK_STARS, stars, STAR_FRAMES);
    ship_ok  = copy_bank(BANK_PLAYER, player_gfx, SHIP_FRAMES * SHIP_W * SHIP_H);   /* K17 */
    ship_ok &= copy_bank(BANK_BOLT, bolt_gfx, 16 * BOLT_W * BOLT_W);                /* K20 */
    gfx2_ok  = copy_bank(BANK_EN4,  en4_gfx,  16 * 20 * 20);                        /* K22 */
    gfx2_ok &= copy_bank(BANK_BEAM, beam_gfx, 16 * 8 * 8);
    gfx2_ok &= copy_bank(BANK_EXPL, expl_gfx, 8 * 24 * 24);
    gfx2_ok &= copy_bank(10, en1_gfx, 16 * 20 * 20);      /* K25: missile1 = enemy1 */
    gfx2_ok &= copy_bank(11, en3_gfx, 16 * 20 * 20);      /*      missile2 = enemy3 */
    gfx2_ok &= copy_bank(19, rock_gfx, 32 * 16 * 16);     /*      rock1              */
    gfx2_ok &= copy_bank(25, ring_gfx, 16 * 16 * 16);     /*      ring               */
    gfx2_ok &= copy_bank(7,  en6_gfx,  16 * 20 * 20);     /* K26: bmr-purple = enemy6 */
    gfx2_ok &= copy_bank(27, bomb_gfx, 16 * 12 * 12);     /*      bomb               */
    gfx2_ok &= copy_bank(28, deto_gfx,  8 * 20 * 20);     /*      bomb detonation    */
    gfx2_ok &= copy_bank(9,  en2_gfx,  16 * 20 * 20);     /* K27: fighter = enemy2   */
    gfx2_ok &= copy_bank(6,  en5_gfx,  16 * 20 * 20);     /*      bmr-green = enemy5 */
    gfx2_ok &= copy_bank(8,  en7_gfx,  16 * 20 * 20);     /*      bmr-pink = enemy7  */
    gfx2_ok &= copy_bank(29, big_gfx,  16 * 36 * 36);     /*      bigship = m1..m4   */
    gfx2_ok &= copy_bank(20, rock2_gfx, 32 * 16 * 16);    /*      rock2              */
    gfx2_ok &= copy_bank(21, rock3_gfx, 48 * 16 * 16);    /*      rock3 (shiny)      */
    gfx2_ok &= copy_bank(BANK_FONT,  font_gfx, FONT_N * 63);  /* K30: panel / menu font */
    gfx2_ok &= copy_bank(BANK_FONTW, font_w,   FONT_N);
    /* K35: RLE explosion sets that live in SDRAM (the frame-buffer ones load with the tiles) */
    fx_info(FX_EXPLO3, (void *)RLE_A);
    fx_info(FX_EXPLO4, (void *)(RLE_A + ((fx_len(FX_EXPLO3) + 3) & ~3u)));
    fx_info(FX_EXPLO5, (void *)RLE_B);
    fx_info(FX_BOLT,   (void *)(RLE_B + ((fx_len(FX_EXPLO5) + 3) & ~3u)));          /* K35c */
    fx_info(FX_BULLET, (void *)((uint32_t)fx_src[FX_BOLT] + ((fx_len(FX_BOLT) + 3) & ~3u)));
    fx_info(FX_ROCK,   (void *)RLE_C);
    fx_info(FX_RING,   (void *)(RLE_C + ((fx_len(FX_ROCK) + 3) & ~3u)));            /* K35c */
    rle_ok  = copy_bank(38, (uint8_t *)fx_src[FX_EXPLO3], fx_len(FX_EXPLO3));
    rle_ok &= copy_bank(39, (uint8_t *)fx_src[FX_EXPLO4], fx_len(FX_EXPLO4));
    rle_ok &= copy_bank(40, (uint8_t *)fx_src[FX_EXPLO5], fx_len(FX_EXPLO5));
    rle_ok &= copy_bank(41, (uint8_t *)fx_src[FX_ROCK],   fx_len(FX_ROCK));
    rle_ok &= copy_bank(37, (uint8_t *)fx_src[FX_BOLT],   fx_len(FX_BOLT));
    rle_ok &= copy_bank(42, (uint8_t *)fx_src[FX_BULLET], fx_len(FX_BULLET));
    rle_ok &= copy_bank(43, (uint8_t *)fx_src[FX_RING],   fx_len(FX_RING));
    {   /* K35d: the menu logo in SDRAM (LOGO_RAM).  WHY: kept in spare frame-buffer memory
           (K35/K35c) it read back wrong - no outline, logo rows where DELUXE belongs, and
           1-2 fps while drawing the damaged data. */
        uint32_t at = LOGO_RAM;
        static const uint8_t ids[3] = { FX_LOGOF, FX_LOGOO, FX_DELUXE };
        int j;
        for (j = 0; j < 3; j++) {
            fx_info(ids[j], (void *)at);
            rle_ok &= copy_bank(fx_bank[ids[j]], (uint8_t *)at, fx_len(ids[j]));
            at += (fx_len(ids[j]) + 3) & ~3u;
        }
    }
    show_status(ST_GFX_OK);                        /* HW2 */
#if KOBO_HW_PCM
    /* K33: sound effects into the Sega CD PCM chip - now, before the music (a CD read) */
    *(volatile uint16_t *)0x2000402C = 0;
    wait_cmd(CMD_LOAD_SFX);
    sfx_ok = *(volatile uint16_t *)0x2000402C;
#else
    sfx_ok = 0; opt_sfx = 0;                       /* HW2: no sound pack, no effect requests */
#endif
    show_status(ST_SFX_OK);
#if KOBO_HW_BRAM
    load_game();                                   /* K36: high scores + options from backup RAM */
#else
    hs_defaults(); bram_ok = 0;                    /* HW2: session-only high scores */
#endif
    show_status(ST_BRAM_OK);
    load_palette();                   /* CRAM is only used in 256-colour mode: the splash is safe */
    pick_colours();
    show_status(0x4B51);              /* HW4: palette loaded */

    /* ---- 3. level 1 map, Kobo's generator ---- */
    gamerand.seed = 0x4B4F424F;       /* fixed seed for M1 ("KOBO"); Kobo seeds from the clock */
    pubrand.seed = 0x12345678;
    build_level(0);                   /* K23: level 1 - maze, guns, ship, first enemy set */
    show_status(0x4B52);              /* HW4: level built */
    (void)i; (void)scene1_ratio; (void)scene1_base;

    /* ---- 4. (K31: the music now starts AFTER all loading - see step 4c) ---- */

    /* ---- 4a. K16: keep the splash up until SPLASH_HOLD frames have passed since it appeared.
       count_vblank() counts vertical blanks; the SH2 owns the frame buffer here (FM=1), so it
       may read the VDP register it uses. */
#if KOBO_SPLASH
    while (frames < SPLASH_HOLD) count_vblank();
#endif

    /* ---- 4b. K15: leave the splash - 256-colour game screen in both frame buffers ---- */
    init_buffer();
    SC_VDP_DISPMODE = (SC_VDP_DISPMODE & 0x8000) | MARS_VDP_MODE_256;   /* 224 lines, PRI 0 */
    set_text(text_shown);                                                /* K11: text on/off */
    show_status(0x4B53);              /* HW4: 256-colour mode + text set (CMD 52 answered) */
    flip_wait();
    init_buffer(); flip_wait();
    show_status(0x4B54);              /* HW4: two frame-buffer swaps done */
    cache_tilesets();                 /* K31: all tile sets into spare frame-buffer memory */
    show_status(ST_TILES_OK);         /* HW2 */
#if !KOBO_DIAG_STRIP
    draw_panel();                     /* K30: Kobo's dashboard, once, into both frame buffers */
#endif
    show_status(ST_PANEL_OK);         /* HW2 */

    /* ---- 4c. music - K31: LAST, after every CD data read.  WHY: the Sega CD drive cannot read
       data and play CD audio at once; in K30 the panel was read from CD just after the music
       started -> the track kept "playing" but was silent (the old inaudible-audio bug).
       From here on nothing reads the CD (tile sets come from the frame-buffer cache). ---- */
#if CDDA_ENABLE
    if (opt_music) { SC_COMM2 = CDDA_TRACK; wait_cmd(CMD_PLAY_CDDA); }   /* AUDIO - call unchanged; K36: option */
    else wait_cmd(54);                /* HW7: music off -> stop the drive (BIOS MSCSTOP) after loading */
    show_status(ST_MUSIC_OK);         /* HW2 */
#endif

    /* ---- 5. main loop: K30 MENU (game_state 0) and GAME (game_state 1) ---- */
    (void)scene1_startx; (void)scene1_starty;   /* K23: build_level() placed ship + enemies */
    show_status(ST_RUNNING);
    {
    int game_state = 0, menu_sel = 0, menu_level = 1, over_timer = 0, banner = 0, menu_x = 0;
    int nsteps = 1, st;                       /* K32 */
    int credits_y = PF_H, credits_t = 0, trans_t = 0;   /* K34 */
    int opt_sel = 0, hs_t0 = 0, hs_hi = -1, paused = 0;  /* K36 */
    int pause_sel = 0;                                   /* PAUSE MENU: 0 continue, 1 restart, 2 menu */
    int idle_ticks = 0, demo_ticks = 0, demo_page = 0, demo_t = 0, menu_y = 0;   /* K37 */
    int ne_rank = 10, ne_pos = 0;
    char ne_name[3] = { 'A', 'A', 'A' };
    uint32_t pframe = 0;
    while (1) {
        int tmo;
        uint16_t press;
#if KOBO_HW_QUIET
        pad = 0; tmo = 1;                             /* HW9 quiet test: no requests at all */
#else
        pad = get_pad(&tmo);
#endif
        press = pad & (uint16_t)~prev;
#if KOBO_FIXED_STEP
        {   /* K32: frames since last time (68K vblank counter) -> 30 ms logic steps.
               Units of 1/600 s: a 60 Hz frame = 10, a 50 Hz (PAL) frame = 12, a step =
               KOBO_STEP_MS * 0.6 (30 ms = 18). */
            static uint16_t last_tick = 0;
            static int have_tick = 0, acc = 0;
            int d = 1;
            if (!tmo) {
                d = (int)(uint16_t)(md_ticks - last_tick);
                last_tick = md_ticks;
                if (!have_tick) { have_tick = 1; d = 1; }
                if (d > 8) d = 8;
            }
            acc += d * (((SC_VDP_DISPMODE & 0x8000) != 0) ? 10 : 12);   /* bit 15: NTSC */
            nsteps = 0;
            while (acc >= KOBO_STEP_MS * 6 / 10 && nsteps < 4) { acc -= KOBO_STEP_MS * 6 / 10; nsteps++; }
            if (nsteps == 4) acc = 0;                  /* far behind: do not try to catch up */
        }
#else
        nsteps = 1;
#endif
        if (game_state == 1) {   /* DIFFICULTY speed: Classic 256 = exactly one step per frame */
            static int sk_acc = 0;
            sk_acc += sk_speed[opt_skill];
            nsteps = (int)((uint32_t)sk_acc >> 8);   /* unsigned shift (no libgcc) */
            sk_acc &= 255;
        }
        if (score > hiscore) hiscore = score;
        if (hs[0].score > hiscore) hiscore = hs[0].score;   /* K36: best of the saved table */

        {   /* K37: idle time on the menu, in 68K frames (60 Hz NTSC / 50 Hz PAL) */
            static uint16_t lt = 0; static int have = 0;
            int d = 0;
            if (!tmo) { d = have ? (int)(uint16_t)(md_ticks - lt) : 0; lt = md_ticks; have = 1; }
            if (d > 60) d = 60;
            if (game_state == 0 && !(pad & 0x0FFF)) idle_ticks += d; else idle_ticks = 0;
            if (game_state == 7) demo_ticks += d;
            if (game_state == 0 && idle_ticks >= TRACK02_SECONDS * ((SC_VDP_DISPMODE & 0x8000) ? 60 : 50)) {
                game_state = 7; demo_page = 0; demo_ticks = 0; demo_t = 0; credits_y = PF_H;
                idle_ticks = 0;
                score = 0; lives = 5; dead_timer = 0; clear_timer = 0; game_over = 0; loop_no = 0;
                demo_mute = 1;
                build_level(0);                                   /* K37c: the demo plays level 1.. */
            }
        }
        if (game_state == 7) {
            /* ---------------- K37 DEMO (attract) - any button -> menu -------------------- */
            int hz = (SC_VDP_DISPMODE & 0x8000) ? 60 : 50, sx, sy;
            uint32_t page_ticks = hz == 60 ? demo_t60[demo_page] : demo_t50[demo_page];   /* no division */
            (void)demo_ms;
            if (press & 0x0FFF & ~PAD_C) {                         /* back to the menu (K37g: not C) */
                game_state = 0; prev = pad; demo_mute = 0;
                score = 0; lives = 5; dead_timer = 0; clear_timer = 0;
            }
            else {
                demo_t += nsteps;
                if (demo_ticks >= (int)page_ticks) {
                    demo_ticks = 0; demo_page = demo_page < 5 ? demo_page + 1 : 0; credits_y = PF_H;
                }
                {   /* K37c: a real level on autopilot (instead of Kobo's fixed weave) */
                    uint16_t ai = demo_ai();
                    for (st = 0; st < nsteps; st++) {
                        if (dead_timer) { if (--dead_timer == 0) respawn(); }
                        else {
                            int cp;
                            ship_step(ai);
                            bolts_step(ai);
                            cp = *mpos((int)((uint32_t)ship_x >> 4), (int)((uint32_t)ship_y >> 4));
                            if (!IS_SPACE(cp) && (cp & HIT_MASK)) ship_die();
                        }
                        demos_step();
                        enemies_step();
                        if (!clear_timer && !dead_timer && cores_left() == 0) clear_timer = 50;
                        if (clear_timer && --clear_timer == 0) build_level(scene_no < 9 ? scene_no + 1 : 0);
                    }
                    draw_playfield((ship_x - PF_W / 2) & (MAP_SIZEX * 16 - 1), (ship_y - PF_H / 2) & (MAP_SIZEY * 16 - 1));
                    draw_enemies();
                    if (!dead_timer)
                        draw_sprite(player_gfx + (ship_di - 1) * 2 * SHIP_W * SHIP_H, SHIP_W, SHIP_H,
                                    PF_W / 2 - SHIP_W / 2, PF_H / 2 - SHIP_H / 2);
                    draw_bolts();
                    (void)sx; (void)sy; (void)menu_y;
                }
                if (demo_page == 0 || demo_page == 2 || demo_page == 4) {
                    draw_logo();
                    if (demo_t & 32) font_center(150, "PRESS A OR B");
                } else if (demo_page == 1) {
                    int i;
                    for (i = 0; instructions[i]; i++) font_center(20 + i * 13, instructions[i]);
                } else if (demo_page == 3) {
                    draw_hs_table(-1, demo_t);
                } else {                                       /* K37b: still page, all credits */
                    static const char *const dc[] = {
                        "CREDITS", "", "XKOBO: AKIRA HIGUCHI", "KOBO DELUXE: DAVID OLOFSON", "",
                        "D32XR 32X CODE", "VICTOR LUCHITS", "", "SEGA CD AND 32X FRAMEWORK",
                        "CHILLY WILLY", "", "SEGA CD32X PORT", "MICRONUT99", 0 };
                    int i;
                    for (i = 0; dc[i]; i++) font_center(24 + i * 14, dc[i]);
                }
                prev = pad;
            }
        } else if (game_state == 2) {
            /* ---------------- K34 CREDITS: Track03, text scrolls up; A/B/C (after 1 s) or the
               end of the text -> back to the menu with Track02 ---------------------------- */
            int i, y;
            menu_x += nsteps;
            credits_t += nsteps;
            if (credits_t & 1) credits_y--;                  /* K35d: back to K34's speed (dev) */
            vx = menu_x & (MAP_SIZEX * 16 - 1);
            vy = (kscene[scene_no].starty * 16 - PF_H / 2) & (MAP_SIZEY * 16 - 1);
            draw_playfield(vx, vy);
            for (i = 0, y = credits_y; credits[i]; i++, y += 14)
                if (y > -10 && y < PF_H) font_center(y, credits[i]);
            if (((press & (PAD_A | PAD_B)) && credits_t > 60) || y < 0) {
                game_state = 0;
                music(CDDA_TRACK);
            }
            prev = pad;
        } else if (game_state == 4) {
            /* ---------------- K36 OPTIONS: UP/DOWN choose, LEFT/RIGHT/A/B/C change, BACK saves */
            static const char *const onoff[2] = { "OFF", "ON" };
            char l[24];
            int i;
            if (press & PAD_UP)   { opt_sel = opt_sel > 0 ? opt_sel - 1 : 3; sfx(SFX_TICK); }   /* 4 items */
            if (press & PAD_DOWN) { opt_sel = opt_sel < 3 ? opt_sel + 1 : 0; sfx(SFX_TICK); }
            if (press & (PAD_LEFT | PAD_RIGHT | PAD_A | PAD_B)) {   /* K37g: no C */
                if (opt_sel == 0) { opt_music ^= 1; music(CDDA_TRACK); }
                if (opt_sel == 1) { opt_sfx ^= 1; sfx(SFX_TICK); }
                if (opt_sel == 2) {                           /* DIFFICULTY: LEFT back, else forward */
                    if (press & PAD_LEFT) opt_skill = opt_skill > 0 ? opt_skill - 1 : NUM_SKILLS - 1;
                    else                  opt_skill = opt_skill < NUM_SKILLS - 1 ? opt_skill + 1 : 0;
                    sfx(SFX_TICK);
                }
                if (opt_sel == 3 && (press & (PAD_A | PAD_B))) { save_game(); game_state = 0; }
            }
            prev = pad;
            menu_x += nsteps;
            vx = menu_x & (MAP_SIZEX * 16 - 1);
            vy = (kscene[scene_no].starty * 16 - PF_H / 2) & (MAP_SIZEY * 16 - 1);
            draw_playfield(vx, vy);
            font_center(40, "OPTIONS");
            for (i = 0; i < 4; i++) {
                const char *a = i == 0 ? "MUSIC: " : i == 1 ? "SOUND FX: " : i == 2 ? "SKILL: " : "BACK";
                const char *b = i == 0 ? onoff[opt_music] : i == 1 ? onoff[opt_sfx] : i == 2 ? skill_name[opt_skill] : "";
                char *q = l;
                while (*a) *q++ = *a++;
                while (*b) *q++ = *b++;
                *q = 0;
                font_text(80, 90 + i * 18, l);
            }
            draw_sprite(player_gfx + 4 * SHIP_W * SHIP_H, SHIP_W, SHIP_H, 56, 90 + opt_sel * 18 - 6);
            font_center(180, bram_ok ? "SAVED IN BACKUP RAM" : "NO BACKUP RAM");
        } else if (game_state == 5) {
            /* ---------------- K36 HIGH SCORES: A/B/C (after 1 s) -> menu ------------------- */
            int i;
            hs_t0 += nsteps;
            if ((press & (PAD_A | PAD_B)) && hs_t0 > 60) game_state = 0;
            prev = pad;
            menu_x += nsteps;
            vx = menu_x & (MAP_SIZEX * 16 - 1);
            vy = (kscene[scene_no].starty * 16 - PF_H / 2) & (MAP_SIZEY * 16 - 1);
            draw_playfield(vx, vy);
            draw_hs_table(hs_hi, hs_t0);                               /* K37: shared */
            (void)i;
        } else if (game_state == 6) {
            /* ---------------- K36 NAME ENTRY: UP/DOWN letter, LEFT/RIGHT move, A/B/C next --- */
            int i;
            if (press & PAD_UP)    ne_name[ne_pos] = ne_name[ne_pos] < 'Z' ? ne_name[ne_pos] + 1 : 'A';
            if (press & PAD_DOWN)  ne_name[ne_pos] = ne_name[ne_pos] > 'A' ? ne_name[ne_pos] - 1 : 'Z';
            if (press & PAD_LEFT)  ne_pos = ne_pos > 0 ? ne_pos - 1 : 0;
            if (press & PAD_RIGHT) ne_pos = ne_pos < 2 ? ne_pos + 1 : 2;
            if (press & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) sfx(SFX_TICK);
            if (press & (PAD_A | PAD_B)) {
                if (ne_pos < 2) ne_pos++;
                else {
                    ne_name[0] = font_ok_char(ne_name[0]); ne_name[1] = font_ok_char(ne_name[1]); ne_name[2] = font_ok_char(ne_name[2]);
                    hs_insert(ne_rank, ne_name, score, scene_no + 1);
                    save_game();
                    game_state = 5; hs_t0 = 0; hs_hi = ne_rank;
                }
            }
            prev = pad;
            draw_playfield((ship_x - PF_W / 2) & (MAP_SIZEX * 16 - 1), (ship_y - PF_H / 2) & (MAP_SIZEY * 16 - 1));
            font_center(60, "NEW HIGH SCORE!");
            font_center(80, "ENTER YOUR NAME");
            for (i = 0; i < 3; i++) {
                char c[2]; c[0] = ne_name[i]; c[1] = 0;
                font_text(96 + i * 12, 110, c);
                if (i == ne_pos) font_text(96 + i * 12, 122, "-");
            }
        } else if (game_state == 3) {
            /* ---------------- K34 TRANSITION: Track04 between levels; ~4 s or A/B/C (after
               1 s) -> next level with Track02 (its level-start jingle) ------------------ */
            char t1[16] = "STAGE ", t2[20] = "NEXT: STAGE ";
            trans_t += nsteps;
            dec(t1 + 6, (uint32_t)(scene_no + 1), 2);
            dec(t2 + 12, (uint32_t)(scene_no + 1 < NUM_SCENES ? scene_no + 2 : 1), 2);
            draw_playfield((ship_x - PF_W / 2) & (MAP_SIZEX * 16 - 1), (ship_y - PF_H / 2) & (MAP_SIZEY * 16 - 1));
            font_center(80, t1);
            font_center(94, "CLEAR!");
            font_center(130, t2);
            if (trans_t > 240 || ((press & (PAD_A | PAD_B)) && trans_t > 60)) {
                if (scene_no + 1 < NUM_SCENES) build_level(scene_no + 1);   /* next level */
                else { loop_no++; build_level(0); }                        /* K27: next round */
                banner = 90;                                               /* K30: "STAGE nn" */
                music(CDDA_TRACK);
                game_state = 1;
            }
            prev = pad;
        } else if (game_state == 0) {
            /* ---------------- K30 MENU: UP/DOWN choose, LEFT/RIGHT level, A or B starts.
               (START is not used: the dev saw START handling cut the audio.) ---------- */
            if (press & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) sfx(SFX_TICK);   /* K33 */
            if (press & PAD_UP)   menu_sel = menu_sel > 0 ? menu_sel - 1 : 4;   /* K36: 5 items */
            if (press & PAD_DOWN) menu_sel = menu_sel < 4 ? menu_sel + 1 : 0;
            if (menu_sel == 1) {
                if (press & PAD_LEFT)  menu_level = menu_level > 1 ? menu_level - 1 : NUM_SCENES;
                if (press & PAD_RIGHT) menu_level = menu_level < NUM_SCENES ? menu_level + 1 : 1;
            }
            if ((press & (PAD_A | PAD_B)) && menu_sel == 2) {   /* K36: OPTIONS */
                prev = pad; game_state = 4; opt_sel = 0; sfx(SFX_PLAY);
                continue;
            }
            if ((press & (PAD_A | PAD_B)) && menu_sel == 3) {   /* K36: HIGH SCORES */
                prev = pad; game_state = 5; hs_t0 = 0; hs_hi = -1; sfx(SFX_PLAY);
                continue;
            }
            if ((press & (PAD_A | PAD_B)) && menu_sel == 4) {   /* K34: CREDITS */
                prev = pad;
                game_state = 2; credits_y = PF_H; credits_t = 0;
                music(CDDA_CREDITS);
                continue;
            }
            if (press & (PAD_A | PAD_B)) {            /* K30b: C starts too */
                score = 0; lives = sk_lives[opt_skill]; game_over = 0; clear_timer = 0; ship_hits = 0;
                bonus_next = sk_bonus1[opt_skill];             /* DIFFICULTY */
                loop_no = 0; dead_timer = 0;
                build_level(menu_level - 1);
                game_state = 1; banner = 90; paused = 0;
                sfx(SFX_PLAY);                                /* K33 */
            }
            prev = pad;
            menu_x += nsteps;                                  /* the level drifts by (K32: per step) */
            vx = menu_x & (MAP_SIZEX * 16 - 1);
            vy = (kscene[scene_no].starty * 16 - PF_H / 2) & (MAP_SIZEY * 16 - 1);
            draw_playfield(vx, vy);
            draw_logo();                                       /* K35 logo, K37: shared */
            {
                char lv[12] = "LEVEL ";
                dec(lv + 6, (uint32_t)menu_level, 2);
                font_text(80, 110, "START GAME");                         /* K35: moved down */
                font_text(80, 126, lv);
                font_text(80, 142, "OPTIONS");                            /* K36 */
                font_text(80, 158, "HIGH SCORES");                        /* K36 */
                font_text(80, 174, "CREDITS");                            /* K34 */
                draw_sprite(player_gfx + 4 * SHIP_W * SHIP_H, SHIP_W, SHIP_H,   /* cursor ship */
                            56, 110 + menu_sel * 16 - 6);
            }
            font_center(196, "A OR B: SELECT");                  /* K37g: C disabled */
            /* K37e: the "DEMO IN" debug counter was removed (dev request) */
            font_center(208, "LEFT/RIGHT: LEVEL");
        } else {
            /* ---------------- GAME (K23 flow; K30: game over -> "GAME OVER" -> menu) ----
               K32: the logic runs 'nsteps' times this frame (Kobo's fixed 30 ms step); edge-
               triggered keys (C, debug X) are handled once per frame. */
            /* K37g: X no longer skips levels - X is PAUSE now */
            /* PAUSE MENU: X (or MODE) pauses and opens CONTINUE / RESTART LEVEL / MAIN MENU.
               UP/DOWN choose, A or B confirm, X again = continue. */
            if (!game_over && ((press & (PAD_X | PAD_MODE)) || (KOBO_START_PAUSE && (press & PAD_START)))) {
                paused ^= 1;
                pause_sel = 0;
                sfx(SFX_TICK);
            } else if (paused) {
                if (press & PAD_UP)   { pause_sel = pause_sel > 0 ? pause_sel - 1 : 2; sfx(SFX_TICK); }
                if (press & PAD_DOWN) { pause_sel = pause_sel < 2 ? pause_sel + 1 : 0; sfx(SFX_TICK); }
                if (press & (PAD_A | PAD_B)) {
                    paused = 0;
                    if (pause_sel == 1) {                      /* RESTART LEVEL: same level, fresh */
                        dead_timer = 0; clear_timer = 0;
                        build_level(scene_no);
                        banner = 90;
                        sfx(SFX_PLAY);
                        music(CDDA_TRACK);                     /* level-start jingle again */
                    } else if (pause_sel == 2) {               /* MAIN MENU: leave the game */
                        dead_timer = 0; clear_timer = 0;
                        score = 0; lives = 5;
                        game_state = 0;
                        sfx(SFX_PLAY);
                    }
                    prev = pad;
                    press = 0;                                 /* the confirm press is used up */
                }
            }
            if (paused) nsteps = 0;                            /* K36: nothing moves */
            for (st = 0; st < nsteps; st++) {
                if (game_over) {
                    if (--over_timer <= 0) {
                        game_over = 0;
                        ne_rank = hs_rank(score);                  /* K36: a high score? */
                        if (ne_rank < 10) { game_state = 6; ne_pos = 0; ne_name[0] = 'A'; ne_name[1] = 'A'; ne_name[2] = 'A'; }
                        else game_state = 0;
                        break;
                    }
                } else if (dead_timer) {
                    if (--dead_timer == 0) {
                        if (--lives <= 0) { game_over = 1; over_timer = 180; sfx(SFX_GAMEOVER); }
                        else respawn();
                    }
                } else {
                    int cp;
                    ship_step(pad);                               /* K17 */
                    bolts_step(pad);                              /* K20 */
                    cp = *mpos((int)((uint32_t)ship_x >> 4), (int)((uint32_t)ship_y >> 4));
                    if (!IS_SPACE(cp) && (cp & HIT_MASK)) ship_die(); /* K23: crashed into a pipe */
                }
                demos_step();                                     /* K21 */
                enemies_step();                                   /* K22 */
                if (ship_health > sk_health[opt_skill] && ++health_fade_t >= 5) {   /* bonus health */
                    health_fade_t = 0; ship_health--;            /* above max slowly fades */
                }
                if (score >= bonus_next) {                        /* DIFFICULTY: bonus ship */
                    lives++;
                    bonus_next += sk_bonusn[opt_skill];
                    sfx(SFX_PLAY);
                }
                if (!clear_timer && !dead_timer && !game_over && cores_left() == 0)
                {
                    clear_timer = 50;                             /* K23: all cores gone (manage.cpp) */
                    if (opt_skill != SKILL_CLASSIC) {             /* DIFFICULTY: +25 % health, stage clear */
                        ship_health += sk_hp25[opt_skill];
                        if (ship_health > 2 * sk_health[opt_skill]) ship_health = 2 * sk_health[opt_skill];
                    }
                }
                if (clear_timer && --clear_timer == 0) {
                    /* K34: level cleared -> transition screen with Track04; the next level
                       is built when it ends (see game_state 3) */
                    game_state = 3; trans_t = 0;
                    music(CDDA_TRANSITION);
                    break;
                }
                if (banner > 0) banner--;
            }
            screen_reset = 0;                                     /* K30: never needed now */
            vx = (ship_x - PF_W / 2) & (MAP_SIZEX * 16 - 1);      /* view centred on the ship */
            vy = (ship_y - PF_H / 2) & (MAP_SIZEY * 16 - 1);
            /* K37g: C disabled - no debug text toggle */
            prev = pad;
            draw_playfield(vx, vy);
            draw_enemies();                                       /* K22 */
            if (!dead_timer && !game_over)                        /* K23: no ship while dead */
                draw_sprite(player_gfx + (ship_di - 1) * 2 * SHIP_W * SHIP_H, SHIP_W, SHIP_H,
                            PF_W / 2 - SHIP_W / 2, PF_H / 2 - SHIP_H / 2);  /* K17: ship, centre */
            draw_bolts();                                         /* K20 */
            if (banner > 0) {                                     /* K30: level start banner */
                char stg[12] = "STAGE ";
                dec(stg + 6, (uint32_t)(scene_no + 1), 2);
                font_center(70, stg);
            }
            if (game_over) font_center(100, "GAME OVER");
            if (paused) {                                       /* PAUSE MENU */
                static const char *const pm[3] = { "CONTINUE", "RESTART LEVEL", "MAIN MENU" };
                int i;
                font_center(70, "PAUSED");
                for (i = 0; i < 3; i++) font_text(80, 96 + i * 16, pm[i]);
                draw_sprite(player_gfx + 4 * SHIP_W * SHIP_H, SHIP_W, SHIP_H, 56, 96 + pause_sel * 16 - 6);
                font_center(160, "A OR B: SELECT");
                font_center(172, "X: CONTINUE");
            }
        }
#if KOBO_HW_DIAG
        {   /* HW1 hardware readout */
            static uint16_t fps_t0 = 0; static int fps_n = 0, fps = 0, fps_have = 0;
            int hz = (SC_VDP_DISPMODE & 0x8000) ? 60 : 50;
            char t[32], *q = t;
            fps_n++;
            if (!tmo) {
                if (!fps_have) { fps_t0 = md_ticks; fps_have = 1; }
                else if ((int)(uint16_t)(md_ticks - fps_t0) >= hz) { fps = fps_n; fps_n = 0; fps_t0 = md_ticks; }
            }
            {   /* HW6: heartbeats read once a second, not every frame.
                   HW7: P = Sub-CPU heartbeat (COMM14), V = 68K vblank heartbeat (COMM6) */
                /* HW8: read EVERY frame - at 1 frame per 60 the values looked frozen during the
                   crawl (60 slow frames = ~40 s).  Two reads a frame is harmless. */
                uint16_t hb = *(volatile uint16_t *)0x2000402E, vb = *(volatile uint16_t *)0x20004026;
                *q++ = 'P'; q = hex(q, hb, 4); *q++ = ' ';
                *q++ = 'V'; q = hex(q, vb, 4); *q++ = ' ';
            }
            *q++ = 'C'; q = hex(q, (uint16_t)MARS_SYS_COMM0 & 0xFF, 2); *q++ = ' ';
            *q++ = 'T'; q = hex(q, pad_timeouts > 255 ? 255 : pad_timeouts, 2); *q++ = ' ';
            *q++ = 'F'; dec(q, (uint32_t)(fps > 99 ? 99 : fps), 2);
            box(0, 216, 110, 8, col_black);
            put_caption(2, 217, t, col_white);
        }
#endif
#if KOBO_DIAG_STRIP
        draw_diag(pad, frames_drawn++, tmo);
#else
        draw_panel_info(pframe++);                                /* K30: Kobo's dashboard */
        (void)tmo; (void)frames_drawn;
#endif
        flip_wait();
    }
    }
    return 0;
}
