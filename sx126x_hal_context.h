#ifndef _SX126X_HAL_CONTEXT_H
#define _SX126X_HAL_CONTEXT_H

#include "hardware/spi.h"

typedef struct lora_spi_context_s {
    spi_inst_t * spihw;
    uint8_t nss_pin;
    uint8_t busy_pin;
} lora_spi_context_t;

#endif