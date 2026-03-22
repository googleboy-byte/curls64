#include "keyboard.h"
#include "../../cpu/ports.h"
#include "../../cpu/isr.h"
#include "screen.h"
#include "../../../libc/string.h"
#include "../../../libc/function.h"
#include "../../../include/module/module_abi_v1.h"
#include "uart.h"
#include <stdint.h>

#define BACKSPACE 0x0E
#define ENTER 0x1C



#define SC_MAX 58
const char *sc_name[] = { "ERROR", "Esc", "1", "2", "3", "4", "5", "6", 
    "7", "8", "9", "0", "-", "=", "Backspace", "Tab", "Q", "W", "E", 
        "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "Lctrl", 
        "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "`", 
        "LShift", "\\", "Z", "X", "C", "V", "B", "N", "M", ",", ".", 
        "/", "RShift", "Keypad *", "LAlt", "Spacebar"};
const char sc_ascii[] = { '?', '?', '1', '2', '3', '4', '5', '6',     
    '7', '8', '9', '0', '-', '=', '?', '?', 'q', 'w', 'e', 'r', 't', 'y', 
        'u', 'i', 'o', 'p', '[', ']', '?', '?', 'a', 's', 'd', 'f', 'g', 
        'h', 'j', 'k', 'l', ';', '\'', '`', '?', '\\', 'z', 'x', 'c', 'v', 
        'b', 'n', 'm', ',', '.', '/', '?', '?', '?', ' '};

/* shift, ctrl and caps lock state */
static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int caps_lock = 0;

// Circular Input Queue for Raw Characters
#define KB_QUEUE_SIZE 256
static char kb_queue[KB_QUEUE_SIZE];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;

static void kb_enqueue(char c) {
    int next = (kb_head + 1) % KB_QUEUE_SIZE;
    if (next != kb_tail) {
        kb_queue[kb_head] = c;
        kb_head = next;
    }
}

static int kb_dequeue(char *c) {
    if (kb_head == kb_tail) return 0;
    *c = kb_queue[kb_tail];
    kb_tail = (kb_tail + 1) % KB_QUEUE_SIZE;
    return 1;
}

volatile uint32_t kb_irq_count = 0;

static void keyboard_callback(void *regs_ptr) {
    kb_irq_count++;
    registers_t *regs = (registers_t *)regs_ptr;
    /* The PIC leaves us the scancode in port 0x60 */
    uint8_t scancode = port_byte_in(0x60);
    
    char trace_msg[32], temp[16];
    for(int _i=0; _i<32; _i++) trace_msg[_i] = 0;
    // basic copy:
    trace_msg[0] = '['; trace_msg[1] = 'K'; trace_msg[2] = 'B'; trace_msg[3] = ']';
    trace_msg[4] = ' '; trace_msg[5] = 's'; trace_msg[6] = 'c'; trace_msg[7] = 'a';
    trace_msg[8] = 'n'; trace_msg[9] = ':'; trace_msg[10] = ' '; trace_msg[11] = '0';
    trace_msg[12] = 'x'; trace_msg[13] = '\0';
    hex_to_ascii(scancode, temp);
    strcat(trace_msg, temp);
    strcat(trace_msg, "\n");
    kprint(trace_msg);

    // Caps Lock toggle (0x3A)
    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }

    // Shift tracking
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return;
    }
    // Ctrl tracking
    if (scancode == 0x1D) {
        ctrl_pressed = 1;
        return;
    }
    if (scancode == 0x9D) {
        ctrl_pressed = 0;
        return;
    }

    if (scancode >= SC_MAX) {
        // Arrow keys
        if (scancode == 0x48) { kb_enqueue(ctrl_pressed ? 0x85 : 0x81); return; } // UP
        if (scancode == 0x50) { kb_enqueue(ctrl_pressed ? 0x86 : 0x82); return; } // DOWN
        if (scancode == 0x4B) { kb_enqueue(ctrl_pressed ? 0x87 : 0x83); return; } // LEFT
        if (scancode == 0x4D) { kb_enqueue(ctrl_pressed ? 0x88 : 0x84); return; } // RIGHT
        return;
    }

    // Check for Ctrl+C (0x2E is 'C')
    if (ctrl_pressed && scancode == 0x2E) {
        kabi_kill_all_children();
        return;
    }

    if (scancode == BACKSPACE) {
        kb_enqueue(0x08);
    } else if (scancode == ENTER) {
        kb_enqueue('\n');
    } else {
        char letter = sc_ascii[(int)scancode];
        
        if (letter == '?') return;

        // Handle Ctrl combinations
        if (ctrl_pressed) {
            if (letter >= 'a' && letter <= 'z') {
                letter = letter - 'a' + 1; // Ctrl+A = 1, Ctrl+B = 2, ..., Ctrl+Z = 26
            }
        } 
        // Handle Caps Lock and Shift for letters
        else if (letter >= 'a' && letter <= 'z') {
            // XOR logic: if one is on but not both, uppercase
            if (shift_pressed ^ caps_lock) {
                letter -= 32;
            }
        } 
        // Handle other Shift-modified characters
        else if (shift_pressed) {
            if (letter >= '0' && letter <= '9') {
                /* Hardcoded specific common US layout shifts */
                char *symbols = ")!@#$%^&*("; // 0123456789
                letter = symbols[letter - '0'];
            } else if (letter == '-') {
                letter = '_';
            } else if (letter == '=') {
                letter = '+';
            } else if (letter == ';') {
                letter = ':';
            } else if (letter == '[') {
                letter = '{';
            } else if (letter == ']') {
                letter = '}';
            } else if (letter == ',') {
                letter = '<';
            } else if (letter == '.') {
                letter = '>';
            } else if (letter == '/') {
                letter = '?';
            } else if (letter == '\'') {
                letter = '\"';
            } else if (letter == '`') {
                letter = '~';
            } else if (letter == '\\') {
                letter = '|';
            }
        }

        kb_enqueue(letter);
    }
    UNUSED(regs);
}

void keyboard_handle_char(char c) {
    if (c == '\r') c = '\n'; // Normalize serial carriage return
    kb_enqueue(c);
}

void get_line(char *buf) {
    int i = 0;
    while (1) {
        char c;
        while (!kb_dequeue(&c)) {
            asm volatile("sti; hlt"); 
        }

        if (c == '\n') {
            kprint("\n");
            buf[i] = '\0';
            break;
        } else if (c == 0x08 || c == 0x7F) { // Backspace
            if (i > 0) {
                i--;
                kprint_backspace();
            }
        } else {
            if (i < 255) {
                buf[i++] = c;
                char str[2] = {c, '\0'};
                kprint(str);
            }
        }
    }
}

void get_char_noecho(char *c) {
    while (!kb_dequeue(c)) {
        asm volatile("sti; hlt");
    }
}

void init_keyboard() {
   kabi_irq_register(1, keyboard_callback); 
}

static void ps2_wait_input(void) {
    int timeout = 100000;
    while ((port_byte_in(0x64) & 0x02) && --timeout);
}

static void ps2_wait_output(void) {
    int timeout = 100000;
    while (!(port_byte_in(0x64) & 0x01) && --timeout);
}

static void ps2_controller_init(void) {
    ps2_wait_input();
    port_byte_out(0x64, 0xAD); 
    ps2_wait_input();
    port_byte_out(0x64, 0xA7); 

    while (port_byte_in(0x64) & 0x01) {
        port_byte_in(0x60);
    }

    ps2_wait_input();
    port_byte_out(0x64, 0x20); 
    ps2_wait_output();
    uint8_t config = port_byte_in(0x60);

    config |=  0x01; 
    config &= ~0x10; 

    ps2_wait_input();
    port_byte_out(0x64, 0x60); 
    ps2_wait_input();
    port_byte_out(0x60, config);

    ps2_wait_input();
    port_byte_out(0x64, 0xAE);

    kprint("[KB] PS/2 controller initialized, IRQ1 enabled\n");
}

void keyboard_driver_init() {
    ps2_controller_init();
    init_keyboard();
}

/* --- Module Registration --- */
static int keyboard_module_init(void) { keyboard_driver_init(); return 0; }

kabi_module_t __kabi_module_keyboard = {
    .name           = "keyboard",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = keyboard_module_init,
    .exit           = NULL,
    .description    = "PS/2 keyboard driver"
};
