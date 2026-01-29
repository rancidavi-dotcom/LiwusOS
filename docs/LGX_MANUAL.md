# 🎨 Manual da API Gráfica: LGX (LiwusOS Graphics eXtension)

Bem-vindo ao **LGX**, a moderna stack gráfica nativa do **LiwusOS**.
O LGX foi projetado para ser leve, rápido e baseado em buffers diretos (Pixel-Perfect), permitindo que aplicativos no nível de usuário desenhem o que quiserem na tela, enquanto o kernel (Compositor Gráfico) gerencia a sobreposição (Z-Order), barras de título, redimensionamento, arrasto de janelas e colisões de mouse.

---

## 🛠️ Como Funciona o Modelo de Renderização?

No LiwusOS, cada janela é apenas um grande **array 1D de Pixels (RGBA de 32-bits)**.
O desenvolvedor do aplicativo solicita ao Kernel uma janela. O Kernel aloca a janela de forma segura, mapeia esse Buffer para a Memória Virtual do Aplicativo (User-Space) e entrega um ponteiro de memória para o app. 

A partir daí, você tem **liberdade total**: pode desenhar pixels individualmente para criar interfaces de usuário, botões, imagens, ou até jogos! Tudo o que você escreve nesse buffer é automaticamente exibido na tela, graças ao Compositor em Ring-0 (Kernel) executando Blitting a 60 FPS com Alpha-Blending.

---

## 📞 As Syscalls do LGX

O LGX expõe a sua interface primária através da interrupção `int $0x80`. 
Existem atualmente 3 syscalls principais para a gestão de janelas.

### 1. `SYS_CREATE_WINDOW` (Syscall 120)
Solicita ao Kernel a criação de uma nova janela interativa e focável.

* **Parâmetros:**
  * `eax = 120` (ID da Syscall)
  * `edi = width` (Largura da janela em pixels)
  * `esi = height` (Altura da janela em pixels)
* **Retorno:**
  * `eax = ID da Janela` (Inteiro positivo) ou `-1` em caso de erro.
* **Comportamento Automático:**
  * A janela será centralizada na tela pelo gerenciador de janelas.
  * O compositor automaticamente anexa uma Barra de Título (Title Bar) com botões de fechar e decorações, além de gerenciar o arrasto da janela pelo usuário através do Mouse.

### 2. `SYS_GET_WINDOW_BUFFER` (Syscall 121)
Recupera o ponteiro de memória compartilhada associado à janela, onde o aplicativo deve desenhar seus pixels.

* **Parâmetros:**
  * `eax = 121` (ID da Syscall)
  * `edi = window_id` (O ID retornado pela criação da janela)
* **Retorno:**
  * `eax = Ponteiro para o Buffer` (do tipo `uint32_t*`) ou `NULL` em caso de erro.
* **Formato do Buffer:**
  * Cada pixel tem 32 bits (4 bytes).
  * Formato da cor: **ARGB** (`0xAARRGGBB`).
    * Exemplo: `0xFFFF0000` = Vermelho Opaco.
    * Exemplo: `0x8000FF00` = Verde Translúcido (50% de Alpha).
  * **Cálculo da Posição (Offset):** Para alterar o pixel na coluna `X` e linha `Y`, acesse o índice `buffer[Y * width + X]`.

### 3. `SYS_REFRESH_WINDOW` (Syscall 122)
(Opcional) Sinaliza ao compositor gráfico que o buffer de tela sofreu uma modificação severa e deve ser re-desenhado imediatamente.
*Nota: Atualmente, o LGX roda em um laço infinito de 60Hz utilizando double-buffering e fast_memcpy nativo, logo, essa chamada muitas vezes atua apenas como sincronia.*

---

## 👨‍💻 Exemplo Prático (Em C)

Aqui está um esqueleto de código completo de um app User-Space mostrando como iniciar uma janela e desenhar nela:

```c
#include <stdint.h>

// Função auxiliar genérica para efetuar Syscalls de 2 argumentos
static inline int syscall2(int num, int arg1, int arg2) {
    int ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2)
        : "memory"
    );
    return ret;
}

// Função auxiliar genérica para efetuar Syscalls de 1 argumento
static inline int syscall1(int num, int arg1) {
    int ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1)
        : "memory"
    );
    return ret;
}

int main() {
    int largura = 250;
    int altura = 150;
    
    // 1. Criar a Janela
    int id = syscall2(120, largura, altura);
    if (id < 0) return 1; // Falhou
    
    // 2. Resgatar o Ponteiro do Framebuffer
    uint32_t* buffer = (uint32_t*)(uint64_t)syscall1(121, id);
    if (!buffer) return 1; // Falhou
    
    // 3. Pintar a Janela (Exemplo: Vermelho Sólido)
    for (int y = 0; y < altura; y++) {
        for (int x = 0; x < largura; x++) {
            // ARGB: 0xFF = Opacidade Total, 0xFF0000 = Vermelho
            buffer[y * largura + x] = 0xFFFF0000;
        }
    }
    
    // Manter o aplicativo rodando
    while (1) {
        syscall1(122, id); // Opcional refresh
        for (volatile int i = 0; i < 1000000; i++) {} // Delay
    }
    
    return 0;
}
```

---

## 🔮 O Futuro da LGX
No futuro, o Kernel e a SDK irão prover estruturas de alto nível como a Biblioteca Padrão Gráfica `liblgx` que incluirá:
- `lgx_draw_rect()`
- `lgx_draw_text(font_t, "Hello World")`
- Botões interativos predefinidos e callbacks de mouse (eventos enviados ao Anel 3 via IPC).
