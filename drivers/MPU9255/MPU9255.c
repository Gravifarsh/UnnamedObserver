/*
 * MPU9255.c
 *
 *  Created on: 21 янв. 2017 г.
 *      Author: developer
 */

//#include <stdio.h>
#include <math.h>

#include <stm32f4xx_hal.h>
#include "diag/Trace.h"

//#include <sofa.h>

#include "FreeRTOS.h"
//#include "task.h"

//#include "kinematic_unit.h"
//#include "MadgwickAHRS.h"
#include "MPU9255.h"
//#include "state.h"
#include "EMAPConfig.h"
//#include "UNICS_bmp280.h"


void iauPmp(float a[3], float b[3], float amb[3]);
void iauCp(float p[3], float c[3]);
void iauRxp(float r[3][3], float p[3], float rp[3]);


static float magnASA[3];


int mpu9255_readRegister(I2C_HandleTypeDef * hi2c, mpu9255_address_t address, uint8_t reg_address, uint8_t * dataRead, uint8_t count)
{
	return HAL_I2C_Mem_Read(hi2c, address, reg_address, I2C_MEMADD_SIZE_8BIT, dataRead, count, 0xFF);
}

int mpu9255_writeRegister(I2C_HandleTypeDef * hi2c, mpu9255_address_t address, uint8_t reg_address, uint8_t dataWrite)
{
	return HAL_I2C_Mem_Write(hi2c, address, reg_address, I2C_MEMADD_SIZE_8BIT, &dataWrite, 1, 0xFF);
}

int mpu9255_rewriteRegister(I2C_HandleTypeDef * hi2c, mpu9255_address_t address, uint8_t reg_address, uint8_t dataWrite)
{
	int error = 0;
	uint8_t regData = 0x00;
	PROCESS_ERROR(mpu9255_readRegister(hi2c, address, reg_address, &regData, 1));


	uint8_t regData_new = (regData | dataWrite);
	return HAL_I2C_Mem_Write(hi2c, address, reg_address, I2C_MEMADD_SIZE_8BIT, &regData_new, 1, 0xFF);

end:
	return error;
}


int mpu9255_init(I2C_HandleTypeDef* hi2c, I2C_TypeDef * instance)
{
	int error = 0;

	if(!USE_MPU9255)
	{
		error = EMAP_ERROR_NO_USE;
		goto end;
	}

	hi2c->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	hi2c->Init.ClockSpeed = 400000;
	hi2c->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	hi2c->Init.DutyCycle = I2C_DUTYCYCLE_2;
	hi2c->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	hi2c->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

	//	TODO: УСТАНОВИТЬ РЕАЛЬНЫЙ АДРЕС
	hi2c->Init.OwnAddress1 = 0x00;
//	hi2c->Init.OwnAddress2 = GYRO_AND_ACCEL;

	hi2c->Instance = instance;
//	hi2c->Instance = I2C2;
	hi2c->Mode = HAL_I2C_MODE_MASTER;

	PROCESS_ERROR(HAL_I2C_Init(hi2c));
	HAL_Delay(400);

	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	107,	0b10000000));	//RESET
	HAL_Delay(400);

	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	25,		0b00000001));	//Sample Rate Divider
	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	26,		0b00000101));	//config (DLPF = 101)
	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	28,		(0b00000000 | (ACCEL_RANGE << 3)))); 	//accel config (rate 4g = 01)
	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	29,		0b00000100));	//accel config 2 (Fch_b = 0, DLPF = 100)
	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	35,		0b00000000));	//FIFO enable (not enabled)
	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	56,		0b00000000));	//interrupt enable (int disable = 0)
	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	106,	0b00000000));	//user control
	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	107,	0b00000001));	//power managment 1
	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	108,	0b00000000));	//power managment 2
