# =============================================================================
# SHENLONG OS - Makefile (Limine Boot Protocol, UEFI + BIOS)
# =============================================================================

NASM    := nasm
CC      := clang
LD      := x86_64-elf-ld
OBJCOPY := x86_64-elf-objcopy

export GCC_EXEC_PREFIX := /usr/local/Cellar/x86_64-elf-gcc/16.2.0/libexec/gcc/

CFLAGS  := -target x86_64-elf \
           -std=c99 \
           -ffreestanding \
           -fno-stack-protector \
           -fno-builtin \
           -fPIE \
           -mno-red-zone \
           -mno-mmx \
           -mno-sse \
           -mno-sse2 \
           -mcmodel=kernel \
           -nostdlib \
           -nostdinc \
           -Wall \
           -Wextra \
           -O2 \
           -Ikernel -MMD -MP

NASMFLAGS := -f elf64
LDFLAGS   := -T build/linker.ld -nostdlib -z max-page-size=0x1000

OUT        := build/out
ISO_DIR    := $(OUT)/iso-workspace
ISO        := $(OUT)/shenlong.iso
LIMINE_DIR := limine

ENTRY_OBJ       := $(OUT)/entry.o
BOOT_LOGO_OBJ   := $(OUT)/boot_logo.o
MEDIA_OBJ       := $(OUT)/media_data.o
WALLPAPER_OBJ   := $(OUT)/wallpaper_data.o
PANIC_LOGO_OBJ  := $(OUT)/panic_logo.o
C_SRCS     := kernel/kernel.c kernel/fb.c kernel/gpu.c kernel/ui.c kernel/ui_font.c kernel/clock.c kernel/net.c kernel/web.c kernel/browser.c \
              kernel/chromium_engine.c kernel/html_ui.c kernel/axis_web.c kernel/pat.c kernel/mem.c kernel/idt.c kernel/icons.c \
              kernel/keyboard.c kernel/mouse.c kernel/ps2.c kernel/fs.c kernel/process.c kernel/kernel_info.c \
              kernel/splash.c kernel/boot_logo.c kernel/tls.c kernel/libc_compat.c kernel/audio.c kernel/media.c kernel/wallpaper.c \
              kernel/ata.c kernel/ssh.c kernel/posix.c shell/shell.c \
              kernel/wddm.c kernel/dwm.c kernel/d3d.c kernel/gdi.c kernel/wpf.c kernel/ai.c
C_OBJS     := $(patsubst %.c,$(OUT)/%.o,$(C_SRCS))
KERNEL_ELF := $(OUT)/shenlong.elf
BEAR_SRCS := $(shell rg --files vendor/bearssl-0.6/src -g '*.c')
BEAR_OBJS := $(patsubst vendor/bearssl-0.6/src/%.c,$(OUT)/bearssl/%.o,$(BEAR_SRCS))
TLS_FLAGS := -Ikernel/libc -Ivendor/bearssl-0.6/inc -Ivendor/bearssl-0.6/src -DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_RAND=0 -DBR_USE_WIN32_TIME=0 -DBR_RDRAND=0 -DBR_SSE2=0 -DBR_AES_X86NI=0 -DBR_POWER8=0 -DBR_INT128=0 -DBR_UMUL128=0

# =============================================================================
.PHONY: all iso clean dirs run desktop test test-network test-native font axis-web-deps

.DEFAULT_GOAL := all
include build/axis-web.mk

all: iso

# Hosted validation build for the real Lexbor + QuickJS sources. These
# libraries are kept outside the freestanding kernel until the AXIS ABI
# adapters are implemented and tested.
axis-web-deps:
	cmake -S vendor/axis-web/lexbor -B build/out/axis-web/lexbor -DLEXBOR_BUILD_SHARED=OFF -DLEXBOR_BUILD_STATIC=ON -DLEXBOR_BUILD_EXAMPLES=OFF
	cmake --build build/out/axis-web/lexbor --config Release
	$(MAKE) -C vendor/axis-web/quickjs libquickjs.a

