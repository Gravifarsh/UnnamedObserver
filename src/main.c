/*
 * This file is part of the µOS++ distribution.
 *   (https://github.com/micro-os-plus)
 * Copyright (c) 2014 Liviu Ionescu.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

// ----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stm32f4xx_hal.h>

#include "diag/Trace.h"
#include "FreeRTOS.h"
#include "task.h"

#include "EMAPConfig.h"
#include "EMAP_Task_IMU.h"
#include "EMAP_Task_DATA.h"
#include "gps_nmea.h"

//	параметры GPS_task
#define GPS_TASK_STACK_SIZE	(60*configMINIMAL_STACK_SIZE)
static StackType_t	_GPSTaskStack[GPS_TASK_STACK_SIZE];
static StaticTask_t	_GPSTaskObj;

//	параметры DATA_task
#define DATA_TASK_STACK_SIZE	(60*configMINIMAL_STACK_SIZE)
static StackType_t	_DATATaskStack[DATA_TASK_STACK_SIZE];
static StaticTask_t	_DATATaskObj;

//	параметры IMU_task
#define IMU_TASK_STACK_SIZE (60*configMINIMAL_STACK_SIZE)
static StackType_t	_IMUTaskStack[IMU_TASK_STACK_SIZE];
static StaticTask_t	_IMUTaskObj;

I2C_HandleTypeDef 	i2c_IMU_1;
I2C_HandleTypeDef 	i2c_IMU_2;
SPI_HandleTypeDef	spi_nRF24L01;
I2C_HandleTypeDef 	i2c_IMU_2;
rscs_bmp280_descriptor_t * IMU_bmp280_1;
rscs_bmp280_descriptor_t * IMU_bmp280_2;

data_raw_BMP280_t 	data_raw_BMP280_1;
data_raw_BMP280_t 	data_raw_BMP280_2;
data_MPU9255_t 		data_MPU9255_isc;
data_MPU9255_t 		data_MPU9255_1;
data_MPU9255_t 		data_MPU9255_2;
data_BMP280_t 		data_BMP280_1;
data_BMP280_t 		data_BMP280_2;
data_GPS_t			data_GPS;
system_state_t 		system_state;
system_state_zero_t system_state_zero;

data_MPU9255_t 	data_prev_MPU9255_1;
data_MPU9255_t 	data_prev_MPU9255_isc;
data_MPU9255_t 	data_prev_MPU9255_2;
system_state_t 	system_prev_state;

// ----------------------------------------------------------------------------
//
// Standalone STM32F4 empty sample (trace via DEBUG).
//
// Trace support is enabled by adding the TRACE macro definition.
// By default the trace messages are forwarded to the DEBUG output,
// but can be rerouted to any device or completely suppressed, by
// changing the definitions required in system/src/diag/trace_impl.c
// (currently OS_USE_TRACE_ITM, OS_USE_TRACE_SEMIHOSTING_DEBUG/_STDOUT).
//

// ----- main() ---------------------------------------------------------------

// Sample pragmas to cope with warnings. Please note the related line at
// the end of this function, used to pop the compiler diagnostics status.
//#pragma GCC optimize("Ofast, unroll-all-loops")
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wreturn-type"


void uarts_test() {
	UART_HandleTypeDef huart4;
	memset(&huart4, 0x00, sizeof(huart4));

	huart4.Instance = UART4;
	huart4.Init.BaudRate = 38400;
	huart4.Init.Mode = UART_MODE_TX_RX;
	huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart4.Init.OverSampling = UART_OVERSAMPLING_8;
	huart4.Init.Parity = UART_PARITY_NONE;
	huart4.Init.StopBits = UART_STOPBITS_1;
	huart4.Init.WordLength = UART_WORDLENGTH_8B;

	HAL_UART_Init(&huart4);

	char msg[100];

	char tdc[] = "_tdcinit;";
	HAL_UART_Transmit(&huart4, (uint8_t*)tdc, sizeof(tdc), 50);

	memset(msg, 0x00, sizeof(msg));
	HAL_UART_Receive(&huart4, (uint8_t*)msg, sizeof(msg) - 1, 50);

	trace_printf("%s\n", msg);

	char meas[] = "_meas;";
	HAL_UART_Transmit(&huart4, (uint8_t*)meas, sizeof(meas), 50);

	memset(msg, 0x00, sizeof(msg));
	HAL_UART_Receive(&huart4, (uint8_t*)msg, sizeof(msg) - 1, 50);

	trace_printf("%s\n", msg);

	UART_HandleTypeDef huart5;
	memset(&huart5, 0x00, sizeof(huart5));

	huart5.Instance = UART5;
	huart5.Init.BaudRate = 9600;
	huart5.Init.Mode = UART_MODE_TX_RX;
	huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart5.Init.OverSampling = UART_OVERSAMPLING_8;
	huart5.Init.Parity = UART_PARITY_NONE;
	huart5.Init.StopBits = UART_STOPBITS_1;
	huart5.Init.WordLength = UART_WORDLENGTH_8B;

	HAL_UART_Init(&huart5);

	memset(msg, 0x00, sizeof(msg));
	HAL_UART_Receive(&huart5, (uint8_t*)msg, sizeof(msg) - 1, 1000);

	trace_printf("%s\n", msg);

	HAL_UART_DeInit(&huart4);
	HAL_UART_DeInit(&huart5);
}

#include "lidar.h"

void lidar_test() {
	UART_HandleTypeDef huart4;
	memset(&huart4, 0x00, sizeof(huart4));

	huart4.Instance = UART4;
	huart4.Init.BaudRate = 38400;
	huart4.Init.Mode = UART_MODE_TX_RX;
	huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart4.Init.OverSampling = UART_OVERSAMPLING_8;
	huart4.Init.Parity = UART_PARITY_NONE;
	huart4.Init.StopBits = UART_STOPBITS_1;
	huart4.Init.WordLength = UART_WORDLENGTH_8B;

	HAL_UART_Init(&huart4);

	lidar_t lidar;
	lidar.huart = &huart4;

	trace_printf("%d\n", lidar_tdcInit(&lidar));

	trace_printf("%s\n", lidar.rxbuffer);

	uint32_t dummy;
	for(;;) {
		trace_printf("%d\n", lidar_meas(&lidar, &dummy));

		trace_printf("%s\n", lidar.rxbuffer);
	}


	/*
	trace_printf("%d\n", lidar_setRepIntFreq(&lidar, 100));

	trace_printf("%s\n", lidar.rxbuffer);


	trace_printf("%d\n", lidar_start(&lidar));

	trace_printf("%s\n", lidar.rxbuffer);

	HAL_Delay(100);

	uint32_t min, mid, max;
	trace_printf("%d\n", lidar_getRes(&lidar, &min, &mid, &max));

	trace_printf("%s\n", lidar.rxbuffer);

	trace_printf("%d\n", lidar_stop(&lidar));

	trace_printf("%s\n", lidar.rxbuffer);

	HAL_UART_DeInit(&huart4);
	*/
}

