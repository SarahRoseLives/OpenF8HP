CC      = clang
CFLAGS  ?= --target=thumbv7m-none-eabi -mcpu=cortex-m4 -mthumb \
           -mfloat-abi=soft -Os -ffreestanding -fno-builtin -nostdlib \
           -fuse-ld=lld -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -Wl,-T,linker.ld -Wl,--gc-sections -Wl,-Map=stub.map

all: stub.bin

stub.elf: stub.c linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ stub.c
	llvm-size $@

stub.bin: stub.elf
	llvm-objcopy -O binary $< $@
	python3 -c "import sys; p='stub.bin'; d=open(p,'rb').read(); n=4096; \
	  sys.exit('stub too big: %d > %d' % (len(d), n)) if len(d) > n else None; \
	  open(p,'wb').write(d + b'\\xff' * (n - len(d)))"

clean:
	rm -f stub.elf stub.bin stub.map

.PHONY: all clean
