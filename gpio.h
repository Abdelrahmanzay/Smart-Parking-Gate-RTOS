#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>
#include <stdint.h>

void GPIO_Init(void);

bool GPIO_DriverOpenPressed(void);
bool GPIO_DriverClosePressed(void);
bool GPIO_SecurityOpenPressed(void);
bool GPIO_SecurityClosePressed(void);
bool GPIO_OpenLimitPressed(void);
bool GPIO_ClosedLimitPressed(void);

void GPIO_SetMotionLeds(bool opening, bool closing);

#endif
