// main.c
#include <stdint.h>
#include <stdarg.h> // Necessário para gerenciar argumentos variáveis (...), nativo do GCC

// Declaração do array de ponteiros para os tratadores gerados no Assembly
extern uint32_t tabela_wrappers_idt[];

volatile uint32_t ticks_do_relogio = 0; // Contador de tempo global do Kernel
// Cria as tabelas na seção BSS (Alinhadas em 4096 bytes como o hardware exige)
__attribute__((aligned(4096))) uint32_t page_directory[1024];
__attribute__((aligned(4096))) uint32_t page_table_0[1024];

// --- VARIÁVEIS E FUNÇÕES DO SHELL EM RING 0 ---
#define BUFFER_SHELL_TAMANHO 64
static char shell_buffer[BUFFER_SHELL_TAMANHO];
static int shell_buffer_idx = 0;

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

// Variáveis globais para controlar a posição atual do cursor na tela (80x25)
static int cursor_x = 0;
static int cursor_y = 0;

// Variável de estado global para controlar se o Shift está pressionado
static int shift_pressionado = 0;

void ativar_paginacao_simples() {
    // 1. Mapeia o primeiro Megabyte de forma linear (1:1) usando a page_table_0
    // Isso garante que o Kernel continue rodando quando ligarmos a paginação!
    for (int i = 0; i < 1024; i++) {
        // Atributos: 0x3 = Página Presente + Leitura/Escrita ativa
        page_table_0[i] = (i * 4096) | 0x3;
    }

    // 2. Coloca a primeira tabela dentro do Diretório de Páginas
    page_directory[0] = ((uint32_t)page_table_0) | 0x3;

    // 3. Deixa todas as outras 1023 entradas do Diretório como ZERO (Não Presentes)
    for (int i = 1; i < 1024; i++) {
        page_directory[i] = 0;
    }

    // 4. Carrega o endereço do Diretório no Registrador de Controle CR3
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(page_directory));

    // 5. Liga o bit de Paginação (Bit 31) no registrador CR0
    uint32_t cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Ativa o bit PG
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
}

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
    cursor_x = 0;
    cursor_y = 0;
    kernel_mover_cursor(0, 0);
}

// Função base para imprimir um único caractere e gerenciar quebras de linha
void kernel_putc(char c, uint8_t cor) {
    volatile char *video_memory = (volatile char *)0xB8000;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\t') {
        // Avança o cursor até o próximo múltiplo de 4 colunas
        int espaços_necessarios = 4 - (cursor_x % 4);
        cursor_x += espaços_necessarios;

        // Se a tabulação estourar o limite da linha (80 colunas), quebra a linha
        if (cursor_x >= 80) {
            cursor_x = 0;
            cursor_y++;
        }
    } else {
        int offset = (cursor_y * 160) + (cursor_x * 2);
        video_memory[offset] = c;
        video_memory[offset + 1] = cor;
        cursor_x++;

        if (cursor_x >= 80) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    // Rolagem de tela simples (Scroll) se estourar as 25 linhas
    if (cursor_y >= 25) {
        for (int i = 0; i < 80 * 24; i++) {
            video_memory[i * 2] = video_memory[(i + 80) * 2];
            video_memory[i * 2 + 1] = video_memory[(i + 80) * 2 + 1];
        }
        for (int i = 80 * 24; i < 80 * 25; i++) {
            video_memory[i * 2] = ' ';
            video_memory[i * 2 + 1] = 0x07;
        }
        cursor_y = 24;
    }
    kernel_mover_cursor(cursor_x, cursor_y);
}

// Imprime uma string simples usando o cursor dinâmico
void kernel_print(const char *str, uint8_t cor) {
    int i = 0;
    while (str[i] != '\0') {
        kernel_putc(str[i], cor);
        i++;
    }
}

// Converte inteiro para string decimal de forma segura
void itoa(int n, char *str) {
    int i = 0, negativo = 0;
    if (n < 0) { negativo = 1; n = -n; }
    do {
        str[i++] = (n % 10) + '0';
    } while ((n /= 10) > 0);
    if (negativo) str[i++] = '-';
    str[i] = '\0';

    // Inverte a string gerada
    for (int j = 0; j < i / 2; j++) {
        char tmp = str[j];
        str[j] = str[i - j - 1];
        str[i - j - 1] = tmp;
    }
}

