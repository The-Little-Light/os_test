#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H
#include "stdint.h"
#include "bitmap.h"
#include "sync.h"


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
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

/* 内存池结构，生成两个实例用于管理内核内存池和用户内存池 */
struct pool {
    struct bitmap pool_bitmap; //本内存池用到的位图结构，用于管理物理内存
    uint32_t phy_addr_start; // 本内存池所管理物理内存的起始地址
    uint32_t pool_size; // 本内存池字节容量
    struct lock lock; // 申请内存时互斥
};


/* 虚拟地址池，用于虚拟地址管理 */
struct virtual_addr {
    struct bitmap vaddr_bitmap; // 虚拟地址用到的位图结构
    uint32_t vaddr_start; // 虚拟地址起始地址
};

extern struct pool kernel_pool, user_pool;
void mem_init(void);

/* 内存池标记，用于判断用哪个内存池 */
enum pool_flags {
    PF_KERNEL = 1, // 内核内存池
    PF_USER = 2 // 用户内存池
};

void* get_kernel_pages(uint32_t pg_cnt);
void* get_user_pages(uint32_t pg_cnt);
void* get_a_page(enum pool_flags pf,uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);


#define PG_P_1 1 // 页表项或页目录项存在属性位
#define PG_P_0 0 // 页表项或页目录项存在属性位
#define PG_RW_R 0 // R/W 属性位值，读/执行
#define PG_RW_W 2 // R/W 属性位值，读/写/执行
#define PG_US_S 0 // U/S 属性位值，系统级
#define PG_US_U 4 // U/S 属性位值，用户级

/* 内存块 */
struct mem_block {
struct list_elem free_elem;
};

/* 内存块描述符 */
struct mem_block_desc {
uint32_t block_size; // 内存块大小
uint32_t blocks_per_arena; // 本 arena 中可容纳此 mem_block 的数量
struct list free_list; // 目前可用的 mem_block 链表
};

#define DESC_CNT 7 // 内存块描述符个数
void* sys_malloc(uint32_t size);
void block_desc_init(struct mem_block_desc* desc_array);

#endif
