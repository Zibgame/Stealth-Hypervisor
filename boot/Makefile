CC = gcc
AR = ar
AS = gcc

EFI_INC = include -Iinclude/x86_64
CFLAGS = -ffreestanding -fshort-wchar -mno-red-zone -nostdlib -DGNU_EFI_USE_MS_ABI -I$(EFI_INC)
LDFLAGS = -nostdlib -Wl,--dynamicbase

LIB_SRCS = $(wildcard lib/*.c) lib/x86_64/callwrap.c lib/x86_64/initplat.c lib/x86_64/math.c \
           lib/runtime/efirtlib.c lib/runtime/rtdata.c lib/runtime/rtlock.c lib/runtime/rtstr.c lib/runtime/vm.c
LIB_OBJS = $(patsubst %.c,build/%.o,$(LIB_SRCS))
ASM_SRCS = src/svm_context_world.S
ASM_OBJS = $(patsubst %.S,build/%.o,$(ASM_SRCS))

OUT = ../iso/EFI/BOOT/BOOTX64.EFI
CORE_OUT = ../iso/EFI/BOOT/HVCORE.EFI
QEMU_ESP_DIR = ../qemu-esp

QEMU = C:/Program Files/qemu/qemu-system-x86_64.exe
OVMF_CODE = ../tools/OVMF_CODE.fd
OVMF_VARS = ../tools/OVMF_VARS.fd
OVMF_RUN_VARS = ../iso/OVMF_VARS_RUN.fd
ISO_DIR = ../iso
WINDOWS_DISK ?=
WINDOWS_DISK_FORMAT ?= raw

KEY ?=
KEY_BOOT_DIR = $(KEY)/EFI/BOOT

QEMU_DISK_ARGS = -drive format=raw,file=fat:rw:../iso -drive format=raw,file=fat:rw:$(QEMU_ESP_DIR)
ifneq ($(strip $(WINDOWS_DISK)),)
QEMU_DISK_ARGS += -drive format=$(WINDOWS_DISK_FORMAT),file="$(WINDOWS_DISK)"
endif

all: clear_screen $(OUT)

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(CFLAGS) -c $< -o $@

build/libefi.a: $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

$(CORE_OUT): src/main.c build/libefi.a $(ASM_OBJS)
	@mkdir -p $(dir $(CORE_OUT))
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-subsystem,12 -e hv_runtime_main -o $(CORE_OUT) src/main.c $(ASM_OBJS) build/libefi.a

$(OUT): src/main.c build/libefi.a $(ASM_OBJS) $(CORE_OUT)
	@mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-subsystem,10 -e efi_main -o $(OUT) src/main.c $(ASM_OBJS) build/libefi.a
	@mkdir -p $(QEMU_ESP_DIR)/EFI/Microsoft/Boot
	@cp /c/Windows/Boot/EFI/bootmgfw.efi $(QEMU_ESP_DIR)/EFI/Microsoft/Boot/bootmgfw.efi
	@powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/copy-host-bcd.ps1 -Destination "$(QEMU_ESP_DIR)/EFI/Microsoft/Boot/BCD" || \
		if [ -f /c/Windows/Boot/DVD/EFI/BCD ]; then cp /c/Windows/Boot/DVD/EFI/BCD $(QEMU_ESP_DIR)/EFI/Microsoft/Boot/BCD; fi
	@if [ -f /c/Windows/Boot/EFI/bootmgr.efi ]; then cp /c/Windows/Boot/EFI/bootmgr.efi $(QEMU_ESP_DIR)/EFI/Microsoft/Boot/bootmgr.efi; fi

run: all_no_clear
	@cp "$(OVMF_VARS)" "$(OVMF_RUN_VARS)"
	"$(QEMU)" -M q35 -m 1024 \
		-cpu qemu64,+svm,+pdpe1gb,+sse,+sse2,+sse4.1,+sse4.2 \
		-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
		-drive if=pflash,format=raw,file="$(OVMF_RUN_VARS)" \
		$(QEMU_DISK_ARGS) \
		-net none \
		-device qemu-xhci \
		-device usb-kbd \
		-device usb-tablet \
		-serial stdio

intokey: re
	@if [ -z "$(strip $(KEY))" ]; then \
		echo "Usage: mingw32-make intokey KEY=/e"; \
		echo "Exemple si ta cle USB est E: -> KEY=/e"; \
		exit 1; \
	fi
	@if [ ! -d "$(KEY)" ]; then \
		echo "Erreur: la cle $(KEY) est introuvable."; \
		echo "Verifie la lettre de ta cle USB."; \
		exit 1; \
	fi
	@echo "Copie de BOOTX64.EFI vers $(KEY_BOOT_DIR)..."
	@mkdir -p "$(KEY_BOOT_DIR)"
	@stamp=$$(date +%Y%m%d-%H%M%S); archive="$(KEY_BOOT_DIR)/archive"; mkdir -p "$$archive"; \
		if [ -f "$(KEY_BOOT_DIR)/BOOTX64.EFI" ]; then cp "$(KEY_BOOT_DIR)/BOOTX64.EFI" "$$archive/BOOTX64.pre-v48.$$stamp.EFI"; fi; \
		if [ -f "$(KEY_BOOT_DIR)/HVCORE.EFI" ]; then cp "$(KEY_BOOT_DIR)/HVCORE.EFI" "$$archive/HVCORE.pre-v48.$$stamp.EFI"; fi; \
		if [ -f "$(KEY)/HVBOOT.LOG" ]; then cp "$(KEY)/HVBOOT.LOG" "$$archive/HVBOOT.pre-v48.$$stamp.LOG"; fi
	@cp "$(OUT)" "$(KEY_BOOT_DIR)/BOOTX64.EFI"
	@cp "$(CORE_OUT)" "$(KEY_BOOT_DIR)/HVCORE.EFI"
	@sync || true
	@echo "OK: BOOTX64.EFI et HVCORE.EFI copies sur la cle USB."
	@echo "Chemin final: $(KEY_BOOT_DIR)/BOOTX64.EFI"

run_clean: re run

re: clear_screen clean_no_clear all_no_clear

all_no_clear: $(OUT)

clean: clear_screen clean_no_clear

clean_no_clear:
	rm -rf build ../iso $(QEMU_ESP_DIR)
	mkdir -p ../iso/EFI/BOOT

clear_screen:
	@powershell.exe -NoProfile -Command "Clear-Host"

.PHONY: all all_no_clear clean clean_no_clear re run run_clean intokey clear_screen
