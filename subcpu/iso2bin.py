#!/usr/bin/env python3
"""
iso2bin.py - Convert a plain 2048-byte-sector ISO9660 image (what genisoimage
produces) into a genuine MODE1/2352 raw-sector .bin file, the format
PicoDrive's Sega CD core actually requires.

Standard CD-ROM Mode 1 sector layout (2352 bytes total):
  offset 0-11    : Sync pattern (fixed 12 bytes: 00 FF FF FF FF FF FF FF FF FF FF 00)
  offset 12-14   : Address (Minute, Second, Frame - BCD encoded MSF timestamp)
  offset 15      : Mode byte (0x01 for Mode 1)
  offset 16-2063 : 2048 bytes of real user data (this is your actual ISO content)
  offset 2064-2067 : EDC (4 bytes) - zero-filled here
  offset 2068-2075 : Reserved (8 bytes) - zero-filled
  offset 2076-2351 : ECC (276 bytes... actually 172 P-parity + 104 Q-parity = 276) - zero-filled

Zero-filling EDC/ECC is a standard, widely-used shortcut: essentially no
modern emulator core validates these fields for Mode 1 data sectors, only
the sync pattern, header, and the 2048 bytes of real data at the correct
offset. This matches what real conversion tools (CDmage, ISOBuster, etc.)
produce when speed/simplicity is prioritized over bit-perfect ECC.

MSF addressing starts at 00:02:00 (150 sectors of standard 2-second
pregap/lead-in), matching the real CD-ROM convention that LBA 0 = MSF 00:02:00.
"""

import sys
import struct

SYNC_PATTERN = bytes([0x00] + [0xFF]*10 + [0x00])
SECTOR_SIZE_IN = 2048
SECTOR_SIZE_OUT = 2352
PREGAP_SECTORS = 150  # 2 seconds at 75 sectors/second

def to_bcd(n):
    return ((n // 10) << 4) | (n % 10)

def msf_for_sector(sector_num):
    total = sector_num + PREGAP_SECTORS
    frame = total % 75
    total //= 75
    second = total % 60
    minute = total // 60
    return to_bcd(minute), to_bcd(second), to_bcd(frame)

def convert(in_path, out_path):
    with open(in_path, 'rb') as fin:
        data = fin.read()

    if len(data) % SECTOR_SIZE_IN != 0:
        pad = SECTOR_SIZE_IN - (len(data) % SECTOR_SIZE_IN)
        data += b'\x00' * pad
        print(f"Warning: input not a multiple of {SECTOR_SIZE_IN} bytes, padded with {pad} zero bytes.")

    num_sectors = len(data) // SECTOR_SIZE_IN

    with open(out_path, 'wb') as fout:
        for i in range(num_sectors):
            sector_data = data[i*SECTOR_SIZE_IN:(i+1)*SECTOR_SIZE_IN]
            m, s, f = msf_for_sector(i)
            header = struct.pack('BBBB', m, s, f, 0x01)  # mode 1
            edc_reserved_ecc = b'\x00' * (SECTOR_SIZE_OUT - len(SYNC_PATTERN) - len(header) - SECTOR_SIZE_IN)
            sector = SYNC_PATTERN + header + sector_data + edc_reserved_ecc
            assert len(sector) == SECTOR_SIZE_OUT, f"sector size mismatch: {len(sector)}"
            fout.write(sector)

    print(f"Converted {num_sectors} sectors -> {out_path}")
    print(f"Input size:  {len(data)} bytes")
    print(f"Output size: {num_sectors * SECTOR_SIZE_OUT} bytes")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.iso output.bin")
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2])
