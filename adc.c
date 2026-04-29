#include "adc.h"

#include "tm4c123gh6pm.h"

#define PIN(n)                  (1UL << (n))
#define ADC_SS3                 PIN(3)
#define ADC_OBSTACLE_PIN        PIN(2)
#define ADC_CHANNEL_PE2_AIN1    1U
#define ADC_TIMEOUT_COUNT       100000UL
#define ADC_CLOCK_SETTLE_DELAY  1000UL

static void ADC_ShortDelay(void)
{
    volatile uint32_t delay;

    for (delay = 0U; delay < ADC_CLOCK_SETTLE_DELAY; delay++) {
    }
}

void ADC0_Init(void)
{
    SYSCTL_RCGCGPIO_R |= PIN(4);
    SYSCTL_RCGCADC_R |= PIN(0);

    /*
     * Real TM4C hardware supports polling SYSCTL_PRGPIO_R/SYSCTL_PRADC_R
     * here, but the Keil/TExaS simulator model used by this project rejects
     * some SYSCTL_PR* reads. A short settle delay avoids access violations.
     */
    ADC_ShortDelay();

    /*
     * The TExaS LaunchPadDLL simulator connects the slide-pot ADC input to
     * PE2/AIN1 in this project file. Configure SS3 for channel 1 to match it.
     */
    GPIO_PORTE_DIR_R &= ~ADC_OBSTACLE_PIN;
    GPIO_PORTE_AFSEL_R |= ADC_OBSTACLE_PIN;
    GPIO_PORTE_DEN_R &= ~ADC_OBSTACLE_PIN;
    GPIO_PORTE_AMSEL_R |= ADC_OBSTACLE_PIN;
    GPIO_PORTE_PCTL_R &= ~0x00000F00UL;

    ADC0_ACTSS_R &= ~ADC_SS3;
    ADC0_EMUX_R &= ~0x0000F000UL;       /* SS3 processor trigger. */
    ADC0_SSMUX3_R = ADC_CHANNEL_PE2_AIN1;
    ADC0_SSCTL3_R = 0x00000006UL;       /* END0 + IE0. */
    ADC0_IM_R &= ~ADC_SS3;
    ADC0_ISC_R = ADC_SS3;
    ADC0_ACTSS_R |= ADC_SS3;
}

uint16_t ADC0_ReadObstacleRaw(void)
{
    uint32_t timeout = ADC_TIMEOUT_COUNT;

    ADC0_PSSI_R = ADC_SS3;
    while (((ADC0_RIS_R & ADC_SS3) == 0U) && (timeout > 0U)) {
        timeout--;
    }

    if (timeout == 0U) {
        return 0U;
    }

    {
        uint16_t value = (uint16_t)(ADC0_SSFIFO3_R & 0x0FFFU);
        ADC0_ISC_R = ADC_SS3;
        return value;
    }
}

bool ADC0_IsObstacle(uint16_t *raw_value)
{
    uint16_t value = ADC0_ReadObstacleRaw();

    if (raw_value != 0) {
        *raw_value = value;
    }

    return value > ADC_OBSTACLE_THRESHOLD;
}
