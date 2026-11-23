#ifndef JDY_FRAMEBUFFER_CONFIG_H
#define JDY_FRAMEBUFFER_CONFIG_H

#include <stddef.h>
#include <stdint.h>

enum {
    FRAMEBUFFER_RGB = 0,
    FRAMEBUFFER_BGR = 1,
    FRAMEBUFFER_BYTES_PER_PIXEL = 4
};

typedef struct {
    uint64_t frame_buffer_base;
    uint64_t frame_buffer_size;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixels_per_scan_line;
    uint32_t pixel_format;
} FramebufferConfig;

#ifdef __cplusplus
static_assert(alignof(FramebufferConfig) == 8);
#define FRAMEBUFFER_ASSERT static_assert
#else
_Static_assert(_Alignof(FramebufferConfig) == 8, "FramebufferConfig alignment");
#define FRAMEBUFFER_ASSERT _Static_assert
#endif

FRAMEBUFFER_ASSERT(sizeof(FramebufferConfig) == 32, "FramebufferConfig size");
FRAMEBUFFER_ASSERT(offsetof(FramebufferConfig, frame_buffer_base) == 0, "base offset");
FRAMEBUFFER_ASSERT(offsetof(FramebufferConfig, frame_buffer_size) == 8, "size offset");
FRAMEBUFFER_ASSERT(offsetof(FramebufferConfig, horizontal_resolution) == 16, "width offset");
FRAMEBUFFER_ASSERT(offsetof(FramebufferConfig, vertical_resolution) == 20, "height offset");
FRAMEBUFFER_ASSERT(offsetof(FramebufferConfig, pixels_per_scan_line) == 24, "stride offset");
FRAMEBUFFER_ASSERT(offsetof(FramebufferConfig, pixel_format) == 28, "format offset");
#undef FRAMEBUFFER_ASSERT

typedef void (__attribute__((sysv_abi)) *KernelEntry)(const FramebufferConfig *);

#endif
