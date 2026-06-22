// main.c
#include <stdint.h>

volatile uint32_t ticks_do_relogio = 0; // Contador de tempo global do Kernel

// Protótipos das funções de Kernel
void inicializar_gdt();
void inicializar_idt();
void programar_pit_timer(uint32_t frequencia);
void outb(uint16_t porta, uint8_t dado);
uint8_t inb(uint16_t porta); // Nova função de leitura de porta de hardware

// Mapa 1: Letras minúsculas e números padrão (Sem Shift)
static const char kbd_map_normal[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
  '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, '*',   0, ' '
};

// Mapa 2: Caracteres equivalentes quando o Shift está ativado
static const char kbd_map_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0,
  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0, '*',   0, ' '
};

// Variável de estado global para controlar se o Shift está pressionado
static int shift_pressionado = 0;
// Posição global para sabermos onde escrever o texto do teclado na tela
static int teclado_cursor_x = 0;
static const int teclado_linha_y = 9;

// Move o cursor piscante do monitor para uma coordenada X, Y específica
void kernel_mover_cursor(int x, int y) {
    uint16_t posicao = (y * 80) + x;

    // Envia o byte superior da posição para o chip VGA (registrador 14)
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)((posicao >> 8) & 0xFF));

    // Envia o byte inferior da posição para o chip VGA (registrador 15)
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(posicao & 0xFF));
}

// Preenche toda a tela de vídeo com espaços em branco e reseta o cursor
void kernel_clear_screen() {
    volatile char *video_memory = (volatile char *)0xB8000;

    // 80 colunas * 25 linhas = 2000 caracteres (cada um ocupa 2 bytes)
    for (int i = 0; i < 80 * 25; i++) {
        video_memory[i * 2] = ' ';       // Caractere vazio
        video_memory[i * 2 + 1] = 0x07; // Cor cinza padrão
    }

    kernel_mover_cursor(0, 0);
}

// Função do Kernel para escrever em coordenadas (X, Y) na tela do QEMU
void kernel_print_at(int x, int y, const char *str, uint8_t cor) {
    volatile char *video_memory = (volatile char *)0xB8000;
    // Cada linha VGA tem 80 caracteres, e cada caractere usa 2 bytes (160 bytes por linha)
    int offset = (y * 160) + (x * 2);

    int i = 0;
    while (str[i] != '\0') {
        video_memory[offset + (i * 2)] = str[i];
        video_memory[offset + (i * 2) + 1] = cor;
        i++;
    }
}

// Tratador do Teclado (Chamado em Ring 0 sempre que uma tecla é pressionada/solta)
void c_keyboard_handler() {
    // Lê o scancode elétrico da porta do chip controlador de teclado (0x60)
    uint8_t scancode = inb(0x60);

     // DETECÇÃO: Verifica se as teclas de Shift (Esquerdo: 0x2A ou Direito: 0x36) foram PRESSIONADAS
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressionado = 1;
        return;
    }

    // DETECÇÃO: Verifica se as teclas de Shift (Esquerdo: 0xAA ou Direito: 0xB6) foram SOLTAS
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressionado = 0;
        return;
    }

    // Se o bit 7 estiver zerado, significa que a tecla foi PRESSIONADA (Key Down)
    // Se estiver em 1, significa que a tecla foi SOLTA (Key Up), o que ignoramos aqui
    if (!(scancode & 0x80)) {
        char caractere = shift_pressionado ? kbd_map_shift[scancode] : kbd_map_normal[scancode];

        if (caractere != 0) {
            volatile char *video_memory = (volatile char *)0xB8000;
            int offset = (teclado_linha_y * 160) + (teclado_cursor_x * 2);

            // Trata a quebra de linha ou o limite horizontal da tela
            if (caractere == '\n' || teclado_cursor_x >= 79) {
                teclado_cursor_x = 0;
            } else {
                video_memory[offset] = caractere;
                video_memory[offset + 1] = 0x0F; // Texto Branco Brilhante
                teclado_cursor_x++;
            }

            // Move o cursor de hardware acompanhando a digitação
            kernel_mover_cursor(teclado_cursor_x, teclado_linha_y);
        }
    }
}

// Tratador do Timer (chamado a cada tique do relógio em Ring 0)
void c_timer_handler() {
    ticks_do_relogio++;

    // A cada 100 tiques (~1 segundo com frequência de 100Hz), atualiza o painel
    if (ticks_do_relogio % 100 == 0) {
        uint32_t segundos = ticks_do_relogio / 100;

        // Conversão ultra simples de inteiro para string (suporta até 99 segundos)
        char tempo_str[] = "Tempo de Boot: 00s";
        tempo_str[15] = '0' + (segundos / 10);
        tempo_str[16] = '0' + (segundos % 10);

        // Escreve de forma fixa na Linha 5, Coluna 0 da tela do QEMU
        kernel_print_at(0, 5, tempo_str, 0x0E); // Texto Amarelo
    }
}

// O manipulador em C que será executado quando a INT 0x80 for disparada no Kernel
void c_syscall_handler() {
    kernel_print_at(0, 2, "-> INTERRUPCAO INT 0x80 CAPTURADA COM SUCESSO EM RING 0!", 0x0E);
}

