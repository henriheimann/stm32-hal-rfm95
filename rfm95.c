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
	RFM95_REGISTER_LNA = 0x0C,
	RFM95_REGISTER_FIFO_ADDR_PTR = 0x0D,
	RFM95_REGISTER_FIFO_TX_BASE_ADDR = 0x0E,
	RFM95_REGISTER_FIFO_RX_BASE_ADDR = 0x0F,
	RFM95_REGISTER_IRQ_FLAGS = 0x12,
	RFM95_REGISTER_FIFO_RX_BYTES_NB = 0x13,
	RFM95_REGISTER_PACKET_SNR = 0x19,
	RFM95_REGISTER_MODEM_CONFIG_1 = 0x1D,
	RFM95_REGISTER_MODEM_CONFIG_2 = 0x1E,
	RFM95_REGISTER_SYMB_TIMEOUT_LSB = 0x1F,
	RFM95_REGISTER_PREAMBLE_MSB = 0x20,
	RFM95_REGISTER_PREAMBLE_LSB = 0x21,
	RFM95_REGISTER_PAYLOAD_LENGTH = 0x22,
	RFM95_REGISTER_MAX_PAYLOAD_LENGTH = 0x23,
	RFM95_REGISTER_MODEM_CONFIG_3 = 0x26,
	RFM95_REGISTER_INVERT_IQ_1 = 0x33,
	RFM95_REGISTER_SYNC_WORD = 0x39,
	RFM95_REGISTER_INVERT_IQ_2 = 0x3B,
	RFM95_REGISTER_DIO_MAPPING_1 = 0x40,
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
#define RFM95_REGISTER_OP_MODE_LORA_SLEEP                       0x80
#define RFM95_REGISTER_OP_MODE_LORA_STANDBY                     0x81
#define RFM95_REGISTER_OP_MODE_LORA_TX                          0x83
#define RFM95_REGISTER_OP_MODE_LORA_RX_SINGLE                   0x86

#define RFM95_REGISTER_PA_DAC_LOW_POWER                         0x84
#define RFM95_REGISTER_PA_DAC_HIGH_POWER                        0x87

#define RFM95_REGISTER_DIO_MAPPING_1_IRQ_FOR_TXDONE             0x40
#define RFM95_REGISTER_DIO_MAPPING_1_IRQ_FOR_RXDONE             0x00

#define RFM95_REGISTER_INVERT_IQ_1_TX                    		0x27
#define RFM95_REGISTER_INVERT_IQ_2_TX							0x1d

#define RFM95_REGISTER_INVERT_IQ_1_RX                    		0x67
#define RFM95_REGISTER_INVERT_IQ_2_RX							0x19

static bool read_register(rfm95_handle_t *handle, rfm95_register_t reg, uint8_t *buffer, size_t length)
{
	HAL_GPIO_WritePin(handle->nss_port, handle->nss_pin, GPIO_PIN_RESET);

	uint8_t transmit_buffer = (uint8_t)reg & 0x7fu;

	if (HAL_SPI_Transmit(handle->spi_handle, &transmit_buffer, 1, RFM95_SPI_TIMEOUT) != HAL_OK) {
		return false;
	}

	if (HAL_SPI_Receive(handle->spi_handle, buffer, length, RFM95_SPI_TIMEOUT) != HAL_OK) {
		return false;
	}

	HAL_GPIO_WritePin(handle->nss_port, handle->nss_pin, GPIO_PIN_SET);

	return true;
}

static bool write_register(rfm95_handle_t *handle, rfm95_register_t reg, uint8_t value)
{
	HAL_GPIO_WritePin(handle->nss_port, handle->nss_pin, GPIO_PIN_RESET);

	uint8_t transmit_buffer[2] = {((uint8_t)reg | 0x80u), value};

	if (HAL_SPI_Transmit(handle->spi_handle, transmit_buffer, 2, RFM95_SPI_TIMEOUT) != HAL_OK) {
		return false;
	}

	HAL_GPIO_WritePin(handle->nss_port, handle->nss_pin, GPIO_PIN_SET);

	return true;
}

