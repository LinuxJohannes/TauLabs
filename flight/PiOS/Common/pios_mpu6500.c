/**
 ******************************************************************************
 * @addtogroup PIOS PIOS Core hardware abstraction layer
 * @{
 * @addtogroup PIOS_MPU6500 MPU6500 Functions
 * @brief Deals with the hardware interface to the Six-Axis (Gyro + Accelerometer)
 * @{
 *
 * @file       pios_MPU6500.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2012.
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2014
 * @brief      MPU6500 6-axis gyro and accel chip
 * @see        The GNU Public License (GPL) Version 3
 *
 ******************************************************************************
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* Project Includes */
#include "pios.h"
#include "physical_constants.h"

#if defined(PIOS_INCLUDE_MPU6500)

/* Private constants */
#define MPU6000_WHOAMI_ID        0x68
#define MPU6500_WHOAMI_ID        0x70

/* Global Variables */

enum pios_mpu6500_dev_magic {
    pios_mpu6500_dev_MAGIC = 0x9da9bccc,
};

#define PIOS_MPU6500_MAX_QUEUESIZE 2

struct mpu6500_dev {
	uint32_t spi_id;
	uint32_t slave_num;
	enum pios_mpu60x0_range gyro_range;
	xQueueHandle gyro_queue;
#if defined(PIOS_MPU6500_ACCEL)
	enum pios_mpu60x0_accel_range accel_range;
	xQueueHandle accel_queue;
#endif /* PIOS_MPU6500_ACCEL */
	const struct pios_mpu60x0_cfg *cfg;
	volatile bool configured;
	enum pios_mpu6500_dev_magic magic;
	enum pios_mpu60x0_filter filter;
};

//! Global structure for this device device
static struct mpu6500_dev *pios_mpu6500_dev;

//! Private functions
static struct mpu6500_dev *PIOS_MPU6500_alloc(void);
static int32_t PIOS_MPU6500_Validate(struct mpu6500_dev *dev);
static void PIOS_MPU6500_Config(const struct pios_mpu60x0_cfg *cfg);
static int32_t PIOS_MPU6500_ClaimBus();
static int32_t PIOS_MPU6500_ReleaseBus();
static int32_t PIOS_MPU6500_SetReg(uint8_t address, uint8_t buffer);
static int32_t PIOS_MPU6500_GetReg(uint8_t address);
/*static int32_t PIOS_MPU6500_ReadID();*/


/**
 * @brief Allocate a new device
 */
static struct mpu6500_dev *PIOS_MPU6500_alloc(void)
{
	struct mpu6500_dev *mpu6500_dev;

	mpu6500_dev = (struct mpu6500_dev *)PIOS_malloc(sizeof(*mpu6500_dev));

	if (!mpu6500_dev) return (NULL);

	mpu6500_dev->magic = pios_mpu6500_dev_MAGIC;

	mpu6500_dev->configured = false;

#if defined(PIOS_MPU6500_ACCEL)
	mpu6500_dev->accel_queue = xQueueCreate(PIOS_MPU6500_MAX_QUEUESIZE, sizeof(struct pios_sensor_accel_data));

	if (mpu6500_dev->accel_queue == NULL) {
		vPortFree(mpu6500_dev);
		return NULL;
	}
#endif /* PIOS_MPU6500_ACCEL */

	mpu6500_dev->gyro_queue = xQueueCreate(PIOS_MPU6500_MAX_QUEUESIZE, sizeof(struct pios_sensor_gyro_data));

	if (mpu6500_dev->gyro_queue == NULL) {
		vPortFree(mpu6500_dev);
		return NULL;
	}

	return mpu6500_dev;
}

/**
 * @brief Validate the handle to the spi device
 * @returns 0 for valid device or -1 otherwise
 */
static int32_t PIOS_MPU6500_Validate(struct mpu6500_dev *dev)
{
	if (dev == NULL)
		return -1;

	if (dev->magic != pios_mpu6500_dev_MAGIC)
		return -2;

	if (dev->spi_id == 0)
		return -3;

	return 0;
}

