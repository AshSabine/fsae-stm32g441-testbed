#include "main.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "spi.h"
#include "rtt.h"
#include "gpio.h"

#include "clock.h"
#include "error_handler.h"
#include "core_config.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "semphr.h"

/**   ███████╗███████╗██████╗ ██████╗  ██████╗ ███╗   ███╗
 *    ██╔════╝██╔════╝██╔══██╗██╔══██╗██╔═══██╗████╗ ████║
 *    █████╗  █████╗  ██████╔╝██████╔╝██║   ██║██╔████╔██║
 *    ██╔══╝  ██╔══╝  ██╔═══╝ ██╔══██╗██║   ██║██║╚██╔╝██║
 *    ███████╗███████╗██║     ██║  ██║╚██████╔╝██║ ╚═╝ ██║
 *    ╚══════╝╚══════╝╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚═╝     ╚═╝
 */

/* Instruction opcodes - table 4 */
#define CMD_WRSR 0x01u
#define CMD_WRIT 0x02u
#define CMD_READ 0x03u
#define CMD_WRDI 0x04u
#define CMD_RDSR 0x05u
#define CMD_WREN 0x06u

/* Status register bits - table 6 */
#define SR_WIP (1u << 0)
#define SR_WEL (1u << 1)

/* Write cycle time - tables 18/19 */
#define WRITE_MAX_MS  5u
#define READY_TIMEOUT_MS 10u
#define POLL_DELAY_TICKS 1u

static SemaphoreHandle_t s_lock;
bool eeprom_init(void) {
	s_lock = xSemaphoreCreateMutex();
	return s_lock != NULL;
}

// See table 5; Assembles opcode + address header. Opcode bit 3
// contains A8 (address bit 8), A7..A0 is the second byte sent.
static void mk_hdr(uint8_t cmd, uint16_t addr, uint8_t out[2]) {
	out[0] = (uint8_t)(cmd | (((addr >> 8) & 1u) << 3));
	out[1] = (uint8_t)(addr & 0xFFu);
}

// See table 6. sends a single RDSR instruction to the EEPROM and
// stores the RDSR byte in sr. used to query WEL & WIP bits. 
static bool read_status(uint8_t *sr) {
	uint8_t tx[2] = { CMD_RDSR, 0x00 };
	uint8_t rx[2];

	if (!core_SPI_start(EEPROM_SPI)) return false;
	bool ok = core_SPI_read_write(EEPROM_SPI, tx, sizeof tx, rx, sizeof rx);
	core_SPI_stop(EEPROM_SPI);

	if (ok) *sr = rx[1];
	return ok;
}

//	
static bool write_page(uint16_t addr, uint16_t n, const uint8_t *in) {
	bool ok;
	uint8_t sr;

	// 1): Write WREN as its own transaction
	uint8_t wren = CMD_WREN;
	
	if (!core_SPI_start(EEPROM_SPI)) return false;
	ok = core_SPI_read_write(EEPROM_SPI, &wren, sizeof wren, NULL, 0);
	core_SPI_stop(EEPROM_SPI);
	if (!ok) return false;

	// 2): confirm WEL is set - catches a W pin held low
	if (!read_status(&sr) || !(sr & SR_WEL)) return false;

	// 3): write page
	uint8_t hdr[2];
	mk_hdr(CMD_WRIT, addr, hdr);

	if (!core_SPI_start(EEPROM_SPI)) return false;
	ok = core_SPI_read_write(EEPROM_SPI, hdr, sizeof hdr, NULL, 0)
	  && core_SPI_read_write(EEPROM_SPI, (uint8_t *)in, n, NULL, 0);
	core_SPI_stop(EEPROM_SPI);
	if (!ok) return false;

	// 4): poll until WIP clears
	TickType_t timeout = pdMS_TO_TICKS(READY_TIMEOUT_MS) + 1;
	TickType_t t0 = xTaskGetTickCount();
	for(;;) {
		if (!read_status(&sr)) return false;
		if (!(sr & SR_WIP)) return true;
		if ((xTaskGetTickCount() - t0) >= timeout) return false;
		vTaskDelay(POLL_DELAY_TICKS);
	}
}

