#pragma once

#include <stdbool.h>

#if (defined STM32L011xx) || (defined STM32L021xx) || \
	(defined STM32L031xx) || (defined STM32L041xx) || \
	(defined STM32L051xx) || (defined STM32L052xx) || (defined STM32L053xx) || \
	(defined STM32L061xx) || (defined STM32L062xx) || (defined STM32L063xx) || \
	(defined STM32L071xx) || (defined STM32L072xx) || (defined STM32L073xx) || \
	(defined STM32L081xx) || (defined STM32L082xx) || (defined STM32L083xx)
#include "stm32l0xx_hal.h"
#elif defined (STM32L412xx) || defined (STM32L422xx) || \
	defined (STM32L431xx) || (defined STM32L432xx) || defined (STM32L433xx) || defined (STM32L442xx) || defined (STM32L443xx) || \
	defined (STM32L451xx) || defined (STM32L452xx) || defined (STM32L462xx) || \
	defined (STM32L471xx) || defined (STM32L475xx) || defined (STM32L476xx) || defined (STM32L485xx) || defined (STM32L486xx) || \
    defined (STM32L496xx) || defined (STM32L4A6xx) || \
    defined (STM32L4R5xx) || defined (STM32L4R7xx) || defined (STM32L4R9xx) || defined (STM32L4S5xx) || defined (STM32L4S7xx) || defined (STM32L4S9xx)
#include "stm32l4xx_hal.h"
#else
#error Platform not implemented
#endif

#ifndef RFM95_SPI_TIMEOUT
#define RFM95_SPI_TIMEOUT 10
#endif

typedef bool (*rfm95_reload_frame_counter_t)(uint16_t *tx_counter, uint16_t *rx_counter);
typedef void (*rfm95_save_frame_counter_t)(uint16_t tx_counter, uint16_t rx_counter);

/**
 * Structure defining a handle describing am RFM95(W) transceiver.
 */
typedef struct {

	/**
	 * The handle to the SPI bus for the device.
	 */
	SPI_HandleTypeDef *spi_handle;

	/**
	 * The port of the NSS pin.
	 */
	GPIO_TypeDef *nss_port;

	/**
	 * The NSS pin.
	 */
	uint16_t nss_pin;

	/**
	 * The port of the RST pin.
	 */
	GPIO_TypeDef *nrst_port;

	/**
	 * The RST pin.
	 */
	uint16_t nrst_pin;

	/**
	 * The port of the IRQ / DIO0 pin.
	 */
	GPIO_TypeDef *irq_port;

	/**
	 * The IRQ / DIO0 pin.
	 */
	uint16_t irq_pin;

	/**
	 * The port of the DIO5 pin.
	 */
	GPIO_TypeDef *dio5_port;

	/**
	 * The DIO5 pin.
	 */
	uint16_t dio5_pin;

	uint8_t device_address[4];

	uint8_t network_session_key[16];

	uint8_t application_session_key[16];

	rfm95_reload_frame_counter_t reload_frame_counter;

	rfm95_save_frame_counter_t save_frame_counter;

	uint16_t rx_frame_count;

	uint16_t tx_frame_count;

} rfm95_handle_t;

bool rfm95_init(rfm95_handle_t *handle);

bool rfm95_set_power(rfm95_handle_t *handle, int8_t power);

bool rfm95_send_data(rfm95_handle_t *handle, const uint8_t *data, size_t length);