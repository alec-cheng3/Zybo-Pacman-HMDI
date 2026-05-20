

#include <pacman_game.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "platform.h"
#include "xil_printf.h"
#include "xtime_l.h"
#include "sleep.h"

#include "renderer.h"

//microseconds per count (1 second = 1,000,000 microseconds)
//COUNTS_PER_SECOND is 333,333,343 so us_per_count is around .003
const double us_per_count = 1000000.0 / COUNTS_PER_SECOND;

void timer_test(){
	//The PMU's Global Counter increments every 2 CPU cycles
	//XTime_Get_Time() just reads from the Global Counter
	XTime time_start, time_end, time_diff;

	//ms = millisecond (1 / 1,000 s)
	//us = microsecond (1 / 1,000,000 s)
	double elapsed_ms, elapsed_us;

	//IMPORTANT: xil_printf() does not support floats or longs
	//	use printf() for more complex prints
	printf("COUNTS_PER_SECOND: %llu\n\r", (long long unsigned int)(COUNTS_PER_SECOND));
	//(us / count) = (1 million us per s) * (seconds / count)
	printf("us_per_count: %f\n\r", us_per_count);

	xil_printf("Record time_start\n\r");
	XTime_GetTime(&time_start);

	//Global Timer Counter still counts while sleeping
	print("Sleeping for 1 million microseconds\n\r");
	usleep(1000000);
	print("Wake up!\n\r");

	xil_printf("Record time_end\n\r");
	XTime_GetTime(&time_end);
	printf("Time Start: %llu\n\r", time_start);
	printf("Time End: %llu\n\r", time_end);

	time_diff = time_end - time_start;
	printf("Time Elapsed: %llu\n\r", time_diff);

	elapsed_us = time_diff * us_per_count;
	elapsed_ms = elapsed_us / 1000.0;
	printf("Elapsed us: %f\n\r", elapsed_us);
	printf("Elapsed ms: %f\n\r", elapsed_ms);
}

void renderer_test(){
	renderer_initialize();
//	renderer_oscillate_test();
	renderer_moving_box_test();
}

int main() {

	//must initialize platform to get xtime to work
    init_platform();

    xil_printf("\n\r==================== START ====================\n\r");
    xil_printf("Hello World\n\r");
    xil_printf("Successfully ran Hello World application\n\r");

    xil_printf("Initializing Renderer\n\r");
    renderer_initialize();
    xil_printf("\n\rRunning Pacman\n\r");
    pacman_game_run();

    xil_printf("\n\r");

    cleanup_platform();
    return 0;
}
//#include "platform.h"
//#include "xil_printf.h"
//#include "sleep.h"
//#include "xil_types.h"
//
//#define BUTTON_BASE 0x41210000
//#define BUTTON_DATA (*(volatile u32 *)(BUTTON_BASE + 0x0))
//#define BUTTON_TRI  (*(volatile u32 *)(BUTTON_BASE + 0x4))
//
//int main()
//{
//    init_platform();
//
//    xil_printf("\n\rBUTTON TEST STARTED\n\r");
//
//    // Force AXI GPIO channel 1 to input mode
//    BUTTON_TRI = 0xF;
//
//    while (1) {
//        u32 buttons = BUTTON_DATA & 0xF;
//        xil_printf("buttons = 0x%lx\n\r", buttons);
//        usleep(250000);
//    }
//
//    cleanup_platform();
//    return 0;
//}
