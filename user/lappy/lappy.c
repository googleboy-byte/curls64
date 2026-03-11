#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

#define MAX_LINES 1000
#define MAX_LINE_LEN 256
#define CTRL(c) ((c) & 0x1F)

char buffer[MAX_LINES][MAX_LINE_LEN];
int line_count = 0;
char filename[128];
int cursor_x = 0;
int cursor_y = 0;
int scroll_y = 0;
int scroll_x = 0;
const int LINE_NUM_WIDTH = 7;

void load_file(const char *path) {
    int fd = uabi_open(path, UABI_O_RDONLY);
    if (fd < 0) {
        line_count = 1;
        buffer[0][0] = '\0';
        return;
    }

    char chunk[512];
    int bytes;
    int cur_line = 0;
    int cur_char = 0;

    while ((bytes = uabi_read(fd, chunk, 511)) > 0) {
        for (int i = 0; i < bytes; i++) {
            if (chunk[i] == '\n') {
                buffer[cur_line][cur_char] = '\0';
                cur_line++;
                cur_char = 0;
                if (cur_line >= MAX_LINES) break;
            } else if (chunk[i] != '\r') {
                if (cur_char < MAX_LINE_LEN - 1) {
                    buffer[cur_line][cur_char++] = chunk[i];
                }
            }
        }
        if (cur_line >= MAX_LINES) break;
    }
    
    if (cur_char > 0 && cur_line < MAX_LINES) {
        buffer[cur_line][cur_char] = '\0';
        cur_line++;
    } else if (cur_line == 0) {
        buffer[0][0] = '\0';
        cur_line = 1;
    }

    line_count = cur_line;
    uabi_close(fd);
}

void save_file(const char *path) {
    int fd = uabi_open(path, UABI_O_WRONLY | UABI_O_CREAT);
    if (fd < 0) return;

    for (int i = 0; i < line_count; i++) {
        uabi_write(fd, buffer[i], ulib_strlen(buffer[i]));
        uabi_write(fd, "\n", 1);
    }

    uabi_close(fd);
}

void redraw() {
    uabi_clear();
    ulib_gotoxy(0, 0);
    ulib_print("Lappy Editor - v1.1 - ");
    ulib_print(filename);
    ulib_print("\n");
    ulib_print("--------------------------------------------------------------------------------\n");

    int visible_cols = 80 - LINE_NUM_WIDTH;

    for (int i = 0; i < 20; i++) {
        int idx = i + scroll_y;
        if (idx < line_count) {
            // Print line number
            char num_str[16];
            ulib_int_to_str(idx + 1, num_str);
            int len = ulib_strlen(num_str);
            for (int space = 0; space < LINE_NUM_WIDTH - len - 1; space++) ulib_print(" ");
            ulib_print(num_str);
            ulib_print("|");

            // Print visible part of the line
            int line_len = ulib_strlen(buffer[idx]);
            if (scroll_x < line_len) {
                char visible_part[81];
                int to_copy = line_len - scroll_x;
                if (to_copy > visible_cols) to_copy = visible_cols;
                
                for (int j = 0; j < to_copy; j++) {
                    visible_part[j] = buffer[idx][scroll_x + j];
                }
                visible_part[to_copy] = '\0';
                ulib_print(visible_part);
            }
        }
        ulib_print("\n");
    }

    ulib_gotoxy(0, 23);
    ulib_print("--------------------------------------------------------------------------------\n");
    ulib_print("^S: Save    ^Q: Quit    Line: ");
    char s[16];
    ulib_int_to_str(cursor_y + 1, s);
    ulib_print(s);
    ulib_print(" Col: ");
    ulib_int_to_str(cursor_x + 1, s);
    ulib_print(s);

    int print_x = cursor_x - scroll_x + LINE_NUM_WIDTH;
    int print_y = cursor_y - scroll_y + 2;
    ulib_gotoxy(print_x, print_y);

    // Draw an underscore if at the end of the line for better visibility
    if (buffer[cursor_y][cursor_x] == '\0') {
        ulib_print("_");
        ulib_gotoxy(print_x, print_y);
    }
}

