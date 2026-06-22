# Definições do protocolo Multiboot 1 (Altamente compatível)
.set MULTIBOOT1_MAGIC,    0x1BADB002
.set FLAGS,               0x00000003
.set CHECKSUM,            -(MULTIBOOT1_MAGIC + FLAGS)

.section .multiboot, "a", @progbits
.align 4
multiboot_header:
    .long MULTIBOOT1_MAGIC
    .long FLAGS
    .long CHECKSUM

.section .text
.code32
.global _start
.extern inicializar_kernel
.extern inicializar_user_mode
_start:
    # 1. Salva o valor de CS em um registrador de uso geral AX
    mov %cs, %ax
    # 2. Isola os 2 bits menos significativos (CPL)
    and $3, %ax
    # 3. Compara o CPL com 0 (Ring 0 - Modo Kernel)
    cmp $0, %ax
    je .loop_kernel
    # 4. Compara o CPL com 3 (Ring 3 - User Mode)
    cmp $3, %ax
    je .loop_user

    # Fallback de segurança: caso caia em Ring 1 ou 2, trava a CPU
.trava_cpu:
    cli
    hlt
    jmp .trava_cpu

.loop_kernel:
    # --- FLUXO RING 0 (BOOT VIA GRUB) ---
    # Configuramos uma pilha temporária para o Kernel funcionar com segurança.
    mov $stack_top, %esp
    # Chama a sua função em C que configura GDT, IDT, TSS e Paginação
    call inicializar_kernel
    # Se o kernel retornar por erro, trava
    jmp .trava_cpu

.loop_user:
    # --- FLUXO RING 3 (LINUX NATIVO) ---
    # 1. Salva o endereço original da pilha do Linux que contém argc/argv
    mov %esp, %ebx
    # 2. Alinha a pilha em 16 bytes para a ABI do GCC de 32 bits
    and $-16, %esp
    # 3. Extrai o ponteiro para o início do array argv (que fica logo após o argc)
    # No x86_32, o array argv começa exatamente onde está o argv[0]
    lea 4(%ebx), %eax
    # 4. Extrai o valor de argc
    mov (%ebx), %ecx
    # 5. Empilha os argumentos para a função C (da direita para a esquerda)
    push %eax           # Segundo argumento de inicializar_user_mode: char **argv
    push %ecx
    # 6. Chama a sua função em C que atuará como a aplicação nativa/UML
    call inicializar_user_mode
    # Finaliza o programa de forma limpa usando a Syscall de Exit do Linux
    mov $1, %eax        # Syscall 1 (sys_exit) em 32-bit (ou 60 para 64-bit)
    xor %ebx, %ebx      # Código de saída 0
    int $0x80

# --- ROTINAS DE HARDWARE ADAPTADAS PARA 32-BITS ---
.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax      # 0x10 é o deslocamento do segmento de dados na GDT
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $.flush # 0x08 é o segmento de código. ljmp força a atualização de CS
.flush:
    ret

# --- MOTOR DE GENERACAO AUTOMATICA DA IDT ---

# Macro para exceções que NÃO empilham código de erro de fábrica (Empilhamos 0 manualmente)
.macro ISR_SEM_ERRO num
.global isr_stub_\num
isr_stub_\num:
    push $0             # Código de erro falso
    push $\num          # Número da interrupção
    jmp wrapper_idt_comum
.endm

# Macro para exceções que JÁ empilham código de erro nativamente
.macro ISR_COM_ERRO num
.global isr_stub_\num
isr_stub_\num:
    push $\num          # Número da interrupção (o erro já foi empilhado pela CPU)
    jmp wrapper_idt_comum
.endm

# 1. Geração das 32 Exceções Internas da CPU (0 a 31)
ISR_SEM_ERRO 0
ISR_SEM_ERRO 1
ISR_SEM_ERRO 2
ISR_SEM_ERRO 3
ISR_SEM_ERRO 4
ISR_SEM_ERRO 5
ISR_SEM_ERRO 6
ISR_SEM_ERRO 7
ISR_COM_ERRO 8
ISR_SEM_ERRO 9
ISR_COM_ERRO 10
ISR_COM_ERRO 11
ISR_COM_ERRO 12
ISR_COM_ERRO 13
ISR_COM_ERRO 14
ISR_SEM_ERRO 15
ISR_SEM_ERRO 16
ISR_COM_ERRO 17
ISR_SEM_ERRO 18
ISR_SEM_ERRO 19
ISR_SEM_ERRO 20
ISR_COM_ERRO 21
ISR_SEM_ERRO 22
ISR_SEM_ERRO 23
ISR_SEM_ERRO 24
ISR_SEM_ERRO 25
ISR_SEM_ERRO 26
ISR_SEM_ERRO 27
ISR_SEM_ERRO 28
ISR_SEM_ERRO 29
ISR_COM_ERRO 30
ISR_SEM_ERRO 31

# 2. Geração das 16 Interrupções de Hardware (IRQs 0-15 mapeadas nos vetores 32 a 47)
ISR_SEM_ERRO 32
ISR_SEM_ERRO 33
ISR_SEM_ERRO 34
ISR_SEM_ERRO 35
ISR_SEM_ERRO 36
ISR_SEM_ERRO 37
ISR_SEM_ERRO 38
ISR_SEM_ERRO 39
ISR_SEM_ERRO 40
ISR_SEM_ERRO 41
ISR_SEM_ERRO 42
ISR_SEM_ERRO 43
ISR_SEM_ERRO 44
ISR_SEM_ERRO 45
ISR_SEM_ERRO 46
ISR_SEM_ERRO 47

# 3. Vetor especial 128 (0x80) para a Syscall por software
ISR_SEM_ERRO 128

# Wrapper unificado que monta o frame de registradores e entrega para o C
.extern despachante_idt_central
wrapper_idt_comum:
    pusha               # Salva todos os registradores de uso geral (EAX, ECX, EDX, etc.)
    push %esp           # Passa o ponteiro da pilha atual (que virará a struct no C)
    call despachante_idt_central
    add $4, %esp        # Desfaz o ponteiro da struct passado no argumento
    popa                # Restaura os registradores de uso geral
    add $8, %esp        # Limpa o num_int e o codigo_erro da pilha simultaneamente
    iret                # Retorna da interrupção de forma limpa

# Criação da tabela indexada de ponteiros que o idt.c lerá no loop de inicialização
.global tabela_wrappers_idt
.align 4
tabela_wrappers_idt:
    .long isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3
    .long isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7
    .long isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11
    .long isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
    .long isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19
    .long isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
    .long isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27
    .long isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
    .long isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35
    .long isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39
    .long isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43
    .long isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47
    .long isr_stub_128  # Posição final mapeando a int 0x80

.global idt_load
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

# --- SEÇÃO DE DADOS PARA A PILHA DO KERNEL ---
.section .bss
.align 16
stack_bottom:
.skip 16384 # Aloca 16 KB de espaço para a pilha do Kernel
stack_top:
