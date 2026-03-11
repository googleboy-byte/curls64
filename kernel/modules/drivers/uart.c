#include "uart.h"
#include "keyboard.h"
#include "../../cpu/ports.h"
#include "../../../include/module/module_abi_v1.h"

void uart_init() {
    port_byte_out(COM1 + 1, 0x00);    // Disable all interrupts
    port_byte_out(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    port_byte_out(COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    port_byte_out(COM1 + 1, 0x00);    //                  (hi byte)
    port_byte_out(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    port_byte_out(COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    port_byte_out(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    port_byte_out(COM1 + 1, 0x01);    // Enable interrupts
    kabi_irq_register(4, uart_callback);
}

void uart_callback(void *regs_ptr) {
    (void)regs_ptr;
    static int ansi_state = 0;
    while (port_byte_in(COM1 + 5) & 0x01) {
        char c = port_byte_in(COM1);
        if (c == 0x03) { // Ctrl+C
            kabi_kill_all_children();
        } else {
            // Very simple ANSI escape sequence handler for arrow keys: ESC [ A/B/C/D
            if (ansi_state == 0 && c == 0x1B) {
                ansi_state = 1;
            } else if (ansi_state == 1 && c == '[') {
                ansi_state = 2;
            } else if (ansi_state == 2) {
                if (c == '1') ansi_state = 3;
                else if (c == 'A') keyboard_handle_char(0x81); // UP
                else if (c == 'B') keyboard_handle_char(0x82); // DOWN
                else if (c == 'C') keyboard_handle_char(0x84); // RIGHT
                else if (c == 'D') keyboard_handle_char(0x83); // LEFT
                else ansi_state = 0;
                if (ansi_state == 2) ansi_state = 0;
            } else if (ansi_state == 3 && c == ';') {
                ansi_state = 4;
            } else if (ansi_state == 4 && c == '5') {
                ansi_state = 5;
            } else if (ansi_state == 5) {
                if (c == 'A') keyboard_handle_char(0x85); // CTRL_UP
                else if (c == 'B') keyboard_handle_char(0x86); // CTRL_DOWN
                else if (c == 'C') keyboard_handle_char(0x88); // CTRL_RIGHT
                else if (c == 'D') keyboard_handle_char(0x87); // CTRL_LEFT
                ansi_state = 0;
            } else {
                ansi_state = 0;
                keyboard_handle_char(c);
            }
        }
    }
}

static int is_transmit_empty() {
    return port_byte_in(COM1 + 5) & 0x20;
}

void uart_send(char c) {
    int timeout = 10000;
    while (is_transmit_empty() == 0 && timeout > 0) timeout--;
    port_byte_out(COM1, c);
}

void uart_send_string(char *s) {
    for (int i = 0; s[i] != '\0'; i++) {
        uart_send(s[i]);
    }
}

/* --- Module Registration --- */
static int uart_module_init(void) { uart_init(); return 0; }

kabi_module_t __kabi_module_uart = {
    .name           = "uart",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = uart_module_init,
    .exit           = NULL,
    .description    = "COM1 serial UART driver"
};