// Converte inteiro para string hexadecimal de forma segura
void itoh(uint32_t n, char *str) {
    char const hex_chars[] = "0123456789ABCDEF";
    int i = 0;
    do {
        str[i++] = hex_chars[n & 0xF];
    } while ((n >>= 4) > 0);
    str[i] = '\0';

    // Inverte a string gerada
    for (int j = 0; j < i / 2; j++) {
        char tmp = str[j];
        str[j] = str[i - j - 1];
        str[i - j - 1] = tmp;
    }
}

// O nosso printf customizado do Kernel (Aceita %s, %d, %x com cor fixa 0x07)
void kernel_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);

    int i = 0;
    char buf[32];

    while (format[i] != '\0') {
        if (format[i] == '%') {
            i++;
            switch (format[i]) {
                case 's': {
                    char *s = va_arg(args, char *);
                    kernel_print(s, 0x07);
                    break;
                }
                case 'd': {
                    int d = va_arg(args, int);
                    itoa(d, buf);
                    kernel_print(buf, 0x07);
                    break;
                }
                case 'x': {
                    uint32_t x = va_arg(args, uint32_t);
                    itoh(x, buf);
                    kernel_print("0x", 0x07);
                    kernel_print(buf, 0x07);
                    break;
                }
                default:
                    kernel_putc('%', 0x07);
                    kernel_putc(format[i], 0x07);
                    break;
            }
        } else {
            kernel_putc(format[i], 0x07);
        }
        i++;
    }
    va_end(args);
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

// Função simples para comparar duas strings no bare-metal (substituta da strcmp)
int kernel_strcmp(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return 1;
        i++;
    }
    if (s1[i] == '\0' && s2[i] == '\0') return 0;
    return 1;
}

