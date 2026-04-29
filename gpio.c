#include "gpio.h"

#include "tm4c123gh6pm.h"

#define PIN(n)                  (1UL << (n))

#define PF_DRIVER_CLOSE         PIN(0)
#define PF_LED_RED              PIN(1)
#define PF_LED_GREEN            PIN(3)
#define PF_DRIVER_OPEN          PIN(4)
#define PF_USED_MASK            (PF_DRIVER_CLOSE | PF_DRIVER_OPEN | PF_LED_RED | PF_LED_GREEN)

#define PB_SECURITY_OPEN        PIN(0)
#define PB_SECURITY_CLOSE       PIN(1)
#define PB_USED_MASK            (PB_SECURITY_OPEN | PB_SECURITY_CLOSE)

#define PD_OPEN_LIMIT           PIN(0)
#define PD_CLOSED_LIMIT         PIN(1)
#define PD_USED_MASK            (PD_OPEN_LIMIT | PD_CLOSED_LIMIT)

#define PE_OBSTACLE_ANALOG      PIN(2)

#define GPIO_LOCK_KEY           0x4C4F434BUL
#define GPIO_CLOCK_SETTLE_DELAY 1000UL

static bool ActiveLowPressed(uint32_t data, uint32_t mask)
{
    return (data & mask) == 0U;
}

static bool ActiveHighPressed(uint32_t data, uint32_t mask)
{
    return (data & mask) != 0U;
}

static void GPIO_ShortDelay(void)
{
    volatile uint32_t delay;

    for (delay = 0U; delay < GPIO_CLOCK_SETTLE_DELAY; delay++) {
    }
}

void GPIO_Init(void)
{
    SYSCTL_RCGCGPIO_R |= PIN(1) | PIN(3) | PIN(4) | PIN(5);
    GPIO_ShortDelay();

    /* Port F: PF4 driver OPEN, PF0 driver CLOSE, PF3 green, PF1 red. */
    GPIO_PORTF_LOCK_R = GPIO_LOCK_KEY;
    GPIO_PORTF_CR_R |= PF_USED_MASK;

    GPIO_PORTF_AMSEL_R &= ~PF_USED_MASK;
    GPIO_PORTF_PCTL_R &= ~0x000FF0FFUL;
    GPIO_PORTF_AFSEL_R &= ~PF_USED_MASK;
    GPIO_PORTF_DIR_R = (GPIO_PORTF_DIR_R | PF_LED_RED | PF_LED_GREEN) &
                       ~(PF_DRIVER_OPEN | PF_DRIVER_CLOSE);
    GPIO_PORTF_PUR_R |= PF_DRIVER_OPEN | PF_DRIVER_CLOSE;
    GPIO_PORTF_DEN_R |= PF_USED_MASK;
    GPIO_PORTF_DATA_R &= ~(PF_LED_RED | PF_LED_GREEN);

    /* Port B: PB0 security OPEN, PB1 security CLOSE. */
    GPIO_PORTB_AMSEL_R &= ~PB_USED_MASK;
    GPIO_PORTB_PCTL_R &= ~0x000000FFUL;
    GPIO_PORTB_AFSEL_R &= ~PB_USED_MASK;
    GPIO_PORTB_DIR_R &= ~PB_USED_MASK;
    GPIO_PORTB_PDR_R |= PB_USED_MASK;
    GPIO_PORTB_DEN_R |= PB_USED_MASK;

    /* Port D: PD0 open limit, PD1 closed limit. */
    GPIO_PORTD_AMSEL_R &= ~PD_USED_MASK;
    GPIO_PORTD_PCTL_R &= ~0x000000FFUL;
    GPIO_PORTD_AFSEL_R &= ~PD_USED_MASK;
    GPIO_PORTD_DIR_R &= ~PD_USED_MASK;
    GPIO_PORTD_PDR_R |= PD_USED_MASK;
    GPIO_PORTD_DEN_R |= PD_USED_MASK;

    /*
     * Port E clock is enabled here for completeness. ADC_Init() owns the
     * analog configuration of the simulator's obstacle input, PE2/AIN1.
     */
    GPIO_PORTE_DEN_R &= ~PE_OBSTACLE_ANALOG;
}

bool GPIO_DriverOpenPressed(void)
{
    return ActiveLowPressed(GPIO_PORTF_DATA_R, PF_DRIVER_OPEN);
}

bool GPIO_DriverClosePressed(void)
{
    return ActiveLowPressed(GPIO_PORTF_DATA_R, PF_DRIVER_CLOSE);
}

bool GPIO_SecurityOpenPressed(void)
{
    return ActiveHighPressed(GPIO_PORTB_DATA_R, PB_SECURITY_OPEN);
}

bool GPIO_SecurityClosePressed(void)
{
    return ActiveHighPressed(GPIO_PORTB_DATA_R, PB_SECURITY_CLOSE);
}

bool GPIO_OpenLimitPressed(void)
{
    return ActiveHighPressed(GPIO_PORTD_DATA_R, PD_OPEN_LIMIT);
}

bool GPIO_ClosedLimitPressed(void)
{
    return ActiveHighPressed(GPIO_PORTD_DATA_R, PD_CLOSED_LIMIT);
}

void GPIO_SetMotionLeds(bool opening, bool closing)
{
    uint32_t data = GPIO_PORTF_DATA_R & ~(PF_LED_RED | PF_LED_GREEN);

    if (opening) {
        data |= PF_LED_GREEN;
    }
    if (closing) {
        data |= PF_LED_RED;
    }

    GPIO_PORTF_DATA_R = data;
}
