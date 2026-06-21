#include <stdio.h>

// Função que extrai o CPL do registrador CS
unsigned char obter_nivel_privilegio() {
    unsigned short cs_valor;

    // Lê o registrador CS de forma segura em qualquer Ring
    __asm__ __volatile__ (
        "mov %%cs, %0"
        : "=r" (cs_valor)
    );

    // Retorna apenas os 2 bits menos significativos (CPL)
    return (cs_valor & 0x03);
}

int main() {
    unsigned char cpl = obter_nivel_privilegio();

    if (cpl == 0) {
        printf("Detectado: Modo Kernel (Ring 0)\n");
    } else if (cpl == 3) {
        printf("Detectado: User Mode (Ring 3)\n");
    } else {
        printf("Detectado: Ring %d\n", cpl); // Rings 1 ou 2 (raros)
    }

    return 0;
}
