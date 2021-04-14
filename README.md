# RFM95
STM32 HAL driver for RFM95 LoraWAN modules.

[![Software License: MIT](https://img.shields.io/badge/Software_License-MIT-blue)](https://opensource.org/licenses/MIT)


## Usage
For CMake based projects (for example when using STM32CubeMX projects with CLion) you can for example setup the driver as submodule and link to it:
```cmake
add_subdirectory("<directory-to-driver>/rfm95")
target_link_libraries(<target-name> stm32-hal-rfm95)
```


## Getting Started
After connecting the RFM95 module to your microcontroller via SPI and initialising the bus using Cube you can use this
library to interact with the RFM95 module as shown in the following example:
```c
// Create the handle for the RFM95 module.
rfm95_handle_t rfm95_handle = {
    .spi_handle = &hspi1,
    .nss_port = RFM95_NSS_GPIO_Port,
    .nss_pin = RFM95_NSS_Pin,
    .nrst_port = RFM95_NRST_GPIO_Port,
    .nrst_pin = RFM95_NRST_Pin,
    .irq_port = RFM95_DIO0_GPIO_Port,
    .irq_pin = RFM95_DIO0_Pin,
    .dio5_port = RFM95_DIO5_GPIO_Port,
    .dio5_pin = RFM95_DIO5_Pin,
    .device_address = {0x00, 0x00, 0x00, 0x00},
    .application_session_key = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .network_session_key = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .reload_frame_counter = NULL,
    .save_frame_counter = NULL
};

// Initialise RFM95 module.
if (!rfm95_init(&rfm95_handle)) {
    printf("RFM95 init failed\n\r");
}

uint8_t[] data_packet = {
    0x01, 0x02, 0x03, 0x4
};

if (!rfm95_send_data(&rfm95_handle, data_packet, sizeof(data_packet))) {
    printf("RFM95 send failed\n\r");
} else {
    printf("RFM95 send success\n\r");
}
```

### Using Frame Counter Functions
The `reload_frame_counter` and `save_frame_counter` functions can be used to store and retrieve RX and TX frame counters in/from non-volatile memory.
For example, when using my EEPROM library (https://github.com/henriheimann/stm32-hal-eeprom) to store the frame counters, an example implementation might look like the following:

```c

// Forward declaration of the frame counter functions.
static bool reload_frame_counter(uint16_t *tx_counter, uint16_t *rx_counter);
static void save_frame_counter(uint16_t tx_counter, uint16_t rx_counter);

// Create the handle for the RFM95 module.
rfm95_handle_t rfm95_handle = {
    // ... see example above
    .reload_frame_counter = reload_frame_counter,
    .save_frame_counter = save_frame_counter
};

// Create the EEPROM handle.
eeprom_handle_t eeprom_handle = {
    .i2c_handle = &hi2c1,
    .device_address = EEPROM_24LC32A_ADDRESS,
    .max_address = EEPROM_24LC32A_MAX_ADDRESS,
    .page_size = EEPROM_24LC32A_PAGE_SIZE
};

// Definition of the reload frame counter function.
static bool reload_frame_counter(uint16_t *tx_counter, uint16_t *rx_counter)
{
    uint8_t buffer[6];

    if (!eeprom_read_bytes(&eeprom_handle, 0x20, buffer, sizeof(buffer))) {
        return false;
        }

    if (buffer[0] == 0x1A && buffer[1] == 0xA1) {
        *tx_counter = (uint16_t)((uint16_t)buffer[2] << 8u) | (uint16_t)buffer[3];
        *rx_counter = (uint16_t)((uint16_t)buffer[4] << 8u) | (uint16_t)buffer[5];
    } else {
        return false;
    }

    return true;
}

// Definition of the save frame counter function.
static void save_frame_counter(uint16_t tx_counter, uint16_t rx_counter)
{
    uint8_t buffer[6] = {
        0x1A, 0xA1, (uint8_t)(tx_counter >> 8u) & 0xffu, tx_counter & 0xffu, (uint8_t)(rx_counter >> 8u) & 0xffu, rx_counter & 0xffu
    };
    eeprom_write_bytes(&eeprom_handle, 0x20, buffer, sizeof(buffer));
}
```

## Supported Platforms
STM32L0, STM32L4 and STM32F4 microcontrollers are supported. The HAL header includes for other microcontrollers may be added in `rfm95.h`.
