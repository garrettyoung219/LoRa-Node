#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "sx126x_hal.h"
#include "sx126x_hal_context.h"

sx126x_hal_status_t sx126x_hal_write( const void* context,
                                      const uint8_t* command,
                                      const uint16_t command_length,
                                      const uint8_t* data,
                                      const uint16_t data_length ) {
    // convert void pointer to a context pointer
    const lora_spi_context_t * context_rp2040_spi = (const lora_spi_context_t *) context;

    while(gpio_get(context_rp2040_spi->busy_pin)) // asserts high if it's busy.
        {
            tight_loop_contents(); // placeholder for don't do anything
        }

    gpio_put(context_rp2040_spi->nss_pin, 0); // Chip select goes low
    // send command_length bytes from command over SPI, ignoring any incoming data
    spi_write_blocking(context_rp2040_spi->spihw, command, command_length);
    // send data_length bytes from data over SPI, ignoring any incoming data
    spi_write_blocking(context_rp2040_spi->spihw, data, data_length);
    gpio_put(context_rp2040_spi->nss_pin, 1); // Chip select goes high

    return SX126X_HAL_STATUS_OK;
}




sx126x_hal_status_t sx126x_hal_read( const void* context,
                                     const uint8_t* command,
                                     const uint16_t command_length,
                                     uint8_t* data,
                                     const uint16_t data_length ) {

    // convert void pointer to a context pointer
    const lora_spi_context_t * context_rp2040_spi = (const lora_spi_context_t *) context;

    while(gpio_get(context_rp2040_spi->busy_pin)) // asserts high if it's busy.
        {
            tight_loop_contents(); // placeholder for don't do anything
        }

    gpio_put(context_rp2040_spi->nss_pin, 0); // Chip select goes low
    // send command_length bytes from bufferTX over SPI, ignoring any incoming data
    spi_write_blocking(context_rp2040_spi->spihw, command, command_length);
    // send data_length bytes of 0s while putting data_length bytes of data in to data                       
    spi_read_blocking(context_rp2040_spi->spihw, SX126X_NOP, data, data_length);
    gpio_put(context_rp2040_spi->nss_pin, 1); // Chip select goes high

    return SX126X_HAL_STATUS_OK;
}