int
main(int argc, char* argv[])
{
	/* FIRERISER */
	GPIO_InitTypeDef gpio;
	__HAL_RCC_GPIOA_CLK_ENABLE();
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_PULLDOWN;
	gpio.Speed = GPIO_SPEED_FREQ_HIGH;
	gpio.Pin = GPIO_PIN_0;
	HAL_GPIO_Init(GPIOA, &gpio);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);

	/* FILL WITH ZEROS */
	memset(&data_raw_BMP280_1,	0x00, sizeof(data_raw_BMP280_1));
	memset(&data_BMP280_1, 		0x00, sizeof(data_BMP280_1));
	memset(&data_MPU9255_1,		0x00, sizeof(data_MPU9255_1));
	memset(&data_MPU9255_isc,	0x00, sizeof(data_MPU9255_isc));
	memset(&data_raw_BMP280_2,	0x00, sizeof(data_raw_BMP280_2));
	memset(&data_BMP280_2, 		0x00, sizeof(data_BMP280_2));
	memset(&data_MPU9255_2,		0x00, sizeof(data_MPU9255_2));
	memset(&data_GPS,			0x00, sizeof(data_GPS));
	memset(&system_state,		0x00, sizeof(system_state));
	memset(&system_state_zero,	0x00, sizeof(system_state_zero));

	memset(&data_prev_MPU9255_1, 	0x00, sizeof(data_prev_MPU9255_1));
	memset(&data_prev_MPU9255_2, 	0x00, sizeof(data_prev_MPU9255_2));
	memset(&data_prev_MPU9255_isc,	0x00, sizeof(data_prev_MPU9255_isc));
	memset(&system_prev_state, 	0x00, sizeof(system_prev_state));

	/* CREATING TASKS */
	//xTaskCreateStatic(DATA_Task,	"DATA",	DATA_TASK_STACK_SIZE,		NULL, 1, _DATATaskStack, 	&_DATATaskObj);
	xTaskCreateStatic(IMU_Task, "IMU",	IMU_TASK_STACK_SIZE, 	NULL, 1, _IMUTaskStack,	&_IMUTaskObj);
	//xTaskCreateStatic(RF_Task, 	"RF", RF_TASK_STACK_SIZE, 	NULL, 1, _RFTaskStack, 	&_RFTaskObj);
	//xTaskCreateStatic(GPS_Task, "GPS",	GPS_TASK_STACK_SIZE, 	NULL, 1, _GPSTaskStack,	&_GPSTaskObj);

	/* CALLING INITS */
	//DATA_Init();

	//SD_Init();

	IMU_Init();
	//HAL_Delay(300);

	//uarts_test();

	//nRF_Init();
	//HAL_Delay(300);

	//GPS_Init();
	//HAL_Delay(300);

	//TSL_Init();

	/* STARTING */
	vTaskStartScheduler();
}

#pragma GCC diagnostic pop

// ----------------------------------------------------------------------------
