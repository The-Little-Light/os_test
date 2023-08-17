#include "memory.h"
#include "fs.h"
#include "file.h"
#include "stdint.h"
#include "global.h"
#include "ide.h"
#include "inode.h"
#include "dir.h"
#include "stdio-kernel.h"
#include "string.h"
#include "debug.h"
#include "list.h"
#include "thread.h"
#include "console.h"

struct partition* cur_part; // 默认情况下操作的是哪个分区

/* 在分区链表中找到名为 part_name 的分区，并将其指针赋值给 cur_part */
static bool mount_partition(struct list_elem* pelem, int arg) {
    char* part_name = (char*)arg;
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    if (!strcmp(part->name, part_name)) {
        cur_part = part;
        struct disk* hd = cur_part->my_disk;

        /* sb_buf 用来存储从硬盘上读入的超级块 */
        struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

        /* 在内存中创建分区 cur_part 的超级块 */
        cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
        if (cur_part->sb == NULL) {
            PANIC("alloc memory failed!");
        }

        /* 读入超级块 */
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);

        /* 把 sb_buf 中超级块的信息复制到分区的超级块 sb 中 */
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

        /********** 将硬盘上的块位图读入到内存 **************/
        cur_part->block_bitmap.bits =(uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
        if (cur_part->block_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->block_bitmap.btmp_bytes_len =sb_buf->block_bitmap_sects * SECTOR_SIZE;
        /* 从硬盘上读入块位图到分区的 block_bitmap.bits */
        ide_read(hd, sb_buf->block_bitmap_lba,  cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);
        /*************************************************************/

        /********** 将硬盘上的 inode 位图读入到内存 ************/
        cur_part->inode_bitmap.bits =(uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
        if (cur_part->inode_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->inode_bitmap.btmp_bytes_len =sb_buf->inode_bitmap_sects * SECTOR_SIZE;
        /* 从硬盘上读入 inode 位图到分区的 inode_bitmap.bits */
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);
        /*************************************************************/

        list_init(&cur_part->open_inodes);
        printk("mount %s done!\n", part->name);

        /* 此处返回 true 是为了迎合主调函数 list_traversal 的实现，
        * 与函数本身功能无关。
        * 只有返回 true 时 list_traversal 才会停止遍历，
        * 减少了后面元素无意义的遍历 */
        return true;
    }
    return false; // 使 list_traversal 继续遍历
}

/* 格式化分区，也就是初始化分区的元信息，创建文件系统 */
static void partition_format(struct partition* part) {
    /* blocks_bitmap_init（为方便实现，一个块大小是一扇区） */
    uint32_t boot_sector_sects = 1;
    uint32_t super_block_sects = 1;
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
    // I 结点位图占用的扇区数，最多支持 4096 个文件
    uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(struct inode) *MAX_FILES_PER_PART)), SECTOR_SIZE);
    uint32_t used_sects = boot_sector_sects + super_block_sects +inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;

    /************** 简单处理块位图占据的扇区数 ***************/
    uint32_t block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    /* block_bitmap_bit_len 是位图中位的长度，也是可用块的数量 */
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);
    /*********************************************************/

    /* 超级块初始化 */
    struct super_block sb;
    sb.magic = 0x19590318;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;

    sb.block_bitmap_lba = sb.part_lba_base + 2;
    // 第 0 块是引导块，第 1 块是超级块
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(struct dir_entry);

    printk("%s info:\n", part->name);
    printk(" magic:0x%x\n part_lba_base:0x%x\nall_sectors:0x%x\n inode_cnt:0x%x\nblock_bitmap_lba:0x%x\n\
    block_bitmap_sectors:0x%x\n inode_bitmap_lba:0x%x\n\
    inode_bitmap_sectors:0x%x\ninode_table_lba:0x%x\n\
    inode_table_sectors:0x%x\ndata_start_lba:0x%x\n",
    sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt,
    sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba,
    sb.inode_bitmap_sects, sb.inode_table_lba,
    sb.inode_table_sects, sb.data_start_lba);
    struct disk* hd = part->my_disk;
    /*******************************
    * 1 将超级块写入本分区的 1 扇区 *
    ******************************/
    ide_write(hd, part->start_lba + 1, &sb, 1);
    printk(" super_block_lba:0x%x\n", part->start_lba + 1);

    /* 找出数据量最大的元信息，用其尺寸做存储缓冲区*/
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
    buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);
    // 申请的内存由内存管理系统清 0 后返回

    /**************************************
    * 2 将块位图初始化并写入 sb.block_bitmap_lba *
    *************************************/
    /* 初始化块位图 block_bitmap */
    buf[0] |= 0x01; // 第 0 个块预留给根目录，位图中先占位
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);
    // last_size 是位图所在最后一个扇区中，不足一扇区的其余部分

    /* 1 先将位图最后一字节到其所在的扇区的结束全置为 1，
    即超出实际块数的部分直接置为已占用*/
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);

    /* 2 再将上一步中覆盖的最后一字节内的有效位重新置 0 */
    uint8_t bit_idx = 0;
    while (bit_idx <= block_bitmap_last_bit) {
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
    }
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);
    /***************************************
    * 3 将 inode 位图初始化并写入 sb.inode_bitmap_lba *
    ***************************************/
    /* 先清空缓冲区*/
    memset(buf, 0, buf_size);
    buf[0] |= 0x1; // 第 0 个 inode 分给了根目录
    /* 由于 inode_table 中共 4096 个 inode，
    * 位图 inode_bitmap 正好占用 1 扇区，
    * 即 inode_bitmap_sects 等于 1，
    * 所以位图中的位全都代表 inode_table 中的 inode，
    * 无需再像 block_bitmap 那样单独处理最后一扇区的剩余部分，
    * inode_bitmap 所在的扇区中没有多余的无效位 */
    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    /***************************************
    * 4 将 inode 数组初始化并写入 sb.inode_table_lba *
    ***************************************/
    /* 准备写 inode_table 中的第 0 项,即根目录所在的 inode */
    memset(buf, 0, buf_size); // 先清空缓冲区 buf
    struct inode* i = (struct inode*)buf;
    i->i_size = sb.dir_entry_size * 2; // .和..
    i->i_no = 0; // 根目录占 inode 数组中第 0 个 inode
    i->i_sectors[0] = sb.data_start_lba;
    // 由于上面的 memset， i_sectors 数组的其他元素都初始化为 0
    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    /***************************************
    * 5 将根目录写入 sb.data_start_lba
    ***************************************/
    /* 写入根目录的两个目录项.和.. */
    memset(buf, 0, buf_size);
    struct dir_entry* p_de = (struct dir_entry*)buf;

    /* 初始化当前目录"." */
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    p_de++;

    /* 初始化当前目录父目录".." */
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = 0; // 根目录的父目录依然是根目录自己
    p_de->f_type = FT_DIRECTORY;

    /* sb.data_start_lba 已经分配给了根目录，里面是根目录的目录项 */
    ide_write(hd, sb.data_start_lba, buf, 1);

    printk(" root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
}
/* 将最上层路径名称解析出来 */
static char* path_parse(char* pathname, char* name_store) {
    if (pathname[0] == '/') { // 根目录不需要单独解析
        /* 路径中出现 1 个或多个连续的字符'/'，将这些'/'跳过，如"///a/b" */
        while(*(++pathname) == '/');
    }

    /* 开始一般的路径解析 */
    while (*pathname != '/' && *pathname != 0) {
        *name_store++ = *pathname++;
    }

    if (pathname[0] == 0) { // 若路径字符串为空，则返回 NULL
        return NULL;
    }
    return pathname;
}

