#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "adc.h"
#include "events.h"
#include "gpio.h"
#include "tasks.h"
#include "uart.h"

#define UART_BAUD_RATE  9600U

extern void SystemCoreClockUpdate(void);

static GateState_t DetectInitialGateState(void)
{
    bool open_limit = GPIO_OpenLimitPressed();
    bool closed_limit = GPIO_ClosedLimitPressed();

    if (open_limit && !closed_limit) {
        return GATE_STATE_IDLE_OPEN;
    }
    if (closed_limit && !open_limit) {
        return GATE_STATE_IDLE_CLOSED;
    }

    return GATE_STATE_STOPPED_MIDWAY;
}

int main(void)
{
    SystemCoreClockUpdate();

    GPIO_Init();
    ADC0_Init();
    UART0_Init(UART_BAUD_RATE);

    Tasks_Init(DetectInitialGateState());

    vTaskStartScheduler();

    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}
