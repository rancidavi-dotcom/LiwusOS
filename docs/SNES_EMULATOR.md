# Emulador de SNES

O app nativo `snes.lxe` usa o núcleo C do LakeSnes para executar ROMs próprias
em formato `.sfc` ou `.smc`. A ISO instala o comando em `/bin/snes` e adiciona
atalhos SNES ao dock e ao menu Aplicativos.

```text
snes
snes /roms/meu_jogo.sfc
```

Sem argumento, a tela inicial aceita o caminho da ROM. Coloque suas ROMs em
`/roms` e digite o caminho completo. Pressione `Enter` para carregar e, depois,
`Enter` novamente para jogar. `Backspace` apaga o caminho e `Esc` sai do app.
Durante o jogo, `Esc` volta à biblioteca; pressione `Esc` de novo para sair.

| Controle do SNES | Tecla |
|---|---|
| Direcional | Setas |
| B / A | Z / X |
| Y / X | A / S |
| L / R | D / C |
| Select / Start | Shift direito / Enter |
| Pausar / continuar | P / Enter |

O núcleo cobre LoROM, HiROM, ExLoROM, ExHiROM e o coprocessador CX4. A emulação
é experimental e alguns jogos ou efeitos podem falhar. O LiwusOS ainda não
oferece uma saída de áudio para apps nativos; a imagem roda sem som. ZIP, SRAM e
save states também não estão disponíveis nesta integração. Nenhuma ROM é
incluída.

O núcleo vem do [LakeSnes de dinkc64](https://github.com/dinkc64/LakeSnes),
licenciado sob MIT; o aviso está em `third_party/lakesnes/LICENSE.txt`.