static void config_set_channel(rfm95_handle_t *handle, uint8_t channel_index, uint32_t frequency)
{
	assert(channel_index < 16);
	handle->config.channels[channel_index].frequency = frequency;
	handle->config.channel_mask |= (1 << channel_index);
}

static void config_load_default(rfm95_handle_t *handle)
{
	handle->config.magic = RFM95_EEPROM_CONFIG_MAGIC;
	handle->config.tx_frame_count = 0;
	handle->config.rx_frame_count = 0;
	handle->config.rx1_delay = 1;
	handle->config.channel_mask = 0;
	config_set_channel(handle, 0, 868100000);
	config_set_channel(handle, 1, 868300000);
	config_set_channel(handle, 2, 868500000);
}

static void reset(rfm95_handle_t *handle)
{
	HAL_GPIO_WritePin(handle->nrst_port, handle->nrst_pin, GPIO_PIN_RESET);
	HAL_Delay(1); // 0.1ms would theoretically be enough
	HAL_GPIO_WritePin(handle->nrst_port, handle->nrst_pin, GPIO_PIN_SET);
	HAL_Delay(5);
}

static bool configure_frequency(rfm95_handle_t *handle, uint32_t frequency)
{
	// FQ = (FRF * 32 Mhz) / (2 ^ 19)
	uint64_t frf = ((uint64_t)frequency << 19) / 32000000;

	if (!write_register(handle, RFM95_REGISTER_FR_MSB, (uint8_t)(frf >> 16))) return false;
	if (!write_register(handle, RFM95_REGISTER_FR_MID, (uint8_t)(frf >> 8))) return false;
	if (!write_register(handle, RFM95_REGISTER_FR_LSB, (uint8_t)(frf >> 0))) return false;

	return true;
}

static bool configure_channel(rfm95_handle_t *handle, size_t channel_index)
{
	assert(handle->config.channel_mask & (1 << channel_index));
	return configure_frequency(handle, handle->config.channels[channel_index].frequency);
}

static bool wait_for_irq(rfm95_handle_t *handle, rfm95_interrupt_t interrupt, uint32_t timeout_ms)
{
	uint32_t timeout_tick = handle->get_precision_tick() + timeout_ms * handle->precision_tick_frequency / 1000;

	while (handle->interrupt_times[interrupt] == 0) {
		if (handle->get_precision_tick() >= timeout_tick) {
			return false;
		}
	}

	return true;
}

static bool wait_for_rx_irqs(rfm95_handle_t *handle)
{
	uint32_t timeout_tick = handle->get_precision_tick() +
	                        RFM95_RECEIVE_TIMEOUT * handle->precision_tick_frequency / 1000;

	while (handle->interrupt_times[RFM95_INTERRUPT_DIO0] == 0 && handle->interrupt_times[RFM95_INTERRUPT_DIO1] == 0) {
		if (handle->get_precision_tick() >= timeout_tick) {
			return false;
		}
	}

	return handle->interrupt_times[RFM95_INTERRUPT_DIO0] != 0;
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

	if (!write_register(handle, RFM95_REGISTER_PA_CONFIG, pa_config.buffer)) return false;
	if (!write_register(handle, RFM95_REGISTER_PA_DAC, pa_dac_config)) return false;

	return true;
}

