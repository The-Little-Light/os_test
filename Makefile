install:
	nasm -I ./Include/ ./boot/mbr.S -o ./boot/mbr.bin
	nasm -I ./Include/ ./boot/loader.S -o ./boot/loader.bin
	dd if=./boot/mbr.bin of=./bochs/hd60M.img bs=512 count=1 conv=notrunc
	dd if=./boot/loader.bin of=./bochs/hd60M.img bs=512 seek=2 count=3 conv=notrunc

	nasm -f elf -o ./build/print.o ./lib/kernel/print.S
	nasm -f elf -o ./build/kernel.o ./kernel/kernel.S 

	gcc -m32 -I ./device/ -I ./lib/kernel -fno-stack-protector  -c -o ./build/timer.o ./device/timer.c
	gcc -m32 -I ./lib/kernel/ -I ./lib/ -I ./kernel/ -c -fno-builtin -fno-stack-protector -o ./build/main.o ./kernel/main.c

	gcc -m32 -I ./lib/kernel/ -I ./lib/ -I ./kernel/ -c -fno-builtin -fno-stack-protector -o ./build/interrupt.o ./kernel/interrupt.c
	gcc -m32 -I ./device/  -I ./lib/kernel/ -I ./lib/ -I ./kernel/ -c -fno-builtin -fno-stack-protector -o ./build/init.o ./kernel/init.c 
	ld -m elf_i386 -Ttext 0xc0001500 -e main -o ./build/kernel.bin ./build/main.o ./build/init.o \
	./build/interrupt.o ./build/print.o ./build/kernel.o ./build/timer.o


	dd if=./build/kernel.bin of=./bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc
	rm -rf ./boot/*.bin ./build/*