# LiwusOS SDK

Bem-vindo ao Kit de Desenvolvimento do LiwusOS!
Este SDK permite criar aplicativos nativos em C para o LiwusOS.

## Estrutura
- `include/`: Headers da biblioteca (`libliw.h`)
- `lib/`: Arquivos de startup (`crt0.s`)

## Como Compilar um App

Requisitos: `gcc` (suporte a multilib/32-bit se estiver em x64).

1. Crie seu arquivo C (ex: `main.c`):
   ```c
   #include "libliw.h"
   
   int main() {
       print("Hello from LiwusOS!\n");
       return 0;
   }
   ```

2. Compile usando o GCC com flags para 32-bit e freestanding:
   ```bash
   gcc -m32 -nostdlib -fno-builtin -I sdk/include \
       sdk/lib/crt0.s main.c -o app.elf
   ```

3. (Opcional) Crie um pacote `.liw`:
   Use a ferramenta `liw-builder` (se disponível) para empacotar.

## API Disponível
- `print(str)`: Imprime no console.
- `exit(code)`: Encerra o programa.
- Syscalls raw (`syscall0`-`syscall4`) disponíveis em `libliw.h`.
