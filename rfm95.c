#include "rfm95.h"
#include "lib/ideetron/Encrypt_V31.h"

#include <assert.h>
#include <string.h>

#define RFM9x_VER 0x12

/**
 * Registers addresses.
 */
typedef enum
{
	RFM95_REGISTER_FIFO_ACCESS = 0x00,
	RFM95_REGISTER_OP_MODE = 0x01,
	RFM95_REGISTER_FR_MSB = 0x06,
	RFM95_REGISTER_FR_MID = 0x07,
	RFM95_REGISTER_FR_LSB = 0x08,
	RFM95_REGISTER_PA_CONFIG = 0x09,
	RFM95_REGISTER_FIFO_ADDR_PTR = 0x0D,
	RFM95_REGISTER_FIFO_TX_BASE_ADDR = 0x0E,
	RFM95_REGISTER_FIFO_RX_BASE_ADDR = 0x0F,
	RFM95_REGISTER_MODEM_CONFIG_1 = 0x1D,
	RFM95_REGISTER_MODEM_CONFIG_2 = 0x1E,
	RFM95_REGISTER_SYMB_TIMEOUT_LSB = 0x1F,
	RFM95_REGISTER_PREAMBLE_MSB = 0x20,
	RFM95_REGISTER_PREAMBLE_LSB = 0x21,
	RFM95_REGISTER_PAYLOAD_LENGTH = 0x22,
	RFM95_REGISTER_MODEM_CONFIG_3 = 0x26,
	RFM95_REGISTER_INVERT_IQ_1 = 0x33,
	RFM95_REGISTER_SYNC_WORD = 0x39,
	RFM95_REGISTER_INVERT_IQ_2 = 0x3B,
	RFM95_REGISTER_DIO_MAPPING_1 = 0x40,
	RFM95_REGISTER_DIO_MAPPING_2 = 0x41,
	RFM95_REGISTER_VERSION = 0x42,
	RFM95_REGISTER_PA_DAC = 0x4D
} rfm95_register_t;

typedef struct
{
	union {
		struct {
			uint8_t output_power : 4;
			uint8_t max_power : 3;
			uint8_t pa_select : 1;
		};
		uint8_t buffer;
	};
} rfm95_register_pa_config_t;

#define RFM95_REGISTER_OP_MODE_SLEEP                            0x00
#define RFM95_REGISTER_OP_MODE_LORA                             0x80
#define RFM95_REGISTER_OP_MODE_LORA_STANDBY                     0x81
#define RFM95_REGISTER_OP_MODE_LORA_TX                          0x83

#define RFM95_REGISTER_PA_DAC_LOW_POWER                         0x84
#define RFM95_REGISTER_PA_DAC_HIGH_POWER                        0x87

#define RFM95_REGISTER_MODEM_CONFIG_3_LDR_OPTIM_AGC_AUTO_ON     0x0C

#define RFM95_REGISTER_DIO_MAPPING_1_IRQ_TXDONE                 0x40
#define RFM95_REGISTER_DIO_MAPPING_2_DIO5_MODEREADY             0x00

#define RFM95_REGISTER_INVERT_IQ_1_ON_TXONLY                    0x27
#define RFM95_REGISTER_INVERT_IQ_1_OFF                          0x26
#define RFM95_REGISTER_INVERT_IQ_2_ON                           0x19
#define RFM95_REGISTER_INVERT_IQ_2_OFF                          0x1D

static const unsigned char eu863_lora_frequency[8][3] = {
	{0xD9, 0x06, 0x8B}, // Channel 0 868.100 MHz / 61.035 Hz = 14222987 = 0xD9068B
	{0xD9, 0x13, 0x58}, // Channel 1 868.300 MHz / 61.035 Hz = 14226264 = 0xD91358
	{0xD9, 0x20, 0x24}, // Channel 2 868.500 MHz / 61.035 Hz = 14229540 = 0xD92024
	{0xD8, 0xC6, 0x8B}, // Channel 3 867.100 MHz / 61.035 Hz = 14206603 = 0xD8C68B
	{0xD8, 0xD3, 0x58}, // Channel 4 867.300 MHz / 61.035 Hz = 14209880 = 0xD8D358
	{0xD8, 0xE0, 0x24}, // Channel 5 867.500 MHz / 61.035 Hz = 14213156 = 0xD8E024
	{0xD8, 0xEC, 0xF1}, // Channel 6 867.700 MHz / 61.035 Hz = 14216433 = 0xD8ECF1
	{0xD8, 0xF9, 0xBE} // Channel 7 867.900 MHz / 61.035 Hz = 14219710 = 0xD8F9BE
};

// Variables for the encryption library.
unsigned char NwkSkey[16];
unsigned char AppSkey[16];
unsigned char DevAddr[4];

