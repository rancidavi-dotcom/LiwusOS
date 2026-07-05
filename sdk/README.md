# LiwusOS SDK

Bem-vindo ao Kit de Desenvolvimento do LiwusOS!
Este SDK permite criar aplicativos nativos em C para o LiwusOS.

## Estrutura
- `include/`: Header de compatibilidade (`libliw.h`)
- `lib/`: Runtime de startup (`crt0.s`) e a biblioteca `libliwc.a`
- `libc/include/`: Headers C básicos (`stdio.h`, `stdlib.h`, `string.h`, `unistd.h`, ...)
- `libc/`: Implementacao da libc de userspace do LiwusOS

## Como Compilar um App

Requisitos: `i686-elf-gcc` e `i686-elf-ar`.

1. Crie seu arquivo C (ex: `main.c`):
   ```c
   #include <stdio.h>
   
   int main() {
       printf("Hello from LiwusOS!\n");
       return 0;
   }
   ```

2. Compile a libc do SDK:
   ```bash
   make sdk/lib/libliwc.a
   ```

3. Compile seu app com o runtime do LiwusOS:
   ```bash
   i686-elf-gcc -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
       -Isdk/include -Isdk/libc/include -nostdlib -static \
       sdk/lib/crt0.s main.c sdk/lib/libliwc.a -o app.elf
   ```

4. (Opcional) Crie um pacote `.liw`:
   Use a ferramenta `liw-builder` (se disponível) para empacotar.

## API Disponível
- `printf()`, `puts()`, `putchar()`
- `malloc()`, `calloc()`, `realloc()`, `free()`
- `read()`, `write()`, `open()`, `close()`, `fork()`, `execve()`, `waitpid()`, `sbrk()`
- `print()` e `print_int()` continuam disponiveis em `libliw.h` como compatibilidade
- Partes puras de `string.h` sao reaproveitadas da PDCLib; a camada de syscall e heap e propria do LiwusOS
