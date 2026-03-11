#ifndef KERNEL_H
#define KERNEL_H

void user_input(char *input);
void panic(char *message);
void assert(int condition, char *message);

#endif