/**
 * @brief Initialize the MPU6500 3-axis gyro sensor.
 * @return 0 for success, -1 for failure
 */
int32_t PIOS_MPU6500_Init(uint32_t spi_id, uint32_t slave_num, const struct pios_mpu60x0_cfg *cfg)
{
	pios_mpu6500_dev = PIOS_MPU6500_alloc();
	if (pios_mpu6500_dev == NULL)
		return -1;

	pios_mpu6500_dev->spi_id = spi_id;
	pios_mpu6500_dev->slave_num = slave_num;
	pios_mpu6500_dev->cfg = cfg;

	/* Configure the MPU6500 Sensor */
	PIOS_SPI_SetClockSpeed(pios_mpu6500_dev->spi_id, 100000);
	PIOS_MPU6500_Config(cfg);
	PIOS_SPI_SetClockSpeed(pios_mpu6500_dev->spi_id, 3000000); // TODO ??

	/* Set up EXTI line */
	PIOS_EXTI_Init(cfg->exti_cfg);

#if defined(PIOS_MPU6500_ACCEL)
	PIOS_SENSORS_Register(PIOS_SENSOR_ACCEL, pios_mpu6500_dev->accel_queue);
#endif /* PIOS_MPU6500_ACCEL */

	PIOS_SENSORS_Register(PIOS_SENSOR_GYRO, pios_mpu6500_dev->gyro_queue);

	return 0;
}

/**
 * @brief Initialize the MPU6500 3-axis gyro sensor
 * \return none
 * \param[in] PIOS_MPU6500_ConfigTypeDef struct to be used to configure sensor.
*
*/
static void PIOS_MPU6500_Config(const struct pios_mpu60x0_cfg *cfg)
{
#if defined(PIOS_MPU6500_SIMPLE_INIT_SEQUENCE)

	// Reset chip registers
	//if (
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_PWR_MGMT_REG, PIOS_MPU60X0_PWRMGMT_IMU_RST); // != 0)
		//return -1;

	// Give chip some time to initialize
	PIOS_DELAY_WaitmS(50);
	PIOS_WDG_Clear();

	// Reset sensors signal path
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_USER_CTRL_REG, PIOS_MPU60X0_USERCTL_GYRO_RST);
	// Give chip some time to initialize
	PIOS_DELAY_WaitmS(10);

	//Power management configuration
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_PWR_MGMT_REG, cfg->Pwr_mgmt_clk);

	// User control
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_USER_CTRL_REG, cfg->User_ctl);

	// Digital low-pass filter and scale
	// set this before sample rate else sample rate calculation will fail
	PIOS_MPU6500_SetLPF(cfg->default_filter);

	// Sample rate
	PIOS_MPU6500_SetSampleRate(cfg->default_samplerate);

	// Set the gyro scale
	PIOS_MPU6500_SetGyroRange(PIOS_MPU60X0_SCALE_500_DEG);

#if defined(PIOS_MPU6500_ACCEL)
	// Set the accel scale
	PIOS_MPU6500_SetAccelRange(PIOS_MPU60X0_ACCEL_8G);
#endif /* PIOS_MPU6500_ACCEL */

	// Interrupt configuration
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_INT_CFG_REG, cfg->interrupt_cfg); //TODO  | 0x02);

	// Interrupt enable
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_INT_EN_REG, cfg->interrupt_en);

	pios_mpu6500_dev->configured = true;
	
#else /* PIOS_MPU6500_SIMPLE_INIT_SEQUENCE */

	/* This init sequence should really be dropped in favor of something
	 * less redundant but it seems to be hard to get it running well
	 * on all different targets.
	 */

	PIOS_MPU6500_ClaimBus();
	PIOS_DELAY_WaitmS(1);
	PIOS_MPU6500_ReleaseBus();
	PIOS_DELAY_WaitmS(10);

	// Reset chip
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_PWR_MGMT_REG, 0x80 | cfg->Pwr_mgmt_clk);
	do {
		PIOS_DELAY_WaitmS(5);
	} while (PIOS_MPU6500_GetReg(PIOS_MPU60X0_PWR_MGMT_REG) & 0x80);

	PIOS_DELAY_WaitmS(25);

	// Reset chip and fifo
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_USER_CTRL_REG, 0x80 | 0x01 | 0x02 | 0x04);
	do {
		PIOS_DELAY_WaitmS(5);
	} while (PIOS_MPU5000_GetReg(PIOS_MPU60X0_USER_CTRL_REG) & 0x07);

	PIOS_DELAY_WaitmS(25);

	//Power management configuration
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_PWR_MGMT_REG, cfg->Pwr_mgmt_clk);

	// Interrupt configuration
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_INT_CFG_REG, cfg->interrupt_cfg);

	// Interrupt configuration
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_INT_EN_REG, cfg->interrupt_en);