.PHONY: test-axis-web
$(OUT)/test-axis-web: tools/test-axis-web.c kernel/axis_web/runtime.c kernel/axis_web/runtime.h kernel/axis_web/allocator.c $(AW_OUT)/bootstrap.h
	$(CC) -O1 -g -fsanitize=address,undefined -I$(AW_OUT) -Ivendor/axis-web/lexbor/source -Ivendor/axis-web/quickjs tools/test-axis-web.c kernel/axis_web/runtime.c kernel/axis_web/allocator.c build/out/axis-web/lexbor/liblexbor_static.a vendor/axis-web/quickjs/libquickjs.a -lm -o $@
test-axis-web: $(OUT)/test-axis-web
	$(OUT)/test-axis-web

RUN_QEMU    := $(shell command -v qemu-system-x86_64 2>/dev/null)
RUN_VGA     := std

run: iso
	@if [ ! -f $(OUT)/shenlong-disk.img ]; then dd if=/dev/zero of=$(OUT)/shenlong-disk.img bs=1M count=64 status=none; fi
	qemu-system-x86_64 -cdrom $(ISO) -drive file=$(OUT)/shenlong-disk.img,format=raw,if=ide -machine q35,i8042=on -cpu max -m 6144M -vga $(RUN_VGA) -serial stdio -netdev user,id=hostnet,hostfwd=tcp:127.0.0.1:2222-:22 -device rtl8139,netdev=hostnet -audiodev coreaudio,id=audio -device AC97,audiodev=audio

# UTM/VirtualBox icin 4K cizim testi: ramfb cizim motoru (4K gecerli mod).
# UTM 4K destek icin host display ayarlarina bakin.
run-4k: iso
	@if [ ! -f $(OUT)/shenlong-disk.img ]; then dd if=/dev/zero of=$(OUT)/shenlong-disk.img bs=1M count=64 status=none; fi
	qemu-system-x86_64 -cdrom $(ISO) -drive file=$(OUT)/shenlong-disk.img,format=raw,if=ide -machine q35,i8042=on -cpu max -m 12288M -device ramfb -serial stdio -netdev user,id=hostnet,hostfwd=tcp:127.0.0.1:2222-:22 -device rtl8139,netdev=hostnet -audiodev coreaudio,id=audio -device AC97,audiodev=audio

# VirtIO-GPU 4K test (GPU passthrough taklidi; ramfb framebuffer 32bpp, 4K)
run-virtio: iso
	@if [ ! -f $(OUT)/shenlong-disk.img ]; then dd if=/dev/zero of=$(OUT)/shenlong-disk.img bs=1M count=64 status=none; fi
	qemu-system-x86_64 -cdrom $(ISO) -drive file=$(OUT)/shenlong-disk.img,format=raw,if=ide -machine q35,i8042=on -cpu max -m 12288M -device ramfb -serial stdio -netdev user,id=hostnet,hostfwd=tcp:127.0.0.1:2222-:22 -device rtl8139,netdev=hostnet

.PHONY: run run-4k run-virtio

dirs:
	@mkdir -p $(OUT)/kernel $(OUT)/shell \
	    $(ISO_DIR)/boot/limine \
	    $(ISO_DIR)/EFI/BOOT

# --- Kernel entry ---
$(ENTRY_OBJ): kernel/entry.asm | dirs
	$(NASM) $(NASMFLAGS) $< -o $@

$(OUT)/boot-logo.rgb: assets/boot/dragon.png tools/build-boot-logo.py | dirs
	python3 tools/build-boot-logo.py $< $@

$(BOOT_LOGO_OBJ): kernel/boot_logo.asm kernel/wallpaper_data.asm $(OUT)/boot-logo.rgb | dirs
	$(NASM) $(NASMFLAGS) $< -o $@

