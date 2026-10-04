ifdef GENDEV
ROOTDIR = $(GENDEV)
else
ROOTDIR = /opt/toolchains/sega
endif

# --- SH-2 (32X ENGINE) TOOLCHAIN ---
PREFIX = $(ROOTDIR)/sh-elf/bin/sh-elf-
CC = $(PREFIX)gcc
LD = $(PREFIX)ld
OBJC = $(PREFIX)objcopy
RM = rm -rf

TARGET = D32XR

# Compiler flags for SH-2 architecture
CFLAGS = -c -std=c11 -m2 -mb -O2 -Wall -ffunction-sections -fdata-sections -D__32X__ -DMARS
# Linker flags pointing to the correct 32X SDRAM linker script
LDFLAGS = -EB -T sh2_sdram.ld -nostdlib -Map=output.map

OBJS = crt0.o sh2_main.o

all: $(TARGET).32x

# Output the raw binary payload to .32x
$(TARGET).32x: $(TARGET).elf
	$(OBJC) -O binary $< $@

$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $(TARGET).elf

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

%.o: %.s
	$(CC) $(CFLAGS) $< -o $@

clean:
	$(RM) *.o *.elf $(TARGET).32x output.map