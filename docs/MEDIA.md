# LiwusOS — Mídia Nativa (MP3 e MP4)

## Estado Atual (v2.2.33)

- **MP3:** decodificação nativa via `minimp3` (domínio público) tanto no utilitário de linha de comando Ring-3 (`play.lxe`) quanto no player gráfico integrado ao Desktop (`desktop.c`).
  - Suporta taxas de amostragem arbitrárias convertidas via resampler de ponto fixo inteiro para 48 kHz estéreo de 16 bits.
  - Streaming contínuo e assíncrono para o controlador Intel HDA através de `/dev/sound` e buffer de anel sincronizado com ponteiro DMA `LPIB` (`HDA_SD_LPIB`), sem travar a interface gráfica LVGL.
- **MP4:** suporte nativo a containers ISO-BMFF (`ftyp`, `moov`, `trak`, `mdia`, `minf`, `stbl`, `stsd`, `stts`, `stsc`, `stsz`, `stco`/`co64`, `mdat`):
  - Demuxer freestanding com parsing seguro de caixas e tabelas de amostras (`src/kernel/core/media.c`).
  - Codec de vídeo: baseline MJPEG/JPEG com IDCT rápido escalado e tabelas de Huffman padrão (Annex K) ou customizadas pelo bitstream, gerando quadros ARGB de 32 bits apresentados na viewport LVGL.
  - Codec de áudio: streams MP3 embutidas em faixas de som, sincronizadas com os quadros de vídeo via PTS e temporizador de ticks do kernel.
  - Inclui barra interativa de seek (posicionamento na linha do tempo), ajuste de volume, Play/Pause/Stop e botões de carregamento rápido.

## Amostras Incluídas no Sistema

O sistema já vem de fábrica (no `initrd.tar`) com duas mídias de demonstração:
- `/musica.mp3`: faixa musical de alta qualidade gerada sinteticamente em 48 kHz estéreo.
- `/video.mp4`: animação com espectro de áudio dinâmico, grid e logo LiwusOS em MJPEG 240x136 @ 12 fps acompanhada de trilha sonora em MP3.

## Como Testar

### 1. No Desktop Gráfico
1. Clique no menu Iniciar e selecione **"Player de Mídia (MP3/MP4)"** ou clique no botão **"Mídia"** na barra de tarefas.
2. Na janela do player:
   - Clique em **"musica.mp3"** para testar reprodução de áudio.
   - Clique em **"video.mp4"** para assistir à animação com áudio sincronizado na janela.
   - Use os controles de Play, Pause, Stop, a barra de progresso (seek) e o controle deslizante de volume.

### 2. No Terminal (Ring-3)
Para executar via shell:

```sh
play /musica.mp3
play /video.mp4
```

O reprodutor Ring-3 envia áudio em lotes assíncronos para `/dev/sound` e exibe metadados de duração, resolução e taxa de bits.