$(MEDIA_OBJ): kernel/media_data.asm assets/media/demo.mpg | dirs
	$(NASM) $(NASMFLAGS) $< -o $@

# --- C dosyaları ---
$(OUT)/kernel/%.o: kernel/%.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/kernel/tls.o: CFLAGS += $(TLS_FLAGS)
$(OUT)/ui_assets.h: tools/embed-ui.py $(wildcard assets/ui/*.html) assets/ui/ui.css | dirs
	python3 tools/embed-ui.py $@

$(OUT)/kernel/html_ui.o: $(OUT)/ui_assets.h
$(OUT)/kernel/html_ui.o: CFLAGS += -I$(OUT)

$(OUT)/kernel/ssh.o: CFLAGS += $(TLS_FLAGS)
$(OUT)/kernel/media.o: CFLAGS += -Ikernel/libc -msse2 -mfpmath=sse
$(OUT)/bearssl/%.o: vendor/bearssl-0.6/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TLS_FLAGS) -Wno-unused-parameter -c $< -o $@

$(OUT)/shell/%.o: shell/%.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

# --- Kernel ELF ---
$(WALLPAPER_OBJ): kernel/wallpaper_data.asm $(OUT)/wallpapers.img | dirs
	$(NASM) $(NASMFLAGS) $< -o $@

$(OUT)/wallpapers.img: $(wildcard assets/wallpapers/*.png) tools/pack-wallpapers.py | dirs
	python3 tools/pack-wallpapers.py

$(PANIC_LOGO_OBJ): kernel/panic_logo.asm $(OUT)/panic-logo.rgb | dirs
	$(NASM) $(NASMFLAGS) $< -o $@

$(OUT)/panic-logo.rgb: /Users/ali_new/Desktop/panic.png tools/build-panic-logo.py | dirs
	python3 tools/build-panic-logo.py $< $@

# Vendored QuickJS 128-bit bolma islemleri icin libgcc (x86_64-elf) gerektirir.
LIBGCC := $(wildcard /usr/local/lib/x86_64-elf-gcc-tmp/gcc/x86_64-elf/*/libgcc.a)

$(KERNEL_ELF): $(ENTRY_OBJ) $(BOOT_LOGO_OBJ) $(MEDIA_OBJ) $(WALLPAPER_OBJ) $(PANIC_LOGO_OBJ) $(C_OBJS) $(BEAR_OBJS) $(AW_LIB) build/linker.ld
	$(LD) $(LDFLAGS) --gc-sections $(ENTRY_OBJ) $(BOOT_LOGO_OBJ) $(MEDIA_OBJ) $(WALLPAPER_OBJ) $(PANIC_LOGO_OBJ) $(C_OBJS) $(BEAR_OBJS) $(AW_LIB) $(LIBGCC) -o $@
	@wc -c $@

$(OUT)/limine-tool: $(LIMINE_DIR)/limine.c $(LIMINE_DIR)/limine-bios-hdd.h | dirs
	$(CC) -o $@ $< -I$(LIMINE_DIR)

$(OUT)/utm-qemu-runner: tools/utm-qemu-runner.c | dirs
	$(CC) $< -Wl,-rpath,/Applications/UTM.app/Contents/Frameworks -o $@

$(OUT)/test-clock: tools/test-clock.c kernel/clock.c kernel/clock.h | dirs
	$(CC) -arch x86_64 -Wall -Wextra -Ikernel tools/test-clock.c kernel/clock.c -o $@

$(OUT)/test-compositor: tools/test-compositor.c kernel/gpu.c kernel/gpu.h | dirs
	$(CC) -arch x86_64 -O2 -fno-builtin -Wall -Wextra -Ikernel tools/test-compositor.c kernel/gpu.c -o $@

