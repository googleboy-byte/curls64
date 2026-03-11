#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

// Current working directory
static char cwd[256] = "/";

// Forward declarations
static void cmd_exec(char **argv, int resolvable);
static int try_resolve_bin(char *cmd, char *out_path);
static void show_cmd_help(char *target);
static void run_simple_command(char *line);
static void history_load();
static void history_save();

// Command history
#define MAX_HISTORY 32
static char history[MAX_HISTORY][256];
static int history_count = 0;
static int history_current = -1;

// Session tracking
static char session_log_path[64] = "";

// Command buffer
static char input[256];

// Shell variables
struct sh_var {
    char name[32];
    char value[128];
};
static struct sh_var vars[32];
static int num_vars = 0;

// Script tracking
static int script_fd = -1;

// For loop state
static int for_active = 0;
static char for_var[32];
static char for_vals[128]; // Space-separated list of values
static char *for_val_ptrs[16];
static int for_num_vals = 0;
static int for_curr_idx = 0;
static int for_body_pos = 0;

static void set_sh_var(const char *name, const char *value) {
    for (int i = 0; i < num_vars; i++) {
        if (ulib_strcmp(vars[i].name, name) == 0) {
            ulib_strcpy(vars[i].value, value);
            return;
        }
    }
    if (num_vars < 32) {
        ulib_strcpy(vars[num_vars].name, name);
        ulib_strcpy(vars[num_vars].value, value);
        num_vars++;
    }
}

static const char* get_sh_var(const char *name) {
    for (int i = 0; i < num_vars; i++) {
        if (ulib_strcmp(vars[i].name, name) == 0) {
            return vars[i].value;
        }
    }
    return "";
}

static void expand_variables(char *line) {
    char expanded[512];
    int e_idx = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '$' && line[i+1] != '(') {
            char var_name[32];
            int v_idx = 0;
            i++;
            // Special case for $?
            if (line[i] == '?') {
                var_name[0] = '?';
                var_name[1] = '\0';
                i++;
            } else {
                while (line[i] && ( (line[i] >= 'a' && line[i] <= 'z') || (line[i] >= 'A' && line[i] <= 'Z') || (line[i] >= '0' && line[i] <= '9') || line[i] == '_')) {
                    var_name[v_idx++] = line[i++];
                }
                var_name[v_idx] = '\0';
            }
            const char *val = get_sh_var(var_name);
            while (*val && e_idx < 511) expanded[e_idx++] = *val++;
            i--; // Step back to let the loop increment handle the end
        } else {
            expanded[e_idx++] = line[i];
        }
    }
    expanded[e_idx] = '\0';
    ulib_strcpy(line, expanded);
}

// Helper: Print prompt
static void print_prompt() {
    ulib_print("(sh:");
    ulib_print(cwd);
    ulib_print(")> ");
}

// Command: CD - Change directory
static void cmd_cd(char *path) {
    if (!path || path[0] == '\0') {
        ulib_strcpy(cwd, "/");
        return;
    }
    
    char new_cwd[256];
    if (path[0] == '/') {
        ulib_strcpy(new_cwd, path);
    } else {
        ulib_strcpy(new_cwd, cwd);
        if (ulib_strcmp(cwd, "/") != 0) {
            ulib_strcat(new_cwd, "/");
        }
        ulib_strcat(new_cwd, path);
    }
    
    if (uabi_chdir(new_cwd) < 0) {
        ulib_print("cd: ");
        ulib_print(path);
        ulib_print(": No such file or directory\n");
    } else {
        // Sync with kernel's normalized path
        uabi_getcwd(cwd, sizeof(cwd));
    }
}