bool rfm95_init(rfm95_handle_t *handle)
{
	assert(handle->spi_handle->Init.Mode == SPI_MODE_MASTER);
	assert(handle->spi_handle->Init.Direction == SPI_DIRECTION_2LINES);
	assert(handle->spi_handle->Init.DataSize == SPI_DATASIZE_8BIT);
	assert(handle->spi_handle->Init.CLKPolarity == SPI_POLARITY_LOW);
	assert(handle->spi_handle->Init.CLKPhase == SPI_PHASE_1EDGE);
	assert(handle->get_precision_tick != NULL);
	assert(handle->random_int != NULL);
	assert(handle->precision_sleep_until != NULL);
	assert(handle->precision_tick_frequency > 10000);

	reset(handle);

	// If there is reload function or the reload was unsuccessful or the magic does not match restore default.
	if (handle->reload_config == NULL || !handle->reload_config(&handle->config) ||
	    handle->config.magic != RFM95_EEPROM_CONFIG_MAGIC) {
		config_load_default(handle);
	}

	// Check for correct version.
	uint8_t version;
	if (!read_register(handle, RFM95_REGISTER_VERSION, &version, 1)) return false;
	if (version != RFM9x_VER) return false;

	// Module must be placed in sleep mode before switching to lora.
	if (!write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_SLEEP)) return false;
	if (!write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_SLEEP)) return false;

	// Default interrupt configuration, must be done to prevent DIO5 clock interrupts at 1Mhz
	if (!write_register(handle, RFM95_REGISTER_DIO_MAPPING_1, RFM95_REGISTER_DIO_MAPPING_1_IRQ_FOR_RXDONE)) return false;

	if (handle->on_after_interrupts_configured != NULL) {
		handle->on_after_interrupts_configured();
	}

	// Set module power to 17dbm.
	if (!rfm95_set_power(handle, 17)) return false;

	// Set LNA to the highest gain with 150% boost.
	if (!write_register(handle, RFM95_REGISTER_LNA, 0x23)) return false;

	// Preamble set to 8 + 4.25 = 12.25 symbols.
	if (!write_register(handle, RFM95_REGISTER_PREAMBLE_MSB, 0x00)) return false;
	if (!write_register(handle, RFM95_REGISTER_PREAMBLE_LSB, 0x08)) return false;

	// Set TTN sync word 0x34.
	if (!write_register(handle, RFM95_REGISTER_SYNC_WORD, 0x34)) return false;

	// Set up TX and RX FIFO base addresses.
	if (!write_register(handle, RFM95_REGISTER_FIFO_TX_BASE_ADDR, 0x80)) return false;
	if (!write_register(handle, RFM95_REGISTER_FIFO_RX_BASE_ADDR, 0x00)) return false;

	// Maximum payload length of the RFM95 is 64.
	if (!write_register(handle, RFM95_REGISTER_MAX_PAYLOAD_LENGTH, 64)) return false;

	// Let module sleep after initialisation.
	if (!write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_SLEEP)) return false;

	return true;
}

static bool process_mac_commands(rfm95_handle_t *handle, const uint8_t *frame_payload,
                                 size_t frame_payload_length, uint8_t answer_buffer[51], uint8_t *answer_buffer_length,
                                 int8_t snr)
{
	uint8_t index = 0;
	uint8_t answer_index = 0;

	while (index < frame_payload_length) {
		switch (frame_payload[index++])
		{
			case 0x01: // ResetConf
			{
				if (index >= frame_payload_length) return false;

				index += 1;
				break;
			}
			case 0x02: // LinkCheckReq
			{
				if ((index + 1) >= frame_payload_length) return false;

				index += 2;
				break;
			}
			case 0x03: // LinkADRReq
			{
				if ((index + 3) >= frame_payload_length) return false;

				index += 4;
				break;
			}
			case 0x04: // DutyCycleReq
			{
				if (index >= frame_payload_length) return false;

				index += 1;
				break;
			}
			case 0x05: // RXParamSetupReq
			{
				if ((index + 4) >= frame_payload_length) return false;
				if ((answer_index + 2) >= 51) return false;

				uint8_t dl_settings = frame_payload[index++];
				uint8_t frequency_lsb = frame_payload[index++];
				uint8_t frequency_msb = frame_payload[index++];
				uint8_t frequency_hsb = frame_payload[index++];
				uint32_t frequency = (frequency_lsb | (frequency_msb << 8) | (frequency_hsb << 16)) * 100;

				answer_buffer[answer_index++] = 0x05;
				answer_buffer[answer_index++] = 0b0000111;
				break;
			}
			case 0x06: // DevStatusReq
			{
				if ((answer_index + 3) >= 51) return false;

				uint8_t margin = (uint8_t)(snr & 0x1f);
				uint8_t battery_level = handle->get_battery_level == NULL ? 0xff : handle->get_battery_level();

				answer_buffer[answer_index++] = 0x06;
				answer_buffer[answer_index++] = battery_level;
				answer_buffer[answer_index++] = margin;
				break;
			}
			case 0x07: // NewChannelReq
			{
				if ((index + 4) >= frame_payload_length) return false;
				if ((answer_index + 2) >= 51) return false;

				uint8_t channel_index = frame_payload[index++];
				uint8_t frequency_lsb = frame_payload[index++];
				uint8_t frequency_msb = frame_payload[index++];
				uint8_t frequency_hsb = frame_payload[index++];
				uint8_t min_max_dr = frame_payload[index++];

				uint32_t frequency = (frequency_lsb | (frequency_msb << 8) | (frequency_hsb << 16)) * 100;
				uint8_t min_dr = min_max_dr & 0x0f;
				uint8_t max_dr = (min_max_dr >> 4) & 0x0f;

				if (channel_index >= 3) {
					config_set_channel(handle, channel_index, frequency);
				}

				bool dr_supports_125kHz_SF7 = min_dr <= 5 || max_dr >= 5;

				answer_buffer[answer_index++] = 0x07;
				answer_buffer[answer_index++] = 0x01 | (dr_supports_125kHz_SF7 << 1);
				break;
			}
			case 0x08: // RXTimingSetupReq
			{
				if (index >= frame_payload_length) return false;
				if ((answer_index + 2) >= 51) return false;

				handle->config.rx1_delay = frame_payload[index++] & 0xf;
				if (handle->config.rx1_delay == 0) {
					handle->config.rx1_delay = 1;
				}

				answer_buffer[answer_index++] = 0x08;
				break;
			}
			case 0x09: // TxParamSetupReq
			{
				if (index >= frame_payload_length) return false;

				break;
			}
			case 0x0a: // DlChannelReq
			{
				if ((index + 4) >= frame_payload_length) return false;

				break;
			}
			case 0x0b: // RekeyConf
			{
				if (index >= frame_payload_length) return false;

				break;
			}
			case 0x0c: // ADRParamSetupReq
			{
				if (index >= frame_payload_length) return false;

				break;
			}
			case 0x0d: // DeviceTimeReq
			{
				break;
			}
		}
	}

	*answer_buffer_length = answer_index;
	return true;
}

static bool receive_at_scheduled_time(rfm95_handle_t *handle, uint32_t scheduled_time)
{
	// Sleep until 1ms before the scheduled time.
	handle->precision_sleep_until(scheduled_time - handle->precision_tick_frequency / 1000);

	// Clear flags and previous interrupt time, configure mapping for RX done.
	if (!write_register(handle, RFM95_REGISTER_DIO_MAPPING_1, RFM95_REGISTER_DIO_MAPPING_1_IRQ_FOR_RXDONE)) return false;
	if (!write_register(handle, RFM95_REGISTER_IRQ_FLAGS, 0xff)) return false;
	handle->interrupt_times[RFM95_INTERRUPT_DIO0] = 0;
	handle->interrupt_times[RFM95_INTERRUPT_DIO1] = 0;
	handle->interrupt_times[RFM95_INTERRUPT_DIO5] = 0;

	// Move modem to lora standby.
	if (!write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_STANDBY)) return false;

	// Wait for the modem to be ready.
	wait_for_irq(handle, RFM95_INTERRUPT_DIO5, RFM95_WAKEUP_TIMEOUT);

	// Now sleep until the real scheduled time.
	handle->precision_sleep_until(scheduled_time);

	if (!write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_RX_SINGLE)) return false;

	return true;
}