$(OUT)/test-html-ui: tools/test-html-ui.c kernel/html_ui.c kernel/html_ui.h kernel/gpu.c kernel/ui_font.c kernel/icons.c kernel/ui_paint.h $(OUT)/ui_assets.h | dirs
	$(CC) -arch x86_64 -O1 -g -fsanitize=address,undefined -fno-builtin -Wall -Wextra -Ikernel -I$(OUT) tools/test-html-ui.c kernel/html_ui.c kernel/gpu.c kernel/ui_font.c kernel/icons.c -o $@

.PHONY: test-win10-ui
test-win10-ui: iso $(OUT)/utm-qemu-runner $(OUT)/test-compositor $(OUT)/test-html-ui
	$(OUT)/test-compositor
	$(OUT)/test-compositor 24
	$(OUT)/test-html-ui
	python3 tools/test-win10-desktop.py
	python3 tools/test-win10-desktop.py --bios

$(OUT)/test-web: tools/test-web.c kernel/web.c kernel/web.h | dirs
	$(CC) -arch x86_64 -O1 -g -fsanitize=address,undefined -fno-builtin -Wall -Wextra -Ikernel tools/test-web.c kernel/web.c -o $@

$(OUT)/test-keyboard: tools/test-keyboard.c kernel/keyboard.c kernel/keyboard.h | dirs
	$(CC) -arch x86_64 -Wall -Wextra -Ikernel tools/test-keyboard.c kernel/keyboard.c -o $@

$(OUT)/test-media: tools/test-media.c kernel/media.c kernel/media.h kernel/ui_paint.h kernel/gpu.h vendor/pl_mpeg.h | dirs
	$(CC) -arch x86_64 -O1 -g -fsanitize=address,undefined -fno-builtin -nostdinc -Ikernel/libc -Ikernel tools/test-media.c -o $@

$(OUT)/test-drive-ssh: tools/test-drive-ssh.c | dirs
	$(CC) -arch x86_64 -O1 -g -fsanitize=address,undefined -Wall -Wextra tools/test-drive-ssh.c -o $@

font:
	python3 tools/build-ui-font.py

-include $(C_OBJS:.o=.d)

# --- ISO ---
iso: dirs $(KERNEL_ELF) $(OUT)/limine-tool
	@echo "ISO olusturuluyor (UEFI + BIOS)..."

	# Limine dosyalarini kopyala
	cp $(LIMINE_DIR)/limine-bios.sys    $(ISO_DIR)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(ISO_DIR)/boot/limine/
	cp build/limine.conf                $(ISO_DIR)/boot/limine/limine.conf

	# EFI binary'leri
	cp $(LIMINE_DIR)/BOOTX64.EFI   $(ISO_DIR)/EFI/BOOT/
	cp $(LIMINE_DIR)/BOOTIA32.EFI  $(ISO_DIR)/EFI/BOOT/
	cp build/limine.conf         $(ISO_DIR)/EFI/BOOT/limine.conf
	cp assets/fonts/OFL.txt      $(ISO_DIR)/FONT-LICENSE.txt
	cp assets/fonts/NotoSansMono-OFL.txt $(ISO_DIR)/TERMINAL-FONT-LICENSE.txt
	cp vendor/bearssl-0.6/LICENSE.txt $(ISO_DIR)/BEARSSL-LICENSE.txt
	cp vendor/pl_mpeg.h $(ISO_DIR)/PL-MPEG-SOURCE.h
	cp vendor/cacert.pem $(ISO_DIR)/CA-ROOTS.pem
	cp THIRD-PARTY.md $(ISO_DIR)/THIRD-PARTY.md
	mkdir -p $(ISO_DIR)/ui
	cp assets/ui/* $(ISO_DIR)/ui/

	# Kernel ELF'i ISO'ya kopyala
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/shenlong.elf

	# Self-contained FAT EFI image: config AND kernel live beside the loader.
	# UEFI never needs to fall back to ISO9660 for either file.
	mformat -i $(OUT)/efi-boot.img -C -T 131072 -v AXIS ::
	mmd -i $(OUT)/efi-boot.img ::/EFI ::/EFI/BOOT ::/boot
	mcopy -i $(OUT)/efi-boot.img $(LIMINE_DIR)/BOOTX64.EFI ::/EFI/BOOT/
	mcopy -i $(OUT)/efi-boot.img $(LIMINE_DIR)/BOOTIA32.EFI ::/EFI/BOOT/
	mcopy -i $(OUT)/efi-boot.img build/limine.conf ::/EFI/BOOT/limine.conf
	mcopy -i $(OUT)/efi-boot.img $(KERNEL_ELF) ::/boot/shenlong.elf
	cp $(OUT)/efi-boot.img $(ISO_DIR)/boot/limine/limine-uefi-cd.bin

	# xorriso ile hybrid ISO olustur (BIOS + UEFI)
	xorriso -as mkisofs \
		-R -J -r \
		-iso-level 3 \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot \
		-boot-load-size 4 \
		-boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image \
		--protective-msdos-label \
		-V AXIS \
		-o $(OUT)/shenlong-next.iso \
		$(ISO_DIR) 2>&1

	# Limine BIOS MBR'ı yaz (BIOS boot icin)
	$(OUT)/limine-tool bios-install $(OUT)/shenlong-next.iso
	mv $(OUT)/shenlong-next.iso $(ISO)
	@echo ""
	@echo "============================================"
	@echo "  SHENLONG OS v7.1 ISO hazır!"
	@ls -lh $(ISO)
	@echo "  Masaustune kopyalamak icin: make desktop"
	@echo "  UTM: Emulate > Other > UEFI secin"
	@echo "  VirtualBox: Other 64-bit > EFI etkin"
	@echo "============================================"

desktop: iso
	cp $(ISO) "$(HOME)/Desktop/axis.iso.new"
	mv "$(HOME)/Desktop/axis.iso.new" "$(HOME)/Desktop/axis.iso"
	cp "$(HOME)/Desktop/axis.iso" "$(HOME)/Desktop/shenlong.iso"

utm: iso
	python3 tools/package-utm.py build/out/SHENLONG-v7.1.utm --name "SHENLONG OS v7.1" --memory 6144 --screenshot build/out/win10-uefi-desktop.png -f
	rm -rf "$(HOME)/Desktop/SHENLONG-v7.1.utm"
	cp -r build/out/SHENLONG-v7.1.utm "$(HOME)/Desktop/SHENLONG-v7.1.utm"
	@echo "SHENLONG 7.1 UTM paketi oluşturuldu: build/out/SHENLONG-v7.1.utm"

parallels: iso
	python3 tools/package-parallels.py --name "AXIS OS v6.0.2" --memory 4096

test: iso $(OUT)/utm-qemu-runner $(OUT)/test-clock $(OUT)/test-compositor $(OUT)/test-web $(OUT)/test-keyboard $(OUT)/test-media $(OUT)/test-drive-ssh
	$(OUT)/test-clock
	$(OUT)/test-compositor
	$(OUT)/test-compositor 24
	$(OUT)/test-web
	$(OUT)/test-keyboard
	UBSAN_OPTIONS=halt_on_error=1 $(OUT)/test-media
	$(OUT)/test-drive-ssh
	python3 tools/test-desktop.py
	python3 tools/test-desktop.py --bios

clean:
	rm -rf $(OUT)
	@echo "Temizlendi."

test-network: iso $(OUT)/utm-qemu-runner
	python3 tools/test-desktop.py --network

test-native: iso $(OUT)/utm-qemu-runner
	python3 tools/test-desktop.py --native-check

.PHONY: test-ui-v25
test-ui-v25: iso $(OUT)/utm-qemu-runner
	python3 tools/test-ui-v25.py
	python3 tools/test-ui-v25.py --bios
