#include <stdint.h>

// Sound / SPCM Stubs
void S_Init(void) {}
void S_Clear(void) {}
void S_Update(void) {}
void S_CopyBufferData(void) {}
void S_SetBufferData(void) {}
void S_PauseSPCMTrack(void) {}
void S_LoadCDBuffers(void) {}
int S_PlaySPCMTrack(const char *name, int repeat) { return 0; }
void S_StopSPCMTrack(void) {}
void S_UnpauseSPCMTrack(void) {}
void *S_GetMemBankPtr(void) { return (void*)0x600000; }
void stream_cd(int lba, int len) {}
int S_PlaySource(void) { return 0; }
void S_PUnPSource(void) {}
void S_RewindSource(void) {}
void S_StopSource(void) {}
void S_UpdateSource(void) {}
int S_GetSourcePosition(void) { return 0; }

// External assembly functions defined in crt.s
extern int find_dir_entry(char *name);
extern void ReadSectorsSUB(void *ptr, int lba, int len);
extern int set_cwd(char *path);
extern int next_dir_sec(void);
extern int DENTRY_OFFSET;
extern int DENTRY_LENGTH;

#define ERR_NO_MORE_ENTRIES -4
#define ERR_NAME_NOT_FOUND  -6

int64_t open_file(const char *name) {
    int r;

    /* Load the root directory sector into the shared disc buffer before
       searching it. Without this, find_dir_entry() searches whatever
       sector init_cd()'s own earlier PVD scan last read - not the root
       directory at all. Confirmed by direct comparison against a real,
       working open_file() implementation (cdfh.c). */
    r = set_cwd("/");
    if (r < 0)
        return -1;

    /* Loop across directory sectors on ERR_NAME_NOT_FOUND, matching the
       real reference exactly - the previous version gave up after
       checking only the first sector, which would incorrectly fail if
       the target file's entry doesn't happen to fall in that sector. */
    while (1) {
        r = find_dir_entry((char*)name);
        if (r >= 0)
            break;

        if (r == ERR_NAME_NOT_FOUND) {
            r = next_dir_sec();
            if (r == ERR_NO_MORE_ENTRIES)
                return -1;
        }

        if (r < 0)
            return r;
    }

    /* Return the file's real length and offset, populated by
       find_dir_entry() into these globals - not a hardcoded fake
       value. The previous version always returned length=1, offset=0
       regardless of what was actually found. */
    return ((int64_t)DENTRY_LENGTH << 32) | (uint32_t)DENTRY_OFFSET;
}

int64_t read_directory(char *path) {
    return -1;
}

void read_sectors(void *ptr, int lba, int len) {
    ReadSectorsSUB(ptr, lba, len);
}