static void calculate_rx_timings(rfm95_handle_t *handle, uint32_t bw, uint8_t sf, uint32_t tx_ticks,
                                 uint32_t *rx_target, uint32_t *rx_window_symbols)
{
	volatile int32_t symbol_rate_ns = (int32_t)(((2 << (sf - 1)) * 1000000) / bw);

	volatile int32_t rx_timing_error_ns = (int32_t)(handle->precision_tick_drift_ns_per_s * handle->config.rx1_delay);
	volatile int32_t rx_window_ns = 2 * symbol_rate_ns + 2 * rx_timing_error_ns;
	volatile int32_t rx_offset_ns = 4 * symbol_rate_ns - (rx_timing_error_ns / 2);
	volatile int32_t rx_offset_ticks = (int32_t)(((int64_t)rx_offset_ns * (int64_t)handle->precision_tick_frequency) / 1000000);
	*rx_target = tx_ticks + handle->precision_tick_frequency * handle->config.rx1_delay + rx_offset_ticks;
	*rx_window_symbols = rx_window_ns / symbol_rate_ns;
}

static bool receive_package(rfm95_handle_t *handle, uint32_t tx_ticks, uint8_t *payload_buf, size_t *payload_len,
                            int8_t *snr)
{
	*payload_len = 0;

	uint32_t rx1_target, rx1_window_symbols;
	calculate_rx_timings(handle, 125000, 7, tx_ticks, &rx1_target, &rx1_window_symbols);

	assert(rx1_window_symbols <= 0x3ff);

	// Configure modem (125kHz, 4/6 error coding rate, SF7, single packet, CRC enable, AGC auto on)
	if (!write_register(handle, RFM95_REGISTER_MODEM_CONFIG_1, 0x72)) return false;
	if (!write_register(handle, RFM95_REGISTER_MODEM_CONFIG_2, 0x74 | ((rx1_window_symbols >> 8) & 0x3))) return false;
	if (!write_register(handle, RFM95_REGISTER_MODEM_CONFIG_3, 0x04)) return false;

	// Set maximum symbol timeout.
	if (!write_register(handle, RFM95_REGISTER_SYMB_TIMEOUT_LSB, rx1_window_symbols)) return false;

	// Set IQ registers according to AN1200.24.
	if (!write_register(handle, RFM95_REGISTER_INVERT_IQ_1, RFM95_REGISTER_INVERT_IQ_1_RX)) return false;
	if (!write_register(handle, RFM95_REGISTER_INVERT_IQ_2, RFM95_REGISTER_INVERT_IQ_2_RX)) return false;

	receive_at_scheduled_time(handle, rx1_target);

	// If there was nothing received during RX1, try RX2.
	if (!wait_for_rx_irqs(handle)) {

		// Return modem to sleep.
		if (!write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_SLEEP)) return false;

		if (handle->receive_mode == RFM95_RECEIVE_MODE_RX12) {

			uint32_t rx2_target, rx2_window_symbols;
			calculate_rx_timings(handle, 125000, 12, tx_ticks, &rx2_target, &rx2_window_symbols);

			// Configure 869.525 MHz
			if (!configure_frequency(handle, 869525000)) return false;

			// Configure modem SF12
			if (!write_register(handle, RFM95_REGISTER_MODEM_CONFIG_1, 0xc2)) return false;
			if (!write_register(handle, RFM95_REGISTER_MODEM_CONFIG_2, 0x74 | ((rx2_window_symbols >> 8) & 0x3))) return false;
			if (!write_register(handle, RFM95_REGISTER_MODEM_CONFIG_3, 0x04)) return false;

			// Set maximum symbol timeout.
			if (!write_register(handle, RFM95_REGISTER_SYMB_TIMEOUT_LSB, rx2_window_symbols)) return false;

			receive_at_scheduled_time(handle, rx2_target);

			if (!wait_for_rx_irqs(handle)) {
				// No payload during in RX1 and RX2
				return true;
			}
		}

		return true;
	}

	uint8_t irq_flags;
	read_register(handle, RFM95_REGISTER_IRQ_FLAGS, &irq_flags, 1);

	// Check if there was a CRC error.
	if (irq_flags & 0x20) {
		return true;
	}

	int8_t packet_snr;
	if (!read_register(handle, RFM95_REGISTER_PACKET_SNR, (uint8_t *)&packet_snr, 1)) return false;
	*snr = (int8_t)(packet_snr / 4);

	// Read received payload length.
	uint8_t payload_len_internal;
	if (!read_register(handle, RFM95_REGISTER_FIFO_RX_BYTES_NB, &payload_len_internal, 1)) return false;

	// Read received payload itself.
	if (!write_register(handle, RFM95_REGISTER_FIFO_ADDR_PTR, 0)) return false;
	if (!read_register(handle, RFM95_REGISTER_FIFO_ACCESS, payload_buf, payload_len_internal)) return false;

	// Return modem to sleep.
	if (!write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_SLEEP)) return false;

	// Successful payload receive, set payload length to tell caller.
	*payload_len = payload_len_internal;
	return true;
}

