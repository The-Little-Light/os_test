BUILD_DIR = ./build
ENTRY_POINT = 0xc0001500
AS = nasm
CC = gcc
LD = ld
LDFLAGS= -m elf_i386 -Ttext $(ENTRY_POINT) -e main
ASFLAGS = -f elf -g
LIB = -I ./lib/ -I ./lib/kernel/ -I ./lib/user/ -I ./kernel/ -I ./device/ -I ./thread/  -I ./userprog/
CFLAGS = -g -Wall $(LIB) -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes -m32 -fno-stack-protector

OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/init.o $(BUILD_DIR)/interrupt.o \
      $(BUILD_DIR)/timer.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/print.o $(BUILD_DIR)/switch.o \
      $(BUILD_DIR)/debug.o $(BUILD_DIR)/string.o $(BUILD_DIR)/memory.o \
      $(BUILD_DIR)/bitmap.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/list.o \
      $(BUILD_DIR)/sync.o $(BUILD_DIR)/console.o $(BUILD_DIR)/keyboard.o \
      $(BUILD_DIR)/ioqueue.o $(BUILD_DIR)/tss.o $(BUILD_DIR)/process.o
      
##############     c代码编译     ###############
$(BUILD_DIR)/main.o: kernel/main.c lib/kernel/print.h \
        lib/stdint.h kernel/init.h lib/string.h kernel/memory.h \
        thread/thread.h kernel/interrupt.h device/console.h \
        device/keyboard.h device/ioqueue.h userprog/process.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/init.o: kernel/init.c kernel/init.h lib/kernel/print.h \
        lib/stdint.h kernel/interrupt.h device/timer.h kernel/memory.h \
        thread/thread.h device/console.h device/keyboard.h userprog/tss.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/interrupt.c kernel/interrupt.h \
        lib/stdint.h kernel/global.h lib/kernel/io.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/timer.o: device/timer.c device/timer.h lib/kernel/io.h lib/kernel/print.h \
        kernel/interrupt.h thread/thread.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/debug.o: kernel/debug.c kernel/debug.h \
        lib/kernel/print.h lib/stdint.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/string.o: lib/string.c lib/string.h \
	kernel/debug.h kernel/global.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/memory.o: kernel/memory.c kernel/memory.h \
	lib/stdint.h lib/kernel/bitmap.h kernel/debug.h lib/string.h \
	thread/sync.h thread/thread.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/bitmap.o: lib/kernel/bitmap.c lib/kernel/bitmap.h kernel/global.h \
	lib/string.h kernel/interrupt.h lib/kernel/print.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/thread.o: thread/thread.c thread/thread.h \
	lib/stdint.h lib/string.h kernel/global.h kernel/memory.h \
	kernel/debug.h kernel/interrupt.h lib/kernel/print.h \
	userprog/process.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/list.o: lib/kernel/list.c lib/kernel/list.h \
	kernel/interrupt.h lib/stdint.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/sync.o: thread/sync.c thread/sync.h \
	lib/stdint.h thread/thread.h kernel/debug.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/console.o: device/console.c device/console.h \
	lib/kernel/print.h thread/sync.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/keyboard.o: device/keyboard.c device/keyboard.h \
	lib/kernel/print.h lib/kernel/io.h kernel/interrupt.h \
	kernel/global.h lib/stdint.h device/ioqueue.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/ioqueue.o: device/ioqueue.c device/ioqueue.h \
	kernel/interrupt.h kernel/global.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/tss.o: userprog/tss.c userprog/tss.h \
	kernel/global.h thread/thread.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/process.o: userprog/process.c userprog/process.h \
	lib/string.h kernel/global.h kernel/memory.h lib/kernel/print.h \
	thread/thread.h kernel/interrupt.h kernel/debug.h device/console.h
	$(CC) $(CFLAGS) $< -o $@
	
	

############## 汇编代码编译 ###############
$(BUILD_DIR)/kernel.o: ./kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@
$(BUILD_DIR)/print.o: ./lib/kernel/print.S
	$(AS) $(ASFLAGS) $< -o $@
$(BUILD_DIR)/switch.o: thread/switch.S
	$(AS) $(ASFLAGS) $< -o $@

############## 链接所有目标文件 #############
$(BUILD_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

.PHONY : mk_dir hd clean all

mk_dir:
	mkdir -p $(BUILD_DIR)

hd:
	dd if=$(BUILD_DIR)/kernel.bin  of=./bochs/hd60M.img  bs=512 count=200 seek=9 conv=notrunc

clean:
	cd $(BUILD_DIR) && rm -f ./*



$(BUILD_DIR)/mbr.bin: ./boot/mbr.S ./Include/boot.inc
	nasm -I ./Include/ ./boot/mbr.S -o $(BUILD_DIR)/mbr.bin
	dd if=$(BUILD_DIR)/mbr.bin of=./bochs/hd60M.img bs=512 count=1 conv=notrunc

$(BUILD_DIR)/loader.bin: ./boot/loader.S ./Include/boot.inc
	nasm -I ./Include/ ./boot/loader.S -o $(BUILD_DIR)/loader.bin
	dd if=$(BUILD_DIR)/loader.bin of=./bochs/hd60M.img bs=512 seek=2 count=3 conv=notrunc

load:  $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/loader.bin

build: $(BUILD_DIR)/kernel.bin

all: mk_dir load build hd