int main(int argc, char **argv) {
    if (argc > 1) {
        ulib_strcpy(filename, argv[1]);
    } else {
        ulib_strcpy(filename, "untitled.txt");
    }

    load_file(filename);

    while (1) {
        redraw();
        int c = uabi_getc();

        if (c == CTRL('q')) {
            uabi_clear();
            uabi_exit(0);
        } else if (c == CTRL('s')) {
            save_file(filename);
        } else if (c == 0x08 || c == 0x7F) { // Backspace or DEL
            if (cursor_x > 0) {
                // Remove char in current line
                int len = ulib_strlen(buffer[cursor_y]);
                for (int i = cursor_x - 1; i < len; i++) {
                    buffer[cursor_y][i] = buffer[cursor_y][i+1];
                }
                cursor_x--;
            } else if (cursor_y > 0) {
                // Merge with previous line
                int prev_len = ulib_strlen(buffer[cursor_y-1]);
                int cur_len = ulib_strlen(buffer[cursor_y]);
                if (prev_len + cur_len < MAX_LINE_LEN - 1) {
                    ulib_strcpy(buffer[cursor_y-1] + prev_len, buffer[cursor_y]);
                    for (int i = cursor_y; i < line_count - 1; i++) {
                        ulib_strcpy(buffer[i], buffer[i+1]);
                    }
                    line_count--;
                    cursor_y--;
                    cursor_x = prev_len;
                }
            }
        } else if (c == '\t') {
            int len = ulib_strlen(buffer[cursor_y]);
            int spaces = 4;
            if (len + spaces < MAX_LINE_LEN) {
                // Shift characters after cursor to make room
                for (int i = len; i >= cursor_x; i--) {
                    buffer[cursor_y][i + spaces] = buffer[cursor_y][i];
                }
                // Insert 4 spaces
                for (int i = 0; i < spaces; i++) {
                    buffer[cursor_y][cursor_x + i] = ' ';
                }
                cursor_x += spaces;
            }
        } else if (c == '\n') {
            if (line_count < MAX_LINES) {
                // Split line
                for (int i = line_count; i > cursor_y + 1; i--) {
                    ulib_strcpy(buffer[i], buffer[i-1]);
                }
                ulib_strcpy(buffer[cursor_y + 1], buffer[cursor_y] + cursor_x);
                buffer[cursor_y][cursor_x] = '\0';
                line_count++;
                cursor_y++;
                cursor_x = 0;
            }
        } else if (c == UABI_KEY_UP) {
            if (cursor_y > 0) {
                cursor_y--;
                int len = ulib_strlen(buffer[cursor_y]);
                if (cursor_x > len) cursor_x = len;
            }
        } else if (c == UABI_KEY_DOWN) {
            if (cursor_y < line_count - 1) {
                cursor_y++;
                int len = ulib_strlen(buffer[cursor_y]);
                if (cursor_x > len) cursor_x = len;
            }
        } else if (c == UABI_KEY_LEFT) {
            if (cursor_x > 0) {
                cursor_x--;
            } else if (cursor_y > 0) {
                cursor_y--;
                cursor_x = ulib_strlen(buffer[cursor_y]);
            }
        } else if (c == UABI_KEY_RIGHT) {
            int len = ulib_strlen(buffer[cursor_y]);
            if (cursor_x < len) {
                cursor_x++;
            } else if (cursor_y < line_count - 1) {
                cursor_y++;
                cursor_x = 0;
            }
        } else if (c == UABI_KEY_CTRL_LEFT) {
            if (cursor_x > 0) {
                // Skip trailing spaces
                while (cursor_x > 0 && buffer[cursor_y][cursor_x-1] == ' ') cursor_x--;
                // Skip word
                while (cursor_x > 0 && buffer[cursor_y][cursor_x-1] != ' ') cursor_x--;
            } else if (cursor_y > 0) {
                cursor_y--;
                cursor_x = ulib_strlen(buffer[cursor_y]);
            }
        } else if (c == UABI_KEY_CTRL_RIGHT) {
            int len = ulib_strlen(buffer[cursor_y]);
            if (cursor_x < len) {
                // Skip word characters
                while (cursor_x < len && buffer[cursor_y][cursor_x] != ' ') cursor_x++;
                // Skip trailing spaces
                while (cursor_x < len && buffer[cursor_y][cursor_x] == ' ') cursor_x++;
            } else if (cursor_y < line_count - 1) {
                cursor_y++;
                cursor_x = 0;
            }
        } else if (c >= 32 && c <= 126) {
            int len = ulib_strlen(buffer[cursor_y]);
            if (len < MAX_LINE_LEN - 1) {
                for (int i = len; i >= cursor_x; i--) {
                    buffer[cursor_y][i+1] = buffer[cursor_y][i];
                }
                buffer[cursor_y][cursor_x] = (char)c;
                cursor_x++;
            }
        }
        
        // Ensure scroll
        if (cursor_y < scroll_y) scroll_y = cursor_y;
        if (cursor_y >= scroll_y + 20) scroll_y = cursor_y - 19;

        int visible_cols = 80 - LINE_NUM_WIDTH;
        if (cursor_x < scroll_x) scroll_x = cursor_x;
        if (cursor_x >= scroll_x + visible_cols) scroll_x = cursor_x - (visible_cols - 1);
    }

    return 0;
}

void _start(int argc, char **argv) {
    uabi_exit(main(argc, argv));
}
