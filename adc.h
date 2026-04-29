#ifndef ADC_H
#define ADC_H

#include <stdbool.h>
#include <stdint.h>

#define ADC_OBSTACLE_THRESHOLD  2048U

void ADC0_Init(void);
uint16_t ADC0_ReadObstacleRaw(void);
bool ADC0_IsObstacle(uint16_t *raw_value);

#endif
