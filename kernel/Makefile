CXX := clang++
LD := ld.lld

BUILD := ../build
OBJECT := $(BUILD)/kernel.o
TARGET := $(BUILD)/kernel.elf

CXXFLAGS := \
    --target=x86_64-unknown-none-elf \
    -std=c++20 -O0 -g \
    -ffreestanding \
    -fno-exceptions -fno-rtti \
    -fno-stack-protector \
    -fno-pic -fno-pie \
    -mno-red-zone -mgeneral-regs-only \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -Wall -Wextra

LDFLAGS := \
    -m elf_x86_64 \
    -static --no-dynamic-linker \
    -z max-page-size=0x1000 \
    -T linker.ld

.PHONY: all clean

all: $(TARGET)

$(OBJECT): main.cpp Makefile | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJECT) linker.ld Makefile
	$(LD) $(LDFLAGS) -o $@ $(OBJECT)

$(BUILD):
	mkdir -p $@

clean:
	rm -f $(OBJECT) $(TARGET)
