# Projeto Dual-Core: Executável Híbrido Agnóstico (Ring 0 / Ring 3)

Este projeto implementa um binário executável híbrido e agnóstico de 32 bits (`x86_32`) capaz de rodar de forma nativa e estável em dois ambientes computacionais diametralmente opostos, chaveando seu comportamento em tempo de execução através da inspeção dinâmica de privilégios de hardware.

1. **No Linux Host (Ring 3):** Atua como uma ferramenta nativa de auditoria de espaço de usuário, extraindo dados de desempenho do hospedeiro lendo o subsistema `/proc` através de interrupções de software (`INT 0x80`).
2. **No QEMU/Bare-metal (Ring 0):** Atua como um micronúcleo de sistema operacional monolítico tradicional, assumindo o controle direto da CPU, habilitando paginação por hardware, gerenciando exceções e expondo um Shell iterativo via driver de teclado VGA.

---

## 🗺️ Mapa de Arquitetura do Binário

O segredo da portabilidade reside na dissociação completa da biblioteca padrão (`-nostdlib`) e na engenharia de linkagem estática absoluta mapeada em `1 MB` (`0x00100000`), um endereço perfeitamente aceito pelo carregador de compatibilidade do Linux e exigido pela especificação Multiboot.

```text
       [ Executável Único: ./meu_sistema_hibrido ]
                           |
                           v
              Ponto de Entrada Assembly: _start
                           |
            +--------------+--------------+

            | Inspeção do Registrador %cs |
            +--------------+--------------+
                           |
            +--------------+--------------+

            |                             |
            v (CPL == 3)                  v (CPL == 0)
    [ User Space - Linux ]         [ Bare-metal - QEMU ]

            |                             |
    ABI de compatibilidade 32-bit  Bootloader Multiboot 1

            |                             |
    Mapeamento de Pilha (RSP)     Pilha Estática (16 KB)

            |                             |
    Captura de argc/argv          Inicialização de Hardware:
            |                       -> GDT & IDT Geral (0-47)
    Syscalls diretas (int \$0x80):   -> Paginação Simples (1:1)
     -> sys_open (/proc)            -> PIT Timer (100Hz) & Teclado
     -> sys_read / sys_write        -> Autocura de Page Fault (#PF)

            |                             |
    Saída limpa (sys_exit 0)      Shell Interativo em C
```

---

## 🔧 Subsistemas Implementados

### 1. Vetor de Inicialização e Bifurcação (`boot.S`)
O ponto de entrada `_start` lê o registrador `%cs` e isola os dois bits de menor peso correspondentes ao **CPL (Current Privilege Level)**. Se o resultado for `0`, desvia para o fluxo bare-metal; se for `3`, preserva a pilha do Linux Host, extrai os parâmetros `argc`/`argv` passados pelo terminal e salta para a rotina de usuário.

### 2. Motor Centralizado de Interrupções (`idt.c`)
Mapeamento plano automatizado via macros em assembly dos primeiros 48 vetores da CPU. O subsistema realiza o remapeamento do chip PIC (`0x20` e `0x28`) tirando as IRQs de hardware da zona de conflito das exceções internas do processador.

### 3. Autocura de Erros de Memória Virtual
Mapeamento de paginação básica 1:1 por hardware. Ao tentar forçar uma gravação no endereço inválido `0xDEADBEEF`, o processador dispara um Page Fault (Vetor 14). O tratador do Kernel intercepta a falha, extrai o endereço linear problemático do registrador `CR2`, cria dinamicamente uma página física válida no `page_directory` em tempo de execução e ordena que a CPU reexecute a instrução de gravação, salvando o sistema operacional do colapso (*Triple Fault*).

### 4. Driver de Vídeo e Teclado VGA Interativo
Implementação de um motor `kernel_printf` flexível com suporte a tabulações (`\t`) e quebras de linha dinâmicas com rolagem de tela (*Scroll*). O driver de teclado decodifica *Make* e *Break* codes elétricos vindo da porta `0x60`, gerencia o estado da tecla *Shift* para caracteres maiúsculos e aceita o *Backspace* (`\b`) para exclusão de caracteres.

---

## 🚀 Como Compilar e Executar

### Pré-requisitos
Certifique-se de possuir o compilador GCC configurado para suporte multilib de 32 bits e o emulador QEMU instalados no seu sistema host:
```bash
sudo apt install gcc-multilib qemu-system-x86
```

### Compilação do Binário Unificado
Para gerar o executável híbrido puro, execute o Makefile do projeto:
```bash
make clean && make
```

### Execução no Modo Usuário (Linux Host)
Execute o binário passando argumentos ou rodando o analisador automatizado do subsistema `/proc`:
```bash
./meu_sistema_hibrido
./meu_sistema_hibrido argumento_teste_1 argumento_teste_2
```

### Execução no Modo Kernel (QEMU Bare-metal)
Para simular o carregamento físico do sistema operacional via Multiboot, inicialize o emulador:
```bash
qemu-system-x86_64 -kernel ./meu_sistema_hibrido
```