#if defined(PIOS_MPU6500_ACCEL)
	// Set the accel scale
	PIOS_MPU6500_SetAccelRange(PIOS_MPU60X0_ACCEL_8G);
#endif

	// Digital low-pass filter and scale
	// set this before sample rate else sample rate calculation will fail
	PIOS_MPU6500_SetLPF(cfg->default_filter);

	// Sample rate
	PIOS_MPU6500_SetSampleRate(cfg->default_samplerate);

	// Set the gyro scale
	PIOS_MPU6500_SetGyroRange(PIOS_MPU60X0_SCALE_500_DEG);

	// Interrupt configuration
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_USER_CTRL_REG, cfg->User_ctl);

	//Power management configuration
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_PWR_MGMT_REG, cfg->Pwr_mgmt_clk);

	// Interrupt configuration
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_INT_CFG_REG, cfg->interrupt_cfg);

	// Interrupt configuration
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_INT_EN_REG, cfg->interrupt_en);

	pios_mpu6500_dev->configured = true;

#endif /* PIOS_MPU6500_SIMPLE_INIT_SEQUENCE */
}

/**
 * Set the gyro range and store it locally for scaling
 */
void PIOS_MPU6500_SetGyroRange(enum pios_mpu60x0_range gyro_range)
{
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_GYRO_CFG_REG, gyro_range);

	switch(gyro_range) {
	case PIOS_MPU60X0_SCALE_250_DEG:
		PIOS_SENSORS_SetMaxGyro(250);
		break;
	case PIOS_MPU60X0_SCALE_500_DEG:
		PIOS_SENSORS_SetMaxGyro(500);
		break;
	case PIOS_MPU60X0_SCALE_1000_DEG:
		PIOS_SENSORS_SetMaxGyro(1000);
		break;
	case PIOS_MPU60X0_SCALE_2000_DEG:
		PIOS_SENSORS_SetMaxGyro(2000);
		break;
	}

	pios_mpu6500_dev->gyro_range = gyro_range;
}

/**
 * Set the accel range and store it locally for scaling
 */
#if defined(PIOS_MPU6500_ACCEL)
void PIOS_MPU6500_SetAccelRange(enum pios_mpu60x0_accel_range accel_range)
{
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_ACCEL_CFG_REG, accel_range);

	pios_mpu6500_dev->accel_range = accel_range;
}
#endif /* PIOS_MPU6500_ACCEL */

/**
 * Set the sample rate in Hz by determining the nearest divisor
 * @param[in] sample rate in Hz
 */
void PIOS_MPU6500_SetSampleRate(uint16_t samplerate_hz)
{
	uint16_t filter_frequency = 8000;

	if (pios_mpu6500_dev->filter != PIOS_MPU60X0_LOWPASS_256_HZ)
		filter_frequency = 1000;

	// limit samplerate to filter frequency
	if (samplerate_hz > filter_frequency)
		samplerate_hz = filter_frequency;

	// calculate divisor, round to nearest integeter
	int32_t divisor = (int32_t)(((float)filter_frequency / samplerate_hz) + 0.5f) - 1;

	// limit resulting divisor to register value range
	if (divisor < 0)
		divisor = 0;

	if (divisor > 0xff)
		divisor = 0xff;

	PIOS_MPU6500_SetReg(PIOS_MPU60X0_SMPLRT_DIV_REG, (uint8_t)divisor);
}

