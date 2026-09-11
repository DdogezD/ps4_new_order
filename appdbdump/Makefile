# Path to your ps4-payload-sdk libPS4. Override with: make LIBPS4=/your/path
LIBPS4 ?= /path/to/ps4-payload-sdk/libPS4
CC := gcc
OBJCOPY := objcopy
ODIR := build
IDIRS := -I$(LIBPS4)/include
LDIRS := -L$(LIBPS4)
CFLAGS := $(IDIRS) -Os -std=c11 -ffunction-sections -fdata-sections -fno-builtin -nostartfiles -nostdlib -Wall -Wextra -masm=intel -march=btver2 -mtune=btver2 -m64 -mabi=sysv -mcmodel=small -fpie -fPIC
LFLAGS := $(LDIRS) -Xlinker -T $(LIBPS4)/linker.x -Xlinker -Map="appdbdump.map" -Wl,--build-id=none -Wl,--gc-sections
LIBS := -lPS4

all: $(ODIR) $(ODIR)/main.o $(ODIR)/debug_sym.o
	$(CC) $(LIBPS4)/crt0.s $(ODIR)/main.o $(ODIR)/debug_sym.o -o temp.t $(CFLAGS) $(LFLAGS) $(LIBS)
	$(OBJCOPY) -O binary temp.t appdbdump_v2.bin
	rm -f temp.t

$(ODIR)/%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

$(ODIR):
	@mkdir -p $@

clean:
	rm -rf $(ODIR) appdbdump_v2.bin appdbdump.map
.PHONY: clean all
