#ifndef UART_H
#define UART_H

#include <stdint.h>

void UART0_Init(uint32_t baud_rate);
void UART0_WriteChar(char c);
void UART0_WriteString(const char *text);
void UART0_WriteLine(const char *text);
void UART0_WriteUInt(uint32_t value);

#endif
