

#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include "xil_cache.h"
#include "sleep.h"
#include "xtime_l.h"

#include "display_ctrl/display_ctrl.h"
#include "profiler.h"

#define DEMO_PATTERN_0 0
#define DEMO_PATTERN_1 1
#define DEMO_PATTERN_2 2

//XPAR redefines
#define DYNCLK_BASEADDR 		XPAR_AXI_DYNCLK_0_S_AXI_LITE_BASEADDR
#define VDMA_ID 				XPAR_AXIVDMA_0_DEVICE_ID
#define HDMI_OUT_VTC_ID 		XPAR_V_TC_OUT_DEVICE_ID
#define HDMI_IN_VTC_ID 			XPAR_V_TC_IN_DEVICE_ID
#define HDMI_IN_GPIO_ID 		XPAR_AXI_GPIO_VIDEO_DEVICE_ID
#define HDMI_IN_VTC_IRPT_ID 	XPAR_FABRIC_V_TC_IN_IRQ_INTR
#define HDMI_IN_GPIO_IRPT_ID 	XPAR_FABRIC_AXI_GPIO_VIDEO_IP2INTC_IRPT_INTR
#define SCU_TIMER_ID 			XPAR_SCUTIMER_DEVICE_ID
#define UART_BASEADDR 			XPAR_PS7_UART_1_BASEADDR

DisplayCtrl dispCtrl;
XAxiVdma vdma;
profiler_s profiler_renderer[10];

//Framebuffers for video data
u8 frameBuf[DISPLAY_NUM_FRAMES][RENDERER_MAX_FRAME] __attribute__((aligned(0x20)));
u8 *pFrames[DISPLAY_NUM_FRAMES]; //array of pointers to the frame buffers

/*
 * The frame we are drawing to.
 * The dispCtrl will be rendering the previous frame.
 * the index range is [1:DISPLAY_NUM_FRAMES] inclusive
 * 		(frame 0 should not be touched)
 */
int current_frame_index;

void DemoPrintTest(u8 *frame, u32 width, u32 height, u32 stride, int pattern);

/*
 * Most of this code is borrowed from Digilent's video_demo.c
 * Some of the initializing steps (video capture, interrupts) are skipped
 *
 */
void renderer_initialize(){
	int Status;
	XAxiVdma_Config *vdmaConfig;
	int i;

	xil_printf("Initializing Renderer\n\r");
	/*
	 * Initialize an array of pointers to the 3 frame buffers
	 */
	for (i = 0; i < DISPLAY_NUM_FRAMES; i++){
		pFrames[i] = frameBuf[i];
	}

	/*
	 * Initialize VDMA driver
	 */
	xil_printf("Initializing VDMA driver\n\r");
	vdmaConfig = XAxiVdma_LookupConfig(VDMA_ID);
	if (!vdmaConfig)
	{
		xil_printf("No video DMA found for ID %d\r\n", VDMA_ID);
		return;
	}
	Status = XAxiVdma_CfgInitialize(&vdma, vdmaConfig, vdmaConfig->BaseAddress);
	if (Status != XST_SUCCESS)
	{
		xil_printf("VDMA Configuration Initialization failed %d\r\n", Status);
		return;
	}

	/*
	 * Initialize the Display controller and start it
	 */
	xil_printf("Initializing and Starting Display Controller\n\r");
	Status = DisplayInitialize(&dispCtrl, &vdma, HDMI_OUT_VTC_ID, DYNCLK_BASEADDR, pFrames, RENDERER_STRIDE);
	if (Status != XST_SUCCESS)
	{
		xil_printf("Display Ctrl initialization failed during demo initialization%d\r\n", Status);
		return;
	}
	Status = DisplayStart(&dispCtrl);
	if (Status != XST_SUCCESS)
	{
		xil_printf("Couldn't start display during demo initialization%d\r\n", Status);
		return;
	}

	//Skip the interrupt controller
	//Skip the Video Capture
	//Skip Callback Setup

//	xil_printf("Printing Test Pattern 1 to current frame\n\r");
//
//	profiler_start(&profiler_renderer[0]);
//
//	DemoPrintTest(
//		dispCtrl.framePtr[dispCtrl.curFrame],
//		dispCtrl.vMode.width,
//		dispCtrl.vMode.height,
//		dispCtrl.stride,
//		DEMO_PATTERN_1
//	);
//
//	profiler_end(&profiler_renderer[0]);
//	printf("DemoPrintTest time elapsed us: %lu\n\r", profiler_renderer[0].elapsed_us);

	//initialize current frame index to the frame after Display Control's current frame
	current_frame_index = dispCtrl.curFrame + 1;
	if (current_frame_index == DISPLAY_NUM_FRAMES)
		current_frame_index = 1;

	xil_printf("Initialization Complete!\n\r\n\r");
}