/**
 * Configure the digital low-pass filter
 */
void PIOS_MPU6500_SetLPF(enum pios_mpu60x0_filter filter)
{
	PIOS_MPU6500_SetReg(PIOS_MPU60X0_DLPF_CFG_REG, filter);

	pios_mpu6500_dev->filter = filter;
}

/**
 * @brief Claim the SPI bus for the accel communications and select this chip
 * @return 0 if successful, -1 for invalid device, -2 if unable to claim bus
 */
static int32_t PIOS_MPU6500_ClaimBus()
{
	if (PIOS_MPU6500_Validate(pios_mpu6500_dev) != 0)
		return -1;

	if (PIOS_SPI_ClaimBus(pios_mpu6500_dev->spi_id) != 0)
		return -2;

	PIOS_SPI_RC_PinSet(pios_mpu6500_dev->spi_id, pios_mpu6500_dev->slave_num, 0);
	return 0;
}

/**
 * @brief Claim the SPI bus for the accel communications and select this chip
 * \param[in] pointer which receives if a task has been woken
 * @return 0 if successful, -1 for invalid device, -2 if unable to claim bus
 */
static int32_t PIOS_MPU6500_ClaimBusISR(bool *woken)
{
	if (PIOS_MPU6500_Validate(pios_mpu6500_dev) != 0)
		return -1;

	if (PIOS_SPI_ClaimBusISR(pios_mpu6500_dev->spi_id, woken) != 0)
		return -2;

	PIOS_SPI_RC_PinSet(pios_mpu6500_dev->spi_id, pios_mpu6500_dev->slave_num, 0);
	return 0;
}

/**
 * @brief Release the SPI bus for the accel communications and end the transaction
 * @return 0 if successful
 */
static int32_t PIOS_MPU6500_ReleaseBus()
{
	if (PIOS_MPU6500_Validate(pios_mpu6500_dev) != 0)
		return -1;

	PIOS_SPI_RC_PinSet(pios_mpu6500_dev->spi_id, pios_mpu6500_dev->slave_num, 1);

	return PIOS_SPI_ReleaseBus(pios_mpu6500_dev->spi_id);
}

/**
 * @brief Release the SPI bus for the accel communications and end the transaction
 * \param[in] pointer which receives if a task has been woken
 * @return 0 if successful
 */
static int32_t PIOS_MPU6500_ReleaseBusISR(bool *woken)
{
	if (PIOS_MPU6500_Validate(pios_mpu6500_dev) != 0)
		return -1;

	PIOS_SPI_RC_PinSet(pios_mpu6500_dev->spi_id, pios_mpu6500_dev->slave_num, 1);

	return PIOS_SPI_ReleaseBusISR(pios_mpu6500_dev->spi_id, woken);
}

/**
 * @brief Read a register from MPU6500
 * @returns The register value or -1 if failure to get bus
 * @param reg[in] Register address to be read
 */
static int32_t PIOS_MPU6500_GetReg(uint8_t reg)
{
	uint8_t data;

	if (PIOS_MPU6500_ClaimBus() != 0)
		return -1;

	PIOS_SPI_TransferByte(pios_mpu6500_dev->spi_id, (0x80 | reg)); // request byte
	data = PIOS_SPI_TransferByte(pios_mpu6500_dev->spi_id, 0);     // receive response

	PIOS_MPU6500_ReleaseBus();
	return data;
}

/**
 * @brief Writes one byte to the MPU6500
 * \param[in] reg Register address
 * \param[in] data Byte to write
 * \return 0 if operation was successful
 * \return -1 if unable to claim SPI bus
 * \return -2 if unable to claim i2c device
 */
static int32_t PIOS_MPU6500_SetReg(uint8_t reg, uint8_t data)
{
	if (PIOS_MPU6500_ClaimBus() != 0)
		return -1;

	if (PIOS_SPI_TransferByte(pios_mpu6500_dev->spi_id, 0x7f & reg) != 0) {
		PIOS_MPU6500_ReleaseBus();
		return -2;
	}

	if (PIOS_SPI_TransferByte(pios_mpu6500_dev->spi_id, data) != 0) {
		PIOS_MPU6500_ReleaseBus();
		return -3;
	}

	PIOS_MPU6500_ReleaseBus();

	return 0;
}