//	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	27,		(0b00000000 | (GYRO_RANGE << 4)) ));	//gyro config (rate 500dps = 01, Fch_b = 00)
	PROCESS_ERROR(mpu9255_rewriteRegister(hi2c, GYRO_AND_ACCEL,	27,		0b000000000 | (1 << 4)));

	//  Magnetometer init
	PROCESS_ERROR(mpu9255_writeRegister(hi2c, GYRO_AND_ACCEL,	55,		0b00000010));	//режим bypass on

	PROCESS_ERROR(mpu9255_writeRegister(hi2c, COMPASS,	0x0A,   AK8963_MODE_POWER_DOWN));	// power down before entering fuse mode
	HAL_Delay(20);

	PROCESS_ERROR(mpu9255_writeRegister(hi2c, COMPASS,	0x0A,   AK8963_MODE_FUSE_ROM));		// Enter Fuse ROM access mode
	HAL_Delay(10);

	static int8_t ASA[3] = {};
	PROCESS_ERROR(mpu9255_readRegister(hi2c, COMPASS,		0x10,   (uint8_t*)ASA, 3));   //ASAX
	//  Recalc magnetometer sensitivity adjustment values to floats to store them
	taskENTER_CRITICAL();
	magnASA[0] = (float)(ASA[0] + 128) / (256);
	magnASA[1] = (float)(ASA[1] + 128) / (256);
	magnASA[2] = (float)(ASA[2] + 128) / (256);
	taskEXIT_CRITICAL();

	PROCESS_ERROR(mpu9255_writeRegister(hi2c, COMPASS,        0x0A,   AK8963_MODE_POWER_DOWN));	// power down after reading
	HAL_Delay(20);

	uint8_t state = 0;
	//	Clear status registers
	PROCESS_ERROR(mpu9255_readRegister(hi2c, COMPASS, 	0x02, &state, 1));
	PROCESS_ERROR(mpu9255_readRegister(hi2c, COMPASS, 	0x09, &state, 1));

	PROCESS_ERROR(mpu9255_writeRegister(hi2c, COMPASS,	0x0A,   AK8963_MODE_100HZ | AK8963_BIT_16_BIT));

end:
	mpu9255_writeRegister(hi2c, GYRO_AND_ACCEL, 55,     0b00000000);   		//режим bypass off
	return error;
}

static int16_t _swapBytesI16(int16_t value)
{
	uint8_t * value_ptr = (uint8_t*)&value;
	uint8_t tmp = value_ptr[0];
	value_ptr[0] = value_ptr[1];
	value_ptr[1] = tmp;

	return value;
}

int mpu9255_readIMU(I2C_HandleTypeDef * hi2c, int16_t * raw_accelData, int16_t * raw_gyroData)
{
	int error = 0;

	PROCESS_ERROR(mpu9255_readRegister(hi2c, GYRO_AND_ACCEL, 59, (uint8_t*)raw_accelData, 6));	//чтение данных с акселерометра
	PROCESS_ERROR(mpu9255_readRegister(hi2c, GYRO_AND_ACCEL, 67, (uint8_t*)raw_gyroData, 6));	//чтение данных с гироскопа

	for (int i = 0; i < 3; i++)
		raw_accelData[i] = _swapBytesI16(raw_accelData[i]);

	for (int i = 0; i < 3; i++)
		raw_gyroData[i] = _swapBytesI16(raw_gyroData[i]);

end:
	return error;
}

int mpu9255_readCompass(I2C_HandleTypeDef * hi2c, int16_t * raw_compassData)
{
	int error = 0;

	//	state of magn (ready to give data or not)
	uint8_t magn_state = 0;

	/*
	 * Bypass mode on, get ST1 register value
	 */
	PROCESS_ERROR(mpu9255_writeRegister(hi2c, GYRO_AND_ACCEL, 55, 0b00000010));	//	bypass on
	PROCESS_ERROR(mpu9255_readRegister(hi2c, COMPASS, 0x02, &magn_state, 1));

	/*
	 * Check if ST1 value bit ready is set
	 */
	if (magn_state & AK8963_DATA_READY)
	{
		magn_state = 0;

		/*
		 * Read data values and read ST2 register to prove that data values is read
		 * I found that I should read 7 bytes together
		 */
		uint8_t bytes[7] = {};
		PROCESS_ERROR(mpu9255_readRegister(hi2c, COMPASS, 0x03, bytes, 7));

		for (int i = 0; i < 3; i++)
			raw_compassData[i] = (int16_t)((bytes[2*i+1] << 8) | bytes[2*i]);

		magn_state = bytes[6];

		/*
		 * Check HOFL bit for overflow
		 */
		if (magn_state & AK8963_DATA_OVERFLOW)
		{
			for (uint8_t i = 0; i < 3; i++)
				raw_compassData[i] = 0;
		}
	}

end:
	mpu9255_writeRegister(hi2c, GYRO_AND_ACCEL, 55, 0b00000000);	//режим bypass off
	return error;
}

