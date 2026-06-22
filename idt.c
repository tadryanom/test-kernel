// idt.c
#include <stdint.h>

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;        // Segmento de código da GDT (0x08)
    uint8_t  always0;    // Sempre 0
    uint8_t  flags;      // Atributos (ex: 0x8E para interrupção de Ring 0)
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idp;

// Estrutura que reflete o estado da pilha empilhado pelo Assembly (pusha + erros)
struct abr_registradores {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Empilhados por pusha
    uint32_t num_int, codigo_erro;                  // Empilhados pelas macros do Assembly
    uint32_t eip, cs, eflags, useresp, ss;          // Empilhados automaticamente pela CPU
};

// Declaração do array de ponteiros para os tratadores gerados no Assembly
extern uint32_t tabela_wrappers_idt[];
extern void idt_load(uint32_t);
// Referencia as estruturas de paginação que estão instanciadas no main.c
extern uint32_t page_directory[1024];
extern uint32_t page_table_0[1024];

// Envia comandos para os chips controladores de IO (Portas de Hardware)
void outb(uint16_t porta, uint8_t dado) {
    __asm__ __volatile__("outb %0, %1" : : "a"(dado), "Nd"(porta));
}
// Função essencial para o processador ler um byte vindo de uma porta física do chip
uint8_t inb(uint16_t porta) {
    uint8_t resultado;
    __asm__ __volatile__("inb %1, %0" : "=a"(resultado) : "Nd"(porta));
    return resultado;
}
// Função utilitária para escrever 16 bits em uma porta física
void outw(uint16_t porta, uint16_t dado) {
    __asm__ __volatile__("outw %0, %1" : : "a"(dado), "Nd"(porta));
}

// Declara a função externa criada no main.c
void kernel_printf(const char *format, ...);
void kernel_print_at(int x, int y, const char *str, uint8_t cor);

// Array de strings amigáveis para descrever as exceções da CPU (0 a 31)
static const char *mensagens_excecao[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "SIMD Floating-Point",
    "Virtualization Exception", "Control Protection Exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection Exception", "VMM Communication Exception", "Security Exception"
};

// Programa o Temporizador Físico (PIT 8253) para a frequência desejada
void programar_pit_timer(uint32_t frequencia) {
    uint32_t divisor = 1193180 / frequencia; // O oscilador nativo roda a 1.19 MHz
    outb(0x43, 0x36);                        // Envia o byte de comando (Modo 3)
    outb(0x40, (uint8_t)(divisor & 0xFF));   // Byte inferior do divisor
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF)); // Byte superior do divisor
}

// Função auxiliar para converter um número hexadecimal em string (para exibir o endereço de CR2)
void hex_para_string(uint32_t num, char *str) {
    char const hex_chars[] = "0123456789ABCDEF";
    str[0] = '0';
    str[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        str[i + 2] = hex_chars[num & 0xF];
        num >>= 4;
    }
    str[10] = '\0';
}

