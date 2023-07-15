install:
	nasm -I ./Include/ ./boot/mbr.S -o ./boot/mbr.bin
	nasm -I ./Include/ ./boot/loader.S -o ./boot/loader.bin
	dd if=./boot/mbr.bin of=./bochs/hd60M.img bs=512 count=1 conv=notrunc
	dd if=./boot/loader.bin of=./bochs/hd60M.img bs=512 seek=2 count=3 conv=notrunc

	nasm -f elf -o ./lib/kernel/print.o ./lib/kernel/print.S
	gcc -m32 -I ./lib/kernel/ -c -o ./kernel/main.o ./kernel/main.c
	ld -m elf_i386 -Ttext 0xc0001500 -e main -o ./kernel/kernel.bin ./kernel/main.o ./lib/kernel/print.o

	dd if=./kernel/kernel.bin of=./bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc
	rm -rf ./boot/*.bin ./kernel/*.o ./kernel/*.bin