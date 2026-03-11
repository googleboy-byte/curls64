#ifndef UART_H
#define UART_H

#include <stdint.h>

#define COM1 0x3F8

void uart_init();
void uart_callback(void *regs_ptr);
void uart_send(char c);
void uart_send_string(const char *s);

#endif