/*
 * @brief Read the identification bytes from the MPU6500 sensor
 * \return ID read from MPU6500 or -1 if failure
*/
static int32_t PIOS_MPU6500_ReadID()
{
	int32_t mpu6500_id = PIOS_MPU6500_GetReg(PIOS_MPU60X0_WHOAMI);

	if (MPU6500_id < 0)
		return -1;

	return mpu6500_id;
}

/**
 * Get the gyro scale based on the active device settings
 * @return Scale in (deg/s) / LSB
 */
static float PIOS_MPU6500_GetGyroScale()
{
	switch (pios_mpu6500_dev->gyro_range) {
	case PIOS_MPU60X0_SCALE_250_DEG:
		return 1.0f / 131.0f;
	case PIOS_MPU60X0_SCALE_500_DEG:
		return 1.0f / 65.5f;
	case PIOS_MPU60X0_SCALE_1000_DEG:
		return 1.0f / 32.8f;
	case PIOS_MPU60X0_SCALE_2000_DEG:
		return 1.0f / 16.4f;
	}

	return 0;
}

/**
 * Get the accel scale based on the active settings
 * @returns Scale in (m/s^2) / LSB
 */
#if defined(PIOS_MPU6500_ACCEL)
static float PIOS_MPU6500_GetAccelScale()
{
	switch (pios_mpu6500_dev->accel_range) {
	case PIOS_MPU60X0_ACCEL_2G:
		return GRAVITY / 16384.0f;
	case PIOS_MPU60X0_ACCEL_4G:
		return GRAVITY / 8192.0f;
	case PIOS_MPU60X0_ACCEL_8G:
		return GRAVITY / 4096.0f;
	case PIOS_MPU60X0_ACCEL_16G:
		return GRAVITY / 2048.0f;
	}

	return 0;
}
#endif /* PIOS_MPU6500_ACCEL */

/**
 * @brief Run self-test operation.
 * \return 0 if test succeeded
 * \return non-zero value if test succeeded
 */
int32_t PIOS_MPU6500_Test(void)
{
	/* Verify that ID matches (MPU6500 ID is MPU6500_WHOAMI_ID) */
	int32_t mpu6500_id = PIOS_MPU6500_ReadID();

	if (mpu6500_id < 0)
		return -1;

	if (mpu6500_id != MPU6500_WHOAMI_ID)
		return -2;

	return 0;
}

