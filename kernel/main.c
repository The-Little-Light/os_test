#include "console.h"
#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "ioqueue.h"
#include "keyboard.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio.h"
#include "fs.h"
#include "file.h"
#include "string.h"
#include "syscall.h"
#include "debug.h"
#include "shell.h"

int main(void) {
    put_str("I am kernel\n");
    init_all();
    cls_screen();
    console_put_str("[my@233 /]$ ");
    while(1);
    return 0;
}

/* init 进程 */
void init(void) {
    printf("i was inited, my pid is %d\n",  getpid());
    uint32_t ret_pid = fork();
    if(ret_pid) {
    // printf("i am father, my pid is %d, child pid is %d\n", getpid(), ret_pid);
    while(1);
    } else {
    // printf("i am child, my pid is %d, ret pid is %d\n", getpid(), ret_pid);
    my_shell();
    }
    PANIC("init: should not be here");
}