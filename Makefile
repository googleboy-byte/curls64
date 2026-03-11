C_SOURCES = $(filter-out kernel/modules/fs_initrd/initrd_data.c, $(wildcard kernel/core/*.c kernel/core/tests/unit/*.c kernel/core/tests/stress/*.c kernel/core/tests/core_tests/*.c kernel/modules/sched_rr/*.c kernel/modules/fs_initrd/*.c kernel/modules/drivers/*.c kernel/modules/shell/*.c kernel/modules/sysmon/*.c kernel/modules/partition/*.c kernel/modules/usb/*.c kernel/modules/test/*.c kernel/fs/fat32/*.c kernel/fs/elf/*.c kernel/proc/*.c kernel/cpu/*.c kernel/ktrace/*.c libc/*.c))
HEADERS = $(wildcard kernel/core/*.h kernel/core/tests/unit/*.h kernel/core/tests/stress/*.h kernel/core/tests/core_tests/*.h kernel/modules/sched_rr/*.h kernel/modules/fs_initrd/*.h kernel/modules/drivers/*.h kernel/modules/shell/*.h kernel/modules/sysmon/*.h kernel/modules/partition/*.h kernel/modules/usb/*.h kernel/modules/test/*.h kernel/fs/fat32/*.h kernel/cpu/*.h libc/*.h)
# Nice syntax for file extension replacement
# Note: initrd_data.o is generated and added explicitly (wildcard can't discover it pre-generation)
OBJ = ${C_SOURCES:.c=.o} kernel/cpu/interrupt.o kernel/core/process.o kernel/cpu/gdt_flush.o kernel/modules/fs_initrd/initrd_data.o

# Change this if your cross-compiler is somewhere else
CC = gcc
GDB = gdb
# -g: Use debugging symbols in gcc
CFLAGS = -g -ffreestanding -m32 -fno-pie -no-pie -fno-pic -Ikernel/include
CFLAGS64 = -g -ffreestanding -m64 -fno-pie -no-pie -fno-pic -Ikernel/include -DARCH_X86_64
USER_BINARIES = user/hello/hello.elf user/argtest/argtest.elf user/init/init.elf user/sh/sh.elf user/lappy/lappy.elf \
                user/ls/ls.elf user/ps/ps.elf user/top/top.elf user/cat/cat.elf user/touch/touch.elf user/clear/clear.elf user/sleep/sleep.elf \
                user/echo/echo.elf user/pwd/pwd.elf user/debug/debug.elf user/write/write.elf user/write_a/write_a.elf user/help/help.elf \
                user/mkdir/mkdir.elf user/rm/rm.elf user/cp/cp.elf \
                user/devs/devs.elf user/mount/mount.elf user/umount/umount.elf

# Build output directories
BUILD_DIR = build
LOG_DIR   = logs
IMG       = $(BUILD_DIR)/disk.img

# Create build and logs directories automatically
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/boot

$(LOG_DIR):
	mkdir -p $(LOG_DIR)

# First rule is run by default
$(BUILD_DIR)/os-image.bin: $(BUILD_DIR)/boot/bootsect.bin $(BUILD_DIR)/boot/stage2.bin $(BUILD_DIR)/kernel.bin | $(BUILD_DIR)
	cat $^ > $@
	truncate -s 1474560 $@

$(BUILD_DIR)/boot/stage2.bin: boot/stage2.asm | $(BUILD_DIR)
	nasm $< -f bin -o $@
	truncate -s 2048 $@

$(BUILD_DIR)/boot/bootsect.bin: boot/bootsect.asm | $(BUILD_DIR)
	nasm $< -f bin -o $@

# '--oformat binary' deletes all symbols as a collateral, so we don't need
# to 'strip' them manually on this case
# kernel.bin depends on the compiled initrd_data.o (which is compiled from the generated .c)
$(BUILD_DIR)/kernel.bin: boot/multiboot2_entry.o boot/kernel_entry.o ${OBJ} | $(BUILD_DIR)
	ld -m elf_i386 -o $@ -T linker.ld boot/multiboot2_entry.o boot/kernel_entry.o ${OBJ} --oformat binary

# Step 1: generate initrd_data.c from the user binaries
kernel/modules/fs_initrd/initrd_data.c: $(USER_BINARIES) kernel/modules/fs_initrd/initrd_gen.py
	python3 kernel/modules/fs_initrd/initrd_gen.py
	xxd -i kernel/modules/fs_initrd/initrd.bin > $@
	sed -i 's/kernel_modules_fs_initrd_initrd_bin/initrd_bin/g' $@

# Step 2: compile initrd_data.c into initrd_data.o
kernel/modules/fs_initrd/initrd_data.o: kernel/modules/fs_initrd/initrd_data.c
	$(CC) $(CFLAGS) -c $< -o $@

# Used for debugging purposes
$(BUILD_DIR)/kernel.elf: boot/multiboot2_entry.o boot/kernel_entry.o ${OBJ} | $(BUILD_DIR)
	ld -m elf_i386 -o $@ -T linker.ld $^

# 64-bit kernel ELF (for Phase 1 verification)
# We re-run CC with CFLAGS64 for these objects.
# For now, we only build a subset of core objects to verify the pipeline.
OBJ64_CORE = $(filter-out kernel/core/boot_multiboot2.o64, $(OBJ:.o=.o64)) kernel/arch/x86_64/mmu/mmu.o64
%.o64: %.c ${HEADERS}
	${CC} ${CFLAGS64} -c $< -o $@

%.o64: %.asm
	nasm $< -f elf64 -o $@

# Specific rules for 64-bit architecture code
kernel/arch/x86_64/boot/multiboot2_entry64.o64: kernel/arch/x86_64/boot/multiboot2_entry64.asm
	nasm $< -f elf64 -o $@

kernel/arch/x86_64/mmu/mmu.o64: kernel/arch/x86_64/mmu/mmu.c
	${CC} ${CFLAGS64} -c $< -o $@

$(BUILD_DIR)/kernel64.elf: kernel/arch/x86_64/boot/multiboot2_entry64.o64 kernel/core/boot_multiboot2_64.o64 ${OBJ64_CORE} | $(BUILD_DIR)
	ld -m elf_x86_64 -o $@ -T linker64.ld $^

# Minimal 64-bit kernel for Phase 3 verification
OBJ64_VERIFY = kernel/arch/x86_64/boot/multiboot2_entry64.o64 \
               kernel/core/boot_multiboot2_64.o64 \
               kernel/core/kernel.o64 \
               kernel/core/core_init.o64 \
               kernel/arch/x86_64/mmu/mmu.o64 \
               kernel/cpu/paging.o64 \
               libc/mem.o64 \
               libc/string.o64 \
               libc/kheap.o64

$(BUILD_DIR)/kernel64_verify.elf: $(OBJ64_VERIFY) | $(BUILD_DIR)
	ld -m elf_x86_64 -o $@ -T linker64.ld $^

ISO_DIR = $(BUILD_DIR)/iso
ISO_IMG = $(BUILD_DIR)/curls.iso

$(ISO_DIR):
	mkdir -p $(ISO_DIR)/boot/grub

$(ISO_IMG): $(BUILD_DIR)/kernel.elf | $(ISO_DIR)
	cp $(BUILD_DIR)/kernel.elf $(ISO_DIR)/boot/kernel.elf
	printf 'set timeout=3\nset default=0\nset gfxpayload=text\nterminal_output console\nmenuentry \"Curls\" {\n  terminal_output console\n  multiboot2 /boot/kernel.elf\n  boot\n}\n' > $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO_IMG) $(ISO_DIR)

$(IMG): $(USER_BINARIES) | $(BUILD_DIR)
	dd if=/dev/zero of=$(IMG) bs=1M count=64
	mformat -i $(IMG) -F ::
	mmd -i $(IMG) ::/BIN
	mmd -i $(IMG) ::/ETC
	mmd -i $(IMG) ::/TMP
	mcopy -i $(IMG) user/hello/hello.elf ::/BIN/HELLO.ELF
	mcopy -i $(IMG) user/argtest/argtest.elf ::/BIN/ARGTEST.ELF
	mcopy -i $(IMG) user/init/init.elf ::/BIN/INIT.ELF
	mcopy -i $(IMG) user/sh/sh.elf ::/BIN/SH.ELF
	mcopy -i $(IMG) user/lappy/lappy.elf ::/BIN/LAPPY.ELF
	mcopy -i $(IMG) user/ls/ls.elf ::/BIN/LS.ELF
	mcopy -i $(IMG) user/ps/ps.elf ::/BIN/PS.ELF
	mcopy -i $(IMG) user/top/top.elf ::/BIN/TOP.ELF
	mcopy -i $(IMG) user/cat/cat.elf ::/BIN/CAT.ELF
	mcopy -i $(IMG) user/touch/touch.elf ::/BIN/TOUCH.ELF
	mcopy -i $(IMG) user/clear/clear.elf ::/BIN/CLEAR.ELF
	mcopy -i $(IMG) user/sleep/sleep.elf ::/BIN/SLEEP.ELF
	mcopy -i $(IMG) user/echo/echo.elf ::/BIN/ECHO.ELF
	mcopy -i $(IMG) user/pwd/pwd.elf ::/BIN/PWD.ELF
	mcopy -i $(IMG) user/debug/debug.elf ::/BIN/DEBUG.ELF
	mcopy -i $(IMG) user/write/write.elf ::/BIN/WRITE.ELF
	mcopy -i $(IMG) user/write_a/write_a.elf ::/BIN/WRITE_A.ELF
	mcopy -i $(IMG) user/help/help.elf ::/BIN/HELP.ELF
	mcopy -i $(IMG) user/mkdir/mkdir.elf ::/BIN/MKDIR.ELF
	mcopy -i $(IMG) user/rm/rm.elf ::/BIN/RM.ELF
	mcopy -i $(IMG) user/devs/devs.elf ::/BIN/DEVS.ELF
	mcopy -i $(IMG) user/mount/mount.elf ::/BIN/MOUNT.ELF
	mcopy -i $(IMG) user/umount/umount.elf ::/BIN/UMOUNT.ELF
	mcopy -i $(IMG) user/cp/cp.elf ::/BIN/CP.ELF
	echo "Welcome to Curls OS!" > $(BUILD_DIR)/motd.txt
	mcopy -i $(IMG) $(BUILD_DIR)/motd.txt ::/ETC/MOTD
	mcopy -i $(IMG) user/sh/test.sh ::/ETC/TEST.SH
	mcopy -i $(IMG) user/sh/verify_sh.sh ::/ETC/VERIFY.SH
	mcopy -i $(IMG) user/sh/verify_for.sh ::/ETC/V_FOR.SH
	mcopy -i $(IMG) user/sh/test_usb.sh ::/ETC/TEST_USB.SH
	mcopy -i $(IMG) user/sh/test_cp.sh ::/ETC/TEST_CP.SH
	rm $(BUILD_DIR)/motd.txt
	mdir -i $(IMG) ::/BIN
	mdir -i $(IMG) ::/ETC

# USB: virtual FAT32 flash drive image + EHCI controller
FLASH_IMG = $(BUILD_DIR)/flash.img
$(FLASH_IMG):
	dd if=/dev/zero of=$(FLASH_IMG) bs=1M count=32 2>/dev/null
	mkfs.vfat -F 32 $(FLASH_IMG) >/dev/null
	@echo "Created 32MB FAT32 flash drive image: $(FLASH_IMG)"

QEMU_USB = -device usb-ehci,id=ehci \
	-drive if=none,id=usbflash,file=$(FLASH_IMG),format=raw \
	-device usb-storage,bus=ehci.0,drive=usbflash

run: $(BUILD_DIR)/os-image.bin $(IMG) $(FLASH_IMG)
	qemu-system-i386 \
	-fda $(BUILD_DIR)/os-image.bin \
	-hda $(IMG) \
	-boot a \
	-m 256 \
	$(QEMU_USB)

run-nox: $(BUILD_DIR)/os-image.bin $(IMG) $(FLASH_IMG) | $(LOG_DIR)
	bash scripts/run_qemu.sh \
	-fda $(BUILD_DIR)/os-image.bin \
	-hda $(IMG) \
	-boot a \
	-m 256 \
	-nographic 2>&1 | tee $(LOG_DIR)/qemu.log

# Run with debugging - pauses on triple fault instead of reboot
run-debug: $(BUILD_DIR)/os-image.bin $(IMG) $(FLASH_IMG) | $(LOG_DIR)
	qemu-system-i386 \
	-fda $(BUILD_DIR)/os-image.bin \
	-hda $(IMG) \
	-boot a \
	-m 256 \
	$(QEMU_USB) \
	-d cpu_reset,int 2>&1 | tee $(LOG_DIR)/qemu.log


# Open the connection to qemu and load our kernel-object file with symbols
debug: $(BUILD_DIR)/os-image.bin $(BUILD_DIR)/kernel.elf $(IMG) | $(LOG_DIR)
	qemu-system-i386 \
	-s \
	-fda $(BUILD_DIR)/os-image.bin \
	-hda $(IMG) \
	-boot a \
	-m 256 \
	-d guest_errors,int 2>&1 | tee $(LOG_DIR)/qemu-debug.log &
	${GDB} -ex "target remote localhost:1234" -ex "symbol-file $(BUILD_DIR)/kernel.elf"

iso: $(ISO_IMG)

run-grub: iso $(IMG) $(FLASH_IMG) | $(LOG_DIR)
	qemu-system-i386 \
	-cdrom $(ISO_IMG) \
	-drive file=$(IMG),format=raw,if=ide,index=0,media=disk \
	-boot d \
	-m 256 \
	-vga std \
	-serial file:$(LOG_DIR)/qemu-grub-serial.log \
	-d guest_errors,int,cpu_reset -D $(LOG_DIR)/qemu-grub-qemu.log \
	$(QEMU_USB)

run-grub-nox: iso $(IMG) $(FLASH_IMG) | $(LOG_DIR)
	bash scripts/run_qemu.sh \
	-cdrom $(ISO_IMG) \
	-drive file=$(IMG),format=raw,if=ide,index=0,media=disk \
	-boot d \
	-m 256 \
	-nographic \
	-d guest_errors,int,cpu_reset -D $(LOG_DIR)/qemu-grub-qemu-nox.log 2>&1 | tee $(LOG_DIR)/qemu-grub-serial-nox.log

# Run minimal 64-bit verification kernel
run-grub64-verify: $(BUILD_DIR)/kernel64_verify.elf | $(LOG_DIR) $(ISO_DIR)
	cp $(BUILD_DIR)/kernel64_verify.elf $(ISO_DIR)/boot/kernel.elf
	printf 'set timeout=0\nset default=0\nmenuentry \"Curls x64 Verify\" {\n  multiboot2 /boot/kernel.elf\n  boot\n}\n' > $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO_IMG) $(ISO_DIR)
	qemu-system-x86_64 -cdrom $(ISO_IMG) -boot d -m 256 -nographic -serial file:$(LOG_DIR)/qemu-verify-serial.log

run-grub64-verify-debug: build/kernel64_verify.elf | $(LOG_DIR) $(ISO_DIR)
	cp $(BUILD_DIR)/kernel64_verify.elf $(ISO_DIR)/boot/kernel.elf
	printf 'set timeout=0\nset default=0\nmenuentry \"Curls x64 Verify Debug\" {\n  multiboot2 /boot/kernel.elf\n  boot\n}\n' > $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO_IMG) $(ISO_DIR)
	qemu-system-x86_64 -cdrom $(ISO_IMG) -boot d -m 256 -nographic -serial mon:stdio -d int,cpu_reset -D $(LOG_DIR)/qemu-verify-debug.log

live-usb: iso
	sudo FORCE=$(FORCE) bash scripts/make_live_usb.sh $(ISO_IMG)

live-usb-dev: iso
	@if [ -z "$(DEV)" ]; then echo "Usage: make live-usb-dev DEV=/dev/sdX [FORCE=1]"; exit 2; fi
	sudo FORCE=$(FORCE) bash scripts/make_live_usb.sh $(ISO_IMG) $(DEV)

# Convenience alias - build the default target
all: $(BUILD_DIR)/os-image.bin $(IMG)

# Generic rules for wildcards
# To make an object, always compile from its .c
%.o: %.c ${HEADERS}
	${CC} ${CFLAGS} -c $< -o $@

%.o: %.asm
	nasm $< -f elf -o $@

# User library objects
user/lib/uabi_syscalls.o: user/lib/uabi_syscalls.s
	nasm $< -f elf -o $@

user/lib/ulib.o: user/lib/ulib.c user/lib/ulib.h
	$(CC) $(CFLAGS) -c $< -o $@

user/lib/syscall.o: user/lib/syscall.s
	nasm -f elf32 $< -o $@

# Program specific rules
user/hello/hello.elf: user/hello/hello.o user/lib/syscall.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/hello/hello.o user/lib/syscall.o

user/argtest/argtest.elf: user/argtest/argtest.o user/lib/syscall.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/argtest/argtest.o user/lib/syscall.o

user/init/init.elf: user/init/init.o user/lib/syscall.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/init/init.o user/lib/syscall.o

user/sh/sh.elf: user/sh/sh.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/sh/sh.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/lappy/lappy.o: user/lappy/lappy.c
	$(CC) $(CFLAGS) -c $< -o $@

user/lappy/lappy.elf: user/lappy/lappy.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/lappy/lappy.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/argtest/argtest.o: user/argtest/argtest.c
	$(CC) $(CFLAGS) -c $< -o $@

user/init/init.o: user/init/init.c
	$(CC) $(CFLAGS) -c $< -o $@

user/sh/sh.o: user/sh/sh.c
	$(CC) $(CFLAGS) -c $< -o $@

user/hello/hello.o: user/hello/hello.c
	$(CC) $(CFLAGS) -c $< -o $@

user/ls/ls.o: user/ls/ls.c
	$(CC) $(CFLAGS) -c $< -o $@

user/ls/ls.elf: user/ls/ls.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/ls/ls.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/ps/ps.o: user/ps/ps.c
	$(CC) $(CFLAGS) -c $< -o $@

user/ps/ps.elf: user/ps/ps.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/ps/ps.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/top/top.o: user/top/top.c
	$(CC) $(CFLAGS) -c $< -o $@

user/top/top.elf: user/top/top.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/top/top.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/cat/cat.o: user/cat/cat.c
	$(CC) $(CFLAGS) -c $< -o $@

user/cat/cat.elf: user/cat/cat.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/cat/cat.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/touch/touch.o: user/touch/touch.c
	$(CC) $(CFLAGS) -c $< -o $@

user/touch/touch.elf: user/touch/touch.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/touch/touch.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/cp/cp.o: user/cp/cp.c
	$(CC) $(CFLAGS) -c $< -o $@

user/cp/cp.elf: user/cp/cp.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/cp/cp.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/clear/clear.o: user/clear/clear.c
	$(CC) $(CFLAGS) -c $< -o $@

user/clear/clear.elf: user/clear/clear.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/clear/clear.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/sleep/sleep.o: user/sleep/sleep.c
	$(CC) $(CFLAGS) -c $< -o $@

user/sleep/sleep.elf: user/sleep/sleep.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/sleep/sleep.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/echo/echo.o: user/echo/echo.c
	$(CC) $(CFLAGS) -c $< -o $@

user/echo/echo.elf: user/echo/echo.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/echo/echo.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/pwd/pwd.o: user/pwd/pwd.c
	$(CC) $(CFLAGS) -c $< -o $@

user/pwd/pwd.elf: user/pwd/pwd.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/pwd/pwd.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/debug/debug.o: user/debug/debug.c
	$(CC) $(CFLAGS) -c $< -o $@

user/debug/debug.elf: user/debug/debug.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/debug/debug.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/write/write.o: user/write/write.c
	$(CC) $(CFLAGS) -c $< -o $@

user/write/write.elf: user/write/write.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/write/write.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/write_a/write_a.o: user/write_a/write_a.c
	$(CC) $(CFLAGS) -c $< -o $@

user/write_a/write_a.elf: user/write_a/write_a.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/write_a/write_a.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/help/help.o: user/help/help.c
	$(CC) $(CFLAGS) -c $< -o $@

user/help/help.elf: user/help/help.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/help/help.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/mkdir/mkdir.o: user/mkdir/mkdir.c
	$(CC) $(CFLAGS) -c $< -o $@

user/mkdir/mkdir.elf: user/mkdir/mkdir.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/mkdir/mkdir.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/rm/rm.o: user/rm/rm.c
	$(CC) $(CFLAGS) -c $< -o $@

user/rm/rm.elf: user/rm/rm.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/rm/rm.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/devs/devs.o: user/devs/devs.c
	$(CC) $(CFLAGS) -c $< -o $@

user/devs/devs.elf: user/devs/devs.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/devs/devs.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/mount/mount.o: user/mount/mount.c
	$(CC) $(CFLAGS) -c $< -o $@

user/mount/mount.elf: user/mount/mount.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/mount/mount.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/umount/umount.o: user/umount/umount.c
	$(CC) $(CFLAGS) -c $< -o $@

user/umount/umount.elf: user/umount/umount.o user/lib/uabi_syscalls.o user/lib/ulib.o user/lib/user.ld
	ld -m elf_i386 -o $@ -T user/lib/user.ld user/umount/umount.o user/lib/uabi_syscalls.o user/lib/ulib.o

user/nano/nano.o: user/nano/nano.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(LOG_DIR)
	find . -name "*.o" -o -name "*.o64" -delete
	find . -name "*.elf" -not -path "./.git/*" -delete
	rm -f kernel/modules/fs_initrd/initrd.bin
	rm -f kernel/modules/fs_initrd/initrd_data.c