/* 返回路径深度，比如/a/b/c，深度为 3 */
int32_t path_depth_cnt(char* pathname) {
    ASSERT(pathname != NULL);
    char* p = pathname;
    char name[MAX_FILE_NAME_LEN];
    // 用于 path_parse 的参数做路径解析
    uint32_t depth = 0;

    /* 解析路径，从中拆分出各级名称 */
    p = path_parse(p, name);
    while (name[0]) {
    depth++;
    memset(name, 0, MAX_FILE_NAME_LEN);
    if (p) { // 如果 p 不等于 NULL，继续分析路径
    p = path_parse(p, name);
    }
    }
    return depth;
}

/* 搜索文件 pathname，若找到则返回其 inode 号，否则返回-1 */
static int search_file(const char* pathname, struct path_search_record* searched_record) {
    /* 如果待查找的是根目录，为避免下面无用的查找，
    直接返回已知根目录信息 */
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") ||!strcmp(pathname, "/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0; // 搜索路径置空
        return 0;
    }

    uint32_t path_len = strlen(pathname);
    /* 保证 pathname 至少是这样的路径/x，且小于最大长度 */
    ASSERT(pathname[0] == '/' && path_len > 1 &&path_len < MAX_PATH_LEN);
    char* sub_path = (char*)pathname;
    struct dir* parent_dir = &root_dir;
    struct dir_entry dir_e;

    /* 记录路径解析出来的各级名称，如路径"/a/b/c"，
    * 数组 name 每次的值分别是"a","b","c" */
    char name[MAX_FILE_NAME_LEN] = {0};

    searched_record->parent_dir = parent_dir;
    searched_record->file_type = FT_UNKNOWN;
    uint32_t parent_inode_no = 0; // 父目录的 inode 号

    sub_path = path_parse(sub_path, name);
    while (name[0]) { // 若第一个字符就是结束符，结束循环
        /* 记录查找过的路径，但不能超过 searched_path 的长度 512 字节 */
        ASSERT(strlen(searched_record->searched_path) < 512);

        /* 记录已存在的父目录 */
        strcat(searched_record->searched_path, "/");
        strcat(searched_record->searched_path, name);

        /* 在所给的目录中查找文件 */
        if (search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
            memset(name, 0, MAX_FILE_NAME_LEN);
            /* 若 sub_path 不等于 NULL，也就是未结束时继续拆分路径 */
            if (sub_path) {
                sub_path = path_parse(sub_path, name);
            }

            if (FT_DIRECTORY == dir_e.f_type) { // 如果被打开的是目录
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);
                parent_dir = dir_open(cur_part, dir_e.i_no); // 更新父目录
                searched_record->parent_dir = parent_dir;
                continue;
            } else if (FT_REGULAR == dir_e.f_type) { // 若是普通文件
                searched_record->file_type = FT_REGULAR;
                return dir_e.i_no;
            }
        } else { //若找不到，则返回-1
            /* 找不到目录项时，要留着 parent_dir 不要关闭，
            * 若是创建新文件的话需要在 parent_dir 中创建 */
            return -1;
        }
    }

    /* 执行到此，必然是遍历了完整路径
    并且查找的文件或目录只有同名目录存在 */
    dir_close(searched_record->parent_dir);

    /* 保存被查找目录的直接父目录 */
    searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
    searched_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;
}

