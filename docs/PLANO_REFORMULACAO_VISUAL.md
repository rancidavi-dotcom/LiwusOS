# Plano de Reformulacao Visual - LiwusOS
## Estilo: Retro Pixel / TV Antiga (CRT) - Verde Fosforo IBM 5151

---

## Visao Geral

Transformar toda a GUI do LiwusOS de um visual "dark glassmorphism moderno" para um
visual **retro pixelado estilo TV antiga (CRT) verde fosforo IBM 5151**. Tudo sera
verde sobre preto, com scanlines, e wallpaper pixel art.

---

## Decisoes do Usuario

- **Estilo CRT:** Verde fosforo (IBM 5151) - monocromatico verde sobre preto
- **Efeitos:** Scanlines (linhas horizontais escuras alternadas)
- **Wallpaper:** Substituir nebula por pixel art retro

---

## Paleta de Cores - Verde Fosforo IBM 5151

```
PRETO CRT:          #0A0A12  (quase preto, leve azul)
VERDE ESCURO:       #0A2E1A  (fundo titlebar/paineis)
VERDE MEDIO:        #00AA00  (bordas, elementos secundarios)
VERDE CLARO:        #00FF41  (texto principal - phosphor)
VERDE BRILHANTE:    #33FF66  (hover, destaques)
PRETO INPUT:        #050A10  (fundo inputs)
VERMELHO CRT:       #FF4444  (botao fechar)
```

---

## Arquivos a Modificar (16 arquivos)

### Fase 1: Theme Engine (muda tudo de uma vez)
1. **`src/kernel/gui/core/theme_engine.c`** - Substituir paleta inteira
2. **`src/kernel/gui/core/theme_engine.h`** - Sem mudancas (slots existentes bastam)

### Fase 2: Efeitos CRT no Compositor
3. **`src/kernel/gui/render/compositor.c`** - Adicionar funcao de scanlines apos frame
4. **`src/kernel/gui/render/fb_renderer.c`** - Opcao de aplicar scanlines no present()

### Fase 3: Widgets Retro
5. **`src/kernel/gui/widgets/window_node.c`** - Titlebar verde, bordas pixel dupla, sem transparencia
6. **`src/kernel/gui/widgets/button.c`** - Botoes estilo DOS com borda 3D
7. **`src/kernel/gui/widgets/panel.c`** - Fundo solido, borda tracejada verde
8. **`src/kernel/gui/widgets/text_input.c`** - Cursor bloco verde, fundo preto

### Fase 4: Apps (cores hardcoded -> tema)
9. **`src/kernel/gui/apps/gui_terminal.c`** - Terminal verde classico
10. **`src/kernel/gui/apps/gui_settings.c`** - Sidebar e paineis verdes
11. **`src/kernel/gui/apps/gui_explorer.c`** - Lista de arquivos verde
12. **`src/kernel/gui/apps/gui_text_editor.c`** - Editor verde
13. **`src/kernel/gui/apps/gui_media.c`** - Player verde
14. **`src/kernel/gui/apps/gui_imageviewer.c`** - Viewer verde

### Fase 5: Boot Splash
15. **`src/drivers/boot_splash.c`** - Logo verde, barra verde, preto puro

### Fase 6: Wallpaper Pixel Art
16. **`include/gui/wallpaper.h`** - Novo wallpaper 1024x768 pixel art retro

---

## Detalhes de Implementacao

### 1. Theme Engine - Paleta Verde Fosforo

```c
void theme_engine_init(void) {
    s_palette[THEME_COLOR_BACKGROUND]      = 0xFF0A0A12; /* Preto CRT */
    s_palette[THEME_COLOR_WINDOW_BG]       = 0xFF0A1510; /* Preto-verde escuro */
    s_palette[THEME_COLOR_WINDOW_TITLEBAR] = 0xFF0A2E1A; /* Verde escuro titlebar */
    s_palette[THEME_COLOR_WINDOW_BORDER]   = 0xFF00AA00; /* Verde fosforo borda */
    s_palette[THEME_COLOR_TEXT_PRIMARY]    = 0xFF00FF41; /* Verde claro (phosphor) */
    s_palette[THEME_COLOR_TEXT_SECONDARY]  = 0xFF00CC33; /* Verde medio */
    s_palette[THEME_COLOR_BUTTON_BG]       = 0xFF0A2E1A; /* Verde escuro botao */
    s_palette[THEME_COLOR_BUTTON_BG_HOVER] = 0xFF1A4A2A; /* Verde medio hover */
    s_palette[THEME_COLOR_BUTTON_BG_PRESS] = 0xFF050A08; /* Verde muito escuro */
    s_palette[THEME_COLOR_BUTTON_BORDER]   = 0xFF00AA00; /* Verde fosforo */
    s_palette[THEME_COLOR_BUTTON_TEXT]     = 0xFF00FF41; /* Verde claro */
    s_palette[THEME_COLOR_CLOSE_BTN]       = 0xFFFF4444; /* Vermelho CRT */
    s_palette[THEME_COLOR_INPUT_BG]        = 0xFF050A10; /* Preto input */
    s_palette[THEME_COLOR_INPUT_BG_FOCUS]  = 0xFF0A1520; /* Preto focado */
    s_palette[THEME_COLOR_INPUT_BORDER]    = 0xFF008800; /* Verde escuro borda */
    s_palette[THEME_COLOR_INPUT_TEXT]      = 0xFF00FF41; /* Verde claro */
    s_palette[THEME_COLOR_INPUT_CURSOR]    = 0xFF00FF41; /* Verde claro cursor */
}
```

### 2. Scanlines no Compositor

Adicionar no `compositor.c`, antes de `renderer_present()`:

```c
static void apply_scanlines(compositor_t *c) {
    uint32_t *backbuf = fb_renderer_backbuf(c->renderer);
    if (!backbuf) return;
    int W = c->renderer->screen_w;
    int H = c->renderer->screen_h;

    for (int y = 0; y < H; y += 2) {  /* Linhas pares = escuras */
        uint32_t *row = backbuf + y * W;
        for (int x = 0; x < W; x++) {
            uint32_t p = row[x];
            /* Escurecer 20%: multiplicar canais por 0.8 */
            uint32_t r = ((p >> 16) & 0xFF) * 4 / 5;
            uint32_t g = ((p >>  8) & 0xFF) * 4 / 5;
            uint32_t b = ((p      ) & 0xFF) * 4 / 5;
            row[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
}
```

Chamar: `apply_scanlines(c);` antes de `renderer_present(c->renderer);`

### 3. Window Node - Janela Verde Retro

```
+==[ TERMINAL ]=====[X]=+
|                       |
|   Conteudo verde      |
|                       |
+=======================+
```

Mudancas em `window_node_draw()`:
- Titlebar: `theme_engine_get_color(THEME_COLOR_WINDOW_TITLEBAR)` (verde escuro)
- Borda focada: 2px verde fosforo (sem bright white)
- Borda sem foco: 1px verde escuro
- Fundo: solido, sem alpha
- Botao fechar: X vermelho sobre fundo vermelho escuro

### 4. Button - Botao DOS Verde

```
+=======================+
|     Botao Texto       |
+=======================+
```

Mudancas em `button_draw()`:
- Fundo verde escuro solido
- Borda 1px verde fosforo
- Hover: borda fica verde brilhante
- Press: efeito 3D invertido (borda superior/esquerda escuro, inferior/direita claro)

### 5. Panel - Painel com Borda Tracejada

Mudancas em `panel_draw()`:
- Fundo solido preto-verde
- Borda: desenhar pixels alternados (tracejado) em verde

### 6. Text Input - Terminal

Mudancas em `text_input_draw()`:
- Fundo preto solido
- Borda verde
- Cursor: bloco 8x16 verde (nao linha de 1px)
- Blink: alternar visibilidade do bloco

### 7. Apps - Substituir Cores Hardcoded

Em todas as apps, trocar:
- `0xFF00FF00` -> `theme_engine_get_color(THEME_COLOR_TEXT_PRIMARY)` (verde)
- `0xFF00FF88` -> `theme_engine_get_color(THEME_COLOR_TEXT_PRIMARY)` (verde)
- `0xFF00FFFF` -> `theme_engine_get_color(THEME_COLOR_TEXT_PRIMARY)` (verde)
- `0xFFCCCCCC` -> `theme_engine_get_color(THEME_COLOR_TEXT_SECONDARY)` (verde medio)
- `0xFFAAAAAA` -> `theme_engine_get_color(THEME_COLOR_TEXT_SECONDARY)` (verde medio)
- `0xFFFFFFFF` -> `theme_engine_get_color(THEME_COLOR_TEXT_PRIMARY)` (verde claro)
- `0xFF888888` -> `theme_engine_get_color(THEME_COLOR_TEXT_SECONDARY)` (verde medio)
- `0xFF4CAF50` -> `theme_engine_get_color(THEME_COLOR_TEXT_PRIMARY)` (verde)
- `0xFF1A1A1A` -> `theme_engine_get_color(THEME_COLOR_WINDOW_BG)` (preto-verde)
- `0xFF252525` -> `theme_engine_get_color(THEME_COLOR_WINDOW_BG)` (preto-verde)
- `0x33252525` -> `0x330A1510` (alpha preto-verde)
- `0x441A1A1A` -> `0x440A1510` (alpha preto-verde)
- `0xAA0A0A15` -> `theme_engine_get_color(THEME_COLOR_WINDOW_BG)` (preto)

### 8. Boot Splash Retro

Mudancas em `boot_splash.c`:
- Fundo: preto puro `0x000000`
- Logo ASCII: verde fosforo `0x00FF41`
- Texto "LiwusOS": verde fosforo
- Barra de progresso: verde fosforo com borda verde escuro
- Texto "Carregando...": verde medio

### 9. Wallpaper Pixel Art

Substituir `wallpaper_data[]` em `include/gui/wallpaper.h` com um novo wallpaper
1024x768 pixel art em tons de verde. O wallpaper deve ser:
- Fundo preto
- Grid de pontos verdes escuros (como monitor CRT ligado)
- Talvez um pixel art de um monitor antigo ou virus ASCII
- Pode ser gerado por um script Python

---

## Ordem de Implementacao

1. `theme_engine.c` - Paleta verde fosforo (5 min)
2. `compositor.c` - Scanlines (15 min)
3. `window_node.c` - Janela verde retro (15 min)
4. `button.c` - Botoes DOS (10 min)
5. `panel.c` - Paineis com borda tracejada (10 min)
6. `text_input.c` - Input terminal (10 min)
7. Apps (6 arquivos) - Substituir cores hardcoded (30 min)
8. `boot_splash.c` - Boot verde (10 min)
9. `wallpaper.h` - Gerar novo wallpaper pixel art (15 min)

**Tempo estimado total: ~2 horas**

---

## Notas Tecnicas

- Scanlines sao O(n) por frame (muito leve)
- A fonte PSF 8x16 ja e pixelada - perfeita para o estilo
- O efeito CRT torna tudo mais coeso - mesmo imperfeicoes ficam "bonitas"
- Toggle global para ligar/desligar scanlines (para debugging)
- Wallpaper pixel art pode ser gerado via script Python e embutido no header
