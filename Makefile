# KallistiOS makefile - Windows CE 2.00 shell for Dreamcast
#
# Requires a KallistiOS toolchain (kos-2.x). Adjust KOS_BASE if needed:
#   make          -> wince.elf
#   make run      -> dc-tool serial/BBA upload (if configured)
#   make 1ST_READ.BIN
#
# The sound code needs <kos/snd_sfx.h>. If your KOS lacks it, set
# USE_SOUND to 0 in main.c.

TARGET  = wince.elf
OBJS    = main.o

ifndef KOS_BASE
KOS_BASE := /opt/toolchains/dc/kos
endif

include $(KOS_BASE)/Makefile.rules

all: rm-elf $(TARGET)

clean: rm-elf
	rm -f $(OBJS)

distclean: clean
	rm -f $(TARGET) 1ST_READ.BIN

rm-elf:
	rm -f $(TARGET)

$(TARGET): $(OBJS)
	$(KOS_CC) $(KOS_CFLAGS) $(KOS_LDFLAGS) -o $(TARGET) $(KOS_START) \
		$(OBJS) $(KOS_LIBS) -lm

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

# raw binary for building a bootable disc (pair with an ip.bin)
1ST_READ.BIN: $(TARGET)
	$(KOS_OBJCOPY) -O binary -R .stack $(TARGET) 1ST_READ.BIN

.c.o:
	$(KOS_CC) $(KOS_CFLAGS) -c $< -o $@
