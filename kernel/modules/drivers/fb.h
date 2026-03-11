#ifndef FB_H
#define FB_H

#include <stdint.h>
#include "../../core/boot_info.h"

typedef struct framebuffer_driver {
    uint8_t  *virt_addr;   // Linear framebuffer virtual base
    uint32_t pitch;        // Bytes per scanline
    uint32_t width;        // Pixels
    uint32_t height;       // Pixels
    uint8_t  bpp;          // Bits per pixel (we expect 24/32)
} framebuffer_driver_t;

extern framebuffer_driver_t fb_driver;

int  fb_init_from_bootinfo(const boot_framebuffer_info_t *info);
void fb_clear(uint32_t rgb);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t rgb);
void fb_put_char_cell(int col, int row, char c, uint32_t fg_rgb, uint32_t bg_rgb);

#endif

