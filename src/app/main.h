#pragma once
#include <stm32g4xx_hal.h>

void Error_Handler(void);

// LED config
#define LED_GPIO_PORT	GPIOA
#define LED_GPIO_PIN	GPIO_PIN_13

#define BLINK_MIN_MS	100U
#define BLINK_MAX_MS	1000U

// Potentiometer config
#define POT_GPIO_PORT	GPIOB
#define POT_GPIO_PIN	GPIO_PIN_12
#define POT_ADC_INST	ADC1

#define POT_SAMPLE_MS	20U
#define POT_FILTER_N	8U

#define POT_ADC_MAX	4095U
#define POT_CAN_ID	0x100U

// CAN config
#define CAN_GPIO_PORT	GPIOA
#define CAN_GPIO_PIN_TX	GPIO_PIN_11
#define CAN_GPIO_PIN_RX	GPIO_PIN_12

#define CAN_INST	FDCAN1
#define CAN_BAUD	500000U

// CAN messaging
#define ROLE_TX	1
#define ROLE_RX 2
#ifndef ROLE
#define ROLE ROLE_TX
#endif