//	End user functionality
bool eeprom_write(uint16_t addr, uint16_t len, const uint8_t *in) {
	if (in == NULL || len == 0) return false;
	if ((uint32_t)addr + len > EEPROM_SIZE) return false;
	if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(EEPROM_LOCKOUT_MS)) != pdTRUE) return false;
	
	bool ok = true;
	while (ok && len > 0) {
		uint16_t room = EEPROM_PAGE - (addr % EEPROM_PAGE);
		uint16_t chunk = (len < room) ? len : room;
		ok = write_page(addr, chunk, in);
		addr += chunk; in += chunk; len -= chunk;
	}

	xSemaphoreGive(s_lock);
	return ok;
}

bool eeprom_read(uint16_t addr, uint16_t len, uint8_t *out) {
	if (len == 0) return false;
	if ((uint32_t)addr + len > EEPROM_SIZE) return false;
	if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(EEPROM_LOCKOUT_MS)) != pdTRUE) return false;
	
	uint8_t hdr[2];
	mk_hdr(CMD_READ, addr, hdr);

	bool ok = core_SPI_start(EEPROM_SPI);
	if (ok) {
		ok = core_SPI_read_write(EEPROM_SPI, hdr, sizeof hdr, NULL, 0)
		  && core_SPI_read_write(EEPROM_SPI, NULL, 0, out, len);
		core_SPI_stop(EEPROM_SPI);
	}

	xSemaphoreGive(s_lock);
	return ok;
}

/**   ███╗   ███╗ █████╗ ██╗███╗   ██╗
 *    ████╗ ████║██╔══██╗██║████╗  ██║
 *    ██╔████╔██║███████║██║██╔██╗ ██║
 *    ██║╚██╔╝██║██╔══██║██║██║╚██╗██║
 *    ██║ ╚═╝ ██║██║  ██║██║██║ ╚████║
 *    ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝
 */

uint8_t tx_buff[24];
uint8_t rx_buff[24];

uint16_t test_addr = 0x0001 & 0x01FF;

void write_task(void *pvParameters) {
	(void) pvParameters;
	vTaskDelay(pdMS_TO_TICKS(100));
	
	const char *banner = "[EEPROM] M95040-W READY!";
	memcpy(&tx_buff[0], banner, sizeof tx_buff);

	bool ok = eeprom_write(0x0001, sizeof tx_buff, tx_buff);
	if (!ok) error_handler();
	else rprintf("[STM32] Write successful!\n");
}

void read_task(void *pvParameters) {
	(void) pvParameters;
	vTaskDelay(pdMS_TO_TICKS(200));
	
	bool ok = eeprom_read(0x0001, sizeof rx_buff, rx_buff);
	if (!ok) error_handler();
	else rprintf("[STM32] Read: \"%.64s\"\n", (char *)rx_buff);
}

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
	if (!core_SPI_init(SPI1, GPIOA, GPIO_PIN_4)) error_handler();
	if (!eeprom_init()) error_handler();

	//	Common
	core_RTT_init();
	core_heartbeat_init(GPIOB, GPIO_PIN_10);
	core_GPIO_set_heartbeat(GPIO_PIN_RESET);
	
	//		Tasks
	int err;
	//	Common
	err = xTaskCreate(heartbeat_task, "heartbeat", 1000, NULL, 4, NULL);
	if (err != pdPASS) error_handler();
	else rprintf("heartbeat PASS\n");

	err = xTaskCreate(write_task, "write", 1000, NULL, 4, NULL);
	if (err != pdPASS) error_handler();
	else rprintf("write PASS\n");

	err = xTaskCreate(read_task, "read", 1000, NULL, 4, NULL);
	if (err != pdPASS) error_handler();
	else rprintf("read PASS\n");
	
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
