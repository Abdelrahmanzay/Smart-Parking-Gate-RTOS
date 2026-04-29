#include "uart.h"

#include "tm4c123gh6pm.h"

#define PIN(n)                  (1UL << (n))
#define UART0_DEFAULT_CLOCK_HZ  16000000UL
#define UART_CLOCK_SETTLE_DELAY 1000UL

extern uint32_t SystemCoreClock;

static void UART0_ShortDelay(void)
{
    volatile uint32_t delay;

    for (delay = 0U; delay < UART_CLOCK_SETTLE_DELAY; delay++) {
    }
}

static uint32_t UART0_ClockHz(void)
{
    return (SystemCoreClock != 0U) ? SystemCoreClock : UART0_DEFAULT_CLOCK_HZ;
}

void UART0_Init(uint32_t baud_rate)
{
    uint32_t clock_hz;
    uint32_t divisor_x64;

    SYSCTL_RCGCUART_R |= PIN(0);
    SYSCTL_RCGCGPIO_R |= PIN(0);

    /*
     * The simulator is stricter than the real TM4C about some SYSCTL_PR*
     * reads. A short delay is enough after enabling UART0 and GPIOA clocks.
     */
    UART0_ShortDelay();

    UART0_CTL_R &= ~0x00000001UL;

    GPIO_PORTA_AMSEL_R &= ~0x03UL;
    GPIO_PORTA_AFSEL_R |= 0x03UL;
    GPIO_PORTA_PCTL_R = (GPIO_PORTA_PCTL_R & ~0x000000FFUL) | 0x00000011UL;
    GPIO_PORTA_DEN_R |= 0x03UL;

    clock_hz = UART0_ClockHz();
    divisor_x64 = ((clock_hz * 4U) + (baud_rate / 2U)) / baud_rate;

    UART0_CC_R = 0x0UL;                 /* System clock. */
    UART0_IBRD_R = divisor_x64 / 64U;
    UART0_FBRD_R = divisor_x64 & 0x3FU;
    UART0_LCRH_R = 0x00000070UL;        /* 8-bit, FIFO enabled. */
    UART0_ICR_R = 0x7FFUL;
    UART0_CTL_R = 0x00000301UL;         /* UARTEN + TXE + RXE. */
}

void UART0_WriteChar(char c)
{
    while ((UART0_FR_R & UART_FR_TXFF) != 0U) {
    }
    UART0_DR_R = (uint32_t)c;
}

void UART0_WriteString(const char *text)
{
    while (*text != '\0') {
        UART0_WriteChar(*text);
        text++;
    }
}

void UART0_WriteLine(const char *text)
{
    UART0_WriteString(text);
    UART0_WriteString("\r\n");
}

void UART0_WriteUInt(uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;

    if (value == 0U) {
        UART0_WriteChar('0');
        return;
    }

    while ((value > 0U) && (count < sizeof(digits))) {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count++;
    }

    while (count > 0U) {
        count--;
        UART0_WriteChar(digits[count]);
    }
}
