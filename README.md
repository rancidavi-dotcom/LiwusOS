# LiwusOS 🌌

LiwusOS é um sistema operacional de 32 bits moderno e ambicioso, construído do zero para a arquitetura x86. Ele combina conceitos clássicos de kernel monolítico com uma stack gráfica moderna inspirada no Vulkan e Wayland.


## ✨ Principais Características

*   **Kernel Monolítico x86:** Implementação completa de GDT, IDT, ISRs e controle de interrupções.
*   **Gerenciamento de Memória:** Paging (VMM), Physical Memory Manager (PMM) e Kernel Heap dinâmico.
*   **Multitarefa Preemptiva:** Escalonador Round-robin com suporte a `fork()` e `execve()`.
*   **LGX (Liwus Graphics eXtension):** Uma API gráfica moderna inspirada no Vulkan para aceleração e gerenciamento de buffers.
*   **Compositor Wayland:** Um compositor de janelas nativo que gerencia superfícies e entradas de mouse/teclado.
*   **Stack de Rede:** Implementação de TCP/IP e suporte a HTTP, com driver para RTL8139.
*   **Sistema de Arquivos:** VFS (Virtual File System) com suporte a FAT32 e Initrd (tar).
*   **SDK para Desenvolvedores:** Biblioteca `libliw` e ferramentas como `liw-builder` para criação de pacotes `.liw`.

## 🏗️ Arquitetura do Sistema

### Kernel
O kernel reside em `src/kernel` e é o coração do sistema. Ele gerencia o hardware e fornece syscalls para o espaço de usuário.
- **Syscalls:** Suporte a chamadas POSIX-like (`read`, `write`, `open`, `close`, `fork`, `execve`, etc.) e extensões para rede.

### Gráficos e GUI
LiwusOS foca em uma experiência visual rica:
- **Drivers de Vídeo:** Suporte a BGA (Bochs Graphic Adapter) e Virtio-GPU.
- **Compositor:** Gerencia janelas como "superfícies" (surfaces), permitindo transparência e interfaces fluidas.
- **Apps Integrados:** Terminal, Explorer (Gerenciador de Arquivos), Browser, Configurações e mais.

### Drivers Suportados
- **Armazenamento:** ATA (PIO mode).
- **Entrada:** Teclado PS/2 e Mouse (com suporte a scroll e botões).
- **Rede:** Realtek RTL8139 e conceitos iniciais de Virtio-net.
- **Comunicação:** Serial (UART) para logs de debug.

## 🚀 Como Executar

### Pré-requisitos
Você precisará de um ambiente Linux com:
- `gcc` e `binutils` (com suporte a i386)
- `make`
- `qemu-system-i386`
- `grub-mkrescue` e `xorriso` (para criar a ISO)

### Compilação e Execução
Para compilar o sistema e iniciar no QEMU:

```bash
make clean
make all
make run
```

O comando `make run` criará automaticamente uma imagem de disco rígido virtual de 100MB e iniciará o sistema com aceleração gráfica `std-vga` e rede configurada.

## 🛠️ Desenvolvimento (SDK)

LiwusOS fornece um SDK em `sdk/` para criar aplicações nativas.
- **`libliw`:** Biblioteca padrão do sistema para interagir com a GUI e syscalls.
- **`.liw` files:** Um formato de bundle customizado que pode conter binários ELF e recursos (como ícones e manifests).

## 📁 Estrutura do Projeto

- `src/boot/`: Código de inicialização (Multiboot) e script do Linker.
- `src/kernel/`: Gerenciamento de CPU, Memória e Processos.
- `src/drivers/`: Drivers de hardware.
- `src/gui/`: LGX, Compositor e Painel do Sistema.
- `src/fs/`: VFS e sistemas de arquivos (FAT32/Initrd).
- `src/net/`: Stack de rede TCP/IP.
- `src/apps/`: Código fonte das aplicações de sistema.
- `include/`: Headers globais do kernel.

## 🤝 Contribuição

Este é um projeto de estudo e paixão. Sinta-se à vontade para explorar o código, abrir issues ou enviar pull requests para melhorar o LiwusOS!

## 👥 Créditos

*   **Davi VilasBoas Ranci:** Criador e Desenvolvedor Principal do Kernel.
*   **Isaac Estevan Geuster:** Colaborador da API Gráfica (LGX) e Design de UX/UI.

---
*Desenvolvido com ❤️ para a comunidade de OSDev.*