// Compilado com opções de Kernel (ex: -ffreestanding)
void inicializar_kernel(void) {
    kernel_clear_screen(); // Limpa completamente o iPXE e rastros da BIOS antes de escrever

    // 1. Configurar GDT nativa
    inicializar_gdt();
    // 2. Configurar IDT (para evitar a triple fault que discutimos antes!)
    inicializar_idt();
    // A partir deste ponto, se o código executar "INT 0x80", ele NÃO causará
    // uma falha tripla, pois o vetor de #GP (13) está devidamente capturado!

    // Configura o chip PIT (Programmable Interval Timer) para disparar 100 vezes por segundo (100 Hz)
    programar_pit_timer(100);
    // Ativa as interrupções de hardware na CPU (limpa a flag de interrupção)
    __asm__ __volatile__("sti");

    kernel_print_at(0, 0, "KERNEL HIBRIDO ATIVO: Rodando com sucesso em Ring 0!", 0x0A);
    // Dispara a interrupção por software manualmente dentro do modo Kernel
    __asm__ __volatile__("int $0x80");
    kernel_print_at(0, 4, "[PIT Timer IRQ0 ativado a 100Hz]", 0x07);

    kernel_print_at(0, 7, "[Teclado IRQ1 ativado]", 0x07);
    kernel_print_at(0, 8, "Digite algo diretamente no QEMU:", 0x0B); // Ciano

    // Posiciona o cursor piscante exatamente onde a digitação do usuário começará
    kernel_mover_cursor(teclado_cursor_x, teclado_linha_y);

    // FORÇANDO UM CRASH PROPOSITAL DE EXCEÇÃO (Vetor 0)
    //volatile int a = 5;
    //volatile int b = 0;
    //volatile int c = a / b; // Isso gerará um erro físico #DE (Vetor 0)
    //(void)c;

    // 3. Configurar TSS
    // 4. Ativar Paginação / Carregar CR3
    // 5. Entrar no loop do Sistema Operacional
    while(1) {
        __asm__("hlt");
    }
}

// Função auxiliar para imprimir strings dinâmicas no terminal do Linux
void imprimir_texto(const char *str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }

    // Usamos syscalls diretas em assembly porque não temos o printf da glibc
    // disponível nativamente por causa do -nostdlib.
    // Syscall write (4 em 32-bit): write(fd=1, buf=msg, count=sizeof(msg))
    __asm__ __volatile__ (
        "mov $4, %%eax\n"
        "mov $1, %%ebx\n"
        "mov %0, %%ecx\n"
        "mov %1, %%edx\n"
        "int $0x80\n"
        :
        : "r"(str), "r"(len)
        : "eax", "ebx", "ecx", "edx"
    );
}

// Compilado respeitando as bibliotecas do Host ou Syscalls diretas
// 1. O ponteiro da pilha atual já contém os argumentos do Linux (argc, argv)
// 2. Mapear memória simulada se necessário
// 3. Executar lógica estilo User-Mode Linux (UML)
// Nova assinatura recebendo os parâmetros diretamente da pilha mapeada pelo Linux
void inicializar_user_mode(int argc, char **argv) {
    char buffer_teclado[64];
    int bytes_lidos = 0;

    imprimir_texto("==================================================\n");
    imprimir_texto(" MODO USUARIO ATIVO: Digite algo e aperte ENTER:\n");
    imprimir_texto("==================================================\n");

    // Syscall 'read' (3) no Linux de 32 bits:
    // EAX = 3 (sys_read)
    // EBX = 0 (stdin - Teclado)
    // ECX = endereço do buffer
    // EDX = tamanho máximo a ler
    __asm__ __volatile__ (
        "mov $3, %%eax\n"
        "mov $0, %%ebx\n"
        "mov %1, %%ecx\n"
        "mov %2, %%edx\n"
        "int $0x80\n"
        "mov %%eax, %0\n" // Retorna em bytes_lidos a quantidade digitada
        : "=r"(bytes_lidos)
        : "r"(buffer_teclado), "r"(sizeof(buffer_teclado) - 1)
        : "eax", "ebx", "ecx", "edx"
    );

    if (bytes_lidos > 0) {
        buffer_teclado[bytes_lidos] = '\0'; // Finaliza a string com nulo de segurança
        imprimir_texto("\n[Linux Host] Voce digitou com sucesso: ");
        imprimir_texto(buffer_teclado);
        imprimir_texto("\n");
    }

    imprimir_texto("Argumentos detectados: ");

    // Converte o número de argumentos para caractere simples (funciona para argc < 10)
    char argc_char = (char)('0' + argc);
    char argc_str[3] = {argc_char, '\n', '\0'};
    imprimir_texto(argc_str);

    // Varre o array argv e imprime cada um dos parâmetros passados no terminal
    for (int i = 0; i < argc; i++) {
        imprimir_texto(" -> argv[");

        char idx_char = (char)('0' + i);
        char idx_str[3] = {idx_char, ']', '\0'};
        imprimir_texto(idx_str);

        imprimir_texto(": ");
        imprimir_texto(argv[i]);
        imprimir_texto("\n");
    }

    imprimir_texto("==================================================\n");
}