static bool send_package(rfm95_handle_t *handle, uint8_t *payload_buf, size_t payload_len, uint8_t channel,
                         uint32_t *tx_ticks)
{
	// Configure channel for transmission.
	if (!configure_channel(handle, channel)) return false;

	// Configure modem (125kHz, 4/6 error coding rate, SF7, single packet, CRC enable, AGC auto on)
	if (!write_register(handle, RFM95_REGISTER_MODEM_CONFIG_1, 0x72)) return false;
	if (!write_register(handle, RFM95_REGISTER_MODEM_CONFIG_2, 0x74)) return false;
	if (!write_register(handle, RFM95_REGISTER_MODEM_CONFIG_3, 0x04)) return false;

	// Set IQ registers according to AN1200.24.
	if (!write_register(handle, RFM95_REGISTER_INVERT_IQ_1, RFM95_REGISTER_INVERT_IQ_1_TX)) return false;
	if (!write_register(handle, RFM95_REGISTER_INVERT_IQ_2, RFM95_REGISTER_INVERT_IQ_2_TX)) return false;

	// Set the payload length.
	if (!write_register(handle, RFM95_REGISTER_PAYLOAD_LENGTH, payload_len)) return false;

	// Enable tx-done interrupt, clear flags and previous interrupt time.
	if (!write_register(handle, RFM95_REGISTER_DIO_MAPPING_1, RFM95_REGISTER_DIO_MAPPING_1_IRQ_FOR_TXDONE)) return false;
	if (!write_register(handle, RFM95_REGISTER_IRQ_FLAGS, 0xff)) return false;
	handle->interrupt_times[RFM95_INTERRUPT_DIO0] = 0;
	handle->interrupt_times[RFM95_INTERRUPT_DIO5] = 0;

	// Move modem to lora standby.
	if (!write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_STANDBY)) return false;

	// Wait for the modem to be ready.
	wait_for_irq(handle, RFM95_INTERRUPT_DIO5, RFM95_WAKEUP_TIMEOUT);

	// Set pointer to start of TX section in FIFO.
	if (!write_register(handle, RFM95_REGISTER_FIFO_ADDR_PTR, 0x80)) return false;

	// Write payload to FIFO.
	for (size_t i = 0; i < payload_len; i++) {
		write_register(handle, RFM95_REGISTER_FIFO_ACCESS, payload_buf[i]);
	}

	// Set modem to tx mode.
	if (!write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_TX)) return false;

	// Wait for the transfer complete interrupt.
	if (!wait_for_irq(handle, RFM95_INTERRUPT_DIO0, RFM95_SEND_TIMEOUT)) return false;

	// Set real tx time in ticks.
	*tx_ticks = handle->interrupt_times[RFM95_INTERRUPT_DIO0];

	// Return modem to sleep.
	if (!write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_SLEEP)) return false;

	// Increment tx frame counter.
	handle->config.tx_frame_count++;

	return true;
}

