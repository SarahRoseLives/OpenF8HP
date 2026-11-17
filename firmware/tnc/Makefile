CC      := clang
CFLAGS  ?= --target=thumbv7m-none-eabi -mcpu=cortex-m4 -mthumb \
           -mfloat-abi=soft -Os -ffreestanding -fno-builtin -nostdlib -fuse-ld=lld \
           -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -Wl,-T,linker.ld -Wl,--gc-sections -Wl,-Map=tnc.map
SIZE    ?= 32768

STUB_DIR = ../stub
STUB_BIN = $(STUB_DIR)/stub.bin
STUB_BLOB = stub_blob.c

all: $(STUB_BLOB) tnc.kdhx

$(STUB_BIN):
	$(MAKE) -C $(STUB_DIR)

$(STUB_BLOB): $(STUB_BIN) ../../tools/bin2c.py
	python3 ../../tools/bin2c.py $< $@ stub_blob stub_blob_len

tnc.elf: tnc.c $(STUB_BLOB) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tnc.c $(STUB_BLOB)

tnc.bin: tnc.elf
	llvm-objcopy -O binary $< $@
	python3 -c "import sys; p='tnc.bin'; d=open(p,'rb').read(); n=$(SIZE); \
	  sys.exit('image too big: %d > %d' % (len(d), n)) if len(d) > n else None; \
	  open(p,'wb').write(d + b'\\xff' * (n - len(d)))"

tnc.kdhx: tnc.bin
	python3 ../../tools/kdhx.py tnc.bin tnc.kdhx

clean:
	rm -f tnc.elf tnc.bin tnc.kdhx tnc.map stub_blob.c

.PHONY: all clean
