#pragma once
#include <stm32g4xx_hal.h>

void Error_Handler(void);

#define EEPROM_SPI SPI1

#define EEPROM_SIZE 512u
#define EEPROM_PAGE 16u
#define EEPROM_LOCKOUT_MS 500u

bool eeprom_read(uint16_t addr, uint16_t len, uint8_t *out);
bool eeprom_write(uint16_t addr, uint16_t len, const uint8_t *in);
