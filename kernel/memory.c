#include "memory.h"
#include "stdint.h"
#include "print.h"

#define PG_SIZE 4096

/************************ 位图地址 *****************************
* 因为 0xc009f000 是内核主线程栈顶， 0xc009e000 是内核主线程的 pcb。
* 一个页框大小的位图可表示 128MB 内存，位图位置安排在地址 0xc009a000，
* 这样本系统最大支持 4 个页框的位图，即 512MB */
#define MEM_BITMAP_BASE 0xc009a000
/******************************************************************/

/* 0xc0000000 是内核从虚拟地址 3G 起。
x100000 意指跨过低端 1MB 内存，使虚拟地址在逻辑上连续 */
#define K_HEAP_START 0xc0100000

/* 内存池结构，生成两个实例用于管理内核内存池和用户内存池 */
struct pool {
    struct bitmap pool_bitmap; //本内存池用到的位图结构，用于管理物理内存
    uint32_t phy_addr_start; // 本内存池所管理物理内存的起始地址
    uint32_t pool_size; // 本内存池字节容量
};

struct pool kernel_pool, user_pool; // 生成内核内存池和用户内存池
struct virtual_addr kernel_vaddr; // 此结构用来给内核分配虚拟地址

/* 初始化内存池 */
static void mem_pool_init(uint32_t all_mem) {
    put_str(" mem_pool_init start\n");
    uint32_t page_table_size = PG_SIZE * 256;
    // 页表大小 = 1 页的页目录表 + 第 0 和第 768 个页目录项指向同一个页表 +
    // 第 769～1022 个页目录项共指向 254 个页表，共 256 个页框
    uint32_t used_mem = page_table_size + 0x100000;
    // 0x100000 为低端 1MB 内存
    uint32_t free_mem = all_mem - used_mem;
    uint16_t all_free_pages = free_mem / PG_SIZE;
    // 1 页为 4KB，不管总内存是不是 4k 的倍数
    // 对于以页为单位的内存分配策略，不足 1 页的内存不用考虑了
