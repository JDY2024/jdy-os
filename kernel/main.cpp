#include "framebuffer_config.h"
#include "font.h"

struct Color {
    uint8_t red, green, blue;
};

class PixelWriter {
public:
    explicit PixelWriter(const FramebufferConfig& config) : config_{config} {}

    void WritePixel(uint32_t x, uint32_t y, Color color) const
    {
        if (x >= config_.horizontal_resolution || y >= config_.vertical_resolution)
            return;

        const uint64_t index = uint64_t{y} * config_.pixels_per_scan_line + x;
        if (index >= config_.frame_buffer_size / FRAMEBUFFER_BYTES_PER_PIXEL)
            return;

        volatile auto* pixel = reinterpret_cast<volatile uint8_t*>(
            config_.frame_buffer_base + index * FRAMEBUFFER_BYTES_PER_PIXEL);
        const bool rgb = config_.pixel_format == FRAMEBUFFER_RGB;
        pixel[0] = rgb ? color.red : color.blue;
        pixel[1] = color.green;
        pixel[2] = rgb ? color.blue : color.red;
        pixel[3] = 0; // Reserved byte.
    }

    void FillRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  Color color) const
    {
        if (x >= config_.horizontal_resolution || y >= config_.vertical_resolution)
            return;
        const uint32_t available_width = config_.horizontal_resolution - x;
        const uint32_t available_height = config_.vertical_resolution - y;
        if (width > available_width) width = available_width;
        if (height > available_height) height = available_height;
        for (uint32_t dy = 0; dy < height; ++dy)
            for (uint32_t dx = 0; dx < width; ++dx)
                WritePixel(x + dx, y + dy, color);
    }

    void DrawChar(uint32_t x, uint32_t y, char c, Color color) const
    {
        if (x >= config_.horizontal_resolution || y >= config_.vertical_resolution)
            return;
        const uint64_t glyph = Glyph(c);
        for (unsigned dy = 0; dy < 16 && dy < config_.vertical_resolution - y; ++dy) {
            const uint8_t bits = GlyphRow(glyph, dy);
            for (unsigned dx = 0; dx < 8 && dx < config_.horizontal_resolution - x; ++dx)
                if (bits & (0x80 >> dx))
                    WritePixel(x + dx, y + dy, color);
        }
    }

    void DrawString(uint32_t x, uint32_t y, const char* text, Color color) const
    {
        for (; *text && x < config_.horizontal_resolution; ++text) {
            DrawChar(x, y, *text, color);
            if (config_.horizontal_resolution - x <= 8) break;
            x += 8;
        }
    }

private:
    const FramebufferConfig& config_;
};

static char* AppendDecimal(char* output, uint32_t value)
{
    char reversed[10];
    unsigned count = 0;
    do {
        reversed[count++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (count) *output++ = reversed[--count];
    return output;
}

[[noreturn]] static void Halt()
{
    for (;;) {
        asm volatile("cli; hlt");
    }
}

extern "C" [[noreturn]] void __attribute__((sysv_abi))
KernelMain(const FramebufferConfig* config)
{
    const PixelWriter writer{*config};
    writer.FillRect(0, 0, config->horizontal_resolution,
                    config->vertical_resolution, {16, 24, 40});

    constexpr Color text{235, 242, 255};
    writer.DrawString(24, 24, "JDY-OS", text);
    writer.DrawString(24, 48, "Hello from Kernel!", text);

    char resolution[22]; // Two uint32_t decimal values, 'x', and the terminator.
    char* end = AppendDecimal(resolution, config->horizontal_resolution);
    *end++ = 'x';
    end = AppendDecimal(end, config->vertical_resolution);
    *end = '\0';
    writer.DrawString(24, 72, "Framebuffer: ", text);
    writer.DrawString(128, 72, resolution, text);

    writer.FillRect(24, 108, 32, 16, {255, 100, 110});
    writer.FillRect(64, 108, 32, 16, {92, 218, 152});
    writer.FillRect(104, 108, 32, 16, {92, 164, 255});
    Halt();
}
