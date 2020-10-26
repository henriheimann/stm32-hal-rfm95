#include "rfm95.h"

#include <assert.h>

#define RFM9x_VER 0x12

/**
 * Registers addresses.
 */
typedef enum
{
	RFM95_REGISTER_VERSION = 0x42
} rfm95_register_t;

bool rfm95_read(rfm95_handle_t *handle, rfm95_register_t reg, uint8_t *buffer)
{
	HAL_GPIO_WritePin(handle->nss_port, handle->nss_pin, GPIO_PIN_RESET);

	uint8_t transmit_buffer = (uint8_t)reg & 0x7fu;

	if (HAL_SPI_Transmit(handle->spi_handle, &transmit_buffer, 1, RFM95_SPI_TIMEOUT) != HAL_OK) {
		return false;
	}

	if (HAL_SPI_Receive(handle->spi_handle, buffer, 1, RFM95_SPI_TIMEOUT) != HAL_OK) {
		return false;
	}

	return true;
}

bool rfm95_init(rfm95_handle_t *handle)
{
	assert(handle->spi_handle->Init.Mode == SPI_MODE_MASTER);
	assert(handle->spi_handle->Init.Direction == SPI_DIRECTION_2LINES);
	assert(handle->spi_handle->Init.DataSize == SPI_DATASIZE_8BIT);
	assert(handle->spi_handle->Init.CLKPolarity == SPI_POLARITY_LOW);
	assert(handle->spi_handle->Init.CLKPhase == SPI_PHASE_1EDGE);

	// Module must be powered on for at least 10ms before usage
	uint32_t tick = HAL_GetTick();
	if (tick < 10) {
		HAL_Delay(10 - tick);
	}

	uint8_t version;
	if (!rfm95_read(handle, RFM95_REGISTER_VERSION, &version)) {
		return false;
	}

	if (version != RFM9x_VER) {
		return false;
	}

	return true;
}

void rfm95_reset(rfm95_handle_t *handle)
{
	HAL_GPIO_WritePin(handle->nrst_port, handle->nrst_pin, GPIO_PIN_RESET);
	HAL_Delay(1); // 0.1ms would theoretically be enough
	HAL_GPIO_WritePin(handle->nrst_port, handle->nrst_pin, GPIO_PIN_SET);
	HAL_Delay(5);
}