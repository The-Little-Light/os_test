#ifndef __SHELL_SHELL_H
#define __SHELL_SHELL_H

#include "stdint.h"
#include "fs.h"

extern char final_path[];
void print_prompt(void);
void my_shell(void);

#endif