static size_t encode_phy_payload(rfm95_handle_t *handle, uint8_t payload_buf[64], const uint8_t *frame_payload,
                                 size_t frame_payload_length, uint8_t port)
{
	size_t payload_len = 0;

	// 64 bytes is maximum size of FIFO
	assert(frame_payload_length + 4 + 9 <= 64);

	payload_buf[0] = 0x40; // MAC Header
	payload_buf[1] = handle->device_address[3];
	payload_buf[2] = handle->device_address[2];
	payload_buf[3] = handle->device_address[1];
	payload_buf[4] = handle->device_address[0];
	payload_buf[5] = 0x00; // Frame Control
	payload_buf[6] = (handle->config.tx_frame_count & 0x00ffu);
	payload_buf[7] = ((uint16_t)(handle->config.tx_frame_count >> 8u) & 0x00ffu);
	payload_buf[8] = port; // Frame Port
	payload_len += 9;

	// Encrypt payload in place in payload_buf.
	memcpy(payload_buf + payload_len, frame_payload, frame_payload_length);
	if (port == 0) {
		Encrypt_Payload(payload_buf + payload_len, frame_payload_length, handle->config.tx_frame_count,
		                0, handle->network_session_key, handle->device_address);
	} else {
		Encrypt_Payload(payload_buf + payload_len, frame_payload_length, handle->config.tx_frame_count,
		                0, handle->application_session_key, handle->device_address);
	}
	payload_len += frame_payload_length;

	// Calculate MIC and copy to last 4 bytes of the payload_buf.
	uint8_t mic[4];
	Calculate_MIC(payload_buf, mic, payload_len, handle->config.tx_frame_count, 0,
	              handle->network_session_key, handle->device_address);
	for (uint8_t i = 0; i < 4; i++) {
		payload_buf[payload_len + i] = mic[i];
	}
	payload_len += 4;

	return payload_len;
}

static bool decode_phy_payload(rfm95_handle_t *handle, uint8_t payload_buf[64], uint8_t payload_length,
                               uint8_t **decoded_frame_payload_ptr, uint8_t *decoded_frame_payload_length, uint8_t *frame_port)
{
	// Only unconfirmed down-links are supported for now.
	if (payload_buf[0] != 0x60) {
		return false;
	}

	// Does the device address match?
	if (payload_buf[1] != handle->device_address[3] || payload_buf[2] != handle->device_address[2] ||
	    payload_buf[3] != handle->device_address[1] || payload_buf[4] != handle->device_address[0]) {
		return false;
	}

	uint8_t frame_control = payload_buf[5];
	uint8_t frame_opts_length = frame_control & 0x0f;
	uint16_t rx_frame_count = (payload_buf[7] << 8) | payload_buf[6];

	// Check if rx frame count is valid and if so, update accordingly.
	if (rx_frame_count < handle->config.rx_frame_count) {
		return false;
	}
	handle->config.rx_frame_count = rx_frame_count;

	uint8_t check_mic[4];
	Calculate_MIC(payload_buf, check_mic, payload_length - 4, rx_frame_count, 1,
	              handle->network_session_key, handle->device_address);
	if (memcmp(check_mic, &payload_buf[payload_length - 4], 4) != 0) {
		return false;
	}

	if (payload_length - 12 - frame_opts_length == 0) {
		*frame_port = 0;
		*decoded_frame_payload_ptr = &payload_buf[8];
		*decoded_frame_payload_length = frame_opts_length;

	} else {
		*frame_port = payload_buf[8];

		uint8_t frame_payload_start = 9 + frame_opts_length;
		uint8_t frame_payload_end = payload_length - 4;
		uint8_t frame_payload_length = frame_payload_end - frame_payload_start;

		if (*frame_port == 0) {
			Encrypt_Payload(&payload_buf[frame_payload_start], frame_payload_length, rx_frame_count,
			                1, handle->network_session_key, handle->device_address);
		} else {
			Encrypt_Payload(&payload_buf[frame_payload_start], frame_payload_length, rx_frame_count,
			                1, handle->application_session_key, handle->device_address);
		}

		*decoded_frame_payload_ptr = &payload_buf[frame_payload_start];
		*decoded_frame_payload_length = frame_payload_length;
	}

	return true;
}

