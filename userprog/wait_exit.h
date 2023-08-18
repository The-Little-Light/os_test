#ifndef __USERPROG_WAIT_EXIT_H
#define __USERPROG_WAIT_EXIT_H
#include "stdint.h"
#include "thread.h"

void release_prog_resource(struct task_struct* release_thread);
pid_t sys_wait(int32_t* status);
void sys_exit(int32_t status);

#endif