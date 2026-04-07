CC = gcc
CFLAGS = -Wall -Wextra

ASM_TARGET = sort_asm
ASM_CFLAGS = $(CFLAGS) -DUSE_ASM

C_TARGET = sort_c
C_CFLAGS = $(CFLAGS)

all: $(ASM_TARGET) $(C_TARGET)

$(ASM_TARGET): sort.c func.c
	$(CC) $(ASM_CFLAGS) -o $@ $^ $(LDFLAGS)

$(C_TARGET): sort.c func.c
	$(CC) $(C_CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(ASM_TARGET) $(C_TARGET)

.PHONY: all clean
