// gdt.c
#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct gdt_entry gdt[3];
struct gdt_ptr gp;

// Função em Assembly para carregar a GDT e atualizar os registradores de segmento
extern void gdt_flush(uint32_t);

void configurar_gdt_entry(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}

void inicializar_gdt() {
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gp.base  = (uint32_t)&gdt;

    // 1. Descritor Nulo
    configurar_gdt_entry(0, 0, 0, 0, 0);
    // 2. Segmento de Código do Kernel (Ring 0): Acesso 0x9A, Granularidade 0xCF
    configurar_gdt_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    // 3. Segmento de Dados do Kernel (Ring 0): Acesso 0x92, Granularidade 0xCF
    configurar_gdt_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    gdt_flush((uint32_t)&gp);
}