static uint8_t select_random_channel(rfm95_handle_t *handle)
{
	uint8_t channel_count = 0;

	for (uint8_t i = 0; i < 16; i++) {
		if (handle->config.channel_mask & (1 << i)) {
			channel_count++;
		}
	}

	uint8_t random_channel = handle->random_int(channel_count);

	for (uint8_t i = 0; i < 16; i++) {
		if (handle->config.channel_mask & (1 << i)) {
			if (random_channel == 0) {
				return i;
			} else {
				random_channel--;
			}
		}
	}

	return 0;
}

bool rfm95_send_receive_cycle(rfm95_handle_t *handle, const uint8_t *send_data, size_t send_data_length)
{
	uint8_t phy_payload_buf[64] = { 0 };

	// Build the up-link phy payload.
	size_t phy_payload_len = encode_phy_payload(handle, phy_payload_buf, send_data, send_data_length, 1);

	uint8_t random_channel = select_random_channel(handle);

	uint32_t tx_ticks;

	// Send the requested up-link.
	if (!send_package(handle, phy_payload_buf, phy_payload_len, random_channel, &tx_ticks)) {
		write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_SLEEP);
		return false;
	}

	// Clear phy payload buffer to reuse for the down-link message.
	memset(phy_payload_buf, 0x00, sizeof(phy_payload_buf));
	phy_payload_len = 0;

	// Only receive if configured to do so.
	if (handle->receive_mode != RFM95_RECEIVE_MODE_NONE) {

		int8_t snr;

		// Try receiving a down-link.
		if (!receive_package(handle, tx_ticks, phy_payload_buf, &phy_payload_len, &snr)) {
			write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_SLEEP);
			if (handle->save_config) {
				handle->save_config(&(handle->config));
			}
			return false;
		}

		// Any RX payload was received.
		if (phy_payload_len != 0) {

			uint8_t *frame_payload;
			uint8_t frame_payload_len = 0;
			uint8_t frame_port;

			// Try decoding the frame payload.
			if (decode_phy_payload(handle, phy_payload_buf, phy_payload_len, &frame_payload, &frame_payload_len,
			                       &frame_port)) {

				// Process Mac Commands
				if (frame_port == 0) {

					uint8_t mac_response_data[51] = {0};
					uint8_t mac_response_len = 0;

					if (process_mac_commands(handle, frame_payload, frame_payload_len, mac_response_data,
					                         &mac_response_len, snr) && mac_response_len != 0) {

						// Build the up-link phy payload.
						phy_payload_len = encode_phy_payload(handle, phy_payload_buf, mac_response_data,
						                                     mac_response_len, 0);

						if (!send_package(handle, phy_payload_buf, phy_payload_len, random_channel,
						                  &tx_ticks)) {
							write_register(handle, RFM95_REGISTER_OP_MODE, RFM95_REGISTER_OP_MODE_LORA_SLEEP);
							if (handle->save_config) {
								handle->save_config(&(handle->config));
							}
							return false;
						}
					}

				} else {
					// Don't process application messages for now!
				}
			}
		}
	}

	if (handle->save_config) {
		handle->save_config(&(handle->config));
	}

	return true;
}

void rfm95_on_interrupt(rfm95_handle_t *handle, rfm95_interrupt_t interrupt)
{
	handle->interrupt_times[interrupt] = handle->get_precision_tick();
}
