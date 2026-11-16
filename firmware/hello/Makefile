CC      := clang
CFLAGS  ?= --target=thumbv7m-none-eabi -mcpu=cortex-m4 -mthumb \
           -mfloat-abi=soft -Os -ffreestanding -fno-builtin -nostdlib -fuse-ld=lld \
           -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -Wl,-T,linker.ld -Wl,--gc-sections -Wl,-Map=hello.map
SIZE    ?= 8192

all: hello.kdhx

hello.elf: hello.c linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ hello.c

hello.bin: hello.elf
	llvm-objcopy -O binary $< $@
	python3 -c "import sys; p='hello.bin'; d=open(p,'rb').read(); n=$(SIZE); \
	  sys.exit('image too big: %d > %d' % (len(d), n)) if len(d) > n else None; \
	  open(p,'wb').write(d + b'\\xff' * (n - len(d)))"

hello.kdhx: hello.bin
	python3 ../../tools/kdhx.py hello.bin hello.kdhx

clean:
	rm -f hello.elf hello.bin hello.kdhx hello.map

.PHONY: all clean
