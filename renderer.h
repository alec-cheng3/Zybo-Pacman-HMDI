#ifndef RENDERER_H
#define RENDERER_H

#include "xil_types.h"

#define RENDERER_MAX_FRAME (1920*1080*3)
#define RENDERER_STRIDE (1920*3)

/*
 * Expose only the necessary API in the header
 */

void renderer_initialize();

//draws pixel to the current frame
void renderer_draw_pixel(u32 x, u32 y, u8 r, u8 g, u8 b);
//draws row of grayscale pixels to the current frame (very fast)
void renderer_draw_grey_row(u32 x, u32 y, u32 width, u8 grey);

/*
 * 1. Flushes the cache for the current frame causing the dirty pixels to be written to the VDMA
 * 2. Sets Display Control's frame to current frame
 * 3. Advances the current frame to the next one
 * 4. Clears the new current frame by setting every pixel to a greyscale color
 */
void renderer_render(u8 grey);

void renderer_oscillate_test();
void renderer_moving_box_test();

#endif /* RENDERER_H */
