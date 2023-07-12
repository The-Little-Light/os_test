#sh
nasm -o mbr.bin mbr.S
dd if=./mbr.bin of=../bochs/hd60M.img bs=512 count=1 conv=notrunc 
nasm -o loader.bin loader.S
dd if=./loader.bin of=../bochs/hd60M.img bs=512 count=1 seek=2 conv=notrunc