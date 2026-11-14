# Capturas de tela do LIWUS

O comando de kernel `screenshot <ip-do-PC> [porta]` captura o framebuffer atual
como BMP 24-bit e envia a imagem por TCP. O destino padrão é a porta `49152`.
O comando espera a confirmação do receptor antes de informar sucesso.

No PC Linux que receberá a imagem, na pasta do projeto:

```sh
python3 tools/liwus_screenshot_receiver.py
```

O receptor imprime o IP local que o LIWUS deve usar e salva as capturas em
`liwus-captures/`. Com o receptor ativo, rode o comando mostrado pelo script no
terminal do LIWUS. As imagens são BMP e podem ser abertas pelo visualizador
normal do PC. O receptor aceita apenas BMP não comprimido até 32 MiB por
conexão e continua aceitando novas capturas até `Ctrl+C`.

Os dois computadores precisam alcançar um ao outro na rede local e a porta TCP
deve estar liberada no firewall do PC. Essa primeira versão exige um endereço
IPv4 numérico e uma tela RGB framebuffer ativa; ela não captura modo VGA texto
sem framebuffer.
