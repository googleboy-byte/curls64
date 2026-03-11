# U-ABI: The User Shell

The Curls OS user shell (`sh.elf`) is a Ring-3 application that provides an interactive command-line interface and script execution capabilities.

## 1. Features
- **Built-in Commands**: `cd`, `exit`, `exec`, `echo`, `help`, `ls`, `ps`, `top`.
- **External Utilities**: Auto-resolves programs in `/BIN/` (e.g., `hello` -> `/BIN/HELLO.ELF`).
- **I/O Redirection**: `command > file` (truncate), `command < file`.
- **Pipes**: `command1 | command2`.
- **Variables**: Local shell variables with `$VAR` expansion.
- **Scripting**: Complex logic with `if`, `else`, and `for` blocks.
- **Command Substitution**: `VAR=$(command)` to capture output into a variable.

## 2. Scripting Guide
### Variables
Assign values with `NAME=VALUE`. Access them using `$NAME`.
```bash
MSG=Hello
echo $MSG
```

### Conditionals (If/Else)
Simple string comparison using `==` or `!=`.
```bash
if $USER == root
    echo Welcome Administrator
else
    echo Welcome User
endif
```

### Loops (For)
Iterate over a space-separated list of values.
```bash
for i in 1 2 3
do
    echo Iteration $i
done
```

Note: In scripts, `do` must be on its own line after the `for` statement.

## 3. Running Scripts
Execute a script by passing it as an argument:
```bash
sh test.sh
```
The shell opens the file, reads it line-by-line, and maintains its own state (variables, loop position) during execution.
