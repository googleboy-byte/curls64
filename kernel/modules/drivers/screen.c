#include "screen.h"
#include "../../cpu/ports.h"
#include "../../../libc/mem.h"
#include "../../../libc/string.h"
#include "uart.h"
#include "fb.h"
#include "../../core/boot_info.h"
#include "../../../include/module/module_abi_v1.h"
#include <stdint.h>

/* Declaration of private functions */
int get_cursor_offset();
void set_cursor_offset(int offset);
int print_char(char c, int col, int row, char attr);
int get_offset(int col, int row);
int get_offset_row(int offset);
int get_offset_col(int offset);

typedef enum {
    SCREEN_MODE_VGA_TEXT = 0,
    SCREEN_MODE_FB_TEXT  = 1
} screen_mode_t;

static screen_mode_t screen_mode = SCREEN_MODE_VGA_TEXT;

/**********************************************************
 * Public Kernel API functions                            *
 **********************************************************/

/**
 * Print a message on the specified location
 * If col, row, are negative, we will use the current offset
 */
void kprint_at(const char *message, int col, int row) {
    /* Set cursor if col/row are negative */
    int offset;
    if (col >= 0 && row >= 0)
        offset = get_offset(col, row);
    else {
        offset = get_cursor_offset();
        row = get_offset_row(offset);
        col = get_offset_col(offset);
    }

    /* Loop through message and print it */
    int i = 0;
    while (message[i] != 0) {
        offset = print_char(message[i++], col, row, GREEN_ON_BLACK);
        /* Compute row/col for next iteration */
        row = get_offset_row(offset);
        col = get_offset_col(offset);
    }
}

void kprint(const char *message) {
    kprint_at(message, -1, -1);
}

void kprint_backspace() {
    int offset = get_cursor_offset()-2;
    int row = get_offset_row(offset);
    int col = get_offset_col(offset);
    print_char(0x08, col, row, WHITE_ON_BLACK);
    
    /* Erase character in serial terminal */
    // print_char(0x08) already sent the first '\b'
    uart_send(' ');
    uart_send('\b');
}


/**********************************************************
 * Private kernel functions                               *
 **********************************************************/


/**
 * Innermost print function for our kernel, directly accesses the video memory 
 *
 * If 'col' and 'row' are negative, we will print at current cursor location
 * If 'attr' is zero it will use 'white on black' as default
 * Returns the offset of the next character
 * Sets the video cursor to the returned offset
 */
int print_char(char c, int col, int row, char attr) {
    uint8_t *vidmem = (uint8_t*) (uintptr_t) VIDEO_ADDRESS;
    if (!attr) attr = WHITE_ON_BLACK;

    /* Also print to serial for debugging in -nographic */
    if (c == '\n') uart_send('\r');
    uart_send(c);

    /* Error control: print a red 'E' if the coords aren't right */
    if (col >= MAX_COLS || row >= MAX_ROWS) {
        vidmem[2*(MAX_COLS)*(MAX_ROWS)-2] = 'E';
        vidmem[2*(MAX_COLS)*(MAX_ROWS)-1] = RED_ON_WHITE;
        return get_offset(col, row);
    }

    int offset;
    if (col >= 0 && row >= 0) offset = get_offset(col, row);
    else offset = get_cursor_offset();

    if (screen_mode == SCREEN_MODE_FB_TEXT) {
        /* Software cursor based on character grid */
        if (c == '\n') {
            row = get_offset_row(offset);
            row += 1;
            col = 0;
            offset = get_offset(col, row);
        } else if (c == 0x08) {
            if (offset > 0) {
                offset -= 2;
                row = get_offset_row(offset);
                col = get_offset_col(offset);
                fb_put_char_cell(col, row, ' ', 0xFFFFFF, 0x000000);
            }
        } else {
            row = get_offset_row(offset);
            col = get_offset_col(offset);
            fb_put_char_cell(col, row, c, 0x00FF00, 0x000000);
            offset += 2;
        }

        /* Scroll if needed */
        if (offset >= MAX_ROWS * MAX_COLS * 2) {
            /* Scroll framebuffer up by one text row */
            uint32_t row_bytes = fb_driver.pitch * 16; /* FB_FONT_HEIGHT */
            uint8_t *base = fb_driver.virt_addr;
            uint32_t total_rows = fb_driver.height / 16;
            if (total_rows >= (uint32_t)MAX_ROWS) {
                memory_copy(base + row_bytes, base, row_bytes * (MAX_ROWS - 1));
                /* Clear last line */
                memory_set(base + row_bytes * (MAX_ROWS - 1), 0, row_bytes);
            }
            offset -= 2 * MAX_COLS;
        }

        set_cursor_offset(offset);
        return offset;
    } else {
        if (offset >= MAX_ROWS * MAX_COLS * 2) {
            int i;
            for (i = 1; i < MAX_ROWS; i++) 
                memory_copy((uint8_t*)(uintptr_t)(get_offset(0, i) + VIDEO_ADDRESS),
                            (uint8_t*)(uintptr_t)(get_offset(0, i-1) + VIDEO_ADDRESS),
                            MAX_COLS * 2);

            /* Blank last line */
            char *last_line = (char*) (uintptr_t) (get_offset(0, MAX_ROWS-1) + VIDEO_ADDRESS);
            for (i = 0; i < MAX_COLS * 2; i++) last_line[i] = 0;

            offset -= 2 * MAX_COLS;
        }

        if (c == '\n') {
            row = get_offset_row(offset);
            offset = get_offset(0, row+1);
        } else if (c == 0x08) { /* Backspace */
            vidmem[offset] = ' ';
            vidmem[offset+1] = attr;
        } else {
            vidmem[offset] = c;
            vidmem[offset+1] = attr;
            offset += 2;
        }

        set_cursor_offset(offset);
        return offset;
    }
}

