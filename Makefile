.PHONY: all clean run

all:
	$(MAKE) -C bootloader
	$(MAKE) -C kernel
	cp build/kernel.elf esp/kernel.elf

clean:
	$(MAKE) -C bootloader clean
	$(MAKE) -C kernel clean
	rm -f esp/kernel.elf

run: all
	qemu-system-x86_64 \
	  -machine q35 \
	  -m 512M \
	  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
	  -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \
	  -drive if=none,id=esp,format=raw,file=fat:rw:esp \
	  -device virtio-blk-pci,drive=esp \
	  -vga std