static bool rfm95_read(rfm95_handle_t *handle, rfm95_register_t reg, uint8_t *buffer)
{
	HAL_GPIO_WritePin(handle->nss_port, handle->nss_pin, GPIO_PIN_RESET);

	uint8_t transmit_buffer = (uint8_t)reg & 0x7fu;

	if (HAL_SPI_Transmit(handle->spi_handle, &transmit_buffer, 1, RFM95_SPI_TIMEOUT) != HAL_OK) {
		return false;
	}

	if (HAL_SPI_Receive(handle->spi_handle, buffer, 1, RFM95_SPI_TIMEOUT) != HAL_OK) {
		return false;
	}

	HAL_GPIO_WritePin(handle->nss_port, handle->nss_pin, GPIO_PIN_SET);

	return true;
}

static bool rfm95_write(rfm95_handle_t *handle, rfm95_register_t reg, uint8_t value)
{
	HAL_GPIO_WritePin(handle->nss_port, handle->nss_pin, GPIO_PIN_RESET);

	uint8_t transmit_buffer[2] = {((uint8_t)reg | 0x80u), value};

	if (HAL_SPI_Transmit(handle->spi_handle, transmit_buffer, 2, RFM95_SPI_TIMEOUT) != HAL_OK) {
		return false;
	}

	HAL_GPIO_WritePin(handle->nss_port, handle->nss_pin, GPIO_PIN_SET);

	return true;
}

static void rfm95_reset(rfm95_handle_t *handle)
{
	HAL_GPIO_WritePin(handle->nrst_port, handle->nrst_pin, GPIO_PIN_RESET);
	HAL_Delay(1); // 0.1ms would theoretically be enough
	HAL_GPIO_WritePin(handle->nrst_port, handle->nrst_pin, GPIO_PIN_SET);
	HAL_Delay(5);
}

bool rfm95_init(rfm95_handle_t *handle)
{
	assert(handle->spi_handle->Init.Mode == SPI_MODE_MASTER);
	assert(handle->spi_handle->Init.Direction == SPI_DIRECTION_2LINES);
	assert(handle->spi_handle->Init.DataSize == SPI_DATASIZE_8BIT);
	assert(handle->spi_handle->Init.CLKPolarity == SPI_POLARITY_LOW);
	assert(handle->spi_handle->Init.CLKPhase == SPI_PHASE_1EDGE);

	rfm95_reset(handle);

	if (handle->reload_frame_counter != NULL) {
		handle->reload_frame_counter(&handle->tx_frame_count, &handle->rx_frame_count);
	} else {
		handle->tx_frame_count = 0;
		handle->rx_frame_count = 0;
	}

	// Check for correct version.
	uint8_t version;
	if (!rfm95_read(handle, RFM95_REGISTER_VERSION, &version)) return false;
	if (version != RFM9x_VER) return false;

	// Module must be placed in sleep mode before switching to lora.
	if (!rfm95_write(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_SLEEP)) return false;
	if (!rfm95_write(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA)) return false;

	// Set module power to 17dbm.
	if (!rfm95_set_power(handle, 17)) return false;

	// RX timeout set to 37 symbols.
	if (!rfm95_write(handle, RFM95_REGISTER_SYMB_TIMEOUT_LSB, 37)) return false;

	// Preamble set to 8 + 4.25 = 12.25 symbols.
	if (!rfm95_write(handle, RFM95_REGISTER_PREAMBLE_MSB, 0x00)) return false;
	if (!rfm95_write(handle, RFM95_REGISTER_PREAMBLE_LSB, 0x08)) return false;

	// Turn on low data rate optimisation (for symbol lengths > 16ms) and automatic AGC
	if (!rfm95_write(handle, RFM95_REGISTER_MODEM_CONFIG_3, RFM95_REGISTER_MODEM_CONFIG_3_LDR_OPTIM_AGC_AUTO_ON)) return false;

	// Set TTN sync word 0x34.
	if (!rfm95_write(handle, RFM95_REGISTER_SYNC_WORD, 0x34)) return false;

	// Set IQ inversion.
	if (!rfm95_write(handle, RFM95_REGISTER_INVERT_IQ_1, RFM95_REGISTER_INVERT_IQ_1_ON_TXONLY)) return false;
	if (!rfm95_write(handle, RFM95_REGISTER_INVERT_IQ_2, RFM95_REGISTER_INVERT_IQ_2_OFF)) return false;

	// Set up TX and RX FIFO base addresses.
	if (!rfm95_write(handle, RFM95_REGISTER_FIFO_TX_BASE_ADDR, 0x80)) return false;
	if (!rfm95_write(handle, RFM95_REGISTER_FIFO_RX_BASE_ADDR, 0x00)) return false;

	return true;
}

bool rfm95_set_power(rfm95_handle_t *handle, int8_t power)
{
	assert((power >= 2 && power <= 17) || power == 20);

	rfm95_register_pa_config_t pa_config = {0};
	uint8_t pa_dac_config = 0;

	if (power >= 2 && power <= 17) {
		pa_config.max_power = 7;
		pa_config.pa_select = 1;
		pa_config.output_power = (power - 2);
		pa_dac_config = RFM95_REGISTER_PA_DAC_LOW_POWER;

	} else if (power == 20) {
		pa_config.max_power = 7;
		pa_config.pa_select = 1;
		pa_config.output_power = 15;
		pa_dac_config = RFM95_REGISTER_PA_DAC_HIGH_POWER;
	}

	if (!rfm95_write(handle, RFM95_REGISTER_PA_CONFIG, pa_config.buffer)) return false;
	if (!rfm95_write(handle, RFM95_REGISTER_PA_DAC, pa_dac_config)) return false;

	return true;
}

