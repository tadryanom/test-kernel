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

extern void idt_load(uint32_t);
extern void ISR_GP_handler(); // Tratador em Assembly para General Protection Fault
extern void ISR_Syscall_handler(); // Protótipo da rotina Assembly
extern void ISR_Timer_handler(); // Nova rotina em Assembly para capturar o Timer

// Envia comandos para os chips controladores de IO (Portas de Hardware)
void outb(uint16_t porta, uint8_t dado) {
    __asm__ __volatile__("outb %0, %1" : : "a"(dado), "Nd"(porta));
}

// Programa o Temporizador Físico (PIT 8253) para a frequência desejada
void programar_pit_timer(uint32_t frequencia) {
    uint32_t divisor = 1193180 / frequencia; // O oscilador nativo roda a 1.19 MHz
    outb(0x43, 0x36);                        // Envia o byte de comando (Modo 3)
    outb(0x40, (uint8_t)(divisor & 0xFF));   // Byte inferior do divisor
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF)); // Byte superior do divisor
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

    // Limpa a IDT preenchendo com zeros
    for(int i = 0; i < 256; i++) {
        configurar_idt_entry(i, 0, 0, 0);
    }

    // Remapeia o controlador PIC de interrupções de hardware para começar no vetor 32
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0x00); outb(0xA1, 0x00);

    // Configura especificamente o vetor 13 (0x0D) - General Protection Fault (#GP)
    // 0x8E = Presente, Ring 0, Tipo Interrupção 32-bit
    configurar_idt_entry(13, (uint32_t)ISR_GP_handler, 0x08, 0x8E);

    // NOVO - Vetor 0x80 (128): Captura a nossa interrupção customizada
    // 0x8E = Presente, Ring 0, Tipo Interrupção 32-bit
    configurar_idt_entry(128, (uint32_t)ISR_Syscall_handler, 0x08, 0x8E);

    // NOVO: Vetor 32 (IRQ0 - Timer do Relógio)
    configurar_idt_entry(32, (uint32_t)ISR_Timer_handler, 0x08, 0x8E);

    idt_load((uint32_t)&idp);
}