// O Interpretador de Comandos do seu Sistema Operacional
void kernel_processar_comando(const char *comando) {
    kernel_printf("\n"); // Pula para a linha de baixo para exibir a resposta

    if (kernel_strcmp(comando, "help") == 0) {
        kernel_printf("Comandos aceitos:\n");
        kernel_printf("  help\t\tExibe esta lista de comandos\n");
        kernel_printf("  clear\t\tLimpa a tela do monitor virtual\n");
        kernel_printf("  reboot\tForca um reinicio fisico via hardware\n");
    } else if (kernel_strcmp(comando, "clear") == 0) {
        kernel_clear_screen();
    } else if (kernel_strcmp(comando, "reboot") == 0) {
        kernel_printf("Reiniciando a CPU...\n");
        // Pulsa a linha de reset do processador através do controlador 8042 (Porta 0x64)
        // Isso força uma Falha Tripla controlada por hardware para reboot imediato!
        uint8_t bom = 0x02;
        while (bom & 0x02) {
            bom = inb(0x64);
        }
        outb(0x64, 0xFE);
    } else if (comando[0] != '\0') {
        kernel_printf("Comando invalido: '%s'. Digite 'help'.\n", comando);
    }

    kernel_printf("\nso_hibrido> ");
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
            // Cenário A: Se pressionar ENTER, processa o comando acumulado
            if (caractere == '\n') {
                shell_buffer[shell_buffer_idx] = '\0'; // Finaliza a string do comando
                kernel_processar_comando(shell_buffer);
                shell_buffer_idx = 0; // Reseta o buffer para o próximo comando
            }
            // Cenário B: Se for caractere normal, joga no buffer e espelha na tela
            else if (shell_buffer_idx < BUFFER_SHELL_TAMANHO - 1) {
                shell_buffer[shell_buffer_idx++] = caractere;
                kernel_putc(caractere, 0x0F); // Texto branco brilhante
            }
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
        char tempo_str[] = "Boot: 00s";
        tempo_str[6] = '0' + (segundos / 10);
        tempo_str[7] = '0' + (segundos % 10);

        // Escreve de forma fixa na Linha 0, Coluna 65 da tela do QEMU
        kernel_print_at(65, 0, tempo_str, 0x0E);
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
    // 3. Configurar TSS

    // Configura o chip PIT (Programmable Interval Timer) para disparar 100 vezes por segundo (100 Hz)
    programar_pit_timer(100);

    // 4. Ativar Paginação / Carregar CR3
    ativar_paginacao_simples();

    // Ativa as interrupções de hardware na CPU (limpa a flag de interrupção)
    __asm__ __volatile__("sti");

    // Testando o kernel_printf atualizado com suporte a tabulações \t
    kernel_printf("TABELA DE STATUS DO HARDWARE (RING 0):\n");
    kernel_printf("-------------------------------------\n");
    kernel_printf("COMPONENTE\tSTATUS\t\tENDERECO\n");
    kernel_printf("GDT\t\t\t[OK]\t\t0x08 (CS Selector)\n");
    kernel_printf("IDT\t\t\t[OK]\t\t%x\n", tabela_wrappers_idt);
    kernel_printf("Paging\t\t[OK]\t\t%x\n", &page_directory);
    kernel_printf("Timer\t\t[ATIVO]\t\t100 Hz\n");

    // Abre a linha de comando oficial do seu SO
    kernel_printf("\nso_hibrido> ");

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

// Função do Modo Usuário para abrir, ler e fechar um arquivo do sistema Host
void ler_arquivo_host(const char *caminho) {
    int fd = 0;
    char buffer_conteudo[256];
    int bytes_lidos = 0;

    // 1. Executa a Syscall 'open' (5) no Linux 32-bit:
    // EAX = 5 (sys_open)
    // EBX = ponteiro para a string do caminho do arquivo
    // ECX = flags de abertura (0 = O_RDONLY, apenas leitura)
    __asm__ __volatile__ (
        "mov $5, %%eax\n"
        "mov %1, %%ebx\n"
        "mov $0, %%ecx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(fd)
        : "r"(caminho)
        : "eax", "ebx", "ecx"
    );

    // Se o retorno do FD for negativo, significa que o arquivo não existe ou não temos permissão
    if (fd < 0) {
        imprimir_texto("\n[Erro] Nao foi possivel abrir o arquivo solicitado.\n");
        return;
    }

    // 2. Executa a Syscall 'read' (3) no Linux 32-bit:
    // EAX = 3 (sys_read)
    // EBX = File Descriptor (fd) retornado no passo anterior
    // ECX = endereço do buffer para guardar os dados lidos
    // EDX = tamanho máximo a ler (255 bytes)
    __asm__ __volatile__ (
        "mov $3, %%eax\n"
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "mov %3, %%edx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(bytes_lidos)
        : "r"(fd), "r"(buffer_conteudo), "r"(sizeof(buffer_conteudo) - 1)
        : "eax", "ebx", "ecx", "edx"
    );

    if (bytes_lidos > 0) {
        buffer_conteudo[bytes_lidos] = '\0'; // Garante o fim da string com nulo
        imprimir_texto("\n[Linux Host] Conteudo do arquivo lido com sucesso:\n");
        imprimir_texto("--------------------------------------------------\n");
        imprimir_texto(buffer_conteudo);
        imprimir_texto("--------------------------------------------------\n");
    } else {
        imprimir_texto("\n[Aviso] O arquivo foi aberto, mas estava vazio.\n");
    }

    // 3. Executa a Syscall 'close' (6) no Linux 32-bit:
    // EAX = 6 (sys_close)
    // EBX = File Descriptor (fd)
    __asm__ __volatile__ (
        "mov $60, %%eax\n" // Nota: No Linux moderno de compatibilidade de 32-bits, sys_close é 6
        "mov $6, %%eax\n"
        "mov %0, %%ebx\n"
        "int $0x80\n"
        :
        : "r"(fd)
        : "eax", "ebx"
    );
}

// Compilado respeitando as bibliotecas do Host ou Syscalls diretas
// 1. O ponteiro da pilha atual já contém os argumentos do Linux (argc, argv)
// 2. Mapear memória simulada se necessário
// 3. Executar lógica estilo User-Mode Linux (UML)
// Nova assinatura recebendo os parâmetros diretamente da pilha mapeada pelo Linux
void inicializar_user_mode(int argc, char **argv) {
    imprimir_texto("==================================================\n");
    imprimir_texto(" MODO USUARIO ATIVO: Auditoria de Sistema Host\n");
    imprimir_texto("==================================================\n");

    // Cenário A: Se o usuário passou um arquivo específico via terminal
    if (argc > 1) {
        imprimir_texto("Tentando ler o arquivo passado por argumento: ");
        imprimir_texto(argv[1]);
        imprimir_texto("\n");
        ler_arquivo_host(argv[1]);
    }
    // Cenário B: Se rodar sem argumentos, ativa a inteligência de auditoria automática
    else {
        imprimir_texto("Nenhum argumento fornecido. Coletando dados do Host...\n\n");
        imprimir_texto("[Auditoria] Versao do Kernel do Linux Hospedeiro:");
        ler_arquivo_host("/proc/version");
        imprimir_texto("\n[Auditoria] Tempo de atividade do Host (segundos ativos / segundos ociosos):");
        ler_arquivo_host("/proc/uptime");
    }

    imprimir_texto("==================================================\n");
}
