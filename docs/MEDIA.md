# LiwusOS — mídia nativa

## Estado desta etapa

- **MP3:** há um player Ring-3 (`play.lxe`) com o decodificador single-header
  `minimp3` (dedicado ao domínio público pelo autor), resampling inteiro para
  48 kHz e saída PCM estéreo por `/dev/sound`/Intel HDA.
- **MP4:** o VFS reconhece corretamente containers ISO-BMFF pelo box `ftyp` e
  classifica o recurso como `VIDEO`, mas a reprodução ainda não é anunciada.
  Ela exige demux de `moov`/`mdat`, relógio A/V e codecs (normalmente H.264 e
  AAC), que não existem no kernel atual. O player retorna erro explícito em vez
  de simular sucesso.

## Como testar MP3

Com um arquivo no SDFS:

```text
play /musica.mp3
```

O formato enviado ao dispositivo nativo é PCM assinado little-endian, 16-bit,
estéreo, 48 kHz. O kernel copia cada lote para uma área controlada antes de
entregar ao HDA; o aplicativo não acessa DMA diretamente.

## Próxima etapa para MP4

1. Implementar um parser limitado de ISO-BMFF com limites de tamanho e sem
   alocar o arquivo inteiro.
2. Escolher codecs suportados explicitamente (não prometer H.264/AAC sem
   decoder portado para o SDK).
3. Adicionar fila de frames, sincronização por PTS e apresentação no LVGL.

Até lá, `canPlayType("video/mp4")` não deve ser usado como prova de playback:
o browser ainda possui uma superfície compatível, mas o backend de vídeo não
está conectado.