static bool rfm95_send_package(rfm95_handle_t *handle, uint8_t *data, size_t length, uint8_t channel)
{
	assert(channel < 8);

	uint32_t tick_start;

	if (!rfm95_write(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_STANDBY)) return false;

	tick_start = HAL_GetTick();
	while (HAL_GPIO_ReadPin(handle->dio5_port, handle->dio5_pin) == GPIO_PIN_RESET) {
		if ((HAL_GetTick() - tick_start) >= RFM95_WAKEUP_TIMEOUT) {
			rfm95_write(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_SLEEP);
			return false;
		}
	}

	if (!rfm95_write(handle, RFM95_REGISTER_FR_MSB, eu863_lora_frequency[channel][0])) return false;
	if (!rfm95_write(handle, RFM95_REGISTER_FR_MID, eu863_lora_frequency[channel][1])) return false;
	if (!rfm95_write(handle, RFM95_REGISTER_FR_LSB, eu863_lora_frequency[channel][2])) return false;

	if (!rfm95_write(handle, RFM95_REGISTER_MODEM_CONFIG_1, 0x72)) return false;
	if (!rfm95_write(handle, RFM95_REGISTER_MODEM_CONFIG_2, 0x74)) return false;
	if (!rfm95_write(handle, RFM95_REGISTER_MODEM_CONFIG_3, 0x04)) return false;

	if (!rfm95_write(handle, RFM95_REGISTER_PAYLOAD_LENGTH, length)) return false;

	// Set SPI pointer to start of TX section in FIFO
	if (!rfm95_write(handle, RFM95_REGISTER_FIFO_ADDR_PTR, 0x80)) return false;

	// Write payload to FIFO.
	for (size_t i = 0; i < length; i++) {
		rfm95_write(handle, RFM95_REGISTER_FIFO_ACCESS, data[i]);
	}

	if (!rfm95_write(handle, RFM95_REGISTER_DIO_MAPPING_1, RFM95_REGISTER_DIO_MAPPING_1_IRQ_TXDONE)) return false;
	if (!rfm95_write(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_TX)) return false;

	tick_start = HAL_GetTick();
	while (HAL_GPIO_ReadPin(handle->irq_port, handle->irq_pin) == GPIO_PIN_RESET) {
		if ((HAL_GetTick() - tick_start) >= RFM95_SEND_TIMEOUT) {
			rfm95_write(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_SLEEP);
			return false;
		}
	}

	if (!rfm95_write(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_SLEEP)) return false;

	return true;
}

bool rfm95_send_data(rfm95_handle_t *handle, const uint8_t *data, size_t length)
{
	// 64 bytes is maximum size of FIFO
	assert(length + 4 + 9 <= 64);

	uint8_t direction = 0; // Up
	uint8_t frame_control = 0x00;
	uint8_t frame_port = 0x01;
	uint8_t mac_header = 0x40;

	uint8_t rfm_data[64 + 4 + 9];
	uint8_t rfm_package_length = 0;
	uint8_t mic[4];

	rfm_data[0] = mac_header;
	rfm_data[1] = handle->device_address[3];
	rfm_data[2] = handle->device_address[2];
	rfm_data[3] = handle->device_address[1];
	rfm_data[4] = handle->device_address[0];
	rfm_data[5] = frame_control;
	rfm_data[6] = (handle->tx_frame_count & 0x00ffu);
	rfm_data[7] = ((uint16_t)(handle->tx_frame_count >> 8u) & 0x00ffu);
	rfm_data[8] = frame_port;
	rfm_package_length += 9;

	// Copy network and application session keys as well as device address to variables of encryption library
	memcpy(NwkSkey, handle->network_session_key, sizeof(handle->network_session_key));
	memcpy(AppSkey, handle->application_session_key, sizeof(handle->application_session_key));
	memcpy(DevAddr, handle->device_address, sizeof(handle->device_address));

	// Encrypt payload in place in package.
	memcpy(rfm_data + rfm_package_length, data, length);
	Encrypt_Payload(rfm_data + rfm_package_length, length, handle->tx_frame_count, direction);
	rfm_package_length += length;

	// Calculate MIC and copy to last 4 bytes of the package.
	Calculate_MIC(rfm_data, mic, rfm_package_length, handle->tx_frame_count, direction);
	for (uint8_t i = 0; i < 4; i++) {
		rfm_data[rfm_package_length + i] = mic[i];
	}
	rfm_package_length += 4;

	uint8_t pseudorandom_channel = rfm_data[rfm_package_length - 1] & 0x7u;

	if (!rfm95_send_package(handle, rfm_data, rfm_package_length, pseudorandom_channel)) {
		return false;
	}

	handle->tx_frame_count++;
	if (handle->save_frame_counter != NULL) {
		handle->save_frame_counter(handle->tx_frame_count, handle->rx_frame_count);
	}
	return true;
}