
# ---------------------------------
# jdy-os UEFI Bootloader Makefile
# Clang + LLD (COFF -> PE32+)
# ---------------------------------

CC := clang
LD := lld-link

BUILD := ../build
ESP := ../esp/EFI/BOOT

TARGET := $(ESP)/BOOTX64.EFI
OBJECT := $(BUILD)/main.obj

CFLAGS := \
    --target=x86_64-pc-windows-msvc \
    -I/usr/include/efi \
    -I/usr/include/efi/x86_64 \
    -ffreestanding \
    -fshort-wchar \
    -fno-stack-protector \
    -mno-red-zone

LDFLAGS := \
    /subsystem:efi_application \
    /entry:efi_main \
    /nodefaultlib \
    /machine:x64

all: $(TARGET)

# 1. C -> COFF object
$(OBJECT): main.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# 2. COFF -> UEFI PE32+
$(TARGET): $(OBJECT)
	mkdir -p $(ESP)
	$(LD) $(LDFLAGS) /out:$@ $<

clean:
	rm -f $(OBJECT) $(TARGET)

.PHONY: all clean