/**
* @brief IRQ Handler.  Read all the data from onboard buffer
*/
bool PIOS_MPU6500_IRQHandler(void)
{
	if (PIOS_MPU6500_Validate(pios_mpu6500_dev) != 0 || pios_mpu6500_dev->configured == false)
		return false;

	bool woken = false;

	if (PIOS_MPU6500_ClaimBusISR(&woken) != 0)
		return false;

	enum {
	    IDX_SPI_DUMMY_BYTE = 0,
	    IDX_ACCEL_XOUT_H,
	    IDX_ACCEL_XOUT_L,
	    IDX_ACCEL_YOUT_H,
	    IDX_ACCEL_YOUT_L,
	    IDX_ACCEL_ZOUT_H,
	    IDX_ACCEL_ZOUT_L,
	    IDX_TEMP_OUT_H,
	    IDX_TEMP_OUT_L,
	    IDX_GYRO_XOUT_H,
	    IDX_GYRO_XOUT_L,
	    IDX_GYRO_YOUT_H,
	    IDX_GYRO_YOUT_L,
	    IDX_GYRO_ZOUT_H,
	    IDX_GYRO_ZOUT_L,
	    BUFFER_SIZE,
	};

	uint8_t mpu6500_send_buf[BUFFER_SIZE] = { PIOS_MPU60X0_ACCEL_X_OUT_MSB | 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint8_t mpu6500_rec_buf[BUFFER_SIZE];

	if (PIOS_SPI_TransferBlock(pios_mpu6500_dev->spi_id, mpu6500_send_buf, mpu6500_rec_buf, sizeof(mpu6500_send_buf), NULL) < 0) {
		PIOS_MPU6500_ReleaseBusISR(&woken);
		return false;
	}

	PIOS_MPU6500_ReleaseBusISR(&woken);


	// Rotate the sensor to OP convention.  The datasheet defines X as towards the right
	// and Y as forward.  OP convention transposes this.  Also the Z is defined negatively
	// to our convention

#if defined(PIOS_MPU6500_ACCEL)

	// Currently we only support rotations on top so switch X/Y accordingly
	struct pios_sensor_accel_data accel_data;
	struct pios_sensor_gyro_data gyro_data;

	switch (pios_mpu6500_dev->cfg->orientation) {
	case PIOS_MPU60X0_TOP_0DEG:
		accel_data.y = (int16_t)(mpu6500_rec_buf[IDX_ACCEL_XOUT_H] << 8 | mpu6500_rec_buf[IDX_ACCEL_XOUT_L]);
		accel_data.x = (int16_t)(mpu6500_rec_buf[IDX_ACCEL_YOUT_H] << 8 | mpu6500_rec_buf[IDX_ACCEL_YOUT_L]);
		gyro_data.y  = (int16_t)(mpu6500_rec_buf[IDX_GYRO_XOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_XOUT_L]);
		gyro_data.x  = (int16_t)(mpu6500_rec_buf[IDX_GYRO_YOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_YOUT_L]);
		break;
	case PIOS_MPU60X0_TOP_90DEG:
		accel_data.y = - (int16_t)(mpu6500_rec_buf[IDX_ACCEL_YOUT_H] << 8 | mpu6500_rec_buf[IDX_ACCEL_YOUT_L]);
		accel_data.x = (int16_t)(mpu6500_rec_buf[IDX_ACCEL_XOUT_H] << 8 | mpu6500_rec_buf[IDX_ACCEL_XOUT_L]);
		gyro_data.y  = - (int16_t)(mpu6500_rec_buf[IDX_GYRO_YOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_YOUT_L]);
		gyro_data.x  = (int16_t)(mpu6500_rec_buf[IDX_GYRO_XOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_XOUT_L]);
		break;
	case PIOS_MPU60X0_TOP_180DEG:
		accel_data.y = - (int16_t)(mpu6500_rec_buf[IDX_ACCEL_XOUT_H] << 8 | mpu6500_rec_buf[IDX_ACCEL_XOUT_L]);
		accel_data.x = - (int16_t)(mpu6500_rec_buf[IDX_ACCEL_YOUT_H] << 8 | mpu6500_rec_buf[IDX_ACCEL_YOUT_L]);
		gyro_data.y  = - (int16_t)(mpu6500_rec_buf[IDX_GYRO_XOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_XOUT_L]);
		gyro_data.x  = - (int16_t)(mpu6500_rec_buf[IDX_GYRO_YOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_YOUT_L]);
		break;
	case PIOS_MPU60X0_TOP_270DEG:
		accel_data.y = (int16_t)(mpu6500_rec_buf[IDX_ACCEL_YOUT_H] << 8 | mpu6500_rec_buf[IDX_ACCEL_YOUT_L]);
		accel_data.x = - (int16_t)(mpu6500_rec_buf[IDX_ACCEL_XOUT_H] << 8 | mpu6500_rec_buf[IDX_ACCEL_XOUT_L]);
		gyro_data.y  = (int16_t)(mpu6500_rec_buf[IDX_GYRO_YOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_YOUT_L]);
		gyro_data.x  = - (int16_t)(mpu6500_rec_buf[IDX_GYRO_XOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_XOUT_L]);
		break;
	}

	gyro_data.z  = - (int16_t)(mpu6500_rec_buf[IDX_GYRO_ZOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_ZOUT_L]);
	accel_data.z = - (int16_t)(mpu6500_rec_buf[IDX_ACCEL_ZOUT_H] << 8 | mpu6500_rec_buf[IDX_ACCEL_ZOUT_L]);

	int16_t raw_temp = (int16_t)(mpu6500_rec_buf[IDX_TEMP_OUT_H] << 8 | mpu6500_rec_buf[IDX_TEMP_OUT_L]);
	float temperature = 35.0f + ((float)raw_temp + 512.0f) / 340.0f;

	// Apply sensor scaling
	float accel_scale = PIOS_MPU6500_GetAccelScale();
	accel_data.x *= accel_scale;
	accel_data.y *= accel_scale;
	accel_data.z *= accel_scale;
	accel_data.temperature = temperature;

	float gyro_scale = PIOS_MPU6500_GetGyroScale();
	gyro_data.x *= gyro_scale;
	gyro_data.y *= gyro_scale;
	gyro_data.z *= gyro_scale;
	gyro_data.temperature = temperature;

	portBASE_TYPE xHigherPriorityTaskWoken_accel;
	xQueueSendToBackFromISR(pios_mpu6500_dev->accel_queue, (void *)&accel_data, &xHigherPriorityTaskWoken_accel);

	portBASE_TYPE xHigherPriorityTaskWoken_gyro;
	xQueueSendToBackFromISR(pios_mpu6500_dev->gyro_queue, (void *)&gyro_data, &xHigherPriorityTaskWoken_gyro);

	return (xHigherPriorityTaskWoken_accel == pdTRUE) || (xHigherPriorityTaskWoken_gyro == pdTRUE) || woken == true;

#else

	struct pios_sensor_gyro_data gyro_data;

	switch (pios_mpu6500_dev->cfg->orientation) {
	case PIOS_MPU60X0_TOP_0DEG:
		gyro_data.y  = (int16_t)(mpu6500_rec_buf[IDX_GYRO_XOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_XOUT_L]);
		gyro_data.x  = (int16_t)(mpu6500_rec_buf[IDX_GYRO_YOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_YOUT_L]);
		break;
	case PIOS_MPU60X0_TOP_90DEG:
		gyro_data.y  = - (int16_t)(mpu6500_rec_buf[IDX_GYRO_YOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_YOUT_L]);
		gyro_data.x  = (int16_t)(mpu6500_rec_buf[IDX_GYRO_XOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_XOUT_L]);
		break;
	case PIOS_MPU60X0_TOP_180DEG:
		gyro_data.y  = - (int16_t)(mpu6500_rec_buf[IDX_GYRO_XOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_XOUT_L]);
		gyro_data.x  = - (int16_t)(mpu6500_rec_buf[IDX_GYRO_YOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_YOUT_L]);
		break;
	case PIOS_MPU60X0_TOP_270DEG:
		gyro_data.y  = (int16_t)(mpu6500_rec_buf[IDX_GYRO_YOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_YOUT_L]);
		gyro_data.x  = - (int16_t)(mpu6500_rec_buf[IDX_GYRO_XOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_XOUT_L]);
		break;
	}

	gyro_data.z = - (int16_t)(mpu6500_rec_buf[IDX_GYRO_ZOUT_H] << 8 | mpu6500_rec_buf[IDX_GYRO_ZOUT_L]);

	int32_t raw_temp = (int16_t)(mpu6500_rec_buf[IDX_TEMP_OUT_H] << 8 | mpu6500_rec_buf[IDX_TEMP_OUT_L]);
	float temperature = 35.0f + ((float)raw_temp + 512.0f) / 340.0f;

	// Apply sensor scaling
	float gyro_scale = PIOS_MPU6500_GetGyroScale();
	gyro_data.x *= gyro_scale;
	gyro_data.y *= gyro_scale;
	gyro_data.z *= gyro_scale;
	gyro_data.temperature = temperature;

	portBASE_TYPE xHigherPriorityTaskWoken_gyro;
	xQueueSendToBackFromISR(pios_mpu6500_dev->gyro_queue, (void *)&gyro_data, &xHigherPriorityTaskWoken_gyro);

	return (xHigherPriorityTaskWoken_gyro == pdTRUE || woken == true);

#endif /* PIOS_MPU6500_ACCEL */

}

#endif

/**
 * @}
 * @}
 */