/* 打开或创建文件成功后，返回文件描述符，否则返回-1 */
int32_t sys_open(const char* pathname, uint8_t flags) {
    /* 对目录要用 dir_open，这里只有 open 文件 */
    if (pathname[strlen(pathname) - 1] == '/') {
        printk("can`t open a directory %s\n",pathname);
        return -1;
    }
    ASSERT(flags <=7);
    int32_t fd = -1; // 默认为找不到

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    /* 记录目录深度，帮助判断中间某个目录不存在的情况 */
    uint32_t pathname_depth = path_depth_cnt((char*)pathname);

    /* 先检查文件是否存在 */
    int inode_no = search_file(pathname, &searched_record);
    bool found = inode_no != -1 ? true : false;

    if (searched_record.file_type == FT_DIRECTORY) {
        printk("can`t open a direcotry with open(), use opendir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);

    /* 先判断是否把 pathname 的各层目录都访问到了，
    即是否在某个中间目录就失败了 */
    if (pathname_depth != path_searched_depth) {
        // 说明并没有访问到全部的路径，某个中间目录是不存在的
        printk("cannot access %s: Not a directory, subpath %s is't exist\n", \
        pathname, searched_record.searched_path);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    /* 若是在最后一个路径上没找到，并且并不是要创建文件，直接返回-1 */
    if (!found && !(flags & O_CREAT)) {
        printk("in path %s, file %s is`t exist\n", \
        searched_record.searched_path, \
        (strrchr(searched_record.searched_path, '/') + 1));
        dir_close(searched_record.parent_dir);
        return -1;
    } else if (found && flags & O_CREAT) { // 若要创建的文件已存在
        printk("%s has already exist!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    switch (flags& O_CREAT) {
        case O_CREAT:
        printk("creating file\n");
        fd = file_create(searched_record.parent_dir,  (strrchr(pathname, '/') + 1), flags);
        dir_close(searched_record.parent_dir);
        // 其余为打开文件
        break;
        default:
        /* 其余情况均为打开已存在文件
        * O_RDONLY,O_WRONLY,O_RDWR */
        fd = file_open(inode_no, flags);
    }

    /* 此 fd 是指任务 pcb->fd_table 数组中的元素下标，
    * 并不是指全局 file_table 中的下标 */
    return fd;
}

/* 将文件描述符转化为文件表的下标 */
static uint32_t fd_local2global(uint32_t local_fd) {
    struct task_struct* cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

/* 关闭文件描述符 fd 指向的文件，成功返回 0，否则返回-1 */
int32_t sys_close(int32_t fd) {
    int32_t ret = -1; // 返回值默认为-1,即失败
    if (fd > 2) {
    uint32_t _fd = fd_local2global(fd);
    ret = file_close(&file_table[_fd]);
    running_thread()->fd_table[fd] = -1; // 使该文件描述符位可用
    }
    return ret;
}



/* 在磁盘上搜索文件系统，若没有则格式化分区创建文件系统 */
void filesys_init() {
    uint8_t channel_no = 0, dev_no, part_idx = 0;

    /* sb_buf 用来存储从硬盘上读入的超级块 */
    struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

    if (sb_buf == NULL) {
        PANIC("alloc memory failed!");
    }
    printk("searching filesystem......\n");
    while (channel_no < channel_cnt) {
        dev_no = 0;
        while(dev_no < 2) {
            if (dev_no == 0) { // 跨过裸盘 hd60M.img
                dev_no++;
                continue;
            }
            struct disk* hd = &channels[channel_no].devices[dev_no];
            struct partition* part = hd->prim_parts;
            while(part_idx < 12) { // 4 个主分区+8 个逻辑
                if (part_idx == 4) { // 开始处理逻辑分区
                    part = hd->logic_parts;
                }

                /* channels 数组是全局变量，默认值为 0， disk 属于其嵌套结构，
                * partition 又为 disk 的嵌套结构，因此 partition 中的成员默认也为 0。
                * 若 partition 未初始化，则 partition 中的成员仍为 0。
                * 下面处理存在的分区 */
                if (part->sec_cnt != 0) { // 如果分区存在
                    memset(sb_buf, 0, SECTOR_SIZE);

                    /* 读出分区的超级块，根据魔数是否正确来判断是否存在文件系统 */
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);

                    /* 只支持自己的文件系统，若磁盘上已经有文件系统就不再格式化了 */
                    if (sb_buf->magic == 0x19590318) {
                        printk("%s has filesystem\n", part->name);
                    } else { // 其他文件系统不支持，一律按无文件系统处理
                        printk("formatting %s`s partition %s......\n",hd->name, part->name);
                        partition_format(part);
                    }
                }
                part_idx++;
                part++; // 下一分区
            }
            dev_no++; // 下一磁盘
        }
        channel_no++; // 下一通道
    }
    sys_free(sb_buf);
    /* 确定默认操作的分区 */
    char default_part[8] = "sdb1";
    /* 挂载分区 */
    list_traversal(&partition_list, mount_partition, (int)default_part);

    /* 将当前分区的根目录打开 */
    open_root_dir(cur_part);

    /* 初始化文件表 */
    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILE_OPEN) {
        file_table[fd_idx++].fd_inode = NULL;
    }
}

/* 将 buf 中连续 count 个字节写入文件描述符 fd，
成功则返回写入的字节数，失败返回-1 */
int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
    if (fd < 0) {
    printk("sys_write: fd error\n");
    return -1;
    }
    if (fd == stdout_no) {
        char tmp_buf[1024] = {0};
        memcpy(tmp_buf, buf, count);
        console_put_str(tmp_buf);
        return count;
    }
    uint32_t _fd = fd_local2global(fd);
    struct file* wr_file = &file_table[_fd];
    if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {
        uint32_t bytes_written = file_write(wr_file, buf, count);
        return bytes_written;
    } else {
        console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY\n");
        return -1;
    }
}
/* 从文件描述符 fd 指向的文件中读取 count 个字节到 buf，
若成功则返回读出的字节数，到文件尾则返回-1 */
int32_t sys_read(int32_t fd, void* buf, uint32_t count) {
    if (fd < 0) {
    printk("sys_read: fd error\n");
    return -1;
    }
    ASSERT(buf != NULL);
    uint32_t _fd = fd_local2global(fd);
    return file_read(&file_table[_fd], buf, count);
}
/* 重置用于文件读写操作的偏移指针。
成功时返回新的偏移量，出错时返回-1 */
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
    if (fd < 0) {
        printk("sys_lseek: fd error\n");
        return -1;
    }
    ASSERT(whence > 0 && whence < 4);
    uint32_t _fd = fd_local2global(fd);
    struct file* pf = &file_table[_fd];
    int32_t new_pos = 0; //新的偏移量必须位于文件大小之内
    int32_t file_size = (int32_t)pf->fd_inode->i_size;
    switch (whence) {
        /* SEEK_SET 新的读写位置是相对于文件开头再增加 offset 个位移量 */
        case SEEK_SET:
        new_pos = offset;
        break;

        /* SEEK_CUR 新的读写位置是相对于当前的位置增加 offset 个位移量 */
        case SEEK_CUR: // offse 可正可负
        new_pos = (int32_t)pf->fd_pos + offset;
        break;

        /* SEEK_END 新的读写位置是相对于文件尺寸再增加 offset 个位移量 */
        case SEEK_END: // 此情况下， offset 应该为负值
        new_pos = file_size + offset;
    }
    if (new_pos < 0 || new_pos > (file_size - 1)) {
        return -1;
    }
    pf->fd_pos = new_pos;
    return pf->fd_pos;
}

/* 删除文件（非目录），成功返回 0，失败返回-1 */
int32_t sys_unlink(const char* pathname) {
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    /* 先检查待删除的文件是否存在 */
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);
    if (inode_no == -1) {
        printk("file %s not found!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }
    if (searched_record.file_type == FT_DIRECTORY) {
        printk("can`t delete a direcotry with unlink(),  use rmdir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    /* 检查是否在已打开文件列表（文件表）中 */
    uint32_t file_idx = 0;
    while (file_idx < MAX_FILE_OPEN) {
        if (file_table[file_idx].fd_inode != NULL && (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no) {
        break;
        }
        file_idx++;
    }
    if (file_idx < MAX_FILE_OPEN) {
        dir_close(searched_record.parent_dir);
        printk("file %s is in use, not allow to delete!\n", pathname);
        return -1;
    }
    ASSERT(file_idx == MAX_FILE_OPEN);

    /* 为 delete_dir_entry 申请缓冲区 */
    void* io_buf = sys_malloc(SECTOR_SIZE + SECTOR_SIZE);
    if (io_buf == NULL) {
        dir_close(searched_record.parent_dir);
        printk("sys_unlink: malloc for io_buf failed\n");
        return -1;
    }

    struct dir* parent_dir = searched_record.parent_dir;
    delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);
    inode_release(cur_part, inode_no);
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0; // 成功删除文件
}

/* 创建目录 pathname，成功返回 0，失败返回-1 */
int32_t sys_mkdir(const char* pathname) {
    uint8_t rollback_step = 0; // 用于操作失败时回滚各资源状态
    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        printk("sys_mkdir: sys_malloc for io_buf failed\n");
        return -1;
    }

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = -1;
    inode_no = search_file(pathname, &searched_record);
    if (inode_no != -1) { // 如果找到了同名目录或文件，失败返回
        printk("sys_mkdir: file or directory %s exist!\n", pathname);
        rollback_step = 1;
        goto rollback;
    } else {
        // 若未找到，也要判断是在最终目录没找到，还是某个中间目录不存在
        uint32_t pathname_depth = path_depth_cnt((char*)pathname);
        uint32_t path_searched_depth =\
        path_depth_cnt(searched_record.searched_path);
        /* 先判断是否把 pathname 的各层目录都访问到了，
        即是否在某个中间目录就失败了 */
        if (pathname_depth != path_searched_depth) {
            // 说明并没有访问到全部的路径，某个中间目录是不存在的
            printk("sys_mkdir: cannot access %s: Not a directory, subpath %s is`t exist\n", pathname, searched_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }
    }

    struct dir* parent_dir = searched_record.parent_dir;
    /* 目录名称后可能会有字符'/',
    所以最好直接用 searched_record.searched_path，无'/' */
    char* dirname = strrchr(searched_record.searched_path, '/') + 1;

    inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("sys_mkdir: allocate inode failed\n");
        rollback_step = 1;
        goto rollback;
    }

    struct inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode); // 初始化 i 结点

    uint32_t block_bitmap_idx = 0;
    // 用来记录 block 对应于 block_bitmap 中的索引
    int32_t block_lba = -1;
    /* 为目录分配一个块，用来写入目录.和.. */
    block_lba = block_bitmap_alloc(cur_part);
    if (block_lba == -1) {
        printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
        rollback_step = 2;
        goto rollback;
    }
    new_dir_inode.i_sectors[0] = block_lba;
    /* 每分配一个块就将位图同步到硬盘 */
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

    /* 将当前目录的目录项'.'和'..'写入目录 */
    memset(io_buf, 0, SECTOR_SIZE * 2); // 清空 io_buf
    struct dir_entry* p_de = (struct dir_entry*)io_buf;

    /* 初始化当前目录"." */
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = inode_no ;
    p_de->f_type = FT_DIRECTORY;

    p_de++;
    /* 初始化当前目录".." */
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = parent_dir->inode->i_no;
    p_de->f_type = FT_DIRECTORY;
    ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);

    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

    /* 在父目录中添加自己的目录项 */
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(dirname, inode_no,  FT_DIRECTORY, &new_dir_entry);
    memset(io_buf, 0, SECTOR_SIZE * 2); // 清空 io_buf
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        // sync_dir_entry 中将 block_bitmap 通过 bitmap_sync 同步到硬盘
        printk("sys_mkdir: sync_dir_entry to disk failed!\n");
        rollback_step = 2;
        goto rollback;
    }

    /* 父目录的 inode 同步到硬盘 */
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    /* 将新创建目录的 inode 同步到硬盘 */
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, &new_dir_inode, io_buf);

    /* 将 inode 位图同步到硬盘 */
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    sys_free(io_buf);

    /* 关闭所创建目录的父目录 */
    dir_close(searched_record.parent_dir);
    return 0;

    /*创建文件或目录需要创建相关的多个资源，
    若某步失败则会执行到下面的回滚步骤 */
    rollback: // 因为某步骤操作失败而回滚
    switch (rollback_step) {
        case 2:
        bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        // 如果新文件的 inode 创建失败，之前位图中分配的 inode_no 也要恢复
        case 1:
        /* 关闭所创建目录的父目录 */
        dir_close(searched_record.parent_dir);
        break;
    }
    sys_free(io_buf);
    return -1;
}

