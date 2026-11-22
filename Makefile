.PHONY: all clean run debug help

# Shared by normal execution and the manually operated GDB session.
QEMU := qemu-system-x86_64
QEMU_FLAGS := \
  -machine q35 \
  -m 512M \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \
  -drive if=none,id=esp,format=raw,file=fat:rw:esp \
  -device virtio-blk-pci,drive=esp \
  -vga std

all:
	$(MAKE) -C bootloader
	$(MAKE) -C kernel
	cp build/kernel.elf esp/kernel.elf

clean:
	$(MAKE) -C bootloader clean
	$(MAKE) -C kernel clean
	rm -f esp/kernel.elf

run: all
	$(QEMU) $(QEMU_FLAGS)

debug: all
	@echo 'GDB: gdb -q build/kernel.elf'
	@echo 'Then: target remote localhost:1234 (CPU is paused until continue)'
	$(QEMU) $(QEMU_FLAGS) -S -gdb tcp:127.0.0.1:1234

help:
	@echo 'make        Build Bootloader and Kernel, and copy Kernel to ESP'
	@echo 'make all    Same as make'
	@echo 'make run    Build and start QEMU normally'
	@echo 'make debug  Build and start QEMU paused, with GDB on localhost:1234'
	@echo 'make clean  Remove Bootloader and Kernel build outputs'
	@echo 'make help   Show this help'
	@echo 'GDB: gdb -q build/kernel.elf -> target remote localhost:1234'
	@echo 'Manual: docs/step2-3-gdb-manual.md'
