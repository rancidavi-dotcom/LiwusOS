#!/bin/bash
# Garante que a imagem do docker esteja atualizada antes de compilar
docker build -t liwus-builder .
docker run --rm -v "$(pwd)":/os-build liwus-builder make all