#include "main.h"

#include <stdbool.h>
#include <stdio.h>

#include "can.h"
#include "clock.h"
#include "gpio.h"
#include "filter.h"
#include "error_handler.h"
#include "core_config.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#if ROLE == ROLE_TX
static core_filter_t g_pot_filt = {
	.type = Filter_ROLLING_AVG,
	.orderX = POT_FILTER_N,
};

static bool init_pot_adc(void) {
	if (!core_ADC_init(POT_ADC_INST)) return false;
	if (!core_ADC_setup_pin(POT_GPIO_PORT, POT_GPIO_PIN, 0)) return false;
	return true;
}

//	This pushes data to the software queue. A seperate function moves data from
//	the software queue to the hardware queue (which must be isolated).
static volatile uint16_t g_pot_out = POT_ADC_MAX;
static bool task_pot_sample(void *pvParameters) {
	(void) pvParameters;
	TickType_t nextWakeTime = xTaskGetTickCount();
	for (;;) {
		//	Read data from ADC
		uint16_t raw;
		if (core_ADC_read_channel(PORT_GPIO_PORT*, PORT_GPIO_PIN*, &raw)) {
			float filt = core_filter_update(raw, &g_pot_filt);
			g_pot_out = (uint16_t)(filt + 0.5f);
		}

		//	Push to software queue
		if (!core_CAN_add_message_to_tx_queue(
			CAN_INST, POT_CAN_ID, 2,
			(uint64_t)g_pot_out) return false;

		vTaskDelayUntil(&nextWakeTime, pdMS_TO_TICKS(POT_SAMPLE_MS));
	}
}

#else
#define BLINK_RANGE (BLINK_MAX_MS - BLINK_MIN_MS)

static volatile uint16_t g_adc_val = 0;
static volatile uint16_t g_blink_ms = BLINK_MIN_MS;

static uint32_t blink_from_adc(uint16_t v) {
	return BLINK_MIN_MS + ((uint32_t)v * BLINK_RANGE) / POT_ADC_MAX;
}

static void task_led_blink(void *pvParameters) {
	(void) pvParameters;
	TickType_t nextWakeTime = xTaskGetTickCount();
	for (;;) {
		//	Find blink time
		g_blink_ms = blink_from_adc(g_adc_val);
		TickType_t half = pdMS_TO_TICKS(g_blink_ms >> 1);

		//	Blink
		core_GPIO_digital_write(LED_GPIO_PORT, LED_GPIO_PIN, true);
		vTaskDelayUntil(&nextWakeTime, half);
		core_GPIO_digital_write(LED_GPIO_PORT, LED_GPIO_PIN, false);
		vTaskDelayUntil(&nextWakeTime, half);
	}
}
#endif

/**    ██████╗ █████╗ ███╗   ██╗
 *    ██╔════╝██╔══██╗████╗  ██║
 *    ██║     ███████║██╔██╗ ██║
 *    ██║     ██╔══██║██║╚██╗██║
 *    ╚██████╗██║  ██║██║ ╚████║
 *     ╚═════╝╚═╝  ╚═╝╚═╝  ╚═══╝
 */
#define CAN_TX_PRIORITY (tskIDLE_PRIORITY + 2)
#define CAN_RX_PRIORITY (tskIDLE_PRIORITY + 2)

static core_CAN_module_t *g_can;
static bool init_CAN(void) {
	if (!core_CAN_init(CAN_INST, CAN_BAUD)) return false;

	g_can = core_CAN_convert(CAN_INST);
	if (g_can == NULL) return false;

	#if ROLE == ROLE_RX
	if (!core_CAN_add_filter(CAN_INST, false, POT_CAN_ID, POT_CAN_ID)) return false;
	#endif

	return true;
}

//	Moves messages from software to hardware queue. Hardware queue is limited to
//	3 messages total and must be closely monitored to avoid packet loss. Moving
//	data in a seperate task prevents this from happening.
#if ROLE == ROLE_TX
static bool CAN_tx_main() {
	core_CAN_send_from_tx_queue_task(CAN_INST);
	return false;
}

static void task_CAN_tx(void *pvParameters) {
	(void) pvParameters;
	CAN_tx_main();
	if (!CAN_tx_main()) error_handler();
}

//	Moves messages from hardware to software queue. Hardware queue is limited to
//	3 messages total and must be closely monitored to avoid packet loss. Moving
//	data in a seperate task prevents this from happening.
#else
static void CAN_rx_main() {
	CanMessage_s msg;
	if (!core_CAN_receive_from_queue(CAN_INST, &msg)) return;
	
	int id = msg.id;
	switch (id) {
		case POT_CAN_ID:
			if (msg.dlc != 2) break;

			uint16 v = (uint16_t)(msg.data & 0xFFFFU);
			if (v > POT_ADC_MAX) v = POT_ADC_MAX;
			g_adc_val = v;
			
			break;
		default: break;
	}
}

static void task_CAN_rx(void *pvParameters) {
	(void) pvParameters;
	for (;;) {CAN_rx_main();}
}
#endif

/**   ███╗   ███╗ █████╗ ██╗███╗   ██╗
 *    ████╗ ████║██╔══██╗██║████╗  ██║
 *    ██╔████╔██║███████║██║██╔██╗ ██║
 *    ██║╚██╔╝██║██╔══██║██║██║╚██╗██║
 *    ██║ ╚═╝ ██║██║  ██║██║██║ ╚████║
 *    ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝
 */
void heartbeat_task(void *pvParameters) {
	(void) pvParameters;
	while(true) {
		core_GPIO_toggle_heartbeat();
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

int main(void) {
	//		Initialization
	HAL_Init();
	
	if (!core_clock_init()) error_handler();
	if (!init_CAN()) error_handler();

	//	Common
	core_heartbeat_init(GPIOB, GPIO_PIN_10);
	core_GPIO_set_heartbeat(GPIO_PIN_RESET);
	
	if (!core_GPIO_init(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL)) error_handler();

	//	Role-dependent
	#if ROLE == ROLE_TX
	if (!init_pot_adc()) error_handler();
	core_filter_init(&g_pot_filt);
	#endif

	//		Tasks
	int err;
	//	Role-dependent
	#if ROLE == ROLE_TX
	err = xTaskCreate(task_CAN_tx, "CAN_tx", 2000, NULL, CAN_TX_PRIORITY, NULL);
	if (err != pdPASS) error_handler();
	
	err = xTaskCreate(task_pot_sample, "pot_sample", 512, NULL, 3, NULL);
	if (err != pdPASS) error_handler();
	#else
	err = xTaskCreate(task_CAN_rx, "CAN_rx", 2000, NULL, CAN_RX_PRIORITY, NULL);
	if (err != pdPASS) error_handler();
	
	err = xTaskCreate(task_led_blink, "led_blink", 512, NULL, 3, NULL);
	if (err != pdPASS) error_handler();
	#endif

	//	Common
	err = xTaskCreate(heartbeat_task, "heartbeat", 1000, NULL, 4, NULL);
	if (err != pdPASS) {
		error_handler();
	}
	
	//	RTOS stuffs
	NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);

	// hand control over to FreeRTOS
	vTaskStartScheduler();

	// we should not get here ever
	error_handler();
	return 1;
}

// Called when stack overflows from rtos
// Not needed in header, since included in FreeRTOS-Kernel/include/task.h
void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName) {
	(void) xTask;
	(void) pcTaskName;

	error_handler();
}
