CC = gcc
AR = ar

EFI_INC = include -Iinclude/x86_64
CFLAGS = -ffreestanding -fshort-wchar -mno-red-zone -nostdlib -DGNU_EFI_USE_MS_ABI -I$(EFI_INC)
LDFLAGS = -Wl,-subsystem,10 -e efi_main -nostdlib -Wl,--dynamicbase

LIB_SRCS = $(wildcard lib/*.c) lib/x86_64/callwrap.c lib/x86_64/initplat.c lib/x86_64/math.c \
           lib/runtime/efirtlib.c lib/runtime/rtdata.c lib/runtime/rtlock.c lib/runtime/rtstr.c lib/runtime/vm.c
LIB_OBJS = $(patsubst %.c,build/%.o,$(LIB_SRCS))

OUT = ../iso/EFI/BOOT/BOOTX64.EFI

QEMU = C:/Program Files/qemu/qemu-system-x86_64.exe
OVMF_CODE = ../tools/OVMF_CODE.fd
OVMF_VARS = ../tools/OVMF_VARS.fd
ISO_DIR = ../iso

all: $(OUT)

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/libefi.a: $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

$(OUT): src/main.c build/libefi.a
	@mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(OUT) src/main.c build/libefi.a

run:
	"C:/Program Files/qemu/qemu-system-x86_64.exe" -M q35 -m 256 \
		-drive if=pflash,format=raw,readonly=on,file="../tools/OVMF_CODE.fd" \
		-drive if=pflash,format=raw,file="../tools/OVMF_VARS.fd" \
		-drive format=raw,file=fat:rw:../iso \
		-net none \
		-device qemu-xhci \
		-device usb-kbd \
		-device usb-tablet \
		-serial stdio

re: clean all

clean:
	rm -rf build ../iso
	mkdir -p ../iso/EFI/BOOT