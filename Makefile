# Configurações do Compilador e Linker
CC = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -Werror -fno-pie -fno-stack-protector
LDFLAGS = -m elf_i386 -T linker.ld -static

# Arquivos do projeto
OBJ = boot.o main.o idt.o gdt.o

all: meu_sistema_hibrido

meu_sistema_hibrido: $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

boot.o: boot.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o meu_sistema_hibrido
