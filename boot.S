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

.global idt_load
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

.global ISR_GP_handler
.extern c_gp_handler
ISR_GP_handler:
    cli
    # O processador empilha um código de erro automaticamente no #GP.
    # Nós o desempilhamos para não corromper a pilha antes de chamar o C.
    add $4, %esp
    call c_gp_handler
    iret

.global ISR_Syscall_handler
.extern c_syscall_handler
ISR_Syscall_handler:
    pusha               # Salva todos os registradores de uso geral (EAX, ECX, etc.)
    call c_syscall_handler
    popa                # Restaura os registradores
    iret                # Retorna da interrupção de forma limpa

.global ISR_Timer_handler
.extern c_timer_handler
ISR_Timer_handler:
    pusha               # Salva o estado atual dos registradores da CPU
    call c_timer_handler
    # Informa ao hardware PIC que a interrupção foi processada (Sinal EOI)
    mov $0x20, %al
    out %al, $0x20
    popa                # Restaura os registradores
    iret                # Retorna da interrupção retomando o código principal

# --- SEÇÃO DE DADOS PARA A PILHA DO KERNEL ---
.section .bss
.align 16
stack_bottom:
.skip 16384 # Aloca 16 KB de espaço para a pilha do Kernel
stack_top:
