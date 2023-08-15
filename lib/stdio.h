#ifndef __LIB__STDIO_H
#define __LIB__STDIO_H
#include "stdint.h"
#define va_start(ap, v) ap = (va_list)&v // 把 ap 指向第一个固定参数 v
#define va_arg(ap, t) *((t*)(ap += 4)) // ap 指向下一个参数并返回其值
#define va_end(ap) ap = NULL // 清除 ap

typedef void* va_list;
uint32_t vsprintf(char* str,const char* format,va_list ap);
uint32_t printf(const char* format, ...);
uint32_t sprintf(char* buf, const char* format, ...);
#endif