void mpu9255_recalcAccel(const int16_t * raw_accelData, float * accelData)
{
	float _accelData[3] = {0, 0, 0};

	int accel_range = 1;
	for (int i = 0; i < ACCEL_RANGE; i++)
		accel_range *= 2;
	float factor = accel_range * MPU9255_ACCEL_SCALE_FACTOR;

	_accelData[0] = - (float)(raw_accelData[0]) * factor;
	_accelData[1] =   (float)(raw_accelData[2]) * factor;
	_accelData[2] =   (float)(raw_accelData[1]) * factor;

	float offset_vector[3] = {X_ACCEL_OFFSET, Y_ACCEL_OFFSET, Z_ACCEL_OFFSET};
	float transform_matrix[3][3] =	{{XX_ACCEL_TRANSFORM_MATIX, XY_ACCEL_TRANSFORM_MATIX, XZ_ACCEL_TRANSFORM_MATIX},
									 {XY_ACCEL_TRANSFORM_MATIX, YY_ACCEL_TRANSFORM_MATIX, YZ_ACCEL_TRANSFORM_MATIX},
									 {XZ_ACCEL_TRANSFORM_MATIX, YZ_ACCEL_TRANSFORM_MATIX, ZZ_ACCEL_TRANSFORM_MATIX}};

	iauPmp(_accelData, offset_vector, accelData);
	iauRxp(transform_matrix, accelData, accelData);

	for (int i = 0; i < 3; i++) {
		accelData[i] = _accelData[i];
	}
}


void mpu9255_recalcGyro(const int16_t * raw_gyroData, float * gyroData)
{
	float _gyroData[3] = {0, 0, 0};

	int gyro_range = 1;
	for (int i = 0; i < GYRO_RANGE; i++)
		gyro_range *= 2;
	float factor = gyro_range * MPU9255_GYRO_SCALE_FACTOR;

	_gyroData[0] = - (float)(raw_gyroData[0]) * factor;
	_gyroData[1] =   (float)(raw_gyroData[2]) * factor;
	_gyroData[2] =   (float)(raw_gyroData[1]) * factor;

	for (int i = 0; i < 3; i++) {
		gyroData[i] = _gyroData[i];
	}

}


void mpu9255_recalcCompass(const int16_t * raw_compassData, float * compassData)
{
	//переводим систему координат магнетометра в систему координат MPU
	float raw_data[3] = {	- (float)raw_compassData[1],
							  (float)raw_compassData[2],
							- (float)raw_compassData[0]};

	/*
	 * Adjustment
	 */
	for (uint8_t i = 0; i < 3; i++)
		raw_data[i] *= magnASA[i];


	float offset_vector[3] = {X_COMPAS_OFFSET, Y_COMPAS_OFFSET, Z_COMPAS_OFFSET};
	float transform_matrix[3][3] =	{	{XX_COMPAS_TRANSFORM_MATIX, XY_COMPAS_TRANSFORM_MATIX, XZ_COMPAS_TRANSFORM_MATIX},
										{XY_COMPAS_TRANSFORM_MATIX, YY_COMPAS_TRANSFORM_MATIX, YZ_COMPAS_TRANSFORM_MATIX},
										{XZ_COMPAS_TRANSFORM_MATIX, YZ_COMPAS_TRANSFORM_MATIX, ZZ_COMPAS_TRANSFORM_MATIX}};

	iauPmp(raw_data, offset_vector, compassData);
	iauRxp(transform_matrix, compassData, compassData);

//	for (int i = 0; i < 3; i++)
//		compassData[i] = raw_data[i];
}


void iauPmp(float a[3], float b[3], float amb[3])
{
   amb[0] = a[0] - b[0];
   amb[1] = a[1] - b[1];
   amb[2] = a[2] - b[2];

   return;
}

void iauCp(float p[3], float c[3])
{
   c[0] = p[0];
   c[1] = p[1];
   c[2] = p[2];

   return;
}

void iauRxp(float r[3][3], float p[3], float rp[3])
{
   float w, wrp[3];
   int i, j;


/* Matrix r * vector p. */
   for (j = 0; j < 3; j++) {
       w = 0.0;
       for (i = 0; i < 3; i++) {
           w += r[j][i] * p[i];
       }
       wrp[j] = w;
   }

/* Return the result. */
   iauCp(wrp, rp);

   return;
}
