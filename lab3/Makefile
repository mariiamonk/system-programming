CC = gcc
ASM = nasm
LD = ld

CFLAGS = -ffreestanding -nostdlib -mno-red-zone -m64 -g
LDFLAGS = -nostdlib -static -T linker.ld

kernel.bin: boot.o kernel.o
	$(LD) $(LDFLAGS) $^ -o $@

boot.o: boot.asm
	$(ASM) -f elf64 $< -o $@

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o kernel.bin kernel.img

# Автоматическое создание образа (удобно)
img:
	dd if=/dev/zero of=kernel.img bs=1M count=100
	sudo mkfs.vfat -F 32 kernel.img
	mkdir -p mnt
	sudo mount kernel.img mnt
	sudo cp kernel.bin mnt/
	sudo umount mnt