int get_cursor_offset() {
    /* Use the VGA ports to get the current cursor position
     * 1. Ask for high byte of the cursor offset (data 14)
     * 2. Ask for low byte (data 15)
     */
    port_byte_out(REG_SCREEN_CTRL, 14);
    int offset = port_byte_in(REG_SCREEN_DATA) << 8; /* High byte: << 8 */
    port_byte_out(REG_SCREEN_CTRL, 15);
    offset += port_byte_in(REG_SCREEN_DATA);
    return offset * 2; /* Position * size of character cell */
}

void set_cursor_offset(int offset) {
    /* Similar to get_cursor_offset, but instead of reading we write data */
    offset /= 2;
    port_byte_out(REG_SCREEN_CTRL, 14);
    port_byte_out(REG_SCREEN_DATA, (uint8_t)(offset >> 8));
    port_byte_out(REG_SCREEN_CTRL, 15);
    port_byte_out(REG_SCREEN_DATA, (uint8_t)(offset & 0xff));
}

void clear_screen() {
    if (screen_mode == SCREEN_MODE_FB_TEXT && fb_driver.virt_addr) {
        fb_clear(0x000000);
        set_cursor_offset(get_offset(0, 0));
        uart_send_string("\033[2J\033[H");
    } else {
        int screen_size = MAX_COLS * MAX_ROWS;
        int i;
        uint8_t *screen = (uint8_t*) (uintptr_t) VIDEO_ADDRESS;

        for (i = 0; i < screen_size; i++) {
            screen[i*2] = ' ';
            screen[i*2+1] = WHITE_ON_BLACK;
        }
        set_cursor_offset(get_offset(0, 0));

        /* Clear serial terminal as well (ANSI escape sequence) */
        uart_send_string("\033[2J\033[H");
    }
}


int get_offset(int col, int row) { return 2 * (row * MAX_COLS + col); }
int get_offset_row(int offset) { return offset / (2 * MAX_COLS); }
int get_offset_col(int offset) { return (offset - (get_offset_row(offset)*2*MAX_COLS))/2; }

void set_cursor_position(int col, int row) {
    int offset = get_offset(col, row);
    set_cursor_offset(offset);

    // Send ANSI escape sequence to UART for terminal support (e.g. QEMU -nographic)
    char buf[16];
    char num[8];
    strcpy(buf, "\033[");
    int_to_ascii(row + 1, num);
    strcat(buf, num);
    strcat(buf, ";");
    int_to_ascii(col + 1, num);
    strcat(buf, num);
    strcat(buf, "H");
    uart_send_string(buf);
}

void screen_driver_init() {
    /* If GRUB provided a framebuffer, prefer that for output. */
    if (boot_fb_info.present && fb_init_from_bootinfo(&boot_fb_info) == 0) {
        screen_mode = SCREEN_MODE_FB_TEXT;
        clear_screen();
        kprint("[DRIVER] Screen (Framebuffer) initialized.\n");
    } else {
        screen_mode = SCREEN_MODE_VGA_TEXT;
        clear_screen();
        kprint("[DRIVER] Screen (VGA) initialized.\n");
    }
}

/* --- Module Registration --- */
static int screen_module_init(void) { screen_driver_init(); return 0; }

kabi_module_t __kabi_module_screen = {
    .name           = "screen",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = screen_module_init,
    .exit           = NULL,
    .description    = "VGA text-mode display driver"
};
