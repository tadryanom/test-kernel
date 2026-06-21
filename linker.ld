ENTRY(_start)

SECTIONS
{
    /* Endereço padrão universal para Kernels Multiboot (1 MB) */
    . = 0x00100000;

    .text : ALIGN(4) {
        *(.multiboot) /* Força a assinatura a ser o byte ZERO do executável */
        *(.text)
    }

    .rodata : ALIGN(4K) {
        *(.rodata)
    }

    .data : ALIGN(4K) {
        *(.data)
    }

    .bss : ALIGN(4K) {
        *(.bss)
    }

    /* Coleta seções extras geradas automaticamente pelo GCC para evitar erros */
    /DISCARD/ : {
        *(.eh_frame)
        *(.note.gnu.property)
    }
}
