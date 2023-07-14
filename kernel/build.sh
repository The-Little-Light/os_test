#sh
gcc -m32 -c -o main.o main.c
ld -m elf_i386 main.o -Ttext 0xc0001500 -e main -o kernel.bin # -Ttext用来起始虚拟地址，-e用来指定入口地址
dd if=kernel.bin of=../bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc