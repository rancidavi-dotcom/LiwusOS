# Craft no LiwusOS

Este port usa o código do [fogleman/Craft](https://github.com/fogleman/Craft),
fixado na revisão `d6888a6e1e54340358ce25b1220f410541360b6b`. O código de mundo,
blocos, colisão, geração procedural e assets continua vindo do jogo original;
`platform.c` adapta entrada e framebuffer, e `softgl.c` rasteriza os shaders
originais no framebuffer do LiwusOS.

## Compilar e abrir

```sh
make craft
make liwusos.iso
```

O segundo comando inclui `craft.lxe`, shaders e texturas na imagem do sistema.
No LiwusOS, abra **Craft** pelo menu ou execute `craft` no terminal.

## Estado do port

- Modo offline para um jogador; autenticação e multiplayer estão desligados.
- O mundo é gerado pelo código original do Craft.
- O desenho usa resolução interna 320×200 e o framebuffer amplia a imagem.
- Os chunks são processados em série porque o SDK ainda não oferece threads
  POSIX para este app.
- Salvamento SQLite ainda não foi ligado ao VFS do LiwusOS; alterações do
  mundo não persistem entre execuções.
- O renderer por software ainda é lento, especialmente durante a geração de
  terreno. A captura e execução em QEMU confirmam a renderização, mas não
  representam uma meta de desempenho para o hardware real.

Controles mantidos do Craft: WASD para andar, Espaço para pular, mouse para
olhar e interagir, clique esquerdo para remover blocos, clique direito para
colocar, `1`–`9` para escolher blocos, `E` para trocar o bloco e Tab para
alternar voo.
