#!/bin/bash
if [ -z "$1" ]; then
    echo "Uso: ./build_app.sh arquivo.c"
    exit 1
fi

APP_NAME=$(basename "$1" .c)
SOURCE_FILE=$1
OUTPUT_FILE="repo/${APP_NAME}.liwpkg"

echo "Compilando $SOURCE_FILE para $OUTPUT_FILE..."

# Usa o docker para rodar o compilador de 32 bits
sudo docker run --rm -v "$(pwd)":/os-build liwus-builder bash -c "
    i686-elf-gcc -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
    -Isdk/include -Isdk/libc/include -nostdlib -static \
    sdk/lib/crt0.s $SOURCE_FILE sdk/lib/libliwc.a \
    -o $OUTPUT_FILE -lgcc
"

if [ $? -eq 0 ]; then
    echo "Sucesso! O programa $APP_NAME foi compilado."
    echo "Agora rode ./run.sh para iniciar o OS com seu novo app."
else
    echo "Erro na compilação!"
fi