void renderer_draw_pixel(u32 x, u32 y, u8 r, u8 g, u8 b){
	u32 pixel_address;
	u8 *frame = pFrames[current_frame_index];

	/*
	 * - frame is a ONE-DIMENSIONAL array that contains RENDER_MAX_FRAME (1920*1080*3) bytes
	 * - the first RENDERER_STRIDE (1920*3) bytes in frame is the first row of the screen
	 * - if the video width is less than 1920, the first row will still be RENDERER_STRIDE bytes long
	 *   - for example: if the video width is 1280 pixels, the first (1280*3) bytes
	 *     will act like normal, but the bytes from (1280*3)+1 to (1920*3)
	 *     will still exist but be unused
	 */

	pixel_address = (x * 3) + (RENDERER_STRIDE * y);
	frame[pixel_address] = b;
	frame[pixel_address+1] = g;
	frame[pixel_address+2] = r;
}

//much faster than drawing each pixel individually but the color must be greyscale
void renderer_draw_grey_row(u32 x, u32 y, u32 width, u8 grey){
	u8 *frame = pFrames[current_frame_index];
	memset(frame + x*3 + RENDERER_STRIDE*y, grey, 3*width*sizeof(u8));
}

/*
 * 1. Flushes the cache for the current frame causing the dirty pixels to be written to the VDMA
 * 2. Sets Display Control's frame to current frame
 * 3. Advances the current frame to the next one
 * 4. Clears the new current frame by setting every pixel to a greyscale color
 */
void renderer_render(u8 grey){
	u8 *current_frame;

	current_frame = pFrames[current_frame_index];

	//flush the cache which somehow writes to the DMA
	Xil_DCacheFlushRange((unsigned int)current_frame, RENDERER_MAX_FRAME);
	//advance Display Controller to current frame
	DisplayChangeFrame(&dispCtrl, current_frame_index);

	//advance current frame to next one
	++current_frame_index;
	if (current_frame_index == DISPLAY_NUM_FRAMES)
		current_frame_index = 1;

	//wipe the new current frame by setting all pixels to the same greyscale color
	current_frame = pFrames[current_frame_index];
	memset(current_frame, grey, RENDERER_MAX_FRAME * sizeof(u8));
}

void renderer_oscillate_test(){
	int pattern_a = DEMO_PATTERN_1;
	int pattern_b = DEMO_PATTERN_2;
	xil_printf("Oscillating between two patterns forever:\n\r");
	while (TRUE){
		sleep(1);
		DemoPrintTest(
			dispCtrl.framePtr[dispCtrl.curFrame],
			dispCtrl.vMode.width,
			dispCtrl.vMode.height,
			dispCtrl.stride,
			pattern_a
		);
		sleep(1);
		DemoPrintTest(
			dispCtrl.framePtr[dispCtrl.curFrame],
			dispCtrl.vMode.width,
			dispCtrl.vMode.height,
			dispCtrl.stride,
			pattern_b
		);
	}
}

volatile unsigned int value;
volatile unsigned int buttons;

