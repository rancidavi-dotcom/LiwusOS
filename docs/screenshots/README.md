# Capturas de interface

Estas capturas vêm da interface LVGL real, iniciada em uma VM QEMU temporária.
Os números de rede, horário e memória são dados de demonstração do ambiente
virtual. A captura de Settings foi feita com a janela aberta somente durante a
captura; isso não altera o comportamento normal do desktop. As imagens não são
evidência de vídeo testado no PC H61.

- `desktop-auto-edid-2048x1152.png`: desktop após o boot escolher o maior modo
  anunciado pelo EDID simulado do QEMU Standard VGA.
- `settings-min-resolution.png`: janela Settings na menor resolução EDID
  anunciada pelo monitor virtual, 720 × 400.
- `settings-audio-live-2048x1152.png`: janela Settings em 2048 × 1152, incluindo
  o controle de volume HDA aplicado imediatamente e salvo no SDFS.