// Helper: Interpret escape sequences (not used in sh anymore)
static void interpret_escapes(char *str) {
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '\\' && *(src + 1) == 'n') {
            *dst++ = '\n';
            src += 2;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static int if_depth = 0;
static int if_active[8]; // Whether we are in the 'true' branch of each if

static void capture_output(char *command, char *output, int max) {
    int p[2];
    if (uabi_pipe(p) < 0) return;
    int pid = uabi_fork();
    if (pid == 0) {
        uabi_dup2(p[1], 1);
        uabi_close(p[0]);
        uabi_close(p[1]);
        // We need a version of parse_and_execute that doesn't expand variables again 
        // or just use the existing one but be careful.
        // Actually, command is already expanded here if called from run_simple_command.
        run_simple_command(command);
        uabi_exit(0);
    }
    uabi_close(p[1]);
    int total = 0;
    while (total < max - 1) {
        int n = uabi_read(p[0], output + total, max - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    output[total] = '\0';
    // Trim trailing newlines and carriage returns
    while (total > 0 && (output[total-1] == '\n' || output[total-1] == '\r')) {
        output[--total] = '\0';
    }
    uabi_close(p[0]);
    uabi_wait();
}

static void show_cmd_help(char *target) {
    if (!target) {
        ulib_print("Shell built-ins: cd, exit, exec, help\n");
        ulib_print("Common utilities: ls, ps, cat, top, echo, pwd, clear, sleep, touch, write, write_a\n");
        return;
    }
    // Forward help to external help utility if possible, 
    // but we can also just let the fallback handle it.
}

// Command: EXEC - Execute program (fork + wait)
// resolvable flag indicates we've already checked existence
static void cmd_exec(char **argv, int resolvable) {
    if (!argv[0]) return;

    if (!resolvable) {
        char bin_path[256];
        if (argv[0][0] == '/') {
            uabi_stat_t st;
            if (uabi_stat(argv[0], &st) != 0) {
                ulib_strcpy(bin_path, argv[0]);
                ulib_strcat(bin_path, ".ELF");
                if (uabi_stat(bin_path, &st) == 0) ulib_strcpy(argv[0], bin_path);
                else { ulib_print("exec: command not found\n"); set_sh_var("?", "127"); return; }
            }
        } else if (try_resolve_bin(argv[0], bin_path)) {
            ulib_strcpy(argv[0], bin_path);
        } else {
            ulib_print("exec: command not found\n");
            set_sh_var("?", "127");
            return;
        }
    }

    int pid = uabi_fork();
    if (pid == 0) {
        uabi_exec(argv[0], argv);
        uabi_exit(1);
    } else if (pid > 0) {
        int status = uabi_wait();
        char s[16];
        ulib_int_to_str(status, s);
        set_sh_var("?", s);
    }
}

static int try_resolve_bin(char *cmd, char *out_path) {
    ulib_strcpy(out_path, "/BIN/");
    ulib_strcat(out_path, cmd);
    ulib_strcat(out_path, ".ELF");
    uabi_stat_t st;
    if (uabi_stat(out_path, &st) == 0) return 1;
    return 0;
}

// Command parser
static void run_simple_command(char *line) {
    // Check if we should skip due to 'if'
    if (if_depth > 0 && !if_active[if_depth - 1]) {
        if (ulib_strncmp(line, "else", 4) == 0) {
            if_active[if_depth - 1] = 1;
            return;
        }
        if (ulib_strncmp(line, "endif", 5) == 0) {
            if_depth--;
            return;
        }
        if (ulib_strncmp(line, "if ", 3) == 0) {
            if_depth++;
            if_active[if_depth - 1] = 0;
        }
        return;
    }

    // Handle 'if'
    if (ulib_strncmp(line, "if ", 3) == 0) {
        char *cond = line + 3;
        char part1[128], op[4], part2[128];
        char *p = cond;
        
        // Find operator first to split strings better
        char *eqeq = 0;
        char *neq = 0;
        for (char *c = cond; *c; c++) {
            if (c[0] == ' ' && c[1] == '=' && c[2] == '=' && c[3] == ' ') { eqeq = c; break; }
            if (c[0] == ' ' && c[1] == '!' && c[2] == '=' && c[3] == ' ') { neq = c; break; }
        }

        int res = 0;
        if (eqeq) {
            int len1 = eqeq - cond;
            if (len1 > 127) len1 = 127;
            ulib_memcpy(part1, cond, len1); part1[len1] = '\0';
            // Trim leading/trailing spaces from part1
            char *p1 = part1; while (*p1 == ' ') p1++;
            char *e1 = p1 + ulib_strlen(p1) - 1;
            while (e1 >= p1 && *e1 == ' ') *e1-- = '\0';

            ulib_strcpy(part2, eqeq + 4);
            // Trim leading/trailing spaces from part2
            char *p2 = part2; while (*p2 == ' ') p2++;
            char *e2 = p2 + ulib_strlen(p2) - 1;
            while (e2 >= p2 && *e2 == ' ') *e2-- = '\0';

            res = (ulib_strcmp(p1, p2) == 0);
        } else if (neq) {
            int len1 = neq - cond;
            if (len1 > 127) len1 = 127;
            ulib_memcpy(part1, cond, len1); part1[len1] = '\0';
            // Trim leading/trailing spaces
            char *p1 = part1; while (*p1 == ' ') p1++;
            char *e1 = p1 + ulib_strlen(p1) - 1;
            while (e1 >= p1 && *e1 == ' ') *e1-- = '\0';

            ulib_strcpy(part2, neq + 4);
            // Trim leading/trailing spaces
            char *p2 = part2; while (*p2 == ' ') p2++;
            char *e2 = p2 + ulib_strlen(p2) - 1;
            while (e2 >= p2 && *e2 == ' ') *e2-- = '\0';

            res = (ulib_strcmp(p1, p2) != 0);
        } else {
            // Fallback to old simple parser for other ops (if any)
            int i = 0;
            while (*p && *p != ' ' && i < 127) part1[i++] = *p++; part1[i] = '\0';
            while (*p == ' ') p++;
            i = 0;
            while (*p != ' ' && *p && i < 3) op[i++] = *p++; op[i] = '\0';
            while (*p == ' ') p++;
            i = 0;
            while (*p && i < 127) part2[i++] = *p++; part2[i] = '\0';
            if (ulib_strcmp(op, "==") == 0) res = (ulib_strcmp(part1, part2) == 0);
            else if (ulib_strcmp(op, "!=") == 0) res = (ulib_strcmp(part1, part2) != 0);
        }

        if (if_depth < 8) {
            if_active[if_depth] = res;
            if_depth++;
        }
        return;
    }
    if (ulib_strncmp(line, "else", 4) == 0) {
        if (if_depth > 0) if_active[if_depth - 1] = !if_active[if_depth - 1];
        return;
    }
    if (ulib_strncmp(line, "endif", 5) == 0) {
        if (if_depth > 0) if_depth--;
        return;
    }
    if (ulib_strncmp(line, "then", 4) == 0) return; // Ignore 'then'

    // Handle 'for'
    if (ulib_strncmp(line, "for ", 4) == 0) {
        char *p = line + 4;
        int i = 0;
        while (*p && *p != ' ' && i < 31) for_var[i++] = *p++; for_var[i] = '\0';
        while (*p == ' ') p++;
        if (ulib_strncmp(p, "in ", 3) == 0) {
            p += 3;
            ulib_strcpy(for_vals, p);
            // Tokenize for_vals
            for_num_vals = 0;
            char *v = for_vals;
            while (*v && for_num_vals < 16) {
                while (*v == ' ') *v++ = '\0';
                if (*v == '\0') break;
                for_val_ptrs[for_num_vals++] = v;
                while (*v && *v != ' ') v++;
            }
            for_curr_idx = 0;
            for_active = 1;
            set_sh_var(for_var, for_val_ptrs[0]);
        }
        return;
    }
    if (ulib_strcmp(line, "do") == 0) {
        if (for_active && script_fd >= 0) {
            for_body_pos = uabi_lseek(script_fd, 0, UABI_SEEK_CUR);
        }
        return;
    }
    if (ulib_strcmp(line, "done") == 0) {
        if (for_active) {
            for_curr_idx++;
            if (for_curr_idx < for_num_vals && script_fd >= 0) {
                set_sh_var(for_var, for_val_ptrs[for_curr_idx]);
                uabi_lseek(script_fd, for_body_pos, UABI_SEEK_SET);
            } else {
                for_active = 0;
            }
        }
        return;
    }

    // Handle assignments: VAR=VAL or VAR=$(cmd)
    char *eq = 0;
    for (char *c = line; *c; c++) {
        if (*c == '=') {
            eq = c;
            break;
        }
    }

    if (eq && (eq == line || *(eq-1) != ' ')) {
        *eq = '\0';
        char *name = line;
        char *val = eq + 1;
        if (val[0] == '$' && val[1] == '(') {
            char cmd[256];
            ulib_strcpy(cmd, val + 2);
            char *end_paren = 0;
            for (char *c = cmd; *c; c++) if (*c == ')') { end_paren = c; break; }
            if (end_paren) *end_paren = '\0';
            
            char output[128];
            capture_output(cmd, output, 128);
            set_sh_var(name, output);
            set_sh_var("?", "0"); // Assume success for capture for now
        } else {
            set_sh_var(name, val);
            set_sh_var("?", "0");
        }
        return;
    }

    char *argv[16];
    int argc = 0;
    char *p = line;
    
    while (*p && argc < 15) {
        while (*p == ' ') *p++ = '\0';
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    argv[argc] = 0;
    if (argc == 0) return;

    char *cmd = argv[0];
    if (ulib_strcmp(cmd, "cd") == 0) {
        if (argc > 1) cmd_cd(argv[1]);
        else ulib_strcpy(cwd, "/");
        set_sh_var("?", "0");
        return;
    } else if (ulib_strcmp(cmd, "exit") == 0) {
        history_save();
        if (session_log_path[0]) uabi_unlink(session_log_path);
        uabi_exit(0);
    } else if (ulib_strcmp(cmd, "exec") == 0) {
        if (argc > 1) cmd_exec(&argv[1], 0);
        return;
    } else if (ulib_strcmp(cmd, "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            ulib_print(argv[i]);
            if (i < argc - 1) ulib_print(" ");
        }
        ulib_print("\n");
        set_sh_var("?", "0");
        return;
    } else if (ulib_strcmp(cmd, "read") == 0) {
        if (argc > 1) {
            char val[128];
            uabi_getline(val, 128);
            set_sh_var(argv[1], val);
        } else {
            uabi_getc(); // Just wait for a key
        }
        set_sh_var("?", "0");
        return;
    }

    // Default: try external binary
    char bin_path[256];
    int resolvable = 0;
    
    if (cmd[0] == '/') {
        uabi_stat_t st;
        if (uabi_stat(cmd, &st) == 0) resolvable = 1;
        else {
            // Try with .ELF if not found
            ulib_strcpy(bin_path, cmd);
            ulib_strcat(bin_path, ".ELF");
            if (uabi_stat(bin_path, &st) == 0) {
                ulib_strcpy(argv[0], bin_path);
                resolvable = 1;
            }
        }
    } else if (try_resolve_bin(cmd, bin_path)) {
        argv[0] = bin_path;
        resolvable = 1;
    }

    if (!resolvable) {
        ulib_print("sh: command not found\n");
        set_sh_var("?", "127");
        return;
    }

    int pid = uabi_fork();
    if (pid == 0) {
        // Redirection support
        for (int i = 0; i < argc; i++) {
            if (!argv[i]) continue;
            if (ulib_strcmp(argv[i], ">") == 0 && i + 1 < argc) {
                int fd = uabi_open(argv[i+1], UABI_O_CREAT | UABI_O_WRONLY | UABI_O_TRUNC);
                if (fd >= 0) { uabi_dup2(fd, 1); uabi_close(fd); }
                argv[i] = 0;
            } else if (ulib_strcmp(argv[i], "<") == 0 && i + 1 < argc) {
                int fd = uabi_open(argv[i+1], UABI_O_RDONLY);
                if (fd >= 0) { uabi_dup2(fd, 0); uabi_close(fd); }
                argv[i] = 0;
            }
        }
        
        uabi_exec(argv[0], argv);
        uabi_exit(1);
    } else if (pid > 0) {
        int status = uabi_wait();
        char s[16];
        ulib_int_to_str(status, s);
        set_sh_var("?", s);
    }
}

static void parse_and_execute(char *line) {
    // Trim leading/trailing spaces
    while (*line == ' ') line++;
    char *end = line + ulib_strlen(line) - 1;
    while (end > line && *end == ' ') *end-- = '\0';
    if (*line == '\0') return;

    // Handle pipes
    char *pipe_ptr = 0;
    for (char *c = line; *c; c++) {
        if (*c == '|') {
            pipe_ptr = c;
            break;
        }
    }

    if (pipe_ptr) {
        *pipe_ptr = '\0';
        char *left = line;
        char *right = pipe_ptr + 1;

        int p[2];
        if (uabi_pipe(p) < 0) {
            ulib_print("pipe failed\n");
            return;
        }

        int pid1 = uabi_fork();
        if (pid1 == 0) {
            uabi_dup2(p[1], 1);
            uabi_close(p[0]);
            uabi_close(p[1]);
            parse_and_execute(left);
            uabi_exit(0);
        }

        int pid2 = uabi_fork();
        if (pid2 == 0) {
            uabi_dup2(p[0], 0);
            uabi_close(p[0]);
            uabi_close(p[1]);
            parse_and_execute(right);
            uabi_exit(0);
        }

        uabi_close(p[0]);
        uabi_close(p[1]);
        uabi_wait(); // Wait for both children
        uabi_wait();
        return;
    }

    expand_variables(line);
    run_simple_command(line);
}

// History management
static void history_load() {
    int fd = uabi_open("/ETC/HISTORY", UABI_O_RDONLY);
    if (fd < 0) return;

    char line[256];
    int len = 0;
    char c;
    while (history_count < MAX_HISTORY) {
        len = 0;
        int bytes;
        while ((bytes = uabi_read(fd, &c, 1)) > 0 && c != '\n' && len < 255) {
            line[len++] = c;
        }
        if (bytes <= 0 && len == 0) break;
        line[len] = '\0';
        if (len > 0) {
            ulib_strcpy(history[history_count++], line);
        }
    }
    uabi_close(fd);
}

static void history_save() {
    int fd = uabi_open("/ETC/HISTORY", UABI_O_CREAT | UABI_O_WRONLY | UABI_O_TRUNC);
    if (fd < 0) return;

    for (int i = 0; i < history_count; i++) {
        uabi_write(fd, history[i], ulib_strlen(history[i]));
        uabi_write(fd, "\n", 1);
    }
    uabi_close(fd);
}

static void history_add(const char *cmd) {
    if (!cmd || cmd[0] == '\0') return;

    // SECURITY: Don't add commands that touch the history file itself
    // to prevent infinite loops when executing history as a script.
    if (ulib_strstr(cmd, "/ETC/HISTORY") || ulib_strstr(cmd, "/etc/history")) {
        return;
    }
    
    // Append to session log if we have one
    if (session_log_path[0] != '\0') {
        int fd = uabi_open(session_log_path, UABI_O_CREAT | UABI_O_WRONLY);
        if (fd >= 0) {
            uabi_lseek(fd, 0, UABI_SEEK_END);
            uabi_write(fd, cmd, ulib_strlen(cmd));
            uabi_write(fd, "\n", 1);
            uabi_close(fd);
        }
    }

    // Don't add if same as last entry
    if (history_count > 0 && ulib_strcmp(history[history_count-1], cmd) == 0) {
        return;
    }

    if (history_count < MAX_HISTORY) {
        ulib_strcpy(history[history_count++], cmd);
    } else {
        // Shift history
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            ulib_strcpy(history[i], history[i+1]);
        }
        ulib_strcpy(history[MAX_HISTORY-1], cmd);
    }
}

static void sh_readline(char *buf, int max) {
    int pos = 0;
    history_current = history_count;
    char current_input[256] = "";

    while (1) {
        int c = uabi_getc();
        if (c == '\n' || c == '\r') {
            ulib_print("\n");
            buf[pos] = '\0';
            break;
        } else if (c == 0x08 || c == 0x7F) { // Backspace
            if (pos > 0) {
                pos--;
                ulib_print("\b \b");
            }
        } else if (c == UABI_KEY_UP) {
            if (history_current > 0) {
                if (history_current == history_count) {
                    buf[pos] = '\0';
                    ulib_strcpy(current_input, buf);
                }
                history_current--;
                // Clear line
                while (pos > 0) { ulib_print("\b \b"); pos--; }
                ulib_strcpy(buf, history[history_current]);
                pos = ulib_strlen(buf);
                if (pos > 0) ulib_print(buf);
            }
        } else if (c == UABI_KEY_DOWN) {
            if (history_current < history_count) {
                history_current++;
                // Clear line
                while (pos > 0) { ulib_print("\b \b"); pos--; }
                if (history_current == history_count) {
                    ulib_strcpy(buf, current_input);
                } else {
                    ulib_strcpy(buf, history[history_current]);
                }
                pos = ulib_strlen(buf);
                if (pos > 0) ulib_print(buf);
            }
        } else if (c >= 32 && c <= 126) {
            if (pos < max - 1) {
                buf[pos++] = (char)c;
                char s[2] = {(char)c, '\0'};
                ulib_print(s);
            }
        }
    }
}

// Helper: Read a line from a file descriptor
static int get_line_from_fd(int fd, char *buf, int max) {
    int i = 0;
    char c;
    while (i < max - 1) {
        int bytes = uabi_read(fd, &c, 1);
        if (bytes <= 0) {
            buf[i] = '\0';
            return i > 0; // Return true if we read something before EOF
        }
        if (c == '\n') {
            buf[i] = '\0';
            return 1; // Always return true on newline (even if i=0)
        }
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return 1;
}

// Entry point
void _start(int argc, char **argv) {
    // Initialize working directory from kernel
    uabi_getcwd(cwd, sizeof(cwd));
    
    // Initialize $?
    set_sh_var("?", "0");

    if (argc > 1) {
        // Script mode
        int fd = uabi_open(argv[1], UABI_O_RDONLY);
        if (fd < 0) {
            ulib_print("sh: cannot open script '");
            ulib_print(argv[1]);
            ulib_print("'\n");
            uabi_exit(1);
        }
        script_fd = fd;

        while (get_line_from_fd(fd, input, 256)) {
            if (input[0] != '#' && input[0] != '\0') {
                parse_and_execute(input);
            }
        }
        uabi_close(fd);
        script_fd = -1;
        uabi_exit(0);
    }

    // Interactive mode
    int pid = uabi_getpid();
    ulib_strcpy(session_log_path, "/TMP/SESS.");
    char s_pid[16];
    ulib_int_to_str(pid, s_pid);
    ulib_strcat(session_log_path, s_pid);

    uabi_print("\n=== Curls OS User Shell (Ring 3) ===\n");
    uabi_print("Type 'help' for available commands\n\n");
    
    history_load();

    while (1) {
        print_prompt();
        sh_readline(input, 256);
        if (input[0] != '\0') {
            history_add(input);
            parse_and_execute(input);
        }
    }
    
    history_save();
    if (session_log_path[0]) uabi_unlink(session_log_path);
    uabi_exit(0);
}