/* 目录打开成功后返回目录指针，失败返回 NULL */
struct dir* sys_opendir(const char* name) {
    ASSERT(strlen(name) < MAX_PATH_LEN);
    /* 如果是根目录'/'，直接返回&root_dir */
    if (name[0] == '/' && name[1] == 0) {
        return &root_dir;
    }

    /* 先检查待打开的目录是否存在 */
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(name, &searched_record);
    struct dir* ret = NULL;
    if (inode_no == -1) { //如果找不到目录，提示不存在的路径
        printk("In %s, sub path %s not exist\n", name, searched_record.searched_path);
    } else {
        if (searched_record.file_type == FT_REGULAR) {
        printk("%s is regular file!\n", name);
        } else if (searched_record.file_type == FT_DIRECTORY) {
        ret = dir_open(cur_part, inode_no);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

/* 成功关闭目录 p_dir 返回 0，失败返回-1 */
int32_t sys_closedir(struct dir* dir) {
    int32_t ret = -1;
    if (dir != NULL) {
        dir_close(dir);
        ret = 0;
    }
    return ret;
}
/*读取目录，成功返回 1 个目录项，失败返回 NULL */
struct dir_entry* dir_read(struct dir* dir) {
    struct dir_entry* dir_e = (struct dir_entry*)dir->dir_buf;
    struct inode* dir_inode = dir->inode;
    uint32_t all_blocks[140] = {0}, block_cnt = 12;
    uint32_t block_idx = 0, dir_entry_idx = 0;
    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (dir_inode->i_sectors[12] != 0) { // 若含有一级间接块表
        ide_read(cur_part->my_disk, dir_inode->i_sectors[12],  all_blocks + 12, 1);
        block_cnt = 140;
    }
    block_idx = 0;

    uint32_t cur_dir_entry_pos = 0;
    // 当前目录项的偏移，此项用来判断是否是之前已经返回过的目录项
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size;
    // 1 扇区内可容纳的目录项个数
    /* 在目录大小内遍历 */
    while (dir->dir_pos < dir_inode->i_size) {
        if (dir->dir_pos >= dir_inode->i_size) {
            return NULL;
        }
        if (all_blocks[block_idx] == 0) {
        // 如果此块地址为 0，即空块，继续读出下一块
            block_idx++;
            continue;
        }
        memset(dir_e, 0, SECTOR_SIZE);
        ide_read(cur_part->my_disk, all_blocks[block_idx], dir_e, 1);
        dir_entry_idx = 0;
        /* 遍历扇区内所有目录项 */
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type) {
            // 如果 f_type 不等于 0，即不等于 FT_UNKNOWN
            /* 判断是不是最新的目录项，避免返回曾经已经返回过的目录项 */
            if (cur_dir_entry_pos < dir->dir_pos) {
                cur_dir_entry_pos += dir_entry_size;
                dir_entry_idx++;
                continue;
            }
            ASSERT(cur_dir_entry_pos == dir->dir_pos);
            dir->dir_pos += dir_entry_size;
            // 更新为新位置，即下一个返回的目录项地址
            return dir_e + dir_entry_idx;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    return NULL;
}

/* 读取目录 dir 的 1 个目录项，成功后返回其目录项地址，
到目录尾时或出错时返回 NULL */
struct dir_entry* sys_readdir(struct dir* dir) {
    ASSERT(dir != NULL);
    return dir_read(dir);
}

/* 把目录 dir 的指针 dir_pos 置 0 */
void sys_rewinddir(struct dir* dir) {
    dir->dir_pos = 0;
}