// O Despachante Central (chamado pelo Assembly para QUALQUER interrupção)
void despachante_idt_central(struct abr_registradores *regs) {
    // 1. Trata Exceções Internas da CPU (0 a 31)
    if (regs->num_int < 32) {
        // SE FOR PAGE FAULT (VETOR 14): Lê o registrador CR2 para saber o endereço inválido
        if (regs->num_int == 14) {
            uint32_t endereco_falha;
            // Instrução Assembly para mover o valor de CR2 para uma variável C
            __asm__ __volatile__("mov %%cr2, %0" : "=r"(endereco_falha));

            // Verifica se a falha ocorreu especificamente no endereço do nosso teste (0xDEADBEEF)
            if (endereco_falha == 0xDEADBEEF) {
                // Alerta visual rápido em amarelo no meio da tela para mostrar que interceptamos
                kernel_print_at(0, 15, "[Page Fault] Endereco 0xDEADBEEF interceptado! Mapeando agora...", 0x0E);
                // Aponta a entrada 890 do Diretório de Páginas para a nossa tabela física
                // Atributos: 0x3 = Presente + Leitura/Escrita
                page_directory[endereco_falha >> 22] = ((uint32_t)&page_table_0) | 0x3;
                // Mapeia o endereço linear de forma 1:1 física temporária apenas para aceitar a escrita
                page_table_0[(endereco_falha >> 12) & 0x3FF] = 0x00100000 | 0x3; // Joga na memória física estável de 1MB
                // OBRIGATÓRIO: Recarrega o CR3 para invalidar o cache da CPU (TLB Flush)
                __asm__ __volatile__("mov %%cr3, %%eax\n mov %%eax, %%cr3\n" : : : "eax");
                // RETORNO SEGURO:
                // O processador x86 joga o EIP exatamente em cima da instrução que falhou.
                // Como agora o endereço está mapeado, basta retornar do despachante.
                // A CPU reexecutará a escrita com sucesso absoluto!
                return;
            }
        }

        // --- PAINEL DE DUMP DE REGISTRADORES FLEXÍVEL ---
        kernel_printf("\n\n!!! ERRO CRITICO DO PROCESSADOR: %s (Vetor %d) !!!\n", mensagens_excecao[regs->num_int], regs->num_int);
        kernel_printf("----------------------------------------------------------------------\n");
        kernel_printf(" EAX: %x   EBX: %x   ECX: %x   EDX: %x\n", regs->eax, regs->ebx, regs->ecx, regs->edx);
        kernel_printf(" EDI: %x   ESI: %x   EBP: %x   ESP: %x\n", regs->edi, regs->esi, regs->ebp, regs->esp);
        kernel_printf(" EIP: %x   CS:  %x   EFLAGS: %x\n", regs->eip, regs->cs, regs->eflags);
        if (regs->num_int == 14) {
            uint32_t cr2_val;
            __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2_val));
            kernel_printf(" CR2 (Endereco do Page Fault): %x\n", cr2_val);
        }
        kernel_printf("----------------------------------------------------------------------\n");
        kernel_printf("CPU paralisada em Ring 0 para protecao de hardware.");

        // Se ocorrer uma falha crítica, paralisamos a CPU com segurança para diagnóstico
        while(1) { __asm__ __volatile__("cli; hlt"); }
    }

    // 2. Trata Interrupções de Hardware (IRQs remapeadas de 32 a 47)
    if (regs->num_int >= 32 && regs->num_int <= 47) {
        // Encaminha para funções específicas baseadas na IRQ
        if (regs->num_int == 32) {
            extern void c_timer_handler();
            c_timer_handler();
        } else if (regs->num_int == 33) {
            extern void c_keyboard_handler();
            c_keyboard_handler();
        }

        // Envia sinal EOI (End of Interrupt) para os chips PICs
        if (regs->num_int >= 40) {
            outb(0xA0, 0x20); // Envia EOI para o PIC Escravo se for IRQ 8-15
        }
        outb(0x20, 0x20);     // Envia EOI para o PIC Mestre
    }

    // 3. Trata interrupções de Software como a Syscall (128)
    if (regs->num_int == 128) {
        extern void c_syscall_handler();
        c_syscall_handler();
    }
}

void configurar_idt_entry(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = base & 0xFFFF;
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel     = sel;
    idt[num].always0 = 0;
    idt[num].flags   = flags;
}

void inicializar_idt() {
    idp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idp.base  = (uint32_t)&idt;

    // Remapeia o controlador PIC de interrupções de hardware para começar no vetor 32
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0x00); outb(0xA1, 0x00);

    // Registra os primeiros 48 vetores automaticamente usando a tabela gerada no Assembly
    for (int i = 0; i < 48; i++) {
        configurar_idt_entry(i, tabela_wrappers_idt[i], 0x08, 0x8E);
    }

    // Registra explicitamente o vetor 128 (0x80) para a Syscall
    configurar_idt_entry(128, tabela_wrappers_idt[48], 0x08, 0x8E);

    idt_load((uint32_t)&idp);
}