void renderer_moving_box_test(){
	static int box_x = 0;
	static int box_y = 0;
	static int box_w = 20;
	static int box_h = 20;
	static int box_vx = 5;
	static int box_vy = 10;

	int framerate = 60;

	int window_w = dispCtrl.vMode.width;
	int window_h = dispCtrl.vMode.height;

	u8 r = 0;
	u8 g = 255;
	u8 b = 0;

	long int frame_counter = 0;
	int debug_update_interval = 1; //interval between profiler prints in seconds

	printf("Launching moving box test\n\r");

	while (TRUE){
		//TODO: add separate profilers for the different stages
		//and print them out occasionally in the terminal
		profiler_start(&profiler_renderer[0]);

		profiler_start(&profiler_renderer[1]);


		value = *(unsigned int*)0x41210000;
//		value = *(unsigned int*)0xFFFFFFFF;
		buttons = value & 0b1111;

		if (buttons & 0b1000) box_vx = -abs(box_vx);
		if (buttons & 0b0100) box_vx = abs(box_vx);
		if (buttons & 0b0010) box_vy = -abs(box_vy);
		if (buttons & 0b0001) box_vy = abs(box_vy);

		//update box movement
		box_x += box_vx;
		if (box_x < 0)
			box_x += window_w;
		else if (box_x >= window_w)
			box_x -= window_w;

		box_y += box_vy;
		if (box_y < 0)
			box_y += window_h;
		else if (box_y >= window_h)
			box_y -= window_h;

		profiler_end(&profiler_renderer[1]);

		profiler_start(&profiler_renderer[2]);

		//draw the box
		for (int xi = box_x; xi <= box_x + box_w; ++xi){
			for (int yi = box_y; yi <= box_y + box_h; ++yi){
				renderer_draw_pixel(xi % window_w, yi % window_h, r, g, b);
			}
		}

		profiler_end(&profiler_renderer[2]);

		profiler_start(&profiler_renderer[3]);

		//push the current frame to the display and advance frame
		renderer_render(0);

		profiler_end(&profiler_renderer[3]);

		//debug profiler prints

		++frame_counter;

		if (frame_counter % (framerate * debug_update_interval) == 0){
			printf("current frame: %lu\n\r", frame_counter);
			printf("box update time: %lu us\n\r", profiler_renderer[1].elapsed_us);
			printf("box drawing time: %lu us\n\r", profiler_renderer[2].elapsed_us);
			printf("rendering time: %lu us\n\r", profiler_renderer[3].elapsed_us);
			printf("Total Time: %lu us\n\r",
					profiler_renderer[1].elapsed_us +
					profiler_renderer[2].elapsed_us +
					profiler_renderer[3].elapsed_us);
			printf("VALUE: %u\n\r", value);
			printf("BUTTONS: %u\n\r", buttons);
			printf("box_x: %d, box_y: %d\n\r", box_x, box_y);
			printf("\n\r");

		}


		profiler_end(&profiler_renderer[0]);

		//if execution time is less than 1/framerate, delay for the remainder of time
		if (profiler_renderer[0].elapsed_us < 1000000/framerate)
			usleep(1000000/framerate - profiler_renderer[0].elapsed_us);
	}
}

/*
 * Copied from Digilent's video_demo.c
 */
void DemoPrintTest(u8 *frame, u32 width, u32 height, u32 stride, int pattern)
{
	u32 xcoi, ycoi;
	u32 iPixelAddr;
	u8 wRed, wBlue, wGreen;
	u32 wCurrentInt;
	double fRed, fBlue, fGreen, fColor;
	u32 xLeft, xMid, xRight, xInt;
	u32 yMid, yInt;
	double xInc, yInc;


	switch (pattern)
	{
	case DEMO_PATTERN_0:

		xInt = width / 4; //Four intervals, each with width/4 pixels
		xLeft = xInt * 3;
		xMid = xInt * 2 * 3;
		xRight = xInt * 3 * 3;
		xInc = 256.0 / ((double) xInt); //256 color intensities are cycled through per interval (overflow must be caught when color=256.0)

		yInt = height / 2; //Two intervals, each with width/2 lines
		yMid = yInt;
		yInc = 256.0 / ((double) yInt); //256 color intensities are cycled through per interval (overflow must be caught when color=256.0)

		fBlue = 0.0;
		fRed = 256.0;
		for(xcoi = 0; xcoi < (width*3); xcoi+=3)
		{
			/*
			 * Convert color intensities to integers < 256, and trim values >=256
			 */
			wRed = (fRed >= 256.0) ? 255 : ((u8) fRed);
			wBlue = (fBlue >= 256.0) ? 255 : ((u8) fBlue);
			iPixelAddr = xcoi;
			fGreen = 0.0;
			for(ycoi = 0; ycoi < height; ycoi++)
			{

				wGreen = (fGreen >= 256.0) ? 255 : ((u8) fGreen);
				frame[iPixelAddr] = wRed;
				frame[iPixelAddr + 1] = wBlue;
				frame[iPixelAddr + 2] = wGreen;
				if (ycoi < yMid)
				{
					fGreen += yInc;
				}
				else
				{
					fGreen -= yInc;
				}

				/*
				 * This pattern is printed one vertical line at a time, so the address must be incremented
				 * by the stride instead of just 1.
				 */
				iPixelAddr += stride;
			}

			if (xcoi < xLeft)
			{
				fBlue = 0.0;
				fRed -= xInc;
			}
			else if (xcoi < xMid)
			{
				fBlue += xInc;
				fRed += xInc;
			}
			else if (xcoi < xRight)
			{
				fBlue -= xInc;
				fRed -= xInc;
			}
			else
			{
				fBlue += xInc;
				fRed = 0;
			}
		}
		/*
		 * Flush the framebuffer memory range to ensure changes are written to the
		 * actual memory, and therefore accessible by the VDMA.
		 */
		Xil_DCacheFlushRange((unsigned int) frame, RENDERER_MAX_FRAME);
		break;
	case DEMO_PATTERN_1:
	case DEMO_PATTERN_2:

		xInt = width / 7; //Seven intervals, each with width/7 pixels
		xInc = 256.0 / ((double) xInt); //256 color intensities per interval. Notice that overflow is handled for this pattern.

		fColor = 0.0;
		wCurrentInt = 1;
		for(xcoi = 0; xcoi < (width*3); xcoi+=3)
		{

			/*
			 * Just draw white in the last partial interval (when width is not divisible by 7)
			 */
			if (wCurrentInt > 7)
			{
				wRed = 255;
				wBlue = 255;
				wGreen = 255;
			}
			else
			{
				if (wCurrentInt & 0b001)
					wRed = (u8) fColor;
				else
					wRed = 0;

				if (wCurrentInt & 0b010)
					wBlue = (u8) fColor;
				else
					wBlue = 0;

				if (wCurrentInt & 0b100)
					wGreen = (u8) fColor;
				else
					wGreen = 0;
			}

			iPixelAddr = xcoi;

			for(ycoi = 0; ycoi < height; ycoi++)
			{
				frame[iPixelAddr] = wRed;
				frame[iPixelAddr + 1] = wBlue;
				frame[iPixelAddr + 2] = wGreen;

				/*
				 * This pattern is printed one vertical line at a time, so the address must be incremented
				 * by the stride instead of just 1.
				 */
				if (pattern == DEMO_PATTERN_2){
					if (xcoi > (width/2 - 50) * 3 &&
						xcoi < (width/2 + 50) * 3 &&
						ycoi > height/2 - 50 &&
						ycoi < height/2 + 50){

						frame[iPixelAddr] = 255;
						frame[iPixelAddr + 1] = 255;
						frame[iPixelAddr + 2] = 0;
					}
				}
				iPixelAddr += stride;
			}

			fColor += xInc;
			if (fColor >= 256.0)
			{
				fColor = 0.0;
				wCurrentInt++;
			}
		}
		/*
		 * Flush the framebuffer memory range to ensure changes are written to the
		 * actual memory, and therefore accessible by the VDMA.
		 */
		profiler_start(&profiler_renderer[1]);
		Xil_DCacheFlushRange((unsigned int) frame, RENDERER_MAX_FRAME);
		profiler_end(&profiler_renderer[1]);
		printf("cache flush time: %lu\n\r", profiler_renderer[1].elapsed_us);
		break;
	default :
		xil_printf("Error: invalid pattern passed to DemoPrintTest");
	}
}
