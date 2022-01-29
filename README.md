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
library to interact with the RFM95 module as shown in the following example. You are required to handle DIO0, 1 and 5 interrupts:
```c

int main() {
    // Create the handle for the RFM95 module.
    rfm95_handle_t rfm95_handle = {
        .spi_handle = &hspi1,
		.nss_port = RFM95_NSS_GPIO_Port,
		.nss_pin = RFM95_NSS_Pin,
		.nrst_port = RFM95_NRST_GPIO_Port,
		.nrst_pin = RFM95_NRST_Pin,
		.device_address = {
            0x00, 0x00, 0x00, 0x00
        },
		.application_session_key = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },
		.network_session_key = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },
		.receive_mode = RFM95_RECEIVE_MODE_NONE
    };

    // Initialise RFM95 module.
    if (!rfm95_init(&rfm95_handle)) {
        printf("RFM95 init failed\n\r");
    }

    uint8_t[] data_packet = {
        0x01, 0x02, 0x03, 0x4
    };

    if (!rfm95_send_receive_cycle(&rfm95_handle, data_packet, sizeof(data_packet))) {
        printf("RFM95 send failed\n\r");
    } else {
        printf("RFM95 send success\n\r");
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == RFM95_DIO0_Pin) {
        rfm95_on_interrupt(&rfm95_handle, RFM95_INTERRUPT_DIO0);
    } else if (GPIO_Pin == RFM95_DIO1_Pin) {
        rfm95_on_interrupt(&rfm95_handle, RFM95_INTERRUPT_DIO1);
    } else if (GPIO_Pin == RFM95_DIO5_Pin) {
        rfm95_on_interrupt(&rfm95_handle, RFM95_INTERRUPT_DIO5);
    }
}
```

### Using the reload- and safe-configuration functions
The `reload_config` and `save_config` functions can be used to store and retrieve RX and TX frame counters as well as other configuration in/from non-volatile memory.
For example, when using my EEPROM library (https://github.com/henriheimann/stm32-hal-eeprom) to store the frame counters, an example implementation might look like the following:

```c

// Forward declaration of the frame counter functions.
static bool reload_config(rfm95_eeprom_config_t *config);
static void save_config(const rfm95_eeprom_config_t *config);

// Create the handle for the RFM95 module.
rfm95_handle_t rfm95_handle = {
    // ... see example above
    .reload_config = reload_config,
    .save_config = save_config,
};

// Create the EEPROM handle.
eeprom_handle_t eeprom_handle = {
    .i2c_handle = &hi2c1,
    .device_address = EEPROM_24LC32A_ADDRESS,
    .max_address = EEPROM_24LC32A_MAX_ADDRESS,
    .page_size = EEPROM_24LC32A_PAGE_SIZE
};

static bool reload_config(rfm95_eeprom_config_t *config)
{
    return eeprom_read_bytes(&eeprom_handle, 0x0000, (uint8_t*)config, sizeof(rfm95_eeprom_config_t));
}

static void save_config(const rfm95_eeprom_config_t *config)
{
    eeprom_write_bytes(&eeprom_handle, 0x0000, (uint8_t*)config, sizeof(rfm95_eeprom_config_t));
}
```

### Enabling Downlink Functionality 

If you want to use the library's ability to receive downlink messages with MAC commands that can configure the end device, you need to provide precision clock and delay functionality as well as a few other functions.
An example implementation using an STM32L4's LPTIM1 might look something like this:

```c

// Forward declaration of the functions.
static uint32_t get_precision_tick();
static void precision_sleep_until(uint32_t target_ticks);
static uint8_t random_int(uint8_t max);
static uint8_t get_battery_level();

// Create the handle for the RFM95 module.
rfm95_handle_t rfm95_handle = {
    // ... see example above
    .precision_tick_frequency = 32768,
    .precision_tick_drift_ns_per_s = 5000,
    .receive_mode = RFM95_RECEIVE_MODE_RX12,
    .get_precision_tick = get_precision_tick,
    .precision_sleep_until = precision_sleep_until,
    .random_int = random_int,
    .get_battery_level = get_battery_level
};

volatile uint32_t lptim_tick_msb = 0;

static uint32_t get_precision_tick()
{
    __disable_irq();
    uint32_t precision_tick = lptim_tick_msb | HAL_LPTIM_ReadCounter(&hlptim1);
    __enable_irq();
    return precision_tick;
}

void HAL_LPTIM_AutoReloadMatchCallback(LPTIM_HandleTypeDef *hlptim)
{
    lptim_tick_msb += 0x10000;
}

static void precision_sleep_until(uint32_t target_ticks)
{
    while (true) {

    uint32_t start_ticks = get_precision_tick();
    if (start_ticks > target_ticks) {
        break;
    }

    uint32_t ticks_to_sleep = target_ticks - start_ticks;

    // Only use sleep for at least 10 ticks.
    if (ticks_to_sleep >= 10) {

        // Calculate required value of compare register for the sleep minus a small buffer time to compensate
        // for any ticks that occur while we perform this calculation.
        uint32_t compare = (start_ticks & 0xffff) + ticks_to_sleep - 2;

        // If the counter auto-reloads we will be woken up anyway.
        if (compare > 0xffff) {
            HAL_SuspendTick();
            HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
            HAL_ResumeTick();

        // Otherwise, set compare register and use the compare match interrupt to wake up in time.
        } else {
            __HAL_LPTIM_COMPARE_SET(&hlptim1, compare);
            while (!__HAL_LPTIM_GET_FLAG(&hlptim1, LPTIM_FLAG_CMPOK));
            __HAL_LPTIM_CLEAR_FLAG(&hlptim1, LPTIM_FLAG_CMPM);
            __HAL_LPTIM_ENABLE_IT(&hlptim1, LPTIM_IT_CMPM);
            HAL_SuspendTick();
            HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
            HAL_ResumeTick();
            __HAL_LPTIM_DISABLE_IT(&hlptim1, LPTIM_IT_CMPM);
        }
    } else {
        break;
    }

    // Busy wait until we have reached the target.
    while (get_precision_tick() < target_ticks);
}

static uint8_t random_int(uint8_t max)
{
    return 0; // Use ADC other means of obtaining a random number.
}

static uint8_t get_battery_level()
{
    return 0xff; // 0xff = Unknown battery level.
}
```

## Supported Platforms
STM32L0, STM32L4 and STM32F4 microcontrollers are supported. The HAL header includes for other microcontrollers may be added in `rfm95.h